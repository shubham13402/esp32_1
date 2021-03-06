// Version 2.0 using RTC RAM SLEEP and SD Card
#include <WiFi.h>
#include "HttpsOTAUpdate.h"
#include <time.h>
#include "RTClib.h"
#include "FS.h"
#include <SD.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <WiFiManager.h>
#include "cert.h"
#include "SparkFunHTU21D.h"
#include "ThingSpeak.h" // always include thingspeak header file after other header files and custom macros

// Create instances of objects
WiFiClient  client;
RTC_DS3231  rtc;
WiFiManager manager;
HTU21D      mySensor;

static        HttpsOTAStatus_t otastatus;

int           TGSPin           = 34;
int           BatteryPin       = 33;
int           CS_pin           = 5;
long          SleepDuration    = 1 * 60; // Every 1-minute
const int     MaxReadings      = 100;
const char*   timezone         = "UTC-05:30";
unsigned long myChannelNumber  = 1386783;
const char *  myWriteAPIKey    = "SL3IS82UAT15J05K";
// https://thingspeak.com/channels/1386783
String FirmwareVer = { "1.3" };
#define URL_fw_Version "https://raw.githubusercontent.com/shubham13402/esp32_1/master/bin_version.txt"
#define URL_fw_Bin "https://raw.githubusercontent.com/shubham13402/esp32_1/master/fw.bin"


// global variables
RTC_DATA_ATTR float  Temp[MaxReadings], Humi[MaxReadings], Gas[MaxReadings], Batt[MaxReadings];
RTC_DATA_ATTR int    Time[MaxReadings];
RTC_DATA_ATTR int    ReadingIndex, UnixTime;
RTC_DATA_ATTR String NTPtimenow, RTCtimenow;
RTC_DATA_ATTR bool   TemporaryReadingsExist;
RTC_DATA_ATTR float  Temp_reading, Humi_reading, Gas_reading, BattVolts;

unsigned long        previousMillis   = 0; // NTP server to request time

const char* ntpServer          = "pool.ntp.org";
const long  gmtOffset_sec      = 0;
const int   daylightOffset_sec = 0;
int         awakeTimer         = 0;
String      daysOfTheWeek[7]   = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

void firmwareUpdate();
int FirmwareVersionCheck();

struct Button {
               const uint8_t PIN; 
               uint32_t numberKeyPresses; 
               bool pressed;
               };

Button button_boot = {0,0,false};


/*void IRAM_ATTR isr(void* arg) {
    Button* s = static_cast<Button*>(arg);
    s->numberKeyPresses += 1;
    s->pressed = true;
}*/

void IRAM_ATTR isr() {
  button_boot.numberKeyPresses += 1;
  button_boot.pressed = true;
}


void setup() {
  awakeTimer = millis();
  SetupSerial(1);           // Setup Stage-1
  SetupGPIO(2);             // Setup Stage-2
  SetupSensors(3);          // Setup Stage-3
  StartRTCclock(4);         // Setup Stage-4
  StartWiFiManager(5);      // Setup Stage-5
  StartTimeServices(6);     // Setup Stage-6
  CheckAdjustRTCclock(7);   // Setup Stage-7
  StartThingSpeak(8);       // Setup Stage-8
  StartSDcard(9);           // Setup Stage-9
  if (FirmwareVersionCheck()) {
      firmwareUpdate();}  // Setup Stage-10
  Serial.println("Started NTP clock = " + NTPtimenow);
  Serial.println("Started RTC clock = " + RTCtimenow);
  ReadSensors();
}

void loop() {
    if (button_boot.pressed) { //to connect wifi via Android esp touch app 
    Serial.println("Firmware update Starting..");
    firmwareUpdate();
    button_boot.pressed = false;
  }
  GetRTCdatetime();
  ReadSensors(); // read and if WiFi not available, then save readings for next upload
  Serial.println(" Date-Time: " + NTPtimenow);
  Serial.println(" UNIX Time: " + String(UnixTime));
  Serial.println("      Temp: " + String(Temp_reading, 1) + "??C");
  Serial.println("      Humi: " + String(Humi_reading, 1) + "%");
  Serial.println("      Gas : " + String(Gas_reading, 1)  + "ppm");
  Serial.println("Batt Volts: " + String(BattVolts, 1)    + "V");
  if (WiFi.status() == WL_CONNECTED) {
    if (TemporaryReadingsExist == true) UploadTemporaryReadings();
    ThingSpeakUpload(UnixTime, Temp_reading, Humi_reading, Gas_reading, BattVolts); // Upload new readings to Thingspeak
  }
  Serial.println("Awake for : " + String((millis() - awakeTimer) / 1000.0, 1) + "-Secs");
  Serial.println("Entering sleep...");
  esp_sleep_enable_timer_wakeup(SleepDuration * 1000000LL);
  esp_deep_sleep_start();   // Sleep for e.g. 10 minutes
}

