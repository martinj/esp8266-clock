//#define BLYNK_PRINT Serial

#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// #define DEBUG_SERIAL

// MDNS name, used for ArduinoOTA
#define DEVICE_NAME "clock"

// WIFI & Blynk
char auth[] = "BLYNK_API_KEY";
char ssid[] = "SSID";
char pass[] = "SSID_PASSWORD";

// Temp sensor
#define DHT_PIN 0
#define DHT_TYPE DHT11
DHT_Unified dht(DHT_PIN, DHT_TYPE);
unsigned long sensorReadingDelayMs;
unsigned long lastSensorReadingMs;
int currentSensorTemp;
int currentSensorHum;

// Weather
#define WEATHER_API_KEY "OPEN_WEATHER_API_KEY"
String weatherCityCountry = "Gothenburg,se";
unsigned int weatherDisplayTimeMs = 60000;

// Neo Pixel
#define LED_PIN 2
#define NUM_PIXELS 30
#define D1_OFFSET 0
#define D2_OFFSET 7
#define D3_OFFSET 16
#define D4_OFFSET 23
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_PIXELS, LED_PIN, NEO_GRBW + NEO_KHZ800);

#define WHITE pixels.Color(255, 255, 255)
uint32_t clockColors[] = {WHITE, WHITE, WHITE, WHITE, WHITE};

// Clock
int dispHour = 0;
int dispMinute = 0;
int currentMinute = 0;
int currentHour = 0;

BlynkTimer timer;
WidgetRTC rtc;

// Weather
unsigned long lastWeatherFetchMs = 0;
int showingTemp = 0;
int degreeC = 0;
unsigned long weatherStartMs = 0;

// Modes
#define CLOCK 1
#define WEATHER 2
unsigned int cycleTimeMs = 600000;
unsigned long lastModeChange = 0;
int selectedMode = CLOCK;

// Virtual Pin Mappings
#define P_BRIGHTNESS V0
#define P_RGBA V1
#define P_COLOR_SELECT V2
#define P_MODE V3
#define P_COLOR_STORAGE V4
#define P_OUT_TEMP V5
#define P_SENSOR_TEMP V6
#define P_SENSOR_HUM V7
#define P_OPENWEATHER_Q V8
#define P_CYCLE_TIME V9
#define P_WEATHER_DISPLAY_TIME V10

int selectedColorItem = 6;

// Function declarations
void showClock(bool forceRefresh);
void setMode(int mode);
void colorToHex(char *hex, uint32_t c);
uint32_t colorFromHex(char *hex);

BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
  Blynk.syncVirtual(P_BRIGHTNESS, P_MODE, P_COLOR_STORAGE, P_COLOR_SELECT, P_CYCLE_TIME, P_WEATHER_DISPLAY_TIME, P_OPENWEATHER_Q);
}

BLYNK_WRITE(P_OPENWEATHER_Q) {
  String query = param.asString();
  if (query.length() == 0) {
    Blynk.virtualWrite(P_OPENWEATHER_Q, weatherCityCountry);
    return;
  }

  // force refresh
  lastWeatherFetchMs = 0;
  weatherCityCountry = query;
}

BLYNK_WRITE(P_CYCLE_TIME) {
  int min = param.asInt();
  if (min == 0) {
    Blynk.virtualWrite(P_CYCLE_TIME, cycleTimeMs / 60000);
    return;
  }

  cycleTimeMs = min * 60000;
}

BLYNK_WRITE(P_WEATHER_DISPLAY_TIME) {
  int min = param.asInt();
  if (min == 0) {
    Blynk.virtualWrite(P_WEATHER_DISPLAY_TIME, weatherDisplayTimeMs / 60000);
    return;
  }

  weatherDisplayTimeMs = min * 60000;
}

BLYNK_WRITE(P_BRIGHTNESS) {
  pixels.setBrightness(param.asInt());
  pixels.show();
}

BLYNK_WRITE(P_MODE) {
  setMode(param.asInt());
}

BLYNK_WRITE(P_COLOR_SELECT) {
  selectedColorItem = param.asInt();
}

BLYNK_WRITE(P_COLOR_STORAGE) {
  if (param[0]) {
    clockColors[0] = (uint32_t) param[0].asInt();
  }

  if (param[1]) {
    clockColors[1] = (uint32_t) param[1].asInt();
  }

  if (param[2]) {
    clockColors[2] = (uint32_t) param[2].asInt();
  }

  if (param[3]) {
    clockColors[3] = (uint32_t) param[3].asInt();
  }

  if (param[4]) {
    clockColors[4] = (uint32_t) param[4].asInt();
  }
}

