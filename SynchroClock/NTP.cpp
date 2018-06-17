/*
 * NTPProto.cpp
 *
 *  Created on: Jul 6, 2017
 *      Author: chris.l
 */

#include "NTP.h"

#include "NTPPrivate.h"
#include <math.h>
#include <stdlib.h>
#ifdef ESP8266
#include <lwip/def.h> // htonl() & ntohl()
#endif

static PROGMEM const char TAG[] = "NTP";

void dumpNTPPacket(NTPPacket* ntp, const char* label)
{
    dlog.trace(FPSTR(TAG), F("::%s: size:       %u"), label, sizeof(*ntp));
    dlog.trace(FPSTR(TAG), F("::%s: firstbyte:  0x%02x"), label, *(uint8_t*)ntp);
    dlog.trace(FPSTR(TAG), F("::%s: li:         %u"), label, getLI(ntp->flags));
    dlog.trace(FPSTR(TAG), F("::%s: version:    %u"), label, getVERS(ntp->flags));
    dlog.trace(FPSTR(TAG), F("::%s: mode:       %u"), label, getMODE(ntp->flags));
    dlog.trace(FPSTR(TAG), F("::%s: stratum:    %u"), label, ntp->stratum);
    dlog.trace(FPSTR(TAG), F("::%s: poll:       %u"), label, ntp->poll);
    dlog.trace(FPSTR(TAG), F("::%s: precision:  %d"), label, ntp->precision);
    dlog.trace(FPSTR(TAG), F("::%s: delay:      %u"), label, ntp->delay);
    dlog.trace(FPSTR(TAG), F("::%s: dispersion: %u"), label, ntp->dispersion);
    dlog.trace(FPSTR(TAG), F("::%s: ref_id:     %02x:%02x:%02x:%02x"), label, ntp->ref_id[0], ntp->ref_id[1], ntp->ref_id[2], ntp->ref_id[3]);
    dlog.trace(FPSTR(TAG), F("::%s: ref_time:   %08x:%08x"), label, ntp->ref_time.seconds, ntp->ref_time.fraction);
    dlog.trace(FPSTR(TAG), F("::%s: orig_time:  %08x:%08x"), label, ntp->orig_time.seconds, ntp->orig_time.fraction);
    dlog.trace(FPSTR(TAG), F("::%s: recv_time:  %08x:%08x"), label, ntp->recv_time.seconds, ntp->recv_time.fraction);
    dlog.trace(FPSTR(TAG), F("::%s: xmit_time:  %08x:%08x"), label, ntp->xmit_time.seconds, ntp->xmit_time.fraction);
}

NTP::NTP(NTPRunTime *runtime, NTPPersist *persist, void (*savePersist)(), int factor)
{
    _runtime     = runtime;
    _persist     = persist;
    _savePersist = savePersist;
    _port        = NTP_PORT;
    _factor      = factor;
    dlog.debug(FPSTR(TAG), F("****** sizeof(NTPRunTime): %d"), sizeof(NTPRunTime));
}

void NTP::begin(int port)
{
    _port   = port;
    _udp.begin(port);
    dlog.info(FPSTR(TAG), F("::begin: nsamples: %d nadjustments: %d, drift: %f"), _runtime->nsamples, _persist->nadjustments, _persist->drift);
    if (_runtime->nsamples == 0 && _runtime->drifted == 0.0)
    {
        // if we have no samples and drifted is 0 then we probably had a power cycle so invalidate the
        // most recent adjustment timestamp.
        _persist->adjustments[0].timestamp = 0;
        dlog.info(FPSTR(TAG), F("::begin: power cycle detected! marking last adjustment as invalid for drift!"));
    }
}

IPAddress NTP::getAddress()
{
    return _runtime->ip;
}

int NTP::getLastOffset(double *offset)
{
    if (_runtime->nsamples > 0)
    {
        *offset = _runtime->samples[0].offset;
        return 0;
    }
    return -1;
}

