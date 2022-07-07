//-----------------------------------------------------------------------------------------------------------------------------------
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "DHT.h"
#include <RtcDS3231.h>
#include <Wire.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>         
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
//-----------------------------------------------------------------------------------------------------------------------------------

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    "****"
#define AIO_KEY         "****"

/************ Global State (you don't need to change this!) ******************/
const uint8_t fingerprint[20] = {0xFC, 0xFE, 0xC1, 0x3F, 0xD9, 0xAC, 0x29, 0x79, 0x28, 0xF9, 0xA5, 0x13, 0x17, 0x1C, 0xF0, 0x70, 0xB9, 0x74, 0x2E, 0xCF};


WiFiClient client;

RtcDS3231<TwoWire> Rtc(Wire);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_USERNAME, AIO_KEY);

Adafruit_MQTT_Publish temperature = Adafruit_MQTT_Publish(&mqtt,  AIO_USERNAME "/feeds/Temperature");
Adafruit_MQTT_Publish humidity = Adafruit_MQTT_Publish(&mqtt,  AIO_USERNAME "/feeds/Humidity");

IPAddress server_addr(192,168,1,108);  // IP of the MySQL *server* here
char user[] = "***";              // MySQL user login username
char password[] = "***";        // MySQL user login password

MySQL_Connection conn(&client);
MySQL_Cursor* cursor;

#define HARDWARE_TYPE MD_MAX72XX::ICSTATION_HW    //PAROLA_HW  FC16_HW GENERIC_HW ICSTATION_HW
#define MAX_DEVICES 8
#define CLK_PIN   14  // d5
#define DATA_PIN  13   //d7
#define CS_PIN    3  //d8 RX

#define DHTTYPE DHT22
#define countof(a) (sizeof(a) / sizeof(a[0]))
const int PIN_LED = 2;
const int TRIGGER_PIN = 0;
const int TRIGGER_PIN2 = 0;
bool initialConfig = false;

//--------------------------------------------Wifi Temp--------------------------------------------------------------

int news_display_time = 3;
int status = WL_IDLE_STATUS;

//--------------------------------------------LCD Matrix--------------------------------------------------------------
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

//--------------------------------------------DHT SENSOR--------------------------------------------------------------
uint8_t DHTPin = 12; //D2 (4)
DHT dht(DHTPin, DHTTYPE);
float Temperature;
float Humidity;
bool Dht_Read_Success = true;
//--------------------------------------------Variables--------------------------------------------------------------

uint8_t scrollSpeed = 25;    // default frame delay value
textEffect_t scrollEffect = PA_SCROLL_UP;
textPosition_t scrollAlign = PA_LEFT;
uint16_t scrollPause = 1000; // in milliseconds



// Global message buffers shared by Serial and Scrolling functions
#define  BUF_SIZE  6
char curMessage[5] = { "" };

char newMessage[10] = { "Boot..." };
bool newMessageAvailable = true;


char temp_humdata[100];
char internet_weather_data[200];
char internet_news[2000];
//char place_holder[2000];

#define  MAX_MESG  20
char  szMesg[MAX_MESG + 1] = "";

uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 };                     // Deg C
uint8_t degF[] = { 6, 3, 3, 124, 20, 20, 4 };                     // Deg F
uint8_t uParrw[] = { 5, 4, 2, 127, 2, 4};                         // Up Arrow
uint8_t dNarrw[] = { 5, 16, 32, 127, 32, 16};                     // Dn Arrow
uint8_t neutralarrw[] = { 5, 20, 34, 127, 34, 20};                // Neutral Arrow

typedef struct
{
  uint8_t spacing;  // character spacing
  char  *msg;   // message to display
} msgDef_t;

msgDef_t  M[] =
{
  { 1, "User char" },
  { 0, "~~~~~~~~~~" },
  { 1, "$" },
  { 0, "+" },
  { 0, "_" },
  { 0, "~" }

};


void disp_free(void) ////wait loop for animation completion
{
  while (!(P.displayAnimate()))
  {
    delay(1);
  }

}


void read_temp_hum(void)

