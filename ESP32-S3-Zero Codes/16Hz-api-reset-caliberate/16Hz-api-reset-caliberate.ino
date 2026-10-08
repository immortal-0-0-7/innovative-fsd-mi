#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

#include <Adafruit_BMP3XX.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA219.h>
#include <Adafruit_NeoPixel.h>

// wifi credentials
const char* WIFI_SSID     = "ESPTest";
const char* WIFI_PASSWORD = "ESPpassword";

// pin assignments
#define SDA_PIN 1
#define SCL_PIN 2
#define OPENLOG_RX 13
#define OPENLOG_TX 5
#define LED_PIN 21
#define LED_COUNT 1

// timing
#define SENSOR_INTERVAL_US 62500UL
#define LOG_EVERY_N_PACKETS 8

// hardware objects
WebServer server(80);
Adafruit_BMP3XX bmp;
Adafruit_BNO055 bno(55, 0x28, &Wire);
Adafruit_INA219 ina219(0x40);
HardwareSerial OpenLog(1);
Adafruit_NeoPixel statusLED(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// sensor status flags
bool bmpAvailable = false;
bool bnoAvailable = false;
bool inaAvailable = false;

// calibration references
float referencePressure = 0.0f;
float bnoReferenceX = 0.0f;
float bnoReferenceY = 0.0f;
float bnoReferenceZ = 0.0f;
bool bnoReferenceValid = false;

// telemetry data structure
struct Telemetry {
  uint32_t packet;
  float pressure;
  float altitude;
  float velocity;
  float orientationX;
  float orientationY;
  float orientationZ;
  float batteryVoltage;
};

Telemetry latestTelemetry = {};
uint32_t packetCount = 0;

// velocity estimation state
float previousAltitude = 0.0f;
bool previousAltitudeValid = false;
uint32_t previousAltitudeTime = 0;

// scheduler
uint32_t nextSensorTime = 0;

// normalize angle to [-180, 180] range
static inline float normalizeAngle(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

// update status led color based on sensor health
void updateStatusLED() {
  uint32_t color;
  if (!bmpAvailable && !bnoAvailable) {
    color = statusLED.Color(0, 0, 255);       // red: both sensors missing
  } else if (bnoAvailable && !bmpAvailable) {
    color = statusLED.Color(255, 80, 0);      // orange: bno only
  } else if (bmpAvailable && !bnoAvailable) {
    color = statusLED.Color(255, 255, 0);     // yellow: bmp only
  } else {
    color = statusLED.Color(0, 255, 0);       // green: all sensors ok
  }
  statusLED.setPixelColor(0, color);
  statusLED.show();
}

// append cors headers to response
void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// calibrate bmp390 reference pressure
void calibrateReferencePressure() {
  if (!bmpAvailable) {
    referencePressure = 0.0f;
    Serial.println("BMP390 unavailable.");
    return;
  }
  if (bmp.performReading()) {
    referencePressure = bmp.pressure / 100.0f;
    Serial.print("New reference pressure: ");
    Serial.print(referencePressure, 2);
    Serial.println(" hPa");
  } else {
    Serial.println("BMP390 reading failed.");
  }
}

// full sensor calibration
void calibrateSensors() {
  Serial.println("\n================================\n       SENSOR CALIBRATION\n================================");
  calibrateReferencePressure();

  if (bnoAvailable) {
    sensors_event_t orientation;
    bno.getEvent(&orientation, Adafruit_BNO055::VECTOR_EULER);
    bnoReferenceX = orientation.orientation.x;
    bnoReferenceY = orientation.orientation.y;
    bnoReferenceZ = orientation.orientation.z;
    bnoReferenceValid = true;

    Serial.println("BNO055 reference:");
    Serial.print("X = "); Serial.println(bnoReferenceX, 2);
    Serial.print("Y = "); Serial.println(bnoReferenceY, 2);
    Serial.print("Z = "); Serial.println(bnoReferenceZ, 2);
  } else {
    bnoReferenceX = bnoReferenceY = bnoReferenceZ = 0.0f;
    bnoReferenceValid = false;
    Serial.println("BNO055 unavailable.");
  }

  previousAltitude = 0.0f;
  previousAltitudeValid = false;
  previousAltitudeTime = micros();
  Serial.println("Velocity reference reset.");
  Serial.println("Calibration complete.");
  Serial.println("================================\n");
}

// boot pressure calibration
void calibrateBootPressure() {
  if (!bmpAvailable) {
    referencePressure = 0.0f;
    Serial.println("BMP390 unavailable.");
    return;
  }

  Serial.println("\n===== BOOT PRESSURE CALIBRATION =====");
  bool fifthReadingValid = false;
  for (int i = 0; i < 5; i++) {
    if (bmp.performReading()) {
      float pressure = bmp.pressure / 100.0f;
      Serial.print("Reading "); Serial.print(i + 1); Serial.print(": ");
      Serial.print(pressure, 2); Serial.println(" hPa");
      if (i == 4) {
        referencePressure = pressure;
        fifthReadingValid = true;
      }
    } else {
      Serial.print("Reading "); Serial.print(i + 1); Serial.println(": FAILED");
    }
    if (i < 4) delay(200);
  }

  if (!fifthReadingValid) referencePressure = 0.0f;
  Serial.print("Reference pressure = ");
  Serial.print(referencePressure, 2);
  Serial.println(" hPa");
  Serial.println("======================================");
}

// read bmp390 telemetry
void readBMP(Telemetry &t) {
  if (!bmpAvailable || !bmp.performReading()) {
    t.pressure = 0.0f;
    t.altitude = 0.0f;
    t.velocity = 0.0f;
    return;
  }

  t.pressure = bmp.pressure / 100.0f;
  t.altitude = (referencePressure > 0.0f) ? bmp.readAltitude(referencePressure) : 0.0f;

  uint32_t now = micros();
  if (previousAltitudeValid) {
    float dt = (now - previousAltitudeTime) / 1000000.0f;
    t.velocity = (dt > 0.001f && dt < 1.0f) ? ((t.altitude - previousAltitude) / dt) : 0.0f;
  } else {
    t.velocity = 0.0f;
    previousAltitudeValid = true;
  }
  previousAltitude = t.altitude;
  previousAltitudeTime = now;
}

// read bno055 telemetry
void readBNO(Telemetry &t) {
  if (!bnoAvailable) {
    t.orientationX = t.orientationY = t.orientationZ = 0.0f;
    return;
  }

  sensors_event_t orientation;
  bno.getEvent(&orientation, Adafruit_BNO055::VECTOR_EULER);

  float rawX = orientation.orientation.x;
  float rawY = orientation.orientation.y;
  float rawZ = orientation.orientation.z;

  if (bnoReferenceValid) {
    rawX -= bnoReferenceX;
    rawY -= bnoReferenceY;
    rawZ -= bnoReferenceZ;
  }

  t.orientationX = normalizeAngle(rawX);
  t.orientationY = normalizeAngle(rawY);
  t.orientationZ = normalizeAngle(rawZ);
}

// read ina219 voltage
void readINA219(Telemetry &t) {
  t.batteryVoltage = inaAvailable ? ina219.getBusVoltage_V() : 0.0f;
}

// sample all sensors
void acquireTelemetry() {
  Telemetry t;
  t.packet = ++packetCount;
  readBMP(t);
  readBNO(t);
  readINA219(t);
  latestTelemetry = t;
}

// log telemetry packet to openlog uart
void logTelemetry(const Telemetry &t) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f",
           (unsigned long)t.packet, t.pressure, t.altitude, t.velocity,
           t.orientationX, t.orientationY, t.orientationZ, t.batteryVoltage);
  OpenLog.println(buf);
}

