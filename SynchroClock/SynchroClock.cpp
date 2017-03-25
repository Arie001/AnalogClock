// Do not remove the include below
#include "SynchroClock.h"

RtcDS3231<TwoWire> rtc(Wire);
FeedbackLED feedback(LED_PIN);
ESP8266WebServer HTTP(80);
SNTP ntp("time.apple.com", 123);
bool syncing = false;
uint8_t last_pin = 0;

uint16_t getValidPosition(String name)
{
    int result = 0;

    char value[10];
    strncpy(value, HTTP.arg(name).c_str(), 9);
    if (strchr(value, ':') != NULL)
    {
        char* s = strtok(value, ":");

        if (s != NULL) {
            result += atoi(s) * 3600; // hours to seconds
            s = strtok(NULL, ":");
        }
        if (s != NULL) {
            result += atoi(s) * 60; // minutes to seconds
            s = strtok(NULL, ":");
        }
        if (s != NULL) {
            result += atoi(s);
        }
    }
    else
    {
        result = atoi(value);
        if (result < 0 || result > 43199)
        {
            Serial.println(
                    "invalid value for " + name + ": " + HTTP.arg(name)
                    + " using 0 instead!");
            result = 0;
        }
    }
    return result;
}

uint16_t getValidCount(String name)
{
  int i = HTTP.arg(name).toInt();
  if (i < 0 || i > 100)
  {
    Serial.println(
        "invalid value for " + name + ": " + HTTP.arg(name)
            + " using 0 instead!");
    i = 0;
  }
  return i;
}

uint16_t getValidDuration(String name)
{
  int i = HTTP.arg(name).toInt();
  if (i < 0 || i > 32767)
  {
    Serial.println(
        "invalid value for " + name + ": " + HTTP.arg(name)
            + " using 32000 instead!");
    i = 32000;
  }
  return i;
}

boolean getValidBoolean(String name)
{
  String value = HTTP.arg(name);
  return value.equalsIgnoreCase("true");
}

void handleAdjustment()
{
  uint16_t adj;
  if (HTTP.hasArg("set"))
  {
    if (HTTP.arg("set").equalsIgnoreCase("auto"))
    {
      Serial.println("Auto Adjust!");
      syncClockToRTC();
    }
    else
    {
      adj = getValidPosition("set");
      Serial.print("setting adjustment:");
      Serial.println(adj);
      setClockAdjustment(adj);
    }
  }

  adj = getClockAdjustment();

  HTTP.send(200, "text/plain", String(adj));
}

void handlePosition()
{
  uint16_t pos;
  if (HTTP.hasArg("set"))
  {
    pos = getValidPosition("set");
    Serial.print("setting position:");
    Serial.println(pos);
    setClockPosition(pos);
  }

  pos = getClockPosition();

  int hours = pos / 3600;
  int minutes = (pos - (hours*3600)) / 60;
  int seconds = pos - (hours*3600) - (minutes*60);
  char message[64];
  sprintf(message, "%d (%02d:%02d:%02d)\n", pos, hours, minutes, seconds);
  HTTP.send(200, "text/Plain", message);
}

void handleTPDuration()
{
  uint16_t value;
  if (HTTP.hasArg("set"))
  {
    value = getValidDuration("set");
    Serial.print("setting tp_duration:");
    Serial.println(value);
    setClockTPDuration(value);
  }

  value = getClockTPDuration();

  HTTP.send(200, "text/plain", String(value));
}

void handleAPDuration()
{
  uint16_t value;
  if (HTTP.hasArg("set"))
  {
    value = getValidDuration("set");
    Serial.print("setting ap_duration:");
    Serial.println(value);
    setClockAPDuration(value);
  }

  value = getClockAPDuration();

  HTTP.send(200, "text/plain", String(value));
}

void handleAPDelay()
{
  uint16_t value;
  if (HTTP.hasArg("set"))
  {
    value = getValidCount("set");
    Serial.print("setting ap_delay:");
    Serial.println(value);
    setClockAPDelay(value);
  }

  value = getClockAPDelay();

  HTTP.send(200, "text/plain", String(value));
}