{
  P.addChar('$', degC);
  Temperature = dht.readTemperature();
  Humidity = dht.readHumidity();

  if (isnan(Temperature) || isnan(Humidity) ) {

    Serial.println("Failed to read from DHT sensor!");

    Dht_Read_Success = false;
  }
  else {
    Serial.print("Temperature: ");
    Serial.print(Temperature);
    Serial.print(" %\t");
    Serial.print("Humidity: ");
    Serial.print(Humidity);
    Serial.println(" *C ");
    Serial.println("Publishing temperature data to MQTT...");
    publish_temp(Temperature);
    publish_humid(Humidity);

    Dht_Read_Success = true;

  }

  if (Dht_Read_Success) {

    sprintf(temp_humdata, "%.1f%s %.1f %%", Temperature, M[2].msg, Humidity);  //Build string for internal humidity/temperature
    P.setIntensity(0);
    P.setCharSpacing(1);
    P.displayText(temp_humdata, PA_CENTER, 20, 2500, PA_OPENING, PA_SCROLL_UP_RIGHT);
    disp_free();

  }
  else {
    P.setIntensity(0);
    P.setCharSpacing(1);
    P.displayText("DHT Sensor is not working!", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_DISSOLVE);
    disp_free();
  }
  P.delChar('$');
}

void readSerial(void)
{
  static char *cp = newMessage;

  while (Serial.available())
  {
    *cp = (char)Serial.read();
    if ((*cp == '\n') || (cp - newMessage >= BUF_SIZE - 2)) // end of message character or full buffer
    {
      *cp = '\0'; // end the string
      // restart the index for next filling spree and flag we have a message waiting
      cp = newMessage;
      newMessageAvailable = true;
    }
    else  // move char pointer to next position
      cp++;
  }
}


void printDateTime(void)
{

  RtcDateTime dt = Rtc.GetDateTime();
  char datestring[20];
  uint16_t yr_s = dt.Year() % 100;

  snprintf_P(datestring,
             countof(datestring),
             PSTR("%02u-%02u %02u:%02u"),
             dt.Day(),
             dt.Month(),
             //yr_s,
             dt.Hour(),
             dt.Minute());
  //dt.Second() );
  //Serial.print(datestring);

  P.setIntensity(0);
  P.setCharSpacing(1);
  P.displayText(datestring, PA_CENTER, scrollSpeed, 15000, PA_SCROLL_DOWN, PA_SCROLL_DOWN);
  disp_free();
}

void setup() {
  delay(1000);
  Serial.begin(9600);
  Serial.println("");

  pinMode(DHTPin, INPUT);
  pinMode(PIN_LED, OUTPUT);
  P.setIntensity(0);
  Serial.println("Intial Boot...");

  Serial.println("Initializing LED Matrix...");
  
  P.begin();
  P.setIntensity(0);
  P.displayText("Booting up...", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_UP, PA_SCROLL_DOWN);
  disp_free();

  Serial.println("WiFi Setup...");
  init_wifi_setup();

  Serial.println("Initializing DHT Module...");
  dht.begin();

  Serial.println("Initializing RTC Module...");
  Rtc.Begin();

Serial.println("Setting up NTP Clock...");
String formattedDate;
String dayStamp;
String timeStamp;
  



  //---------------------------------------------------------------------------------------RTC----------------------------------------------
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  Serial.println();

  if (!Rtc.IsDateTimeValid())
  {
    if (Rtc.LastError() != 0)
    {
      // we have a communications error
      Serial.print("RTC communications error = ");
      Serial.println(Rtc.LastError());
    }
    else
    {
      // Common Causes:
      //    1) first time you ran and the device wasn't running yet
      //    2) the battery on the device is low or even missing

      Serial.println("RTC lost confidence in the DateTime!");

      // following line sets the RTC to the date & time this sketch was compiled
      // it will also reset the valid flag internally unless the Rtc device is
      // having an issue

      //Rtc.SetDateTime(compiled);
    }
  }

  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled)
  {
    Serial.println("RTC is older than compile time!  (Updating DateTime)");
    Rtc.SetDateTime(compiled);
  }
  else if (now > compiled)
  {
    Serial.println("RTC is newer than compile time. (this is expected)");
  }
  else if (now == compiled)
  {
    Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);

  //Setting up RTC clock from internet
  Serial.println("Fetching time from internet...");

  if (WiFi.status() == WL_CONNECTED) {
  timeClient.begin();
  timeClient.setTimeOffset(19800);
  
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  Serial.print("Got the date from ntp server:");
formattedDate = timeClient.getFormattedDate();
//              2022-03-29T22:41:17Z


  Serial.println(formattedDate);
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  Serial.print("Date: ");
  Serial.println(dayStamp);
//  2022-03-29

  // Extract time
  timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
  Serial.print("Time: ");
  Serial.println(timeStamp);
// 22:41:17
//  Serial.print("Hour:");
//  Serial.println (timeClient.getHours());
//  Serial.print("Minute:");
//  Serial.println (timeClient.getMinutes());
//   Serial.print("Second:");
//  Serial.println (timeClient.getSeconds());
//
//  Serial.print("Year:");
//  Serial.println (dayStamp.substring(2,4));
//
//  Serial.print("Month:");
//  Serial.println (dayStamp.substring(5,7));
//
//  Serial.print("Date:");

//  Serial.println (dayStamp.substring(8,10));
//  RtcDateTime currentTime = RtcDateTime(16, 05, 18, 21, 20, 0);

uint16_t year=dayStamp.substring(2,4).toInt();
  Serial.println("Preparing the date object to update RTC.");
  RtcDateTime currentTime = RtcDateTime(dayStamp.substring(2,4).toInt(), dayStamp.substring(5,7).toInt(), dayStamp.substring(8,10).toInt(), timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());
// RtcDateTime currentTime = RtcDateTime(dayStamp.substring(2,4), dayStamp.substring(5,7), dayStamp.substring(8,10), timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());
  Serial.println("Updating RTC.");
//  Rtc.SetDateTime(formattedDate);
  Serial.println("RTC was synced with ntp time.");
  
  }
  

  printDateTime();
  read_temp_hum();
  fetch_internet_weather();
  news_display();

} // bootup_loop_ended
//----------------------------------------------------------------------Wifi Setup----------------------------------------------