uint32_t NTP::getPollInterval()
{
    double seconds = 3600/_factor;

    dlog.info(FPSTR(TAG), F("::getPollInterval: drift_estimate: %0.16f poll_interval: %0.16f"), _runtime->drift_estimate, _runtime->poll_interval);

    //if (_runtime->poll_interval > 0.0)
    if (_runtime->drift_estimate != 0.0)
    {
        //
        // estimate the time till we apply the next offset
        //
        if (_runtime->samples[0].timestamp == _runtime->update_timestamp)
        {
            seconds = _runtime->poll_interval;
        }
        else
        {
            seconds = fabs(_runtime->samples[0].offset) / NTP_OFFSET_THRESHOLD * _runtime->poll_interval;
        }
        dlog.debug(FPSTR(TAG), F("::getPollInterval: seconds: %f"), seconds);

        if (seconds > (NTP_MAX_INTERVAL/_factor))
        {
            dlog.debug(FPSTR(TAG), F("::getPollInterval: maxing interval out at %f days!"), (double)NTP_MAX_INTERVAL/86400.0);
            seconds = NTP_MAX_INTERVAL/_factor;
        }
        else if (seconds < (900/_factor))
        {
            dlog.debug(FPSTR(TAG), F("::getPollInterval: min interval is 15 min!!"));
            seconds = 900/_factor;
        }
    }

    if (_runtime->nsamples < NTP_SAMPLE_COUNT)
    {
        //
        // if we don't have all the samples yet, use a very short interval
        //
        dlog.debug(FPSTR(TAG), F("::getPollInterval: samples not full, 15 minutes!"));
        seconds = 900 / _factor; // 15 minutes
    }
    else if ((_runtime->reach & 0x07) == 0)
    {
        //
        // if the last three polls failed use a very short interval
        //
        dlog.warning(FPSTR(TAG), F("::getPollInterval: last three polls failed, using 15 minutes!"));
        seconds =  900 / _factor;
    }
    else if ((_runtime->reach & 0x01) == 0)
    {
        //
        // if the last poll failed then use a shorter interval
        //
        dlog.warning(FPSTR(TAG), F("::getPollInterval: last poll failed, using 1 hour!"));
        seconds =  3600 / _factor;
    }

    return (uint32_t)seconds;
}

int NTP::getOffsetUsingDrift(double *offset_result, int (*getTime)(uint32_t *result))
{
    if (_persist->drift == 0.0)
    {
        dlog.debug(FPSTR(TAG), F("::getOffsetUsingDrift: not enough data to compute/use drift!"));
        return -1;
    }

    uint32_t now;
    if (getTime(&now))
    {
        dlog.error(FPSTR(TAG), F("::getOffsetUsingDrift: failed to getTime() failed!"));
        return -1;
    }

    if (_runtime->drift_timestamp == 0)
    {
        dlog.debug(FPSTR(TAG), F("::getOffsetUsingDrift: first time, setting initial timestamp! (now=%lu)"), now);
        _runtime->drift_timestamp = now;
        return -1;
    }

    if (_runtime->drift_timestamp >= now)
    {
        dlog.warning(FPSTR(TAG), F("::getOffsetUsingDrift: timewarped! resetting timestamp! (%lu >= %lu)"), _runtime->drift_timestamp, now);
        _runtime->drift_timestamp = now;
        return -1;
    }

    uint32_t interval = now - _runtime->drift_timestamp;
    double   offset   = (double)interval * _persist->drift / 1000000.0;
    dlog.info(FPSTR(TAG), F("::getOffsetUsingDrift: interval: %u drift: %f offset: %f"), interval, _persist->drift, offset);

    //
    // don't use this offset if it does not meet the threshold
    //
    if (fabs(offset) < NTP_OFFSET_THRESHOLD)
    {
        dlog.info(FPSTR(TAG), F("::getOffsetUsingDrift: offset not big enough for adjust!"));
        return -1;
    }

    *offset_result = offset;
    _runtime->drift_timestamp = now;
    _runtime->drifted += offset;
    return 0;
}

