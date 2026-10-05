#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

#include <Adafruit_BMP3XX.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA219.h>
#include <Adafruit_NeoPixel.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID     = "ESPTest";
const char* WIFI_PASSWORD = "ESPpassword";

// =====================================================
// PINS
// =====================================================

#define SDA_PIN 1
#define SCL_PIN 2

// OpenLog UART
#define OPENLOG_RX 13
#define OPENLOG_TX 5

// ESP32-S3-Zero onboard RGB LED
#define LED_PIN 21
#define LED_COUNT 1

// =====================================================
// TIMING
// =====================================================

// 16 Hz = 62.5 ms
#define SENSOR_INTERVAL_US 62500UL

// Log every 8th packet
// 16 Hz / 8 = 2 Hz
#define LOG_EVERY_N_PACKETS 8

// =====================================================
// OBJECTS
// =====================================================

WebServer server(80);

Adafruit_BMP3XX bmp;

Adafruit_BNO055 bno(
  55,
  0x28,
  &Wire
);

Adafruit_INA219 ina219(
  0x40
);

HardwareSerial OpenLog(1);

Adafruit_NeoPixel statusLED(
  LED_COUNT,
  LED_PIN,
  NEO_GRB + NEO_KHZ800
);

// =====================================================
// SENSOR STATUS
// =====================================================

bool bmpAvailable = false;
bool bnoAvailable = false;
bool inaAvailable = false;

// =====================================================
// CALIBRATION REFERENCES
// =====================================================

// BMP390 reference pressure
float referencePressure = 0.0f;

// BNO055 software zero/reference
float bnoReferenceX = 0.0f;
float bnoReferenceY = 0.0f;
float bnoReferenceZ = 0.0f;

bool bnoReferenceValid = false;

// =====================================================
// TELEMETRY STRUCTURE
// =====================================================

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

// Latest telemetry packet only
Telemetry latestTelemetry;

// =====================================================
// PACKET COUNTER
// =====================================================

uint32_t packetCount = 0;

// =====================================================
// VELOCITY
// =====================================================

float previousAltitude = 0.0f;

bool previousAltitudeValid = false;

uint32_t previousAltitudeTime = 0;

// =====================================================
// SENSOR SCHEDULER
// =====================================================

uint32_t nextSensorTime = 0;

// =====================================================
// RGB LED STATUS
// =====================================================

void updateStatusLED() {

  uint32_t color;

  // ---------------------------------------------------
  // Neither BMP nor BNO works
  // RED
  // ---------------------------------------------------

  if (
    !bmpAvailable &&
    !bnoAvailable
  ) {

    color = statusLED.Color(
      255,
      0,
      0
    );
  }

  // ---------------------------------------------------
  // BNO works, BMP doesn't
  // ORANGE
  // ---------------------------------------------------

  else if (
    bnoAvailable &&
    !bmpAvailable
  ) {

    color = statusLED.Color(
      255,
      80,
      0
    );
  }

  // ---------------------------------------------------
  // BMP works, BNO doesn't
  // YELLOW
  // ---------------------------------------------------

  else if (
    bmpAvailable &&
    !bnoAvailable
  ) {

    color = statusLED.Color(
      255,
      255,
      0
    );
  }

  // ---------------------------------------------------
  // Both work
  // GREEN
  // ---------------------------------------------------

  else {

    color = statusLED.Color(
      0,
      255,
      0
    );
  }

  statusLED.setPixelColor(
    0,
    color
  );

  statusLED.show();
}

// =====================================================
// CORS
// =====================================================

void addCORSHeaders() {

  server.sendHeader(
    "Access-Control-Allow-Origin",
    "*"
  );

  server.sendHeader(
    "Access-Control-Allow-Methods",
    "GET,POST,OPTIONS"
  );

  server.sendHeader(
    "Access-Control-Allow-Headers",
    "Content-Type"
  );
}

// =====================================================
// CORS OPTIONS
// =====================================================

void handleOptions() {

  addCORSHeaders();

  server.send(
    204
  );
}

// =====================================================
// BMP CALIBRATION
// =====================================================