void init_wifi_setup(void) {
  Serial.println("\n Starting");
  //WiFi.printDiag(Serial); //Remove this line if you do not want to see WiFi password printed

  if (WiFi.SSID() == "") {
    Serial.println("We haven't got any access point credentials, so get them now");
    initialConfig = true;
  }
  else {
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    unsigned long startedAt = millis();
    Serial.print("After waiting ");
    int connRes = WiFi.waitForConnectResult();
    float waited = (millis() - startedAt);
    Serial.print(waited / 1000);
    Serial.print(" secs in setup() connection result is ");
    Serial.println(connRes);
  }
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(TRIGGER_PIN2, INPUT_PULLUP);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("failed to connect, finishing setup anyway");
    P.displayText("WiFi NOT OK.", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_DISSOLVE);
    disp_free();
  } else {
    Serial.print("connected with local ip: ");
    Serial.println(WiFi.localIP());

    sprintf(temp_humdata, "WiFi OK. %s", WiFi.localIP().toString().c_str());
    P.displayText(temp_humdata, scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_SCROLL_UP);
    disp_free();

  }
}

//------------------------------------------------------------------On Demand Wifi Config Mode--------------------------------------------------
void on_demand_wifi_config (void) {
  if ((digitalRead(TRIGGER_PIN) == LOW) || (digitalRead(TRIGGER_PIN2) == LOW) || (initialConfig)) {
    Serial.println("Configuration portal requested.");
    P.displayText("Connect AP Clock V1 with Password theclockv1 to configure WiFi. ", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_DISSOLVE);
    disp_free();
    digitalWrite(PIN_LED, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
    //Local intialization. Once its business is done, there is no need to keep it around
    P.displayText("Config IP is 198.168.4.1", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_DISSOLVE);
    disp_free();
    WiFiManager wifiManager;

    //sets timeout in seconds until configuration portal gets turned off.
    //If not specified device will remain in configuration mode until
    //switched off via webserver or device is restarted.
    //wifiManager.setConfigPortalTimeout(600);

    //it starts an access point
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.startConfigPortal("ClockVer1", "theclockv1")) {
      Serial.println("Not connected to WiFi but continuing anyway.");
      P.displayText("Not connected to a valid WiFi access point, Internet functionality will be disabled. ", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_DISSOLVE);
      disp_free();
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected...yeey :)");
      P.displayText("Succesfully connected to a valid WiFi access point, Internet functionality will be enabled. ", scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_DISSOLVE);
      disp_free();
    }
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up
    // so resetting the device allows to go back into config mode again when it reboots.
    delay(5000);
  }


}


//-----------------------------------------------------------------------Web Temp Humidity-----------------------------------------------------------------------

