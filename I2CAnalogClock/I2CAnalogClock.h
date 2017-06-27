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

#ifdef __AVR_ATtinyX5__
#include "USIWire.h"
#else
#include "Wire.h"
#define DEBUG_I2CAC     1
#define DEBUG_I2C       1
//#define DEBUG_TIMER     1
#define SERIAL_BAUD     115200
#endif

#define USE_SLEEP
#define USE_POWER_DOWN_MODE 1
#define DRV8838         1

#ifdef __AVR_ATtinyX5__
#define INT_PIN         1
#define A_PIN           3
#define B_PIN           4
#define DRV_SLEEP       5 // reset pin!
//#define LED_PIN         LED_BUILTIN
#else
#define INT_PIN         3
#define A_PIN           4
#define B_PIN           5
#define DRV_SLEEP       6
#define LED_PIN         LED_BUILTIN
#endif


#ifdef DRV8838
#define DRV_PHASE       A_PIN
#define DRV_ENABLE      B_PIN
#endif

#define MAX_SECONDS     43200

#define TICK_ON         HIGH
#define TICK_OFF        LOW

#define I2C_ADDRESS     0x09

#define CMD_ID          0x00
#define CMD_POSITION    0x01
#define CMD_ADJUSTMENT  0x02
#define CMD_CONTROL     0x03
#define CMD_STATUS      0x04
#define CMD_TP_DURATION 0x05
#define CMD_AP_DURATION 0x07
#define CMD_AP_DELAY    0x09
#define CMD_SLEEP_DELAY 0x0a
#define CMD_STAY_ACTIVE 0x0b

// control register bits
#define BIT_ENABLE      0x80
#define BIT_STAY_ACTIVE 0x40

// status register bits
#define STATUS_BIT_TICK 0x01

#define ID_VALUE        0x42

#define isEnabled()     (control &  BIT_ENABLE)
#define isStayActive()  (control &  BIT_STAY_ACTIVE)

#define isTick()        (status &  STATUS_BIT_TICK)
#define toggleTick()    (status ^= STATUS_BIT_TICK)

//
// Timing defaults
//

#ifdef __AVR_ATtinyX5__
#define CLOCK_HZ        8000000
#define PRESCALE        4096
#define PRESCALE_BITS   ((1 << CS13) | (1 << CS12) | (1 <<CS10)) // 4096 prescaler
#define ms2Timer(x) ((uint8_t)(CLOCK_HZ /(PRESCALE * (1/((double)x/1000)))))
#else
#define CLOCK_HZ        16000000
#define PRESCALE        256
#define PRESCALE_BITS   (1 << CS12) // 256 prescaler
#define ms2Timer(x) ((uint16_t)(CLOCK_HZ /(PRESCALE * (1/((double)x/1000)))))
#endif

#define DEFAULT_TP_DURATION_MS 24  // pulse duration in ms.
#define DEFAULT_AP_DURATION_MS 16  // pulse duration during adjust
#define DEFAULT_AP_DELAY_MS    12  // delay between adjust pulses in ms.
#define DEFAULT_SLEEP_DELAY    50  // delay before sleeping the DEV8838

void startAdjust();
void adjustClock();
void advanceClock(uint16_t duration);
void tick();
void sleepDRV8838();

#endif /* _I2CAnalogClock_H_ */