void calibrateReferencePressure() {

  if (!bmpAvailable) {

    referencePressure = 0.0f;

    Serial.println(
      "BMP390 unavailable."
    );

    return;
  }

  if (
    bmp.performReading()
  ) {

    referencePressure =
      bmp.pressure / 100.0f;

    Serial.print(
      "New reference pressure: "
    );

    Serial.print(
      referencePressure,
      2
    );

    Serial.println(
      " hPa"
    );

  } else {

    Serial.println(
      "BMP390 reading failed."
    );
  }
}

// =====================================================
// COMPLETE SENSOR CALIBRATION
// =====================================================

void calibrateSensors() {

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "       SENSOR CALIBRATION"
  );

  Serial.println(
    "================================"
  );

  // ===================================================
  // BMP390
  // ===================================================

  calibrateReferencePressure();

  // ===================================================
  // BNO055
  // ===================================================

  if (bnoAvailable) {

    sensors_event_t orientation;

    bno.getEvent(
      &orientation,
      Adafruit_BNO055::VECTOR_EULER
    );

    bnoReferenceX =
      orientation.orientation.x;

    bnoReferenceY =
      orientation.orientation.y;

    bnoReferenceZ =
      orientation.orientation.z;

    bnoReferenceValid = true;

    Serial.println(
      "BNO055 reference:"
    );

    Serial.print(
      "X = "
    );

    Serial.println(
      bnoReferenceX,
      2
    );

    Serial.print(
      "Y = "
    );

    Serial.println(
      bnoReferenceY,
      2
    );

    Serial.print(
      "Z = "
    );

    Serial.println(
      bnoReferenceZ,
      2
    );

  } else {

    bnoReferenceX = 0.0f;
    bnoReferenceY = 0.0f;
    bnoReferenceZ = 0.0f;

    bnoReferenceValid = false;

    Serial.println(
      "BNO055 unavailable."
    );
  }

  // ===================================================
  // RESET VELOCITY REFERENCE
  // ===================================================

  previousAltitude = 0.0f;

  previousAltitudeValid = false;

  previousAltitudeTime =
    micros();

  Serial.println(
    "Velocity reference reset."
  );

  Serial.println(
    "Calibration complete."
  );

  Serial.println(
    "================================"
  );
  Serial.println();
}

// =====================================================
// INITIAL BOOT PRESSURE CALIBRATION
// =====================================================

void calibrateBootPressure() {

  if (!bmpAvailable) {

    referencePressure = 0.0f;

    Serial.println(
      "BMP390 unavailable."
    );

    return;
  }

  Serial.println();
  Serial.println(
    "===== BOOT PRESSURE CALIBRATION ====="
  );

  bool fifthReadingValid = false;

  for (
    int i = 0;
    i < 5;
    i++
  ) {

    if (
      bmp.performReading()
    ) {

      float pressure =
        bmp.pressure / 100.0f;

      Serial.print(
        "Reading "
      );

      Serial.print(
        i + 1
      );

      Serial.print(
        ": "
      );

      Serial.print(
        pressure,
        2
      );

      Serial.println(
        " hPa"
      );

      // Fifth reading becomes reference
      if (i == 4) {

        referencePressure =
          pressure;

        fifthReadingValid = true;
      }

    } else {

      Serial.print(
        "Reading "
      );

      Serial.print(
        i + 1
      );

      Serial.println(
        ": FAILED"
      );
    }

    if (i < 4) {

      delay(200);
    }
  }

  if (!fifthReadingValid) {

    referencePressure = 0.0f;
  }

  Serial.print(
    "Reference pressure = "
  );

  Serial.print(
    referencePressure,
    2
  );

  Serial.println(
    " hPa"
  );

  Serial.println(
    "======================================"
  );
}

// =====================================================
// READ BMP390
// =====================================================