// return next poll delay or -1 on error.
int NTP::getOffset(const char* server, double *offset, int (*getTime)(uint32_t *result))
{
    _runtime->reach <<= 1;

    IPAddress address = _runtime->ip;

    //
    // if we don't have an ip address or the server name does not match the the one we have one or
    // its not reachable then lookup a new one.
    //
    if (_runtime->ip == 0 || strncmp(server, _runtime->server, NTP_SERVER_LENGTH) != 0 || _runtime->reach == 0)
    {
        dlog.trace(FPSTR(TAG), F("::getOffset: updating server and address!"));
        if (!WiFi.hostByName(server, address))
        {
            dlog.error(FPSTR(TAG), F("::getOffset: DNS lookup on %s failed!"), server);
            return -1;
        }

        memset((void*)_runtime->server, 0, sizeof(_runtime->server ));
        strncpy(_runtime->server, server, NTP_SERVER_LENGTH-1);
        _runtime->ip = address;

        dlog.info(FPSTR(TAG), F("::getOffset: NEW server: %s address: %s"), server, address.toString().c_str());

        // we forget the existing data when we change NTP servers
        _runtime->nsamples = 0;
    }

    //
    // Ping the server first, we don't care about the result.  This updates any
    // ARP cache etc.  Without this we see a varying 20ms -> 80ms delay on the
    // NTP packet.
    //
    Ping ping;
    ping.ping(address);

    Timer timer;
    NTPTime now;
    NTPPacket ntp;

    memset((void*) &ntp, 0, sizeof(ntp));
    ntp.flags = setLI(LI_NONE) | setVERS(NTP_VERSION) | setMODE(MODE_CLIENT);
    ntp.poll  = MINPOLL;

    uint32_t start;
    if (getTime(&start))
    {
        dlog.error(FPSTR(TAG), F("::getOffset: failed to getTime() failed!"));
        return -1;
    }

    timer.start();

    now.seconds = toNTP(start);
    now.fraction = 0;
    // put non-zero timestamps in network byte order
    ntp.orig_time.seconds = htonl(ntp.orig_time.seconds);
    ntp.orig_time.fraction = htonl(ntp.orig_time.fraction);

    ntp.orig_time = now;

    dumpNTPPacket(&ntp, "getOffset");


    _udp.open(address, _port);
    _udp.send(&ntp, sizeof(ntp));

    memset(&ntp, 0, sizeof(ntp));

    int size = _udp.recv(&ntp, sizeof(ntp), 1000);
    uint32_t duration = timer.stop();

    dlog.info(FPSTR(TAG), F("::getOffset: used server: %s address: %s"), _runtime->server, address.toString().c_str());
    dlog.info(FPSTR(TAG), F("::getOffset: packet size: %d"), size);
    dlog.info(FPSTR(TAG), F("::getOffset: duration %ums"), duration);

    if (size != 48)
    {
        dlog.error(FPSTR(TAG), F("::getOffset: bad packet!"));
        return -1;
    }

    ntp.delay = ntohl(ntp.delay);
    ntp.dispersion = ntohl(ntp.dispersion);
    ntp.ref_time.seconds = ntohl(ntp.ref_time.seconds);
    ntp.ref_time.fraction = ntohl(ntp.ref_time.fraction);
    ntp.recv_time.seconds = ntohl(ntp.recv_time.seconds);
    ntp.recv_time.fraction = ntohl(ntp.recv_time.fraction);
    ntp.xmit_time.seconds = ntohl(ntp.xmit_time.seconds);
    ntp.xmit_time.fraction = ntohl(ntp.xmit_time.fraction);
    ntp.orig_time = now;

    dumpNTPPacket(&ntp, "getOffset");

    //
    // assumes start at second start and less than 1 second duration
    //
    now.fraction = ms2fraction(duration);

    int err = packet(&ntp, now);
    if (err)
    {
        dlog.error(FPSTR(TAG), F("::getOffset: packet returns: %d"), err);
        return err;
    }

    *offset = _runtime->samples[0].offset;

    //
    // set the update and drift timestamps.
    //
    _runtime->update_timestamp = _runtime->samples[0].timestamp;
    _runtime->drift_timestamp  = _runtime->samples[0].timestamp;
    return 0;
}