void parseweatherJson(const char * current_data, const char * future_data) {

  const size_t current_bufferSize = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 2 * JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(13) + 355;
  const size_t future_bufferSize =  2 * JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(2) + 4 * JSON_OBJECT_SIZE(1) + 3 * JSON_OBJECT_SIZE(2) + 2 * JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + 2 * JSON_OBJECT_SIZE(7) + 2 * JSON_OBJECT_SIZE(8) + 696;
//allocating buffers in RAM for calculations

  DynamicJsonBuffer jsonBuffer_c(current_bufferSize);
  JsonObject& root_c = jsonBuffer_c.parseObject(current_data);

  if (!root_c.success()) {
    Serial.println("Json Parser failed for current weather data.");
    return;
  }

  JsonObject& weather_0 = root_c["weather"][0];
  JsonObject& main = root_c["main"];
  JsonObject& sys = root_c["sys"];
  //----------------------------------------------------------Current Weather------------------------------

  String city = root_c["name"];
  String cur_weather_main = weather_0["main"]; // "Mist"
  String cur_weather_desc = weather_0["description"];
  String cur_weather_full = cur_weather_main + " ( " + cur_weather_desc + " )";
  
  float cur_temp = main["temp"]; // 30.8
  
  float feels_like = main["feels_like"]; // 30.8
  float main_temp_min = main["temp_min"]; // 29
  float main_temp_max = main["temp_max"]; // 31.67
  
  int cur_pressure = main["pressure"]; // 89
  int cur_humidity = main["humidity"]; // 89

  int  sun_rise = sys["sunrise"];
  int  sun_set = sys["sunset"];

  //----------------------------------------------------------Future Weather--------------------------------

  DynamicJsonBuffer jsonBuffer_f(future_bufferSize);
  JsonObject& root_f = jsonBuffer_f.parseObject(future_data);

  if (!root_f.success()) {
    Serial.println("Json Parser failed for future weather data.");
    return;
  }

  JsonObject& list_1 = root_f["list"][1];
  JsonObject& list_1_main = list_1["main"];
  JsonObject& list_1_weather_0 = list_1["weather"][0];

  float fut_temp = list_1_main["temp"]; // 31.32
  String fut_weather_main = list_1_weather_0["main"]; // "Clear"
  String fut_weather_desc = list_1_weather_0["description"]; // "clear sky"
  String fut_weather_full = fut_weather_main + " ( " + fut_weather_desc + " )";
  String temp_diff_ind;
  //---------------------------------------------------------------------------------------------------------------------------------------------------

  P.addChar('+', uParrw);
  P.addChar('_', dNarrw);
  P.addChar('~', neutralarrw);
  P.addChar('$', degC);


  if (fut_temp > cur_temp ) {
    temp_diff_ind = M[3].msg;     //(up arrow)
  }
  else if (fut_temp < cur_temp) {
    temp_diff_ind = M[4].msg;    //(down arrow)
  }
  else {
    temp_diff_ind = M[5].msg;    //(neutral arrow)
  }

  sprintf(internet_weather_data, "%s, Now:%.1f%s ( %.1f - %.1f ) %d %% %s * Later: %.1f%s %s %s", city.c_str(), cur_temp, M[2].msg, main_temp_min, main_temp_max, cur_humidity, cur_weather_full.c_str(), fut_temp, M[2].msg, temp_diff_ind.c_str(), fut_weather_full.c_str());

  Serial.println(internet_weather_data);
  P.setIntensity(0);
  P.setCharSpacing(1);
  P.displayText(internet_weather_data, scrollAlign, scrollSpeed, scrollPause, PA_SCROLL_LEFT, PA_SCROLL_UP_LEFT);
  disp_free();

  P.delChar('+');
  P.delChar('_');
  P.delChar('~');
  P.delChar('$');

Serial.println("Calling SQL insert...");
  Serial.print("cur_temp:");
  Serial.println(cur_temp);
ext_weather_insert(cur_temp,feels_like,main_temp_min,main_temp_max,cur_pressure,cur_humidity,sun_rise,sun_set,cur_weather_main.c_str(),cur_weather_desc.c_str());

}


void show_timed_news(void) {

  RtcDateTime dt = Rtc.GetDateTime();

  uint8_t cur_minute = dt.Minute();
  if (cur_minute == 30 || cur_minute == 31 || cur_minute == 00 || cur_minute == 01) {

    news_display();
  } else {
    Serial.print("News display was bypassed...");
    Serial.print("Minute:");
    Serial.println(cur_minute);
  }


}