// reset packet counter
void resetPacketCount() {
  packetCount = 0;
  latestTelemetry.packet = 0;
  Serial.println("Packet counter reset.");
}

// api: live telemetry
void handleLiveAPI() {
  addCORSHeaders();
  char json[220];
  snprintf(json, sizeof(json),
           "{\"p\":%lu,\"pr\":%.2f,\"a\":%.2f,\"v\":%.2f,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"bv\":%.3f}",
           (unsigned long)latestTelemetry.packet, latestTelemetry.pressure, latestTelemetry.altitude,
           latestTelemetry.velocity, latestTelemetry.orientationX, latestTelemetry.orientationY,
           latestTelemetry.orientationZ, latestTelemetry.batteryVoltage);
  server.send(200, "application/json", json);
}

// api: system status
void handleStatusAPI() {
  addCORSHeaders();
  char json[128];
  snprintf(json, sizeof(json),
           "{\"bmp\":%s,\"bno\":%s,\"ina\":%s,\"ref\":%.2f}",
           bmpAvailable ? "true" : "false",
           bnoAvailable ? "true" : "false",
           inaAvailable ? "true" : "false",
           referencePressure);
  server.send(200, "application/json", json);
}

// api: reset
void handleResetAPI() {
  addCORSHeaders();
  resetPacketCount();
  server.send(200, "application/json", "{\"success\":true,\"action\":\"reset\"}");
}