int NTP::packet(NTPPacket* ntp, NTPTime now)
{
    dlog.info(FPSTR(TAG), F("::packet: nsamples: %d nadjustments: %d"), _runtime->nsamples, _persist->nadjustments);
    if (ntp->stratum == 0)
    {
        dlog.error(FPSTR(TAG), F("::packet: bad stratum!"));
        return -1;
    }

    if (getLI(ntp->flags) == LI_NOSYNC)
    {
        dlog.warning(FPSTR(TAG), F("::packet: leap indicator indicates NOSYNC!"));
        return -1; /* unsynchronized */
    }

    /*
     * Verify valid root distance.
     */
    //dbprintf("root distance: %lf reftime:%u xmit_time:%u\n", p->rootdelay / 2 + p->rootdisp, p->reftime_s, ntp->xmit_time.seconds);
    //if (p->rootdelay / 2 + p->rootdisp >= MAXDISP || p->reftime_s > ntp->xmit_time.seconds)
    //{
    //    dbprintln("invalid root distance or new time before prev time!");
    //    return -1;                 /* invalid header values */
    //}

    uint64_t T1 = toUINT64(ntp->orig_time);
    uint64_t T2 = toUINT64(ntp->recv_time);
    uint64_t T3 = toUINT64(ntp->xmit_time);
    uint64_t T4 = toUINT64(now);

    double offset     = LFP2D(((int64_t)(T2 - T1) + (int64_t)(T3 - T4)) / 2);
    double delay      = LFP2D( (int64_t)(T4 - T1) - (int64_t)(T3 - T2));

    dlog.info(FPSTR(TAG), F("::packet: offset: %0.6lf delay: %0.6lf"), offset, delay);

    int i;
    for (i = _runtime->nsamples - 1; i >= 0; --i)
    {
        if (i == NTP_SAMPLE_COUNT - 1)
        {
            continue;
        }
        _runtime->samples[i + 1] = _runtime->samples[i];
        dlog.info(FPSTR(TAG), F("::packet: samples[%d]: %lf delay:%lf timestamp:%u"),
                i + 1, _runtime->samples[i+1].offset, _runtime->samples[i+1].delay, _runtime->samples[i+1].timestamp);
    }

    _runtime->samples[0].timestamp  = now.seconds;
    _runtime->samples[0].offset     = offset;
    _runtime->samples[0].delay      = delay;
    dlog.info(FPSTR(TAG), F("::packet: samples[%d]: %lf delay:%lf timestamp:%u"),
            0, _runtime->samples[0].offset, _runtime->samples[0].delay, _runtime->samples[0].timestamp);

    if (_runtime->nsamples < NTP_SAMPLE_COUNT)
    {
        _runtime->nsamples += 1;
    }

    // compute the delay mean and std deviation

    double mean = 0.0;
    for (i = 0; i < _runtime->nsamples; ++i)
    {
        mean = mean + _runtime->samples[i].delay;
    }
    mean = mean / _runtime->nsamples;
    double delay_std = 0.0;
    for (i = 0; i < _runtime->nsamples; ++i)
    {
        delay_std = delay_std + pow(_runtime->samples[i].delay - mean, 2);
    }
    delay_std = SQRT(delay_std / _runtime->nsamples);
    dlog.info(FPSTR(TAG), F("::packet: delay STD DEV: %lf, mean: %lf"), delay_std, mean);

    //
    // don't use this offset if its off of the mean by moth than one std deviation
    if ((fabs(_runtime->samples[0].delay) - mean) > delay_std)
    {
        dlog.info(FPSTR(TAG), F("::packet: sample delay too big!"));
        return -1;
    }

    //
    // good delay - we can mark this as reachable
    //
    _runtime->reach |= 1;

    //
    // update drift estimate
    //
    updateDriftEstimate();

    //
    // don't use this offset if it does not meet the threshold
    //
    if (fabs(offset) < NTP_OFFSET_THRESHOLD)
    {
        dlog.info(FPSTR(TAG), F("::packet: offset not big enough for adjust!"));
        return -1;
    }

    //
    // save adjustment samples & compute drift
    //
    clock();

    //
    // sample ok to use.
    //
    return 0;
}