void show_timed_internet_weather(void) {

  RtcDateTime dt = Rtc.GetDateTime();

  uint8_t cur_minute = dt.Minute();
  if (cur_minute == 00 || cur_minute == 01 || cur_minute == 15 || cur_minute == 16 || cur_minute == 30 || cur_minute == 31 || cur_minute == 45 || cur_minute == 46) {

    fetch_internet_weather();
  } else {
    Serial.print("Internet weather bypassed...");
    Serial.print("Minute:");
    Serial.println(cur_minute);
  }


}
//-----------------------------------------------------------------News Json Parser------------------------------------------------------------------------------

void parsenewsJson(const char * news) {

  Serial.println("Let's display NEWS...");

  const size_t capacity = JSON_ARRAY_SIZE(5) + 5*JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 5*JSON_OBJECT_SIZE(6)+ 5000;
                          
  //DynamicJsonBuffer jsonBuffer(capacity);


  DynamicJsonBuffer jsonBuffer_news(capacity);
  JsonObject& root_news = jsonBuffer_news.parseObject(news);

  if (!root_news.success()) {
    Serial.println("Json Parser failed for NEWS.");
    return;
  }
  JsonArray& articles = root_news["articles"];
  JsonObject& articles_0 = articles[0];
  JsonObject& articles_1 = articles[1];
  JsonObject& articles_2 = articles[2];
  JsonObject& articles_3 = articles[3];
  JsonObject& articles_4 = articles[4];

  //  JsonObject& articles_5 = articles[5];
  //  JsonObject& articles_6 = articles[6];
  //  JsonObject& articles_7 = articles[7];
  //  JsonObject& articles_8 = articles[8];
  //  JsonObject& articles_9 = articles[9];

  String news_1 = articles_0["title"];
  String news_2 = articles_1["title"];
  String news_3 = articles_2["title"];
  String news_4 = articles_3["title"];
  String news_5 = articles_4["title"];

  //  String news_6 = articles_5["title"];
  //  String news_7 = articles_6["title"];
  //  String news_8 = articles_7["title"];
  //  String news_9 = articles_8["title"];
  //  String news_10 = articles_9["title"];

  Serial.println("");
  sprintf(internet_news, "News: %s * %s * %s * %s * %s.", news_1.c_str(), news_2.c_str(), news_3.c_str(), news_4.c_str(), news_5.c_str());
  //sprintf(internet_news, "News: %s * %s * %s * %s * %s * %s * %s * %s * %s * %s.",news_1.c_str(),news_2.c_str(),news_3.c_str(),news_4.c_str(),news_5.c_str(),news_6.c_str(),news_7.c_str(),news_8.c_str(),news_9.c_str(),news_10.c_str());
  for (int i = 0; i < news_display_time; i++) {

    P.setIntensity(0);
    P.setCharSpacing(1);
    String clean_news = internet_news;
    clean_news.replace("u2018", "'");
    clean_news.replace("u2019", "'");
    clean_news.replace("u201c", """");
    clean_news.replace("u201d", """");
    Serial.println(clean_news);
    P.displayText(clean_news.c_str(), scrollAlign, 22, scrollPause, PA_SCROLL_LEFT, PA_SCROLL_UP_LEFT);
    disp_free();
    delay(500);
    printDateTime();
  }

}
//---------------------------------------------------------------------------------------------------------------------------------------------------------------
void fetch_internet_weather(void) {

  String future_url = "http://api.openweathermap.org/data/2.5/forecast?APPID=****&cnt=2&q=delhi,in&units=metric";
  String current_url = "http://api.openweathermap.org/data/2.5/weather?id=1273294&APPID=****&units=metric";




  HTTPClient http;

  http.begin(current_url);
  int httpCode_current = http.GET();

  if (httpCode_current = 200) {
    Serial.println("Current weather data was succesfully fetched.");
    String current_payload = http.getString();
    http.end();
    http.begin(future_url);
    int httpCode_future = http.GET();
    if (httpCode_future = 200) {
      String future_payload = http.getString();

      http.end();
      parseweatherJson(current_payload.c_str(), future_payload.c_str());
    }
    else {
      Serial.print("Unable to fetch future weather data. Http Code:");
      Serial.println(httpCode_future);
    }


  }
  else {
    Serial.print("Unable to fetch current weather data. Http Code:");
    Serial.println(httpCode_current);
  }


}


void news_display(void) {

  Serial.println("News api triggering...");
  
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  
  
  client->setFingerprint(fingerprint);

  HTTPClient https;   //the websitemigrated to HTTPS so have to use the bearer SSL connection setup, finger print needs to be updatded after 30 days :(
  https.begin(*client, "https://gnews.io/api/v3/top-news?token=***&country=in&max=5");


  // http.begin(news_url);
  int httpCode_news = https.GET();
  Serial.println("News api was triggered via get...");
  Serial.println(httpCode_news);
  //Serial.println(https.getString());

  if (httpCode_news = 200) {
    String news_payload = https.getString();
    https.end();
    //Serial.print(news_payload);
    parsenewsJson(news_payload.c_str());

  } else {
    Serial.println("Unable to fetch news data. Http Code:");
    Serial.print(httpCode_news);
  }

}
//-------------------------------------------------------------------------------------------------------------------------------------------------------
void loop()
{
  on_demand_wifi_config();
  printDateTime();
  read_temp_hum();
  show_timed_internet_weather();
  show_timed_news();
    

}

void ext_weather_insert(float cur_temp_f,float feels_like_f, float main_temp_min_f,float main_temp_max_f,int cur_pressure_f,int cur_humidity_f,int sun_rise_f, int sun_set_f,const char *weather_main_f,const char *weather_desc_f){


Serial.println("----------------------------Lets update db now.----------------------");
  

char ext_query[500];
char int_query[100];

char ext_tab_insert[] = "INSERT INTO grafanadb.EXT_WEATHER (CURR_TEMP,FEELS_LIKE,MIN_TEMP,MAX_TEMP,PRESSURE, HUMIDITY,SUN_RISE,SUN_SET,WEATHER_SHRT,WEATHER_DESC) VALUES (%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,'%s','%s');";
char int_tab_insert[] = "INSERT INTO grafanadb.INT_WEATHER (CURR_TEMP, HUMIDITY) VALUES (%.2f,%.2f);";

sprintf(ext_query, ext_tab_insert, cur_temp_f,feels_like_f,main_temp_min_f,main_temp_max_f,cur_pressure_f,cur_humidity_f,sun_rise_f,sun_set_f,weather_main_f,weather_desc_f);
sprintf(int_query, int_tab_insert, Temperature,Humidity);

Serial.print("Internal table query:");
Serial.println(int_query);

Serial.print("External table query:");
Serial.println(ext_query);

Serial.println("Connecting to database..");

if (conn.connect(server_addr, 3306, user, password)) {
    
    Serial.println("Inserting data to internal weather table..");
    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
    cur_mem->execute(int_query);
    //cur_mem->close();
    //delete cur_mem;

    Serial.println("Inserting data to external weather table..");
//    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
    cur_mem->execute(ext_query);
    cur_mem->close();
    delete cur_mem;
     
    Serial.println("Tables updated..");
    conn.close();
  }
  else
    Serial.println("Database Connection failed.");
}

void publish_temp(float tmp){
   Serial.print("Going to publish temp data:");
   Serial.println(tmp);
   
  //MQTT_connect();
  MQTT_connect2();

  if (! temperature.publish(tmp)) {
    Serial.println(F("Failed to publish temperature data to MQTT"));
  } else {
    Serial.println(F("Published temperature temperature data to MQTT"));
  }
  
  }


void publish_humid(float humid){
   Serial.print("Going to publish humidity data:");
   Serial.println(humid);
   
  //MQTT_connect();
  MQTT_connect2();

  if (! humidity.publish(humid)) {
    Serial.println(F("Failed to publish temperature data to MQTT"));
  } else {
    Serial.println(F("Published temperature temperature data to MQTT"));
  }
  
  }


//--------------------------------------------MQTT Definitions---------------------------------------------
void MQTT_connect() {
  int8_t ret;
  int8_t tries=1;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  while ((ret = mqtt.connect()) != 0 or tries < 4) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(1000);  // wait 5 seconds
       tries++;
       
  }
  Serial.println("MQTT Connected!");
}

void MQTT_connect2() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT Version2... ");

  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(50000);  // wait 5 seconds
  }
  Serial.println("MQTT Connected!");
}