void handleEnable()
{
  boolean enable;
  if (HTTP.hasArg("set"))
  {
    enable = getValidBoolean("set");
    setClockEnable(enable);
  }
  enable = getClockEnable();
  HTTP.send(200, "text/Plain", String(enable));
}

void handleRTC()
{
  uint16_t value = getRTCTimeAsPosition();
  HTTP.send(200, "text/plain", String(value));
}

void handleNTP()
{
  Serial.println("disabling the clock!");
  setClockEnable(false);
  syncing = true;
  Serial.println("syncing now true!");
  HTTP.send(200, "text/Plain", "OK\n");
}

void setup()
{
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Startup!");

  pinMode(SYNC_PIN, INPUT);

  rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!rtc.IsDateTimeValid())
  {
    // Common Causes:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing

    Serial.println("RTC lost confidence in the DateTime!");

    // following line sets the RTC to the date & time this sketch was compiled
    // it will also reset the valid flag internally unless the Rtc device is
    // having an issue

    rtc.SetDateTime(compiled);
  }

  if (!rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    rtc.SetIsRunning(true);
  }

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  rtc.Enable32kHzPin(false);

  boolean enabled = getClockEnable();
  Serial.println("clock enable is:" + String(enabled));

  if (!enabled)
  {
    Serial.println("set mode none");
    rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
    Serial.println("set 1Hz");
    rtc.SetSquareWavePinClockFrequency(DS3231SquareWaveClock_1Hz);
  }
  else
  {
    Serial.println("clock is enabled, skipping init of RTC");
  }

  // setup wifi, blink let slow while connecting and fast if portal activated.
  feedback.blink(FEEDBACK_LED_SLOW);
  WiFiManager wifi;
  wifi.setAPCallback([](WiFiManager *)
  { feedback.blink(FEEDBACK_LED_FAST);});
  String ssid = "SynchroClock" + String(ESP.getChipId());
  wifi.autoConnect(ssid.c_str(), NULL);
  feedback.off();

  //Serial.println("set 1kHz");
  //rtc.SetSquareWavePinClockFrequency(DS3231SquareWaveClock_1kHz);

  if (!enabled)
  {
    Serial.println("setting clock position");
    setClockPosition(0);
    Serial.println("enabling clock");
    setClockEnable(true);
    Serial.println("starting square wave");
    rtc.SetSquareWavePin(DS3231SquareWavePin_ModeClock);
  }
  else
  {
    Serial.println("clock is enabled, skipping init of clock!");
  }

  Serial.println("starting HTTP");
  HTTP.on("/adjust", HTTP_GET, handleAdjustment);
  HTTP.on("/position", HTTP_GET, handlePosition);
  HTTP.on("/tp_duration", HTTP_GET, handleTPDuration);
  HTTP.on("/ap_duration", HTTP_GET, handleAPDuration);
  HTTP.on("/ap_delay", HTTP_GET, handleAPDelay);
  HTTP.on("/enable", HTTP_GET, handleEnable);
  HTTP.on("/rtc", HTTP_GET, handleRTC);
  HTTP.on("/ntp", HTTP_GET, handleNTP);
  HTTP.begin();
  ntp.begin(1235);
  syncing = false;
  last_pin = 0;
}

void loop()
{
  HTTP.handleClient();
  uint8_t pin = digitalRead(SYNC_PIN);
  if (syncing && last_pin == 1 && pin == 0)
  {
    EpochTime start;
    start.seconds = rtc.GetDateTime().Epoch32Time();
    start.fraction = 0;
    EpochTime end = ntp.getTime(start);

    // compute the delay to the next second

    uint32_t msdelay = 1000 - (((uint64_t) end.fraction * 1000) >> 32);
    Serial.print("msdelay: ");
    Serial.println(msdelay);
    // wait for the next second
    if (msdelay > 0 && msdelay < 1000)
    {
      delay(msdelay);
    }
    RtcDateTime dt(end.seconds + 1); // +1 because we waited for the next second
    rtc.SetDateTime(dt);
    syncing = false;
    last_pin = 0;
    delay(500);
    Serial.println("rtc updated, starting clock!");
    setClockEnable(true);
    Serial.println("syncing clock to RTC");
    syncClockToRTC();
  }
  else
  {
    last_pin = pin;
  }
}

