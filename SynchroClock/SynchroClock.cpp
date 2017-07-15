/*
 * SynchroClock.cpp
 *
 * Copyright 2017 Christopher B. Liebman
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 *  Created on: May 26, 2017
 *      Author: liebman
 */

#include "SynchroClock.h"

#define DEBUG
#include "Logger.h"

//
// These are only used when debugging
//
//#define DISABLE_DEEP_SLEEP
//#define DISABLE_INITIAL_NTP
//#define DISABLE_INITIAL_SYNC
//#define HARD_CODE_WIFI

#ifdef HARD_CODED_WIFI
#define HARD_CODED_SSID "Whatever"
#define HARD_CODED_PASS "the password"
// define to declare clock position if its not already running.
//#define HARD_CODED_POSITION "00:00:00"
#endif

Config           config;                    // configuration persisted in the EEPROM
DeepSleepData    dsd;                       // data persisted in the RTC memory
FeedbackLED      feedback(LED_PIN);         // used to blink LED to indicate status
ESP8266WebServer HTTP(80);                  // used when debugging/stay awake mode
NTP              ntp(&(dsd.ntp_persist));   // handles NTP communication & filtering
Clock            clk(SYNC_PIN);             // clock ticker, manages position of clock
DS3231           rtc;                       // real time clock on i2c interface

boolean save_config  = false; // used by wifi manager when settings were updated.
boolean force_config = false; // reset handler sets this to force into config mode if button held
boolean stay_awake   = false; // don't use deep sleep (from config mode option)

char message[128]; // buffer for http return values

boolean parseBoolean(const char* value)
{
    if (!strcmp(value, "true") || !strcmp(value, "True") || !strcmp(value, "TRUE") || atoi(value))
    {
        return true;
    }
    return false;
}

int getValidOffset(String name)
{
    int result = TimeUtils::parseOffset(HTTP.arg(name).c_str());

    return result;
}

uint16_t getValidPosition(String name)
{
    int result = TimeUtils::parsePosition(HTTP.arg(name).c_str());

    return result;
}

uint8_t getValidDuration(String name)
{
    uint8_t result = TimeUtils::parseSmallDuration(HTTP.arg(name).c_str());
    return result;
}

boolean getValidBoolean(String name)
{
    boolean result = parseBoolean(HTTP.arg(name).c_str());
    return result;
}

void handleOffset()
{
    if (HTTP.hasArg("set"))
    {
        config.tz_offset = getValidOffset("set");
        dbprintf("seconds offset:%d\n", config.tz_offset);
        saveConfig();
    }

    HTTP.send(200, "text/plain", String(config.tz_offset) + "\n");
}

void handleAdjustment()
{
    uint16_t adj;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        if (HTTP.arg("set").equalsIgnoreCase("auto"))
        {
            dbprintln("Auto Adjust!");
            setCLKfromRTC();
        }
        else
        {
            adj = getValidPosition("set");
            dbprintf("setting adjustment:%u\n", adj);
            if (clk.writeAdjustment(adj))
            {
                dbprintln("failed to set adjustment!");
            }
        }
    }

    if (clk.readAdjustment(&adj))
    {
        sprintf(message, "failed to read adjustment!\n");
    }
    else
    {
        sprintf(message, "adjustment: %d\n", adj);
    }

    clk.setStayActive(false);

    HTTP.send(200, "text/plain", message);
}

void handlePosition()
{
    uint16_t pos;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        pos = getValidPosition("set");
        dbprintf("setting position:%u\n", pos);
        if (clk.writePosition(pos))
        {
            dbprintln("failed to set position!");
        }
    }

    if (clk.readPosition(&pos))
    {
        sprintf(message, "failed to read position!\n");
    }
    else
    {
        int hours = pos / 3600;
        int minutes = (pos - (hours * 3600)) / 60;
        int seconds = pos - (hours * 3600) - (minutes * 60);
        sprintf(message, "position: %d (%02d:%02d:%02d)\n", pos, hours, minutes, seconds);
    }

    clk.setStayActive(false);

    HTTP.send(200, "text/Plain", message);
}

void handleTPDuration()
{
    uint8_t value;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        value = getValidDuration("set");
        dbprintf("setting tp_duration:%u\n", value);
        if (clk.writeTPDuration(value))
        {
            dbprintln("failed to set TP duration!");
        }
    }

    if (clk.readTPDuration(&value))
    {
        sprintf(message, "failed to read TP duration\n");
    }
    else
    {
        sprintf(message, "TP duration: %u\n", value);
    }
    clk.setStayActive(false);

    HTTP.send(200, "text/plain", message);
}

