/*
 * I2CAnalogClock.h
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

#include "I2CAnalogClock.h"

volatile uint16_t position; // This is the position that we believe the clock is in.
volatile uint16_t adjustment;   // This is the adjustment to be made.
volatile uint8_t tp_duration;
volatile uint8_t tp_duty;
volatile uint8_t ap_duration;
volatile uint8_t ap_duty;
volatile uint8_t ap_delay;     // delay in ms between ticks during adjustment
#if defined(DRV8838)
volatile uint8_t sleep_delay;  // delay to sleep the DEV8838
#endif

volatile uint8_t control;       // This is our control "register".
volatile uint8_t status;        // status register (has tick bit)

volatile uint8_t command;       // This is which "register" to be read/written.

volatile unsigned int pwm_duration;
volatile bool adjust_active;
volatile unsigned int receives;
volatile unsigned int requests;
volatile unsigned int errors;
volatile unsigned int ticks;
volatile unsigned int control_count;
volatile unsigned int id_count;

// i2c receive handler
void i2creceive(int size)
{
    ++receives;
    command = Wire.read();
    --size;
    // check for a write command
    if (size > 0)
    {
        switch (command)
        {
        case CMD_POSITION:
            position = Wire.read() | Wire.read() << 8;
            break;
        case CMD_ADJUSTMENT:
            adjustment = Wire.read() | Wire.read() << 8;
            // adjustment will start on the next tick!
            break;
        case CMD_TP_DURATION:
            tp_duration = Wire.read();
            break;
        case CMD_TP_DUTY:
            tp_duty = Wire.read();
            break;
        case CMD_AP_DURATION:
            ap_duration = Wire.read();
            break;
        case CMD_AP_DUTY:
            ap_duty = Wire.read();
            break;
        case CMD_AP_DELAY:
            ap_delay = Wire.read();
            break;
#if defined(DRV8838)
        case CMD_SLEEP_DELAY:
            sleep_delay = Wire.read();
            break;
#endif
        case CMD_CONTROL:
            control = Wire.read();
            ++control_count;
            break;
        default:
            ++errors;
        }
        command = 0xff;
    }
}

// i2c request handler
void i2crequest()
{
    ++requests;
    uint16_t value;
    switch (command)
    {
    case CMD_ID:
        Wire.write(ID_VALUE);
        ++id_count;
        break;
    case CMD_POSITION:
        value = position;
        Wire.write(value & 0xff);
        Wire.write(value >> 8);
        break;
    case CMD_ADJUSTMENT:
        value = adjustment;
        Wire.write(value & 0xff);
        Wire.write(value >> 8);
        break;
    case CMD_TP_DURATION:
        value = tp_duration;
        Wire.write(value);
        break;
    case CMD_TP_DUTY:
        value = tp_duty;
        Wire.write(value);
        break;
    case CMD_AP_DURATION:
        value = ap_duration;
        Wire.write(value);
        break;
    case CMD_AP_DUTY:
        value = ap_duty;
        Wire.write(value);
        break;
    case CMD_AP_DELAY:
        value = ap_delay;
        Wire.write(value);
        break;
#if defined(DRV8838)
    case CMD_SLEEP_DELAY:
        value = sleep_delay;
        Wire.write(value);
        break;
#endif
    case CMD_CONTROL:
        size_t res;
        res = Wire.write(control);
        if (res == 0)
        {
            ++errors;
        }
        break;
    case CMD_STATUS:
        Wire.write(status);
        break;
    default:
        ++errors;
    }
    command = 0xff;
}

void (*timer_cb)();
volatile bool timer_running;
volatile unsigned int start_time;

#ifdef DEBUG_TIMER
volatile unsigned int starts;
volatile unsigned int stops;
volatile unsigned int ints;
volatile unsigned int last_duration;
volatile unsigned int stop_time;
#endif


void clearTimer()
{
#if defined(__AVR_ATtinyX5__)
    TCCR1 = 0;
    GTCCR = 0;
    TIMSK &= ~(_BV(TOIE1) | _BV(OCIE1A) | _BV(OCIE1A));
#else
    TCCR1A = 0;
    TCCR1B = 0;
    TIMSK1 = 0;
#endif
    OCR1A  = 0;
    OCR1B  = 0;
}

ISR(TIMER1_OVF_vect)
{
    if (pwm_duration == 0)
    {
        return;
    }

    pwm_duration -= 1;
    if (pwm_duration == 0)
    {
        clearTimer();
        timer_running = false;
        if (timer_cb != NULL)
        {
            timer_cb();
        }
    }
}

ISR(TIMER1_COMPA_vect)
{
    unsigned int int_time = millis();
#ifdef DEBUG_TIMER
    ints += 1;
#endif

    // why to we get an immediate interrupt? (ignore it)
    if (int_time == start_time || int_time == start_time+1)
    {
        return;
    }

#if defined(__AVR_ATtinyX5__) //|| defined(__AVR_ATtinyX4__)
    TIMSK &= ~(1 << OCIE1A); // disable timer1 interrupts as we only want this one.
#else
    TIMSK1 &= ~(1 << OCIE1A); // disable timer1 interrupts as we only want this one.
#endif
    timer_running = false;
    if (timer_cb != NULL)
    {
#ifdef DEBUG_TIMER
        stops+= 1;
        stop_time = int_time;
#endif
        timer_cb();
    }
}

void startTimer(int ms, void (*func)())
{
#ifdef DEBUG_TIMER
    starts += 1;
    last_duration = ms;
#endif
    start_time = millis();
    uint16_t timer = ms2Timer(ms);
    // initialize timer1
    noInterrupts();
    // disable all interrupts
    timer_cb = func;
#if defined(__AVR_ATtinyX5__) //|| defined(__AVR_ATtinyX4__)
    TCCR1 = 0;
    TCNT1 = 0;

    OCR1A = timer;   // compare match register
    TCCR1 |= (1 << CTC1);// CTC mode
    TCCR1 |= PRESCALE_BITS;
    TIMSK |= (1 << OCIE1A);// enable timer compare interrupt
    // clear any already pending interrupt?  does not work :-(
    TIFR &= ~(1 << OCIE1A);
#else
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;

    OCR1A = timer;   // compare match register
    TCCR1B |= (1 << WGM12);   // CTC mode
    TCCR1B |= PRESCALE_BITS;
    TIMSK1 |= (1 << OCIE1A);  // enable timer compare interrupt
    // clear any already pending interrupt?  does not work :-(
    TIFR1 &= ~(1 << OCIE1A);
#endif
    timer_running = true;
    interrupts();
    // enable all interrupts
}

void startPWM(unsigned int duration, unsigned int duty, void (*func)())
{
    noInterrupts();
    timer_cb = func;

    clearTimer();

    TCNT1 = 200; // needed???

    if (isTick())
    {
#if defined(__AVR_ATtinyX5__)
        TCCR1 = _BV(COM1A1) | _BV(PWM1A) | PWM_PRESCALE_BITS;
#else
        TCCR1A =  _BV(COM1A1) | _BV(WGM10);
#endif
        OCR1A = duty2pwm(duty);
    }
    else
    {
#if defined(__AVR_ATtinyX5__)
        TCCR1 = PWM_PRESCALE_BITS;
        GTCCR = _BV(COM1B1) | _BV(PWM1B);
#else
        TCCR1A =  _BV(COM1B1) | _BV(WGM10);
#endif
        OCR1B = duty2pwm(duty);
    }

#if defined(__AVR_ATtinyX5__)
    TIMSK |= _BV(TOIE1);
#else
    TCCR1B = _BV(WGM12) | PWM_PRESCALE_BITS;
    TIMSK1 = _BV(TOIE1);
#endif

    pwm_duration = ms2PWMCount(duration);;
    timer_running = true;
    interrupts();
}

#if !defined(USE_PWM)
void startTick()
{
#ifdef DRV8838
    digitalWrite(DRV_SLEEP, HIGH);
    delayMicroseconds(30); // data sheet says the DRV8838 take 30us to wake from sleep
    digitalWrite(DRV_PHASE, isTick());
    digitalWrite(DRV_ENABLE, TICK_ON);
#else
    if (isTick())
    {
        digitalWrite(A_PIN, TICK_ON);
#ifndef __AVR_ATtinyX5__
        digitalWrite(A2_PIN, TICK_ON);
#endif
    }
    else
    {
        digitalWrite(B_PIN, TICK_ON);
#ifndef __AVR_ATtinyX5__
        digitalWrite(B2_PIN, TICK_ON);
#endif
    }
#endif
}
#endif

void endTick()
{
#if !defined(USE_PWM)
#ifdef DRV8838
    digitalWrite(DRV_ENABLE, TICK_OFF);
    digitalWrite(DRV_PHASE, LOW); // per DRV8838 datasheet this reduces power usage
#else
    if (isTick())
    {
        digitalWrite(A_PIN, TICK_OFF);
#ifndef __AVR_ATtinyX5__
        digitalWrite(A2_PIN, TICK_OFF);
#endif
    }
    else
    {
        digitalWrite(B_PIN, TICK_OFF);
#ifndef __AVR_ATtinyX5__
        digitalWrite(B2_PIN, TICK_OFF);
#endif
    }
#endif
#endif

#ifdef TEST_MODE
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
#endif

    timer_cb = NULL;
    toggleTick();

    if (adjustment != 0)
    {
        adjustment--;
        if (adjustment != 0)
        {
            startTimer(ap_delay, &adjustClock);
        }
        else
        {
            //
            // we are done with adjustment, stop the timer
            // and schedule the sleep.
            //
            adjust_active = false;
#if defined(DRV8838)
            startTimer(sleep_delay, &sleepDRV8838);
#endif
        }
    }
    else
    {
        if (adjust_active)
        {
            adjust_active = false;
        }
#if defined(DRV8838)
        startTimer(sleep_delay, &sleepDRV8838);
#endif
    }
}

#if defined(DRV8838)
void sleepDRV8838()
{
    // only sleep the chip if we are not adjusting
    if (adjustment == 0)
    {
        digitalWrite(DRV_SLEEP, LOW);
    }
}
#endif

// advance the position
void advancePosition()
{
    position += 1;
    if (position >= MAX_SECONDS)
    {
        position = 0;
    }
}

void adjustClock()
{
#ifdef USE_PWM
            advanceClock(ap_duration, ap_duty);
#else
            advanceClock(ap_duration);
#endif
}

//
//  Advance the clock by one second.
//
#ifdef USE_PWM
void advanceClock(uint16_t duration, uint8_t duty)
#else
void advanceClock(uint16_t duration)
#endif
{
    advancePosition();
#if defined(USE_PWM)
    startPWM(duration, duty, &endTick);
#else
    startTick();
    startTimer(duration, &endTick);
#endif
}

void startAdjust()
{
    if (!adjust_active)
    {
        adjust_active = true;

#ifdef USE_PWM
            advanceClock(ap_duration, ap_duty);
#else
            advanceClock(ap_duration);
#endif
    }
}

//
// ISR for 1hz interrupt
//
void tick()
{
    ++ticks;
#if !defined(__AVR_ATtinyX5__) && !defined(__AVR_ATtinyX4__)
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
#endif
    if (isEnabled())
    {
        if (adjustment != 0)
        {
            ++adjustment;
            startAdjust();
        }
        else
        {
#ifdef USE_PWM
            advanceClock(tp_duration, tp_duty);
#else
            advanceClock(tp_duration);
#endif
        }
    }
    else
    {
        if (adjustment != 0)
        {
            startAdjust();
        }
#if defined(DRV8838)
        else
        {
            // start a timer to sleep the DRV - we just need a timer going
            // so we stay awake for a bit to receive commands.
            startTimer(sleep_delay, &sleepDRV8838);
        }
#endif
    }
}

void setup()
{
#ifdef DEBUG_I2CAC
    Serial.begin(SERIAL_BAUD);
    Serial.println("");
    Serial.println("Startup!");
#endif

#if !defined(__AVR_ATtinyX5__) && !defined(__AVR_ATtinyX4__)
    digitalWrite(LED_PIN, LOW);
    pinMode(LED_PIN, OUTPUT);
#endif

#ifndef TEST_MODE
    ADCSRA &= ~(1 << ADEN); // Disable ADC as we don't use it, saves ~230uA
    PRR |= (1 << PRADC); // Turn off ADC clock
#endif

    tp_duration   = DEFAULT_TP_DURATION_MS;
    tp_duty       = DEFAULT_TP_DUTY;
    ap_duration   = DEFAULT_AP_DURATION_MS;
    ap_duty       = DEFAULT_AP_DUTY;
    ap_delay      = DEFAULT_AP_DELAY_MS;
#if defined(DRV8838)
    sleep_delay   = DEFAULT_SLEEP_DELAY;
#endif
    adjust_active = false;

#ifdef SKIP_INITIAL_ADJUST
    position      = 0;
    adjustment    = 0;
#else
    //
    // we need a single adjust at startup to insure that the clock motor
    // is synched as a tick/tock.  This first tick will "misfire" if the motor
    // is out of sync and after that will be in sync.
    position      = MAX_SECONDS - 1;
    adjustment    = 1;
#endif

#if defined(TEST_MODE) || defined(START_ENABLED)
    control       = BIT_ENABLE;
#else
    control       = 0;
#endif

#ifndef TEST_MODE
    Wire.begin(I2C_ADDRESS);
    Wire.onReceive(&i2creceive);
    Wire.onRequest(&i2crequest);
#endif

    digitalWrite(A_PIN, TICK_OFF);
#ifndef __AVR_ATtinyX5__
    digitalWrite(A2_PIN, TICK_OFF);
#endif
    digitalWrite(B_PIN, TICK_OFF);
#ifndef __AVR_ATtinyX5__
    digitalWrite(B2_PIN, TICK_OFF);
#endif

#ifdef DRV8838
    digitalWrite(DRV_SLEEP, LOW);
    pinMode(DRV_SLEEP, OUTPUT);
#endif

    pinMode(A_PIN, OUTPUT);
#ifndef __AVR_ATtinyX5__
    pinMode(A2_PIN, OUTPUT);
#endif
    pinMode(B_PIN, OUTPUT);
#ifndef __AVR_ATtinyX5__
    pinMode(B2_PIN, OUTPUT);
#endif
#ifdef TEST_MODE
    digitalWrite(LED_PIN, LOW);
    pinMode(LED_PIN, OUTPUT);
#else
    pinMode(INT_PIN, INPUT);
    attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(INT_PIN), &tick, FALLING);
#endif
}

#ifdef DEBUG_I2CAC
unsigned int last_print;
unsigned int sleep_count;
#endif

void loop()
{
#ifdef USE_SLEEP
#ifdef USE_POWER_DOWN_MODE
    //
    // conserve power if i2c is not active and
    // there is no timer/PWM running
    //
    if (!timer_running && !Wire.isActive()) {
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    }
    else
    {
        set_sleep_mode(SLEEP_MODE_IDLE);
    }
#endif

#ifdef DEBUG_I2CAC
    sleep_count += 1;
#endif
    // sleep!
    sleep_enable();
    sleep_cpu();
    sleep_disable();
#endif

#ifdef DEBUG_I2CAC
    unsigned int now = ticks;
    char buffer[256];
    if (now != last_print && (now%10)==0)
    {
        last_print = now;
#ifdef DEBUG_TIMER
        if (timer_running)
        {
            Serial.println("timer running, adding delay!");
            delay(100);
        }
        int last_actual = stop_time - start_time;
        snprintf(buffer, 127, "starts:%d stops: %d ints:%d duration:%d actual:%d\n",
                starts, stops, ints, last_duration, last_actual);
        Serial.print(buffer);
#endif
#ifdef DEBUG_POSITION
        snprintf(buffer, 255, "position:%u adjustment:%u control:0x%02x status:0x%02x drvsleep:%d adjust_active:%d sleep_count:%u control_count:%u id_count:%u\n",
                position, adjustment, control, status, digitalRead(DRV_SLEEP), adjust_active, sleep_count, control_count, id_count);
        Serial.print(buffer);
#endif
#ifdef DEBUG_I2C
        snprintf(buffer, 127, "receives:%d requests:%d errors: %d\n",
                receives, requests, errors);
        Serial.print(buffer);
#endif
    }
#endif
#ifndef USE_SLEEP
#ifdef TEST_MODE
    if (!timer_running)
    {
        tick();
        delay(1000-tp_duration);
    }
#else
    delay(100);
#endif
#endif
}
