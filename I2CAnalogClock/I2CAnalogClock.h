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

#ifndef _I2CAnalogClock_H_
#define _I2CAnalogClock_H_
#include "Arduino.h"
#include "PinChangeInterrupt.h"
#include <avr/sleep.h>
#include "Wire.h"
#include "EEPROM.h"

#if !defined(__AVR_ATtinyX5__)
//#define DEBUG_I2CAC
//#define DEBUG_POSITION
#define SERIAL_BAUD     115200
#endif

//#define USE_4MHZ
//#define USE_1MHZ   // i2c does not work :-(
//#define START_ENABLED
//#define SKIP_INITIAL_ADJUST
//#define TEST_MODE


#ifndef TEST_MODE
#define USE_SLEEP
#define USE_POWER_DOWN_MODE
#endif

#ifdef __AVR_ATtinyX5__
#ifdef TEST_MODE
#define LED_PIN         3
#else
#define INT_PIN         3
#endif
#define A_PIN           1
#define B_PIN           4
#define PWRFAIL_PIN     5
#else
#define INT_PIN         3
#define A_PIN           9
#define B_PIN           10
#define PWRFAIL_PIN     2
#define LED_PIN         LED_BUILTIN
#endif

#define TICK_ON         HIGH
#define TICK_OFF        LOW

#define MAX_SECONDS     43200


#define I2C_ADDRESS     0x09

#define CMD_ID          0x00
#define CMD_POSITION    0x01
#define CMD_ADJUSTMENT  0x02
#define CMD_CONTROL     0x03
#define CMD_STATUS      0x04
#define CMD_TP_DURATION 0x05
#define CMD_AP_DURATION 0x07
#define CMD_AP_DELAY    0x09
#define CMD_TP_DUTY     0x0b
#define CMD_AP_DUTY     0x0c

// control register bits
#define BIT_ENABLE      0x80

// status register bits
#define STATUS_BIT_TICK 0x01

#define ID_VALUE        0x42

#define isEnabled()     (control &  BIT_ENABLE)

#define isTick()        (status &  STATUS_BIT_TICK)
#define toggleTick()    (status ^= STATUS_BIT_TICK)

//
// Timing defaults
//


#if defined(__AVR_ATtinyX5__)

#if defined(USE_1MHZ)
#undef F_CPU
#define F_CPU 1000000L
#elif defined(USE_4MHZ)
#undef F_CPU
#define F_CPU 4000000L
#endif

#if F_CPU == 8000000L
#define PWM_PRESCALE      4
#define PWM_PRESCALE_BITS (_BV(CS11) | _BV(CS10))
#elif F_CPU == 4000000L
#define PWM_PRESCALE      2
#define PWM_PRESCALE_BITS (_BV(CS11))
#elif F_CPU == 1000000L
#define PWM_PRESCALE      1
#define PWM_PRESCALE_BITS (_BV(CS10))
#endif
#else
#define PWM_PRESCALE      8
#define PWM_PRESCALE_BITS (_BV(CS11))
#endif

#define PWM_TOP           256
#define ms2PWMCount(x)    (F_CPU / PWM_PRESCALE / PWM_TOP / (1000 / (x)))
#define duty2pwm(x)       ((x)*PWM_TOP/100)

#ifdef __AVR_ATtinyX5__
#if F_CPU == 8000000L
#define PRESCALE        4096
#define PRESCALE_BITS   ((1 << CS13) | (1 << CS12) | (1 <<CS10)) // 4096 prescaler
#elif F_CPU == 4000000L
#define PRESCALE        2048
#define PRESCALE_BITS   ((1 << CS13) | (1 << CS12)) // 2048 prescaler
#elif F_CPU == 1000000L
#define PRESCALE        512
#define PRESCALE_BITS   ((1 << CS13) | (1 << CS11)) // 512 prescaler
#endif
#define ms2Timer(x) ((uint8_t)(F_CPU /(PRESCALE * (1/((double)x/1000)))))
#else
#define PRESCALE        256
#define PRESCALE_BITS   (1 << CS12) // 256 prescaler
#define ms2Timer(x) ((uint16_t)(F_CPU /(PRESCALE * (1/((double)x/1000)))))
#endif

#define DEFAULT_TP_DURATION_MS 38  // pulse duration in ms.
#define DEFAULT_TP_DUTY        60  // duty cycle %.
#define DEFAULT_AP_DURATION_MS 18  // pulse duration during adjust
#define DEFAULT_AP_DUTY        65  // duty cycle %.
#define DEFAULT_AP_DELAY_MS    9   // delay between adjust pulses in ms.

typedef struct power_fail_data
{
    uint16_t pfd_position;
    uint8_t  pfd_control;
    uint8_t  pfd_status;
    uint8_t  pfd_tp_duration;
    uint8_t  pfd_tp_duty;
    uint8_t  pfd_ap_duration;
    uint8_t  pfd_ap_duty;
    uint8_t  pfd_ap_delay;
} PowerFailData;

typedef struct ee_power_fail_data
{
    PowerFailData data;
    uint32_t      crc;
} EEPowerFailData;

void startAdjust();
void adjustClock();
void advanceClock(uint16_t duration, uint8_t duty);
void tick();

#if defined(PWRFAIL_PIN)
uint32_t calculateCRC32(const uint8_t *data, size_t length);
boolean loadPowerFailData();
void savePowerFailData();
#endif

#endif /* _I2CAnalogClock_H_ */