// ############ Firmware Update ###########################################
void firmwareUpdate(void) {
  WiFiClientSecure client;
  client.setCACert(rootCACertificate);
  httpUpdate.setLedPin(LED_BUILTIN, LOW);
  t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}
int FirmwareVersionCheck(void) {
  String payload;
  int httpCode;
  String fwurl = "";
  fwurl += URL_fw_Version;
  fwurl += "?";
  fwurl += String(rand());
  Serial.println(fwurl);
  WiFiClientSecure * client = new WiFiClientSecure;

  if (client) 
  {
    client -> setCACert(rootCACertificate);

    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
    HTTPClient https;

    if (https.begin( * client, fwurl)) 
    { // HTTPS      
      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      delay(100);
      httpCode = https.GET();
      delay(100);
      if (httpCode == HTTP_CODE_OK) // if version received
      {
        payload = https.getString(); // save received version
      } else {
        Serial.print("error in downloading version file:");
        Serial.println(httpCode);
      }
      https.end();
    }
    delete client;
  }
      
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    payload.trim();
    if (payload.equals(FirmwareVer)) {
      Serial.printf("\nDevice already on latest firmware version:%s\n", FirmwareVer);
      return 0;
    } 
    else 
    {
      Serial.println(payload);
      Serial.println("New firmware detected");
      return 1;
    }
  } 
  return 0;  
}
// ############ SYSTEM Services ##########################################
void SetupSerial(int stage) {
  Serial.begin(115200);
  while (!Serial); delay(100);
  Serial.println(String(stage)  + ". Starting Serial Services...");
}

void SetupGPIO(int stage) {
  Serial.println(String(stage)  + ". Setting up GPIO ports...");
  pinMode(TGSPin, INPUT);
  pinMode(BatteryPin, INPUT);
  pinMode(CS_pin, OUTPUT);
  pinMode(button_boot.PIN, INPUT);
  attachInterrupt(button_boot.PIN, isr, RISING);
}

// ############ Thingspeak Services ##########################################
void StartThingSpeak(int stage) {
  Serial.println(String(stage)  + ". Starting Thingspeak Services...");
  ThingSpeak.begin(client);  // Initialize ThingSpeak
}

void ThingSpeakUpload(int datetime, float temp, float humi, float gas, float batt) {
  ThingSpeak.setField(1, datetime);
  ThingSpeak.setField(2, temp);
  ThingSpeak.setField(3, humi);
  ThingSpeak.setField(4, gas);
  ThingSpeak.setField(5, batt);
  // write to the ThingSpeak channel
  int uploadStatus = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (uploadStatus == 200) {
    Serial.println("Channel update successful.");
  }
  else
  {
    Serial.println("Problem updating channel. HTTP error code " + String(uploadStatus));
  }
}

void UploadTemporaryReadings() {
  Serial.println("Uploading Temporary Readings...");
  int  uploadIndex = 0;
  bool uploadFinished = false;
  while (uploadIndex < ReadingIndex || uploadFinished) {
    ThingSpeakUpload(Time[uploadIndex], Temp[uploadIndex], Humi[uploadIndex], Gas[uploadIndex], Batt[uploadIndex]);
    uploadIndex++;
    if (uploadIndex > ReadingIndex) uploadFinished = true;
  }
  ClearSensorReadings();
  TemporaryReadingsExist = false;
}

// ############ Sensor Services ##########################################
void SetupSensors(int stage) {
  Serial.println(String(stage)  + ". Starting Sensor Services...");
  mySensor.begin();
}

void ReadSensors() {
  Temp_reading = mySensor.readTemperature();            //random(180, 200)   / 10.0;
  Humi_reading = mySensor.readHumidity();               //random(450, 550)   / 10.0;
  Gas_reading  = analogRead(TGSPin);                    //random(2500, 3500) / 10.0;
  BattVolts    = analogRead(BatteryPin) / 2048.0 * 3.3; //random(38, 42)     / 10.0;
  LogData(UnixTime, Temp_reading, Humi_reading, Gas_reading, BattVolts);
  if (WiFi.status() != WL_CONNECTED) {  // Now save readings if WiFi is not connected/available
    Time[ReadingIndex] = UnixTime;
    Temp[ReadingIndex] = Temp_reading;
    Humi[ReadingIndex] = Humi_reading;
    Gas[ReadingIndex]  = Gas_reading;
    Batt[ReadingIndex] = BattVolts;
    ReadingIndex++;
    if (ReadingIndex > MaxReadings) ReadingIndex = MaxReadings;
    TemporaryReadingsExist = true;
  }
}