//
// process local clock, return drift
//
void NTP::clock()
{
    if (_runtime->nsamples >= NTP_SAMPLE_COUNT )
    {
        for (int i = _persist->nadjustments - 1; i >= 0; --i)
        {
            if (i == NTP_ADJUSTMENT_COUNT - 1)
            {
                continue;
            }
            _persist->adjustments[i + 1] = _persist->adjustments[i];
            dlog.info(FPSTR(TAG), F("::clock: adjustments[%d]: %lf timestamp:%u"),
                    i + 1, _persist->adjustments[i+1].adjustment, _persist->adjustments[i+1].timestamp);
        }

        // use the newest sample and include any drift we have applied.
        _persist->adjustments[0].timestamp  = _runtime->samples[0].timestamp;
        _persist->adjustments[0].adjustment = _runtime->samples[0].offset + _runtime->drifted;
        _runtime->drifted = 0.0;
        dlog.info(FPSTR(TAG), F("::clock: adjustments[%d]: %lf timestamp:%u"),
                0, _persist->adjustments[0].adjustment, _persist->adjustments[0].timestamp);

        if (_persist->nadjustments < NTP_ADJUSTMENT_COUNT)
        {
            _persist->nadjustments += 1;
        }

        //
        // calculate drift if we have some NTP_ADJUSTMENT_COUNT adjustments
        //
        if (_persist->nadjustments >= 4)
        {
            computeDrift(&_persist->drift);
            dlog.info(FPSTR(TAG), F("::clock: drift: %f"), _persist->drift);
        }
        dlog.debug(FPSTR(TAG), F("::clock: saving 'persist' data!"));
        _savePersist();
    }
}

//
// drift is the adjustment "per second" converted to parts per million
//
int NTP::computeDrift(double* drift_result)
{
    double a = 0.0;

    uint32_t seconds = 0;
    for(int i = 0; i <= _persist->nadjustments-2; ++i)
    {
        if (_persist->adjustments[i].timestamp != 0 &&  _persist->adjustments[i+1].timestamp != 0)
        {
            // valid sample!
            seconds += _persist->adjustments[i].timestamp - _persist->adjustments[i+1].timestamp;
            a += _persist->adjustments[i].adjustment;
            dlog.debug(FPSTR(TAG), F("::computeDrift: using adjustment %d and %d delta: %d adj:%f"), i, i+1, _persist->adjustments[i].timestamp - _persist->adjustments[i+1].timestamp, _persist->adjustments[i].adjustment);
        }
    }
    a = a / (double)seconds;
    double drift = a * 1000000;
    dlog.debug(FPSTR(TAG), F("::computeDrift: seconds: %d a: %f drift: %f"), seconds, a, drift);

    dlog.info(FPSTR(TAG), F("::computeDrift: drift: %f PPM"), drift);

    if (drift_result != NULL)
    {
        *drift_result = drift;
    }

    return 0;
}

void NTP::updateDriftEstimate()
{
    uint32_t timebase = _runtime->update_timestamp; // we only look at timestamps after this.
    // no update yet!
    if (timebase == 0)
    {
        timebase = _runtime->samples[_runtime->nsamples-1].timestamp;
    }
    double sx  = 0.0;
    double sy  = 0.0;
    double sxy = 0.0;
    double sxx = 0.0;
    int n = 0;
    for (int i = 0; i < _runtime->nsamples && _runtime->samples[i].timestamp >= timebase; ++i)
    {
        double x = (double)(_runtime->samples[i].timestamp - timebase);
        double y = _runtime->samples[i].offset;
        dlog.debug(FPSTR(TAG), F("::computeDriftEstimate: x:%-0.8f y:%-0.8f"), x, y);
        sx  += x;
        sy  += y;
        sxy += x*y;
        sxx += x*x;
        ++n;
    }

    dlog.debug(FPSTR(TAG), F("::computeDriftEstimate: found %d samples"), n);

    if (n < 4)
    {
        dlog.debug(FPSTR(TAG), F("::computeDriftEstimate: not enough points!"));
    }
    else
    {
        double slope = ( sx*sy - n*sxy ) / ( sx*sx - n*sxx );
        dlog.debug(FPSTR(TAG), F("::computeDriftEstimate: slope: %0.16f"), slope);

        _runtime->drift_estimate = slope * 1000000;

        _runtime->poll_interval = NTP_OFFSET_THRESHOLD / (fabs(_runtime->drift_estimate) / 1000000.0);
        dlog.info(FPSTR(TAG), F("::updateDriftEstimate: poll interval: %f"), _runtime->poll_interval);
    }

    dlog.info(FPSTR(TAG), F("::computeDriftEstimate: ESTIMATED DRIFT: %0.16f"), _runtime->drift_estimate);
}