void readBMP(
  Telemetry &t
) {

  if (!bmpAvailable) {

    t.pressure = 0.0f;
    t.altitude = 0.0f;
    t.velocity = 0.0f;

    return;
  }

  if (
    !bmp.performReading()
  ) {

    t.pressure = 0.0f;
    t.altitude = 0.0f;
    t.velocity = 0.0f;

    return;
  }

  // ---------------------------------------------------
  // Pressure
  // ---------------------------------------------------

  t.pressure =
    bmp.pressure / 100.0f;

  // ---------------------------------------------------
  // Altitude
  // ---------------------------------------------------

  if (
    referencePressure > 0.0f
  ) {

    t.altitude =
      bmp.readAltitude(
        referencePressure
      );

  } else {

    t.altitude = 0.0f;
  }

  // ---------------------------------------------------
  // Velocity
  // ---------------------------------------------------

  uint32_t now =
    micros();

  if (
    previousAltitudeValid
  ) {

    float dt =
      (now - previousAltitudeTime)
      / 1000000.0f;

    if (
      dt > 0.001f &&
      dt < 1.0f
    ) {

      t.velocity =
        (
          t.altitude -
          previousAltitude
        ) / dt;

    } else {

      t.velocity = 0.0f;
    }

  } else {

    t.velocity = 0.0f;

    previousAltitudeValid =
      true;
  }

  previousAltitude =
    t.altitude;

  previousAltitudeTime =
    now;
}

// =====================================================
// READ BNO055
// =====================================================

void readBNO(
  Telemetry &t
) {

  if (!bnoAvailable) {

    t.orientationX = 0.0f;
    t.orientationY = 0.0f;
    t.orientationZ = 0.0f;

    return;
  }

  sensors_event_t orientation;

  bno.getEvent(
    &orientation,
    Adafruit_BNO055::VECTOR_EULER
  );

  float rawX =
    orientation.orientation.x;

  float rawY =
    orientation.orientation.y;

  float rawZ =
    orientation.orientation.z;

  // ---------------------------------------------------
  // Apply calibration reference
  // ---------------------------------------------------

  if (
    bnoReferenceValid
  ) {

    t.orientationX =
      rawX - bnoReferenceX;

    t.orientationY =
      rawY - bnoReferenceY;

    t.orientationZ =
      rawZ - bnoReferenceZ;

  } else {

    t.orientationX =
      rawX;

    t.orientationY =
      rawY;

    t.orientationZ =
      rawZ;
  }

  // ---------------------------------------------------
  // Normalize angles
  // ---------------------------------------------------

  if (
    t.orientationX > 180.0f
  )
    t.orientationX -= 360.0f;

  if (
    t.orientationX < -180.0f
  )
    t.orientationX += 360.0f;

  if (
    t.orientationY > 180.0f
  )
    t.orientationY -= 360.0f;

  if (
    t.orientationY < -180.0f
  )
    t.orientationY += 360.0f;

  if (
    t.orientationZ > 180.0f
  )
    t.orientationZ -= 360.0f;

  if (
    t.orientationZ < -180.0f
  )
    t.orientationZ += 360.0f;
}

// =====================================================
// READ INA219
// =====================================================

void readINA219(
  Telemetry &t
) {

  if (!inaAvailable) {

    t.batteryVoltage = 0.0f;

    return;
  }

  t.batteryVoltage =
    ina219.getBusVoltage_V();
}

// =====================================================
// ACQUIRE TELEMETRY
// =====================================================

void acquireTelemetry() {

  Telemetry t;

  // ---------------------------------------------------
  // Packet
  // ---------------------------------------------------

  t.packet =
    ++packetCount;

  // ---------------------------------------------------
  // Defaults
  // ---------------------------------------------------

  t.pressure = 0.0f;
  t.altitude = 0.0f;
  t.velocity = 0.0f;

  t.orientationX = 0.0f;
  t.orientationY = 0.0f;
  t.orientationZ = 0.0f;

  t.batteryVoltage = 0.0f;

  // ---------------------------------------------------
  // Read sensors
  // ---------------------------------------------------

  readBMP(t);

  readBNO(t);

  readINA219(t);

  // ---------------------------------------------------
  // Update latest packet
  // ---------------------------------------------------

  latestTelemetry = t;
}

// =====================================================
// OPENLOG
// =====================================================

void logTelemetry(
  const Telemetry &t
) {

  OpenLog.print(
    t.packet
  );

  OpenLog.print(",");

  OpenLog.print(
    t.pressure,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.altitude,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.velocity,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.orientationX,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.orientationY,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.orientationZ,
    2
  );

  OpenLog.print(",");

  OpenLog.println(
    t.batteryVoltage,
    3
  );
}

// =====================================================
// RESET PACKET COUNT
// =====================================================

void resetPacketCount() {

  packetCount = 0;

  latestTelemetry.packet = 0;

  Serial.println(
    "Packet counter reset."
  );
}