void ClearSensorReadings() {
  for (int r = 0; r <= MaxReadings; r++) {
    Temp[r] = 0;
    Humi[r] = 0;
    Gas[r]  = 0;
    Batt[r] = 0;
  }
  ReadingIndex = 0;
}

// ############ SD CARD logging #########################################
void StartSDcard(int stage) {
  Serial.print(String(stage) + ". Starting SD Card services...");
  if (SD.begin()) Serial.println(" SD card is ready to use...");
  else            Serial.println(" SD card initialisation failed");
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("*** No SD card inserted ***");
  }
  File file = SD.open("/data.txt", FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing...");
  }
  file.close();
}

void LogData(int datetime, float temp, float humi, float gas, float battvolts) {
  File file = SD.open("/data.txt", FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending...");
  }
  else
  {
    file.println(String(datetime) + "," + String(temp, 1) + "," + String(humi, 1) + "," + String(gas, 1)  + "," + String(battvolts, 1) + "\r\n");
  }
  file.close();
}

// ############ WiFi Services ##########################################
void StartWiFiManager(int stage) {
  Serial.println(String(stage) + ". Starting WiFi Connection...");
  WiFiManager manager;
  bool success = manager.autoConnect("esp32_AP", "");   //soft ap credentials
  if (!success) {
    Serial.println("Failed to connect...");
    Serial.println("Please enter http://192.168.4.1/ in your browser address bar");
    Serial.println("Follow the instuctions for WiFi setup, then reset the ESP");
    while (1);
  }
  else {
    Serial.println("Connected");
  }
  Serial.println("WiFi connected at: " + WiFi.localIP().toString());
  Serial.println("WiFi Connection Started...");
}

// ############ RTC Time Services ##########################################
void StartRTCclock(int stage) {
  Serial.println(String(stage) + ". Starting RTC Clock...");
  delay(200);
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  } else
  {
    Serial.println("Found RTC clock...");
  }
  GetRTCdatetime();
}

void GetRTCdatetime() {
  Serial.println("Getting RTC Date-Time...");
  DateTime now = rtc.now();
  UnixTime = now.unixtime();
  // Now format datetime, because time format needed for Thingspeak is 2011-07-08T01:02:03Z
  String RTCdatetime = String(now.year()) + "-";
  RTCdatetime       += String(now.month()  < 10 ? "0" : "") + String(now.month())  + "-";
  RTCdatetime       += String(now.day()    < 10 ? "0" : "") + String(now.day())    + "T";
  RTCdatetime       += String(now.hour()   < 10 ? "0" : "") + String(now.hour())   + ":";
  RTCdatetime       += String(now.minute() < 10 ? "0" : "") + String(now.minute()) + ":";
  RTCdatetime       += String(now.second() < 10 ? "0" : "") + String(now.second()) + "Z";
  // Now example RTCDatetime = 2021-10-29T11:22:33Z
  // Now example RTCDatetime = 2021-10-29 11:22:33 ISO8601 format
  RTCtimenow         = RTCdatetime;
  Serial.println("Got RTC Date-Time...");
}

void CheckAdjustRTCclock(int stage) {
  Serial.println(String(stage)  + ". Checking RTC time accuracy");
  String NTP = get_NTP_time();
  String RTC = get_RTC_time();
  if (NTP == RTC) Serial.println("NTP = RTC"); else Serial.println("RTC Time is being adjusted");
  if (NTP != RTC) {
    struct tm time;
    if (!getLocalTime(&time)) {
      Serial.println("Could not obtain time info");
    }
    else
    {
      int Year    = time.tm_year + 1900; // Number of years since 1900
      int Month   = time.tm_mon + 1;
      int Day     = time.tm_mday;
      int DofWeek = time.tm_wday;
      int Hour    = time.tm_hour;
      int Minute  = time.tm_min;
      int Second  = time.tm_sec;
      rtc.adjust(DateTime(Year, Month, Day, Hour, Minute, Second));
    }
  }
  Serial.println("RTC time accuracy checked and adjusted if necessary");
}