// api: calibrate
void handleCalibrateAPI() {
  addCORSHeaders();
  calibrateSensors();
  server.send(200, "application/json", "{\"success\":true,\"action\":\"calibrate\"}");
}

// api: cors options preflight
void handleOptionsAPI() {
  addCORSHeaders();
  server.send(204);
}

// api: 404 handler
void handleNotFound() {
  addCORSHeaders();
  server.send(404, "application/json", "{\"error\":\"not_found\"}");
}

// system setup
void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println(" ESP32-S3 CANSAT TELEMETRY SYSTEM");
  Serial.println(" 16 Hz SENSOR / 16 Hz API / 2 Hz LOG");
  Serial.println("========================================");

  // status led
  statusLED.begin();
  statusLED.setBrightness(40);
  statusLED.clear();
  statusLED.show();

  // i2c bus at 400 khz
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  Serial.println("I2C = 400 kHz");

  // bmp390 setup
  Serial.println("Checking BMP390...");
  if (bmp.begin_I2C(0x77)) {
    bmpAvailable = true;
    Serial.println("BMP390 OK");
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
  } else {
    bmpAvailable = false;
    Serial.println("BMP390 NOT FOUND");
  }

  // bno055 setup
  Serial.println("Checking BNO055...");
  if (bno.begin()) {
    bnoAvailable = true;
    Serial.println("BNO055 OK");
    delay(1000);
    bno.setExtCrystalUse(true);
  } else {
    bnoAvailable = false;
    Serial.println("BNO055 NOT FOUND");
  }

  // ina219 setup
  Serial.println("Checking INA219...");
  if (ina219.begin()) {
    inaAvailable = true;
    Serial.println("INA219 OK");
    ina219.setCalibration_32V_2A();
  } else {
    inaAvailable = false;
    Serial.println("INA219 NOT FOUND");
  }

  updateStatusLED();
  calibrateBootPressure();

  // openlog serial
  Serial.println("Starting OpenLog...");
  OpenLog.begin(115200, SERIAL_8N1, OPENLOG_RX, OPENLOG_TX);
  delay(500);
  OpenLog.println("packet,pressure,altitude,velocity,orientationX,orientationY,orientationZ,batteryVoltage");
  Serial.println("OpenLog = 115200 baud");

  // default telemetry state
  latestTelemetry = {};

  // wifi connection
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // api routes
  server.on("/api/live", HTTP_GET, handleLiveAPI);
  server.on("/api/status", HTTP_GET, handleStatusAPI);
  server.on("/api/reset", HTTP_POST, handleResetAPI);
  server.on("/api/calibrate", HTTP_POST, handleCalibrateAPI);
  server.on("/api/live", HTTP_OPTIONS, handleOptionsAPI);
  server.on("/api/status", HTTP_OPTIONS, handleOptionsAPI);
  server.on("/api/reset", HTTP_OPTIONS, handleOptionsAPI);
  server.on("/api/calibrate", HTTP_OPTIONS, handleOptionsAPI);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP API started.");
  Serial.print("Live API: http://"); Serial.print(WiFi.localIP()); Serial.println("/api/live");
  Serial.print("Status API: http://"); Serial.print(WiFi.localIP()); Serial.println("/api/status");
  Serial.print("Reset API: POST http://"); Serial.print(WiFi.localIP()); Serial.println("/api/reset");
  Serial.print("Calibrate API: POST http://"); Serial.print(WiFi.localIP()); Serial.println("/api/calibrate\n");
  Serial.println("SYSTEM READY");

  nextSensorTime = micros();
}

// main execution loop
void loop() {
  server.handleClient();

  uint32_t now = micros();
  if ((int32_t)(now - nextSensorTime) >= 0) {
    if ((int32_t)(now - nextSensorTime) > (int32_t)SENSOR_INTERVAL_US) {
      nextSensorTime = now + SENSOR_INTERVAL_US;
    } else {
      nextSensorTime += SENSOR_INTERVAL_US;
    }

    acquireTelemetry();

    if ((packetCount % LOG_EVERY_N_PACKETS) == 0) {
      logTelemetry(latestTelemetry);
    }
  }
}