void handleAPDuration()
{
    uint8_t value;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        value = getValidDuration("set");
        dbprintf("setting ap_duration:%u\n", value);
        if (clk.writeAPDuration(value))
        {
            dbprintln("failed to set AP duration");
        }
    }

    if (clk.readAPDuration(&value))
    {
        sprintf(message, "failed to read AP duration\n");
    }
    else
    {
        sprintf(message, "AP duration: %u\n", value);
    }
    clk.setStayActive(false);

    HTTP.send(200, "text/plain", message);
}

void handleAPDelay()
{
    uint8_t value;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        value = getValidDuration("set");
        dbprintf("setting ap_delay:%u\n", value);
        if (clk.writeAPDelay(value))
        {
            dbprintln("failed to set AP delay");
        }
    }

    if (clk.readAPDelay(&value))
    {
        sprintf(message, "failed to read AP delay\n");
    }
    else
    {
        sprintf(message, "AP delay: %u\n", value);
    }
    clk.setStayActive(false);

    HTTP.send(200, "text/plain", message);
}

void handleSleepDelay()
{
    uint8_t value;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        value = getValidDuration("set");
        dbprintf("setting sleep_delay:%u\n", value);
        if (clk.writeSleepDelay(value))
        {
            dbprintln("failed to set sleep delay");
        }
    }

    if (clk.readSleepDelay(&value))
    {
        sprintf(message, "failed to read sleep delay\n");
    }
    else
    {
        sprintf(message, "sleep delay: %u\n", value);
    }
    clk.setStayActive(false);

    HTTP.send(200, "text/plain", String(value) + "\n");
}

void handleEnable()
{
    boolean enable;
    clk.setStayActive(true);
    if (HTTP.hasArg("set"))
    {
        enable = getValidBoolean("set");
        clk.setEnable(enable);
    }
    enable = clk.getEnable();
    clk.setStayActive(false);
    HTTP.send(200, "text/Plain", String(enable) + "\n");
}

void handleRTC()
{
    char message[64];
    DS3231DateTime dt;
    int err = rtc.readTime(dt);

    if (!err)
    {
        dbprintf("handleRTC: RTC : %s (UTC)\n", dt.string());

        clk.setStayActive(true);

        if (HTTP.hasArg("offset"))
        {
            double offset = strtod(HTTP.arg("offset").c_str(), NULL);
            dbprintf("handleRTC: offset: %lf\n", offset);
            setRTCfromOffset(offset, true);
        }
        else if (HTTP.hasArg("sync") && getValidBoolean("sync"))
        {
            setCLKfromRTC();
        }

        uint16_t value = dt.getPosition(config.tz_offset);

        clk.setStayActive(false);
        int hours = value / 3600;
        int minutes = (value - (hours * 3600)) / 60;
        int seconds = value - (hours * 3600) - (minutes * 60);
        sprintf(message, "%d (%02d:%02d:%02d)\n", value, hours, minutes, seconds);
    }
    else
    {
        sprintf(message, "rtc.readTime() failed!\n");
    }

    HTTP.send(200, "text/plain", message);
}

void handleNTP()
{
    char server[64];
    strcpy(server, config.ntp_server);
    boolean sync = false;

    if (HTTP.hasArg("server"))
    {
        dbprintf("SERVER: %s\n", HTTP.arg("server").c_str());
        strncpy(server, HTTP.arg("server").c_str(), 63);
        server[63] = 0;
    }

    if (HTTP.hasArg("sync"))
    {
        sync = getValidBoolean("sync");
    }

    clk.setStayActive(true);

    double offset;
    IPAddress address;
    char message[64];
    int code;
    if (setRTCfromNTP(server, sync, &offset, &address))
    {
        code = 500;
        snprintf(message, 64, "NTP Failed!\n");
    }
    else
    {
        if (sync)
        {
            dbprintln("Syncing clock to RTC!");
            setCLKfromRTC();
        }
        code = 200;

        snprintf(message, 64, "OFFSET: %0.6lf (%s)\n", offset, address.toString().c_str());
    }

    clk.setStayActive(false);

    dbprintf(message);
    HTTP.send(code, "text/Plain", message);
    return;
}

