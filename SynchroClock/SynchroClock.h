/*
 * SynchroClock.h
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

#ifndef _SynchroClock_H_
#define _SynchroClock_H_
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "FeedbackLED.h"
#include "NTP.h"
#include "Clock.h"
#include "DS3231.h"
#include "WireUtils.h"
#include "TimeUtils.h"
#include "ConfigParam.h"

#define USE_DRIFT                   // apply drift
#define USE_NTP_POLL_ESTIMATE       // use ntp estimated drift for sleep duration calculation
#define USE_STOP_THE_CLOCK          // if defined then stop the clock for small negative adjustments
#define STOP_THE_CLOCK_MAX     60   // maximum difference where we will use stop the clock
#define STOP_THE_CLOCK_EXTRA   2    // extra seconds to leave the clock stopped
#define USE_US_PACIFIC_TIMECHANGE   // use US/Pacific timezone by default

// pin definitions
#define LED_PIN                D7   // (GPIO13) LED on pin, active low
#define SYNC_PIN               D5   // (GPIO14) pin tied to 1hz square wave from RTC
#define CONFIG_PIN             D6   // (GPIO12) button tied to pin

#define DEFAULT_TZ_OFFSET      0    // default timzezone offset in seconds
#define DEFAULT_NTP_SERVER     "0.zoddotcom.pool.ntp.org"
#define DEFAULT_SLEEP_DURATION 28800 // default is 8hrs when we are not using the poll estimate

#define CLOCK_STRETCH_LIMIT    1500 // i2c clock stretch timeout in microseconds
#define MAX_SLEEP_DURATION     3600 // we do multiple sleep of this to handle bigger sleeps
#define CONNECTION_TIMEOUT     300  // wifi portal timeout - we will deep sleep and try again later

#define offset2longDouble(x)   ((long double)x / 4294967296L)

#if defined(USE_US_PACIFIC_TIMECHANGE)
#define DEFAULT_TC0_OCCUR  2
#define DEFAULT_TC0_DOW    0
#define DEFAULT_TC0_MONTH  3
#define DEFAULT_TC0_HOUR   2
#define DEFAULT_TC0_OFFSET -25200
#define DEFAULT_TC1_OCCUR  1
#define DEFAULT_TC1_DOW    0
#define DEFAULT_TC1_MONTH  11
#define DEFAULT_TC1_HOUR   2
#define DEFAULT_TC1_OFFSET -28800
#else // default no offset/no time changes
#define DEFAULT_TC0_OCCUR  1
#define DEFAULT_TC0_DOW    0
#define DEFAULT_TC0_MONTH  0
#define DEFAULT_TC0_HOUR   0
#define DEFAULT_TC0_OFFSET 0
#define DEFAULT_TC1_OCCUR  1
#define DEFAULT_TC1_DOW    0
#define DEFAULT_TC1_MONTH  0
#define DEFAULT_TC1_HOUR   0
#define DEFAULT_TC1_OFFSET 0
#endif


// error codes for setRTCfromNTP()
#define ERROR_DNS -1
#define ERROR_RTC -2
#define ERROR_NTP -3

#define TIME_CHANGE_COUNT  2

typedef struct config
{
    uint32_t   sleep_duration;           // deep sleep duration in seconds
    int        tz_offset;                // time offset in seconds from UTC
    uint16_t   network_logger_port;      // port for network logging
    TimeChange tc[TIME_CHANGE_COUNT];    // time change description
    char       ntp_server[64];           // host to use for ntp
    char       network_logger_host[64];  // host for network logging
    NTPPersist ntp_persist;              // ntp persisted data
} Config;

typedef struct ee_config
{
    uint32_t crc;
    uint8_t data[sizeof(Config)];
} EEConfig;

typedef struct deep_sleep_data
{
    uint32_t sleep_delay_left;          // number seconds still to sleep
    NTPRunTime ntp_runtime;             // NTP runtime data
} DeepSleepData;

typedef struct rtc_deep_sleep_data
{
    uint32_t crc;
    uint8_t data[sizeof(DeepSleepData)];
} RTCDeepSleepData;

boolean parseBoolean(const char* value);
uint8_t parseDuty(const char* value);
int getValidOffset(String name);
uint16_t getValidPosition(String name);
uint8_t getValidDuration(String name);
uint8_t getValidDuty(String name);
boolean getValidBoolean(String name);
void handleOffset();
void handleAdjustment();
void handlePosition();
void handleTPDuration();
void handleTPDuty();
void handleTPCount();
void handleAPDuration();
void handleAPDuty();
void handleAPCount();
void handleAPDelay();
void handleEnable();
void handleRTC();
void handleNTP();
void handleSave();
void sleepFor(uint32_t sleep_duration);
int setRTCfromOffset(double offset_ms, bool sync);
int getTime(uint32_t *result);
int setRTCfromDrift();
int setRTCfromNTP(const char* server, bool sync, double* result_offset, IPAddress* result_address);
int setCLKfromRTC();
void saveConfig();
boolean loadConfig();
boolean readDeepSleepData();
boolean writeDeepSleepData();

extern unsigned int snprintf(char*, unsigned int, ...); // because esp8266 does not declare it in a header.

#endif /* _SynchroClock_H_ */