BLYNK_WRITE(P_RGBA) {
  uint32_t color = pixels.Color(param[0].asInt(), param[1].asInt(), param[2].asInt());
  if (selectedColorItem == 6) {
    // set colors on all
    for (int i = 0 ; i < 5 ; i++) {
      clockColors[i] = color;
    }
  } else {
    clockColors[selectedColorItem - 1] = color;
  }

  int digit1 = (int) clockColors[0];
  int digit2 = (int) clockColors[1];
  int dots = (int) clockColors[2];
  int digit3 = (int) clockColors[3];
  int digit4 = (int) clockColors[4];

  Blynk.virtualWrite(P_COLOR_STORAGE, digit1, digit2, dots, digit3, digit4);
  showClock(true);
}

void setMode(int mode) {
  selectedMode = mode;
  lastModeChange = millis();
  // reset vars to force redisplay
  showingTemp = 0;
  dispHour = 0;
  dispMinute = 0;

  if (selectedMode == WEATHER) {
    weatherStartMs = millis();
  }

  Blynk.virtualWrite(P_MODE, mode);

  #ifdef DEBUG_SERIAL
    Serial.printf("New mode selected: %d\n", selectedMode);
  #endif
}

void drawDigit(int offset, uint32_t color, int n) {
  if (n == 2 || n == 3 || n == 4 || n == 5 || n == 6 || n == 8 || n == 9) { //MIDDLE
    pixels.setPixelColor(0 + offset, color);
  } else {
    pixels.setPixelColor(0 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 1 || n == 2 || n == 3 || n == 4 || n == 7 || n == 8 || n == 9) { //TOP RIGHT
    pixels.setPixelColor(1 + offset, color);
  } else {
    pixels.setPixelColor(1 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 2 || n == 3 || n == 5 || n == 6 || n == 7 || n == 8 || n == 9) { //TOP
    pixels.setPixelColor(2 + offset, color);
  } else {
    pixels.setPixelColor(2 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 4 || n == 5 || n == 6 || n == 8 || n == 9) { //TOP LEFT
    pixels.setPixelColor(3 + offset, color);
  } else {
    pixels.setPixelColor(3 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 2 || n == 6 || n == 8) { //BOTTOM LEFT
    pixels.setPixelColor(4 + offset, color);
  } else {
    pixels.setPixelColor(4 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 2 || n == 3 || n == 5 || n == 6 || n == 8 || n == 9) { //BOTTOM
    pixels.setPixelColor(5 + offset, color);
  } else {
    pixels.setPixelColor(5 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 1 || n == 3 || n == 4 || n == 5 || n == 6 || n == 7 || n == 8 || n == 9) { //BOTTOM RIGHT
    pixels.setPixelColor(6 + offset, color);
  } else {
    pixels.setPixelColor(6 + offset, pixels.Color(0, 0, 0));
  }
}

void drawDots() {
  pixels.setPixelColor(D3_OFFSET - 1, clockColors[2]);
  pixels.setPixelColor(D3_OFFSET - 2, clockColors[2]);
}

void refreshTime() {
  currentHour = hour();
  currentMinute = minute();
  #ifdef DEBUG_SERIAL
    Serial.printf("Refresh time: %d:%d\n", currentHour, currentMinute);
  #endif
}

uint32_t getTempColor(int temp) {
  if (temp < -20) return pixels.Color(56, 209, 180);
  if (temp < -10) return pixels.Color(84, 216, 156);
  if (temp < 0) return pixels.Color(112, 222, 131);
  if (temp < 4) return pixels.Color(140, 229, 107);
  if (temp < 8) return pixels.Color(168, 235, 83);
  if (temp < 14) return pixels.Color(196, 242, 58);
  if (temp < 18) return pixels.Color(224, 248, 34);
  if (temp < 22) return pixels.Color(253, 244, 10);
  if (temp < 26) return pixels.Color(253, 233, 10);
  if (temp < 30) return pixels.Color(254, 142, 10);
  if (temp < 34) return pixels.Color(254, 105, 10);
  if (temp > 40) return pixels.Color(255, 68, 10);

  return pixels.Color(61, 225, 255);
}

void showClock(bool forceRefresh) {
  if (!forceRefresh && currentHour == dispHour && currentMinute == dispMinute) {
    return;
  }
  dispHour = currentHour;
  dispMinute = currentMinute;

  #ifdef DEBUG_SERIAL
    Serial.printf("Updating Time: %d:%d\n", dispHour, dispMinute);
  #endif

  drawDigit(D1_OFFSET, clockColors[0], dispHour / 10);
  drawDigit(D2_OFFSET, clockColors[1], dispHour - ((dispHour / 10) * 10));

  drawDigit(D3_OFFSET, clockColors[3], dispMinute / 10);
  drawDigit(D4_OFFSET, clockColors[4], dispMinute - ((dispMinute / 10) * 10));

  drawDots();
  pixels.show();
}

void updateWeather() {
  unsigned long now = millis();
  if (lastWeatherFetchMs != 0 && (now - lastWeatherFetchMs) < 300000) { // only fetch every 5th min
    return;
  }

  #ifdef DEBUG_SERIAL
    Serial.println("Updating weather");
  #endif

  lastWeatherFetchMs = now;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + (String) weatherCityCountry + "&APPID=" + (String) WEATHER_API_KEY + "&mode=json&units=metric";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(12) + 380;
    DynamicJsonBuffer jsonBuffer(capacity);

    JsonObject& root = jsonBuffer.parseObject(http.getString());
    JsonObject& main = root["main"];
    degreeC = main["temp"];
    Blynk.virtualWrite(P_OUT_TEMP, degreeC);
    #ifdef DEBUG_SERIAL
      Serial.printf("Temp %d\n", degreeC);
    #endif
  }

  http.end();
}

void updateTemperature() {
  unsigned long now = millis();
  if (!isnan(lastSensorReadingMs) && (now - lastSensorReadingMs) < sensorReadingDelayMs) {
    return;
  }
  lastSensorReadingMs = now;

  sensors_event_t tempEvt, humEvt;
  dht.temperature().getEvent(&tempEvt);
  dht.humidity().getEvent(&humEvt);
  if (isnan(tempEvt.temperature) || isnan(humEvt.temperature)) {
    #ifdef DEBUG_SERIAL
      Serial.println("Error reading temperature/humidity from sensor!");
    #endif
    return;
  }

  currentSensorTemp = tempEvt.temperature;
  currentSensorHum = humEvt.relative_humidity;
  #ifdef DEBUG_SERIAL
    Serial.printf("Sensor temp: %d, humidity: %d\n", currentSensorTemp, currentSensorHum);
  #endif
}

void writeTemperature() {
  Blynk.virtualWrite(P_SENSOR_TEMP, currentSensorTemp);
  Blynk.virtualWrite(P_SENSOR_HUM, currentSensorHum);
}

void showWeather() {
  if (degreeC == showingTemp) {
    return;
  }

  showingTemp = degreeC;
  int temp = degreeC;
  uint32_t tempColor = getTempColor(temp);
  bool positive = temp > -1;
  if (!positive) {
    temp = abs(temp);
  }
  int digit2 = temp % 10;
  temp /= 10; // modulos
  int digit1 = temp % 10;
  bool isTwoDigits = digit1 > 0;

  // clear strip
  for (int i = 0; i < NUM_PIXELS; i++) pixels.setPixelColor(i, pixels.Color(0, 0, 0));

  if (isTwoDigits) {
    if (positive) {
      drawDigit(D1_OFFSET, tempColor, digit1);
      drawDigit(D2_OFFSET, tempColor, digit2);
    } else {
      drawDigit(D2_OFFSET, tempColor, digit1);
      drawDigit(D3_OFFSET, tempColor, digit2);
    }
  } else {
      drawDigit(D2_OFFSET, tempColor, digit2);
  }

  if (!positive) {
    pixels.setPixelColor(D1_OFFSET + 0, tempColor);
  }

  if ((isTwoDigits && positive) || !isTwoDigits) {
    // degree symbol
    pixels.setPixelColor(D3_OFFSET + 0, tempColor);
    pixels.setPixelColor(D3_OFFSET + 1, tempColor);
    pixels.setPixelColor(D3_OFFSET + 2, tempColor);
    pixels.setPixelColor(D3_OFFSET + 3, tempColor);
  }

  // C
  pixels.setPixelColor(D4_OFFSET + 2, tempColor);
  pixels.setPixelColor(D4_OFFSET + 3, tempColor);
  pixels.setPixelColor(D4_OFFSET + 4, tempColor);
  pixels.setPixelColor(D4_OFFSET + 5, tempColor);
  pixels.show();
}

void cycleMode() {
  if (selectedMode == CLOCK && (millis() - lastModeChange) >= cycleTimeMs) {
    setMode(WEATHER); // weather will automatically switch back to clock after a while
  }
}

void setup() {
  Serial.begin(9600);
  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.begin();

  Blynk.begin(auth, ssid, pass);

  setSyncInterval(10 * 60); // Sync interval for rtc clock in seconds (10 minutes)

  dht.begin();
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  sensorReadingDelayMs = sensor.min_delay / 1000;

  timer.setInterval(60000L, writeTemperature);
  timer.setInterval(10000L, refreshTime);

  pixels.begin();
  pixels.show();
}

void loop() {
  ArduinoOTA.handle();
  Blynk.run();
  timer.run();
  cycleMode();
  updateTemperature();
  updateWeather();

  switch (selectedMode) {
    case CLOCK:
      showClock(false);
      break;
    case WEATHER:
      showWeather();
      if ((millis() - weatherStartMs) >= weatherDisplayTimeMs) {
        setMode(CLOCK);
      }
      break;
    default:
      showClock(false);
      break;
  }
}