// ############ NTP Time Services ##########################################
void StartTimeServices(int stage) {
  Serial.println(String(stage) + ". Starting time services...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "time.nist.gov"); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
  setenv("TZ", timezone, 1);
  tzset();
  delay(200);
  NTPtimenow = UpdateLocalTime();
  Serial.println("Time services started...");
}

String UpdateLocalTime() {
  struct tm timeinfo;
  char   time_output[60];
  while (!getLocalTime(&timeinfo, 10000)) { // Wait for 10-sec for time to synchronise
    Serial.println("Failed to obtain time");
  }
  // time format needed for Thingspeak is 2011-07-18T01:02:03Z
  // http://www.cplusplus.com/reference/ctime/strftime/
  strftime(time_output, sizeof(time_output), "%FT%TZ", &timeinfo); // 'ccyy-mm-ddThh:mm:ssZ
  GetRTCdatetime();
  return String(time_output);
}

String get_NTP_time() {
  time_t now;
  time(&now);
  char time_output[30];
  // See http://www.cplusplus.com/reference/ctime/strftime/ for strftime functions
  strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
  return String(time_output); // returns Sat 20-Apr-19 12:31:45
}

String get_RTC_time() {
  DateTime now = rtc.now();
  UnixTime = now.unixtime();
  String Datetime = "";
  Datetime  = String(now.year());
  Datetime += "-" + String(now.month() < 10 ? "0" : "") + String(now.month() + 1);
  Datetime += "-" + String(now.day()   < 10 ? "0" : "") + String(now.day());
  Datetime += " ";
  Datetime += daysOfTheWeek[now.dayOfTheWeek()];
  Datetime += " ";
  Datetime += String(now.hour()   < 10 ? "0" : "") + String(now.hour()) + ":";
  Datetime += String(now.minute() < 10 ? "0" : "") + String(now.minute()) + ":";
  Datetime += String(now.second() < 10 ? "0" : "") + String(now.second());
  return Datetime;
}


// 429 of lines of code on 11/11/21

/*
  So my overall summary of the project is  to make a esp32 (esp wroom 32) driven project,
  the components used are HTU21D(Temperature and Humidity sensor),  DS3231 (RTC Chip), TGS8100(GAS Sensor), SD Card module, Battery Fuel Gauge(MAX17043).
  I need  to code it  as I need to take sensor readings and upload it to the TS server,
  Also save it to the SD Card.

  Also it should be like as ESP Turns ON,It should take all the reading at interval of 10 mins and save it in SD Card and then upload it to Thingspeak,
  If wifi or some internet issue happens It just need to save it to SD Card only that time and during next data taking time, before uploading the fresh data,
  it should check any Old pending data to be uploaded and upload that pending data first and then upload new data after that, then it goes to sleep. // **** OK done that
  // **** The problem was you were using FILE_WRITE which erases the file and starts a new, should have been FILE_APPEND, corrected that
  // **** Your code was writing new readings to the the SD Card but was not noting how many readings, example
  // Reading1
  // Reading2
  // Reading3
  // When the ESP wakes up (but only after no wifi), it needs to upload Reading1, 2 and 3 then upload the current reading
  // Then it needs to erase the SD file to start again as TS has the new readings
  // I have added the Library function for Thingspeak upload to simplify the upload process, but further testing is required because the last time I tried
  // using a time-stamp does not work and TS know that they are working on it. TS takes readings sequentially, so you don't really need time stamps

  And The file that is saved in the SD Card should be CSV Format.
  // **** I have formated SD card writing to CSV format, it was not correct

  with data : Date & time(In time stamp format) Temp, Humidity, Gas value. and Battery percentage
  (As ESP Will be powered by Li-ion battery). // ** You will only get about 1-month battery life at most for 10-mins readings.
  Now reading in DS3231 RTC Chip, it is required to calibrate with NTP Server Every particular time to remove some lags
  // **** This now happens watch the start-up messages, if the RTC is out it will adjust the RTC, I've set it to BST, you need to adjust that for your time zone
    or some time delay, also converting it into timestamp (As it will be able to upload it to TS)
  then upload it to TS.

  Also in TGS2610 (Gas sensor) while taking readings, graphs of those readings should be smoothed.    // **** Thingspeak can do this
  Also to find a way to connect the esp 32 to WIFI Without putting credentials in the code.           // **** The ESP32 can do this with WiFi Manager, easy but not yet
  Also In the future if we need to modify the code, It should be via Github, like OTA Update be done  // Can be done, buit need to get the rest working first, a long way to go yet
  through GitHub.
  Also, when ESP 32 connects to WIfi , it should also check any new version of code from GITHub if it is available it should automatically update to the new version of code.
*/