void syncClockToRTC()
{
  uint16_t rtc_pos = getRTCTimeAsPosition();
  Serial.printf("RTC position:%d", rtc_pos);
  uint16_t clock_pos = getClockPosition();
  if (clock_pos != rtc_pos)
  {
    int adj = rtc_pos - clock_pos;
    if (adj < 0)
    {
      adj += 43200;
    }
    Serial.printf("sending adjustment of %d\n", adj);
    setClockAdjustment(adj);
  }
}

//
// RTC functions
//
uint16_t getRTCTimeAsPosition()
{
  RtcDateTime time = rtc.GetDateTime();
  uint16_t hour = time.Hour();
  uint16_t minute = time.Minute();
  uint16_t second = time.Second();
  if (hour > 11)
  {
    hour -= 12;
    Serial.printf("%02d:%02d:%02d (CORRECTED)\n", hour, minute, second);
  }
  else
  {
    Serial.printf("%02d:%02d:%02d\n", hour, minute, second);
  }
  uint16_t position = hour * 60 * 60 + minute * 60 + second;
  return position;
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt)
{
  char datestring[20];

  snprintf_P(datestring, countof(datestring),
      PSTR("%02u/%02u/%04u %02u:%02u:%02u"), dt.Month(), dt.Day(), dt.Year(),
      dt.Hour(), dt.Minute(), dt.Second());
  Serial.print(datestring);
}

//
// access to analog clock controler via i2c
//
uint16_t getClockAdjustment()
{
  return readClock16(CMD_ADJUSTMENT);
}

void setClockAdjustment(uint16_t value)
{
  writeClock16(CMD_ADJUSTMENT, value);
}

uint16_t getClockPosition()
{
  return readClock16(CMD_POSITION);
}

void setClockPosition(uint16_t value)
{
  writeClock16(CMD_POSITION, value);
}

uint16_t getClockTPDuration()
{
  return readClock16(CMD_TP_DURATION);
}

void setClockTPDuration(uint16_t value)
{
  writeClock16(CMD_TP_DURATION, value);
}

uint16_t getClockAPDuration()
{
  return readClock16(CMD_AP_DURATION);
}

void setClockAPDuration(uint16_t value)
{
  writeClock16(CMD_AP_DURATION, value);
}

uint16_t getClockAPDelay()
{
  return readClock16(CMD_AP_DELAY);
}

void setClockAPDelay(uint16_t value)
{
  writeClock16(CMD_AP_DELAY, value);
}

boolean getClockEnable()
{
  Wire.beginTransmission((uint8_t) I2C_ADDRESS);
  Wire.write(CMD_CONTROL);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t) I2C_ADDRESS, (uint8_t) 1);
  uint8_t value = Wire.read();
  return ((value & BIT_ENABLE) == BIT_ENABLE);
}

void setClockEnable(boolean enable)
{
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(CMD_CONTROL);
  Wire.endTransmission();

  Wire.requestFrom((uint8_t) I2C_ADDRESS, (uint8_t) 1);
  uint8_t value = Wire.read();

  if (enable)
  {
    value |= BIT_ENABLE;
  }
  else
  {
    value &= ~BIT_ENABLE;
  }

  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(CMD_CONTROL);
  Wire.write(value);
  Wire.endTransmission();
}

// send a command, read a 16 bit value
uint16_t readClock16(uint8_t command)
{
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(command);
  Wire.endTransmission();
  uint16_t value;
  Wire.requestFrom(I2C_ADDRESS, sizeof(value));
  Wire.readBytes((uint8_t*) &value, sizeof(value));
  return (value);
}

// send a command with a 16 bit value
void writeClock16(uint8_t command, uint16_t value)
{
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(command);
  Wire.write((uint8_t*) &value, sizeof(value));
  Wire.endTransmission();
}