// =====================================================
// API: LIVE TELEMETRY
// =====================================================

void handleLiveAPI() {

  addCORSHeaders();

  Telemetry t =
    latestTelemetry;

  char json[220];

  snprintf(
    json,
    sizeof(json),

    "{\"p\":%lu,"
    "\"pr\":%.2f,"
    "\"a\":%.2f,"
    "\"v\":%.2f,"
    "\"x\":%.2f,"
    "\"y\":%.2f,"
    "\"z\":%.2f,"
    "\"bv\":%.3f}",

    (unsigned long)t.packet,

    t.pressure,

    t.altitude,

    t.velocity,

    t.orientationX,

    t.orientationY,

    t.orientationZ,

    t.batteryVoltage
  );

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// API: STATUS
// =====================================================

void handleStatusAPI() {

  addCORSHeaders();

  char json[180];

  snprintf(
    json,
    sizeof(json),

    "{\"bmp\":%s,"
    "\"bno\":%s,"
    "\"ina\":%s,"
    "\"ref\":%.2f}",

    bmpAvailable
      ? "true"
      : "false",

    bnoAvailable
      ? "true"
      : "false",

    inaAvailable
      ? "true"
      : "false",

    referencePressure
  );

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// API: RESET
// =====================================================

void handleResetAPI() {

  addCORSHeaders();

  resetPacketCount();

  server.send(
    200,
    "application/json",
    "{\"success\":true,\"action\":\"reset\"}"
  );
}

// =====================================================
// API: CALIBRATE
// =====================================================

void handleCalibrateAPI() {

  addCORSHeaders();

  calibrateSensors();

  server.send(
    200,
    "application/json",
    "{\"success\":true,\"action\":\"calibrate\"}"
  );
}

// =====================================================
// API: OPTIONS
// =====================================================

void handleOptionsAPI() {

  addCORSHeaders();

  server.send(
    204
  );
}

// =====================================================
// 404
// =====================================================

void handleNotFound() {

  addCORSHeaders();

  server.send(
    404,
    "application/json",
    "{\"error\":\"not_found\"}"
  );
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  // ---------------------------------------------------
  // Initial delay
  // ---------------------------------------------------

  delay(1000);

  // ---------------------------------------------------
  // Serial
  // ---------------------------------------------------

  Serial.begin(
    115200
  );

  delay(500);

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    " ESP32-S3 CANSAT TELEMETRY SYSTEM"
  );

  Serial.println(
    " 16 Hz SENSOR / 16 Hz API / 2 Hz LOG"
  );

  Serial.println(
    "========================================"
  );

  // ---------------------------------------------------
  // RGB LED
  // ---------------------------------------------------

  statusLED.begin();

  statusLED.setBrightness(
    40
  );

  statusLED.clear();

  statusLED.show();

  // ---------------------------------------------------
  // I2C
  // ---------------------------------------------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  // Fast I2C
  Wire.setClock(
    400000
  );

  Serial.println(
    "I2C = 400 kHz"
  );

  // ---------------------------------------------------
  // BMP390
  // ---------------------------------------------------

  Serial.println(
    "Checking BMP390..."
  );

  if (
    bmp.begin_I2C(
      0x77
    )
  ) {

    bmpAvailable = true;

    Serial.println(
      "BMP390 OK"
    );

    bmp.setTemperatureOversampling(
      BMP3_OVERSAMPLING_2X
    );

    bmp.setPressureOversampling(
      BMP3_OVERSAMPLING_4X
    );

    bmp.setIIRFilterCoeff(
      BMP3_IIR_FILTER_COEFF_3
    );

  } else {

    bmpAvailable = false;

    Serial.println(
      "BMP390 NOT FOUND"
    );
  }

  // ---------------------------------------------------
  // BNO055
  // ---------------------------------------------------

  Serial.println(
    "Checking BNO055..."
  );

  if (
    bno.begin()
  ) {

    bnoAvailable = true;

    Serial.println(
      "BNO055 OK"
    );

    delay(1000);

    bno.setExtCrystalUse(
      true
    );

  } else {

    bnoAvailable = false;

    Serial.println(
      "BNO055 NOT FOUND"
    );
  }

  // ---------------------------------------------------
  // INA219
  // ---------------------------------------------------

  Serial.println(
    "Checking INA219..."
  );

  if (
    ina219.begin()
  ) {

    inaAvailable = true;

    Serial.println(
      "INA219 OK"
    );

    ina219.setCalibration_32V_2A();

  } else {

    inaAvailable = false;

    Serial.println(
      "INA219 NOT FOUND"
    );
  }

  // ---------------------------------------------------
  // LED
  // ---------------------------------------------------

  updateStatusLED();

  // ---------------------------------------------------
  // Boot pressure calibration
  // ---------------------------------------------------

  calibrateBootPressure();

  // ---------------------------------------------------
  // OpenLog
  // ---------------------------------------------------

  Serial.println(
    "Starting OpenLog..."
  );

  OpenLog.begin(
    115200,
    SERIAL_8N1,
    OPENLOG_RX,
    OPENLOG_TX
  );

  delay(500);

  OpenLog.println(
    "packet,pressure,altitude,velocity,"
    "orientationX,orientationY,"
    "orientationZ,batteryVoltage"
  );

  Serial.println(
    "OpenLog = 115200 baud"
  );

  // ---------------------------------------------------
  // Initialize telemetry
  // ---------------------------------------------------

  latestTelemetry.packet = 0;

  latestTelemetry.pressure = 0.0f;
  latestTelemetry.altitude = 0.0f;
  latestTelemetry.velocity = 0.0f;

  latestTelemetry.orientationX = 0.0f;
  latestTelemetry.orientationY = 0.0f;
  latestTelemetry.orientationZ = 0.0f;

  latestTelemetry.batteryVoltage = 0.0f;

  // ---------------------------------------------------
  // WiFi
  // ---------------------------------------------------

  Serial.println(
    "Connecting to WiFi..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(250);

    Serial.print(".");
  }

  Serial.println();

  Serial.println(
    "WiFi connected."
  );

  Serial.print(
    "ESP32 IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  // ---------------------------------------------------
  // API ROUTES
  // ---------------------------------------------------

  server.on(
    "/api/live",
    HTTP_GET,
    handleLiveAPI
  );

  server.on(
    "/api/status",
    HTTP_GET,
    handleStatusAPI
  );

  server.on(
    "/api/reset",
    HTTP_POST,
    handleResetAPI
  );

  server.on(
    "/api/calibrate",
    HTTP_POST,
    handleCalibrateAPI
  );

  server.on(
    "/api/live",
    HTTP_OPTIONS,
    handleOptionsAPI
  );

  server.on(
    "/api/status",
    HTTP_OPTIONS,
    handleOptionsAPI
  );

  server.on(
    "/api/reset",
    HTTP_OPTIONS,
    handleOptionsAPI
  );

  server.on(
    "/api/calibrate",
    HTTP_OPTIONS,
    handleOptionsAPI
  );

  server.onNotFound(
    handleNotFound
  );

  server.begin();

  Serial.println(
    "HTTP API started."
  );

  Serial.print(
    "Live API: http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/api/live"
  );

  Serial.print(
    "Status API: http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/api/status"
  );

  Serial.print(
    "Reset API: POST http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/api/reset"
  );

  Serial.print(
    "Calibrate API: POST http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/api/calibrate"
  );

  Serial.println();

  Serial.println(
    "SYSTEM READY"
  );

  // ---------------------------------------------------
  // Start 16 Hz scheduler
  // ---------------------------------------------------

  nextSensorTime =
    micros();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // ---------------------------------------------------
  // Handle API requests
  // ---------------------------------------------------

  server.handleClient();

  // ---------------------------------------------------
  // 16 Hz sensor acquisition
  // ---------------------------------------------------

  uint32_t now =
    micros();

  if (
    (int32_t)(
      now - nextSensorTime
    ) >= 0
  ) {

    nextSensorTime +=
      SENSOR_INTERVAL_US;

    // -------------------------------------------------
    // Acquire telemetry
    // -------------------------------------------------

    acquireTelemetry();

    // -------------------------------------------------
    // OpenLog: 2 Hz
    // -------------------------------------------------

    if (
      packetCount %
      LOG_EVERY_N_PACKETS
      == 0
    ) {

      logTelemetry(
        latestTelemetry
      );
    }
  }
}