void handleWire()
{
    char message[64];
    int rtn = WireUtils.clearBus();
    if (rtn != 0)
    {
        snprintf(message, 64, "I2C bus error. Could not clear\n");
        if (rtn == 1)
        {
            snprintf(message, 64, "SCL clock line held low\n");
        }
        else if (rtn == 2)
        {
            snprintf(message, 64, "SCL clock line held low by slave clock stretch\n");
        }
        else if (rtn == 3)
        {
            snprintf(message, 64, "SDA data line held low\n");
        }
    }
    else
    {
        snprintf(message, 64, "Recovered!\n");
    }

    HTTP.send(200, "text/Plain", message);
}

void initWiFi()
{
    dbprintln("starting wifi!");

    // setup wifi, blink let slow while connecting and fast if portal activated.
    feedback.blink(FEEDBACK_LED_SLOW);

    WiFiManager wifi(30);
    wifi.setDebugOutput(false);
    wifi.setConnectTimeout(CONNECTION_TIMEOUT);

    wifi.setSaveConfigCallback([]()
    {
        save_config = true;
    });
    wifi.setAPCallback([](WiFiManager *)
    {
        feedback.blink(FEEDBACK_LED_FAST);
    });

    ConfigParam offset(wifi, "offset", "Time Zone", config.tz_offset, 8, [](const char* result)
    {
        config.tz_offset = TimeUtils::parseOffset(result);
        dbprintf("setting tz_offset to %d\n", config.tz_offset);
    });
    ConfigParam position(wifi, "position", "Clock Position", "", 8, [](const char* result)
    {
        if (strlen(result))
        {
            uint16_t position = TimeUtils::parsePosition(result);
            dbprintf("setting position to %d\n", position);
            if (clk.writePosition(position))
            {
                dbprintln("failed to set initial position!");
            }
        }
    });
    ConfigParam ntp_server(wifi, "ntp_server", "NTP Server", config.ntp_server, 32, [](const char* result)
    {
        strncpy(config.ntp_server, result, sizeof(config.ntp_server) - 1);
    });

    ConfigParam tc1_label(wifi, "<p>1st Time Change</p>");
    ConfigParam tc1_occurence(wifi, "tc1_occurrence", "occurrence", config.tc[0].occurrence, 2, [](const char* result)
    {
        config.tc[0].occurrence = TimeUtils::parseOccurrence(result);
    });
    ConfigParam tc1_day_of_week(wifi, "tc1_day_of_week", "Day of Week (Sun=0)", config.tc[0].day_of_week, 2, [](const char* result)
    {
        config.tc[0].day_of_week = TimeUtils::parseDayOfWeek(result);
    });
    ConfigParam tc1_month(wifi, "tc1_month", "Month (Jan=1)", config.tc[0].month, 3, [](const char* result)
    {
        config.tc[0].month = TimeUtils::parseMonth(result);
    });
    ConfigParam tc1_hour(wifi, "tc1_hour", "Hour (0-23)", config.tc[0].hour, 3, [](const char* result)
    {
        config.tc[0].hour = TimeUtils::parseHour(result);
    });
    ConfigParam tc1_offset(wifi, "tc1_offset", "Time Offset", config.tc[0].tz_offset, 8, [](const char* result)
    {
        config.tc[0].tz_offset = TimeUtils::parseOffset(result);
    });

    ConfigParam tc2_label(wifi, "<p>2ns Time Change</p>");
    ConfigParam tc2_occurence(wifi, "tc2_occurrence", "occurrence", config.tc[1].occurrence, 2, [](const char* result)
    {
        config.tc[1].occurrence = TimeUtils::parseOccurrence(result);
    });
    ConfigParam tc2_day_of_week(wifi, "tc2_day_of_week", "Day of Week (Sun=0)", config.tc[1].day_of_week, 2, [](const char* result)
    {
        config.tc[1].day_of_week = TimeUtils::parseDayOfWeek(result);
    });
    ConfigParam tc2_month(wifi, "tc2_month", "Month (Jan=1)", config.tc[1].month, 3, [](const char* result)
    {
        config.tc[1].month = TimeUtils::parseMonth(result);
    });
    ConfigParam tc2_hour(wifi, "tc2_hour", "Hour (0-23)", config.tc[1].hour, 3, [](const char* result)
    {
        config.tc[1].hour = TimeUtils::parseHour(result);
    });
    ConfigParam tc2_offset(wifi, "tc2_offset", "Time Offset", config.tc[1].tz_offset, 8, [](const char* result)
    {
        config.tc[1].tz_offset = TimeUtils::parseOffset(result);
    });

    ConfigParam advance_label(wifi, "<p>Advanced Settings!</p>");
    ConfigParam no_sleep(wifi, "stay_awake", "Stay Awake 'true'", "", 8, [](const char* result)
    {
        stay_awake = parseBoolean(result);
        dbprintf("no_sleep: result:'%s' -> stay_awake:%d\n", result, stay_awake);
    });
    ConfigParam sleep_duration(wifi, "sleep_duration", "Sleep", config.sleep_duration, 8, [](const char* result)
    {
        config.sleep_duration = atoi(result);
    });
    ConfigParam tp_duration(wifi, "tp_duration", "Tick Pulse", config.tp_duration, 8, [](const char* result)
    {
        config.tp_duration = TimeUtils::parseSmallDuration(result);
        clk.writeTPDuration(config.tp_duration);
    });
    ConfigParam ap_duration(wifi, "ap_duration", "Adjust Pulse", config.ap_duration, 4, [](const char* result)
    {
        config.ap_duration = TimeUtils::parseSmallDuration(result);
        clk.writeAPDuration(config.ap_duration);
    });
    ConfigParam ap_delay(wifi, "ap_delay", "Adjust Delay", config.ap_delay, 4, [](const char* result)
    {
        config.ap_delay = TimeUtils::parseSmallDuration(result);
        clk.writeAPDelay(config.ap_delay);
    });
    ConfigParam sleep_delay(wifi, "sleep_delay", "Sleep Delay", config.sleep_delay, 5, [](const char* result)
    {
        config.sleep_delay = TimeUtils::parseSmallDuration(result);
        clk.writeSleepDelay(config.sleep_delay);
    });
    ConfigParam network_logger_host(wifi, "network_logger_host", "Network Log Host", config.network_logger_host, 32, [](const char* result)
    {
        strncpy(config.network_logger_host, result, sizeof(config.network_logger_host) - 1);
    });
    ConfigParam network_logger_port(wifi, "network_logger_port", "Network Log Port", config.network_logger_port, 6, [](const char* result)
    {
        config.network_logger_port = atoi(result);
    });

    String ssid = "SynchroClock" + String(ESP.getChipId());
    dbflush();
#ifdef HARD_CODE_WIFI
    config.network_logger_host[0] = 0;
    save_config = true;
#ifdef HARD_CODED_POSITION
    if (!clk.getEnable())
    {
        uint16_t position = TimeUtils::parsePosition(HARD_CODED_POSITION);
        dbprintf("setting position to %d\n", position);
        if (clk.writePosition(position))
        {
            dbprintln("failed to set initial position!");
        }
    }
#endif
    Serial.print("HACK: Connecting to wifi");
    WiFi.begin(HARD_CODED_SSID, HARD_CODED_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("done!");
#else
    if (force_config)
    {
        wifi.startConfigPortal(ssid.c_str(), NULL);
    }
    else
    {
        wifi.autoConnect(ssid.c_str(), NULL);
    }
#endif
    feedback.off();

    //
    // if we are not connected then deep sleep and try again.
    if (!WiFi.isConnected())
    {
        dbprintln("failed to connect to wifi!");
        dbprintf("Deep Sleep Time: %d\n", MAX_SLEEP_DURATION);
        dbflush();
        dbend();
        ESP.deepSleep(MAX_SLEEP_DURATION, RF_DEFAULT);
    }

    //
    //  Config was set from captive portal!
    //
    if (save_config)
    {
        offset.applyIfChanged();
        position.applyIfChanged();
        ntp_server.applyIfChanged();
        tc1_occurence.applyIfChanged();
        tc1_day_of_week.applyIfChanged();
        tc1_month.applyIfChanged();
        tc1_hour.applyIfChanged();
        tc1_offset.applyIfChanged();
        tc2_occurence.applyIfChanged();
        tc2_day_of_week.applyIfChanged();
        tc2_month.applyIfChanged();
        tc2_hour.applyIfChanged();
        tc2_offset.applyIfChanged();
        no_sleep.applyIfChanged();
        tp_duration.applyIfChanged();
        ap_duration.applyIfChanged();
        ap_delay.applyIfChanged();
        sleep_delay.applyIfChanged();
        sleep_duration.applyIfChanged();
        network_logger_host.applyIfChanged();
        network_logger_port.applyIfChanged();
        saveConfig();
    }

    // Configure network logging if enabled
    dbnetlog(config.network_logger_host, config.network_logger_port);

    IPAddress ip = WiFi.localIP();
    dbprintf("Connected! IP address is: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
}

void setup()
{
    dbbegin(115200);
    dbprintln("");
    dbprintln("Startup!");

    uint32_t sleep_duration = MAX_SLEEP_DURATION;
    RFMode mode = RF_DEFAULT;

    pinMode(SYNC_PIN, INPUT);
    pinMode(FACTORY_RESET_PIN, INPUT);
    feedback.off();

    //
    // lets read deep sleep data and see if we need to immed go back to sleep.
    //
    memset(&dsd, 0, sizeof(dsd));
    readDeepSleepData();

    Wire.begin();

    while (rtc.begin())
    {
        dbprintln("RTC begin failed! Attempting recovery...");

        while (WireUtils.clearBus())
        {
            delay(10000);
            dbprintln("lets try that again...");
        }
        delay(1000);
    }

    DS3231DateTime dt;
    while (rtc.readTime(dt))
    {
        dbprintln("read RTC failed! Attempting recovery...");
        //
        //
        while (WireUtils.clearBus())
        {
            delay(10000);
            dbprintln("lets try that again...");
        }
        delay(10000);
    }
    dbprintf("intial RTC: %s\n", dt.string());

    //
    // initialize config to defaults then load.
    memset(&config, 0, sizeof(config));
    config.drift = 0.0;
    config.sleep_duration = DEFAULT_SLEEP_DURATION;
    config.tz_offset = 0;

    //
    // make sure the device is available!
    //
    feedback.blink(0.9);
    dbprintln("starting clock interface");
    while (clk.begin())
    {
        dbprintln("can't talk with Clock Controller!");
        while (WireUtils.clearBus())
        {
            delay(10000);
            dbprintln("lets try that again...");
        }
        delay(10000);
    }
    dbprintln("clock interface started");
    feedback.off();

    clk.setStayActive(true);

    uint8_t value;
    config.tp_duration = clk.readTPDuration(&value) ? DEFAULT_TP_DURATION : value;
    config.ap_duration = clk.readAPDuration(&value) ? DEFAULT_AP_DURATION : value;
    config.ap_delay = clk.readAPDelay(&value) ? DEFAULT_AP_DELAY : value;
    config.sleep_delay = clk.readSleepDelay(&value) ? DEFAULT_SLEEP_DELAY : value;
    strncpy(config.ntp_server, DEFAULT_NTP_SERVER, sizeof(config.ntp_server) - 1);
    config.ntp_server[sizeof(config.ntp_server) - 1] = 0;

    // Default to disabled (all tz_offsets = 0)
    config.tc[0].occurrence = 1;
    config.tc[0].day_of_week = 0;
    config.tc[0].month = 1;
    config.tc[0].hour = 0;
    config.tc[0].tz_offset = 0;
    config.tc[1].occurrence = 1;
    config.tc[1].day_of_week = 0;
    config.tc[1].month = 1;
    config.tc[1].hour = 0;
    config.tc[1].tz_offset = 0;

    dbprintf("defaults: tz:%d tp:%u,%u ap:%u ntp:%s logging: %s:%d\n", config.tz_offset, config.tp_duration, config.ap_duration, config.ap_delay,
            config.ntp_server, config.network_logger_host, config.network_logger_port);

    EEPROM.begin(sizeof(EEConfig));
    delay(100);
    // if the saved config was not good then force config.
    if (!loadConfig())
    {
        force_config = true;
    }

    dbprintf("config: tz:%d tp:%u,%u ap:%u ntp:%s logging: %s:%d\n", config.tz_offset, config.tp_duration, config.ap_duration, config.ap_delay,
            config.ntp_server, config.network_logger_host, config.network_logger_port);

    dt.applyOffset(config.tz_offset);
    int new_offset = TimeUtils::computeUTCOffset(dt.getYear(), dt.getMonth(), dt.getDate(), dt.getHour(), config.tc,
    TIME_CHANGE_COUNT);

    // if the time zone changed then save the new value and set the the clock
    if (config.tz_offset != new_offset)
    {
        dbprintf("time zone offset changed from %d to %d\n", config.tz_offset, new_offset);
        config.tz_offset = new_offset;
        saveConfig();
        setCLKfromRTC();
    }

    // if the config button is pressed then force config
    if (digitalRead(FACTORY_RESET_PIN) == 0)
    {
        //
        // If we wake with the reset button pressed and sleep_delay_left then the radio is off
        // so set it to 0 and use a very short deepSleep to turn the radio back on.
        //
        if (dsd.sleep_delay_left != 0)
        {
            clk.setStayActive(false);
            dbprintln("reset button pressed with radio off, short sleep to enable!");
            dsd.sleep_delay_left = 0;
            writeDeepSleepData();
            ESP.deepSleep(1, RF_DEFAULT); // super short sleep to enable the radio!
        }
        dbprintln("reset button pressed, forcing config!");
        force_config = true;
    }

    dbprintf("sleep_delay_left: %lu\n", dsd.sleep_delay_left);

    if (dsd.sleep_delay_left != 0)
    {

        if (dsd.sleep_delay_left > MAX_SLEEP_DURATION)
        {
            dsd.sleep_delay_left = dsd.sleep_delay_left - MAX_SLEEP_DURATION;
            mode = RF_DISABLED;
            dbprintf("delay still greater than max, mode=DISABLED sleep_delay_left=%lu\n", dsd.sleep_delay_left);
        }
        else
        {
            sleep_duration = dsd.sleep_delay_left;
            dsd.sleep_delay_left = 0;
            dbprintf("delay less than max, mode=DEFAULT sleep_delay_left=%lu\n", dsd.sleep_delay_left);
        }

        clk.setStayActive(false);
        writeDeepSleepData();
        dbprintf("Deep Sleep Time: %lu\n", sleep_duration);
        dbflush();
        dbend();
        ESP.deepSleep(sleep_duration * 1000000, mode);
    }

    //
    // clock parameters could have changed, set them
    clk.writeTPDuration(config.tp_duration);
    clk.writeAPDuration(config.ap_duration);
    clk.writeAPDelay(config.ap_delay);

    boolean enabled = clk.getEnable();
    dbprintf("clock enable is:%u\n", enabled);

    //
    // if the clock is not running advance it to sync tick/tock
    //
#ifndef DISABLE_DEEP_SLEEP
    if (!enabled)
    {
        force_config = true; // force the config portal to set the position if the clock is not running
    }
#endif

    initWiFi();

    dbprintf("###### rtc data size: %d\n", sizeof(RTCDeepSleepData));

    ntp.begin(NTP_PORT, config.drift);

    if (!enabled)
    {
        dbprintln("enabling clock");
        clk.setEnable(true);
    }
    else
    {
        dbprintln("clock is enabled, skipping init of RTC");
    }

#ifndef DISABLE_INITIAL_NTP
    dbprintln("syncing RTC from NTP!");
    setRTCfromNTP(config.ntp_server, true, NULL, NULL);
#endif
#ifndef DISABLE_INITIAL_SYNC
    dbprintln("syncing clock to RTC!");
    setCLKfromRTC();
#endif

    clk.setStayActive(false);

#ifdef DISABLE_DEEP_SLEEP
    stay_awake = true;
#endif

    if (stay_awake)
    {
        dbprintln("starting HTTP");
        HTTP.on("/offset", HTTP_GET, handleOffset);
        HTTP.on("/adjust", HTTP_GET, handleAdjustment);
        HTTP.on("/position", HTTP_GET, handlePosition);
        HTTP.on("/tp_duration", HTTP_GET, handleTPDuration);
        HTTP.on("/ap_duration", HTTP_GET, handleAPDuration);
        HTTP.on("/ap_delay", HTTP_GET, handleAPDelay);
        HTTP.on("/sleep_delay", HTTP_GET, handleSleepDelay);
        HTTP.on("/enable", HTTP_GET, handleEnable);
        HTTP.on("/rtc", HTTP_GET, handleRTC);
        HTTP.on("/ntp", HTTP_GET, handleNTP);
        HTTP.on("/wire", HTTP_GET, handleWire);
        HTTP.begin();
    }
    else
    {
        dsd.sleep_delay_left = 0;
        sleep_duration = config.sleep_duration;
        mode = RF_DEFAULT;
        if (sleep_duration > MAX_SLEEP_DURATION)
        {
            dsd.sleep_delay_left = sleep_duration - MAX_SLEEP_DURATION;
            sleep_duration = MAX_SLEEP_DURATION;
            mode = RF_DISABLED;
            dbprintf("sleep_duration > max, mode=DISABLE, sleep_delay_left = %lu\n", dsd.sleep_delay_left);
        }
        writeDeepSleepData();
        dbprintf("Deep Sleep Time: %lu\n", sleep_duration);
        dbflush();
        delay(100);
        dbend();
        ESP.deepSleep(sleep_duration * 1000000, mode);
    }
}

void loop()
{
    if (stay_awake)
    {
        HTTP.handleClient();
    }
    delay(100);
}

int setRTCfromOffset(double offset, bool sync)
{
    int32_t seconds = floor(offset);
    uint32_t msdelay = fabs((offset - seconds) * 1000);
    if (offset > 0)
    {
        seconds = seconds + 1;
        msdelay = 1000 - msdelay;
    }
    dbprintf("offset: %lf seconds: %d msdelay: %d\n", offset, seconds, msdelay);

    DS3231DateTime dt;

    clk.waitForEdge(CLOCK_EDGE_FALLING);
    if (rtc.readTime(dt))
    {
        dbprintln("setRTCfromOffset: failed to read from RTC!");
        return ERROR_RTC;
    }

    // wait for where the next second should start
    if (msdelay > 0 && msdelay < 1000)
    {
        delay(msdelay);
    }

    uint32_t old_time = dt.getUnixTime();
    uint32_t new_time = old_time + seconds; // + 1; // +1 because we waited for the next second
    dt.setUnixTime(new_time);

    if (sync)
    {
        if (rtc.writeTime(dt))
        {
            dbprintf("setRTCfromOffset: FAILED to set RTC: %s\n", dt.string());
            return ERROR_RTC;
        }
    }

    dbprintf("old_time: %d new_time: %d\n", old_time, new_time);
    dbprintf("set RTC: %s\n", dt.string());
    return 0;
}

/*
 * sync to a second boundary and return the current time in seconds
 */
int getTime(uint32_t *result)
{
    clk.waitForEdge(CLOCK_EDGE_FALLING);

    DS3231DateTime dt;
    if (rtc.readTime(dt))
    {
        dbprintln("getTime: failed to read from RTC!");
        return -1;
    }
    *result = dt.getUnixTime();
    return 0;
}

int setRTCfromNTP(const char* server, bool sync, double* result_offset, IPAddress* result_address)
{
    dbprintf("using server: %s\n", server);

    // wait for the next falling edge of the 1hz square wave
    clk.waitForEdge(CLOCK_EDGE_FALLING);

    DS3231DateTime dt;
    if (rtc.readTime(dt))
    {
        dbprintln("setRTCfromNTP: failed to read from RTC!");
        return ERROR_RTC;
    }

    double offset;
    if (ntp.getOffset(server, &offset, &getTime))
    {
        dbprintf("setRTCfromNTP: NTP Failed!\n");
        return ERROR_NTP;
    }

    if (result_address)
    {
        *result_address = ntp.getAddress();
    }

    if (result_offset != NULL)
    {
        *result_offset = offset;
    }

    dbprintf("********* NTP OFFSET: %lf\n", offset);

    int error = setRTCfromOffset(offset, sync);
    if (error)
    {
        return error;
    }

    dbprintln("setRTCfromNTP: returning OK");
    return 0;
}

int setCLKfromRTC()
{
    // if there is already an adjustment in progress then stop it.
    if (clk.writeAdjustment(0))
    {
        dbprintln("setCLKfromRTC: failed to clear adjustment!");
        return -1;
    }

#ifdef USE_STOP_THE_CLOCK
    int delta;
    uint16_t clock_pos;
    uint16_t rtc_pos;
    do
    {
        clk.waitForEdge(CLOCK_EDGE_FALLING);
        delay(10);

        if (clk.readPosition(&clock_pos))
        {
            dbprintln("setCLKfromRTC: failed to read position, ignoring");
            return -1;
        }
        dbprintf("setCLKfromRTC: clock position:%d\n", clock_pos);

        DS3231DateTime dt;
        if (rtc.readTime(dt))
        {
            dbprintln("setCLKfromRTC: FAILED to read RTC");
            return -1;
        }
        rtc_pos = dt.getPosition(config.tz_offset);
        dbprintf("setCLKfromRTC: RTC position:%d\n", rtc_pos);

        delta = rtc_pos - clock_pos;
        if (delta < 0 && abs(delta) < STOP_THE_CLOCK_MAX)
        {
            int stop_for = abs(delta) + STOP_THE_CLOCK_EXTRA;
            dbprintf("setCLKfromRTC: stop the clock delta is %d stopping for %d seconds\n", delta, stop_for);

            // stop the clock for delta seconds
            clk.setEnable(false);
            delay(stop_for * 1000);
            clk.setEnable(true);
        }
        //
        // if we stopped the clock then repeat block to re-read clock & RTC
        //
    } while (delta < 0 && abs(delta) < STOP_THE_CLOCK_MAX);
#else
    clk.waitForEdge(CLOCK_EDGE_FALLING);
    delay(10);
    uint16_t clock_pos;
    if (clk.readPosition(&clock_pos))
    {
        dbprintln("setCLKfromRTC: failed to read position, ignoring");
        return -1;
    }
    dbprintf("setCLKfromRTC: clock position:%d\n", clock_pos);

    DS3231DateTime dt;
    if (rtc.readTime(dt))
    {
        dbprintln("setCLKfromRTC: FAILED to read RTC");
        return -1;
    }
    uint16_t rtc_pos = dt.getPosition(config.tz_offset);
    dbprintf("setCLKfromRTC: RTC position:%d\n", rtc_pos);
#endif

    if (clock_pos != rtc_pos)
    {
        int adj = rtc_pos - clock_pos;
        if (adj < 0)
        {
            adj += MAX_POSITION;
        }
        dbprintf("setCLKfromRTC: sending adjustment of %u\n", adj);
        clk.waitForEdge(CLOCK_EDGE_RISING);
        if (clk.writeAdjustment(adj))
        {
            dbprintln("setCLKfromRTC: failed to set adjustment!");
            return -1;
        }
    }

    return 0;
}

uint32_t calculateCRC32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffff;
    while (length--)
    {
        uint8_t c = *data++;
        for (uint32_t i = 0x80; i > 0; i >>= 1)
        {
            bool bit = crc & 0x80000000;
            if (c & i)
            {
                bit = !bit;
            }
            crc <<= 1;
            if (bit)
            {
                crc ^= 0x04c11db7;
            }
        }
    }
    return crc;
}

boolean loadConfig()
{
    EEConfig cfg;
    // Read struct from EEPROM
    dbprintln("loading config from EEPROM");
    uint8_t i;
    uint8_t* p = (uint8_t*) &cfg;
    for (i = 0; i < sizeof(cfg); ++i)
    {
        p[i] = EEPROM.read(i);
    }

    uint32_t crcOfData = calculateCRC32(((uint8_t*) &cfg.data), sizeof(cfg.data));
    //dbprintf("CRC32 of data: %08x\n", crcOfData);
    //dbprintf("CRC32 read from EEPROM: %08x\n", cfg.crc);
    if (crcOfData != cfg.crc)
    {
        dbprintln("CRC32 in EEPROM memory doesn't match CRC32 of data. Data is probably invalid!");
        return false;
    }
    //dbprintln("CRC32 check ok, data is probably valid.");
    memcpy(&config, &cfg.data, sizeof(config));
    return true;
}

void saveConfig()
{
    EEConfig cfg;
    memcpy(&cfg.data, &config, sizeof(cfg.data));
    cfg.crc = calculateCRC32(((uint8_t*) &cfg.data), sizeof(cfg.data));
    //dbprintf("caculated CRC: %08x\n", cfg.crc);
    //dbprintln("Saving config to EEPROM");

    uint8_t i;
    uint8_t* p = (uint8_t*) &cfg;
    for (i = 0; i < sizeof(cfg); ++i)
    {
        EEPROM.write(i, p[i]);
    }
    EEPROM.commit();
}

boolean readDeepSleepData()
{
    RTCDeepSleepData rtcdsd;
    dbprintln("loading deep sleep data from RTC Memory");
    if (!ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcdsd, sizeof(rtcdsd)))
    {
        dbprintln("readDeepSleepData: failed to read RTC Memory");
        return false;
    }

    uint32_t crcOfData = calculateCRC32(((uint8_t*) &rtcdsd.data), sizeof(rtcdsd.data));
    if (crcOfData != rtcdsd.crc)
    {
        dbprintln("CRC32 in RTC Memory doesn't match CRC32 of data. Data is probably invalid!");
        return false;
    }
    memcpy(&dsd, &rtcdsd.data, sizeof(dsd));
    return true;
}

boolean writeDeepSleepData()
{
    RTCDeepSleepData rtcdsd;
    memcpy(&rtcdsd.data, &dsd, sizeof(rtcdsd.data));
    rtcdsd.crc = calculateCRC32(((uint8_t*) &rtcdsd.data), sizeof(rtcdsd.data));

    if (!ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcdsd, sizeof(rtcdsd)))
    {
        dbprintln("writeDeepSleepData: failed to write RTC Memory");
        return false;
    }
    return true;
}
