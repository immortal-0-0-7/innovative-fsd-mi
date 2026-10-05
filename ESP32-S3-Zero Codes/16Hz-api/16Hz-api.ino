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

// OpenLog
#define OPENLOG_RX 13
#define OPENLOG_TX 5

// ESP32-S3-Zero onboard RGB LED
#define LED_PIN 21
#define LED_COUNT 1

// =====================================================
// TIMING
// =====================================================

#define SENSOR_INTERVAL_US 62500UL   // 16 Hz
#define LOG_EVERY_N_PACKETS 8        // 16 / 8 = 2 Hz

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
// BMP REFERENCE
// =====================================================

float referencePressure = 0.0f;

// =====================================================
// TELEMETRY
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

// Only ONE packet is maintained.
// No telemetry history is stored.
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
// SENSOR TIMING
// =====================================================

uint32_t nextSensorTime = 0;

// =====================================================
// RGB STATUS LED
// =====================================================

void updateStatusLED() {

  uint32_t color;

  // BMP + BNO missing
  if (!bmpAvailable && !bnoAvailable) {

    // RED
    color = statusLED.Color(
      255,
      0,
      0
    );
  }

  // BNO works, BMP missing
  else if (
    bnoAvailable &&
    !bmpAvailable
  ) {

    // ORANGE
    color = statusLED.Color(
      255,
      80,
      0
    );
  }

  // BMP works, BNO missing
  else if (
    bmpAvailable &&
    !bnoAvailable
  ) {

    // YELLOW
    color = statusLED.Color(
      255,
      255,
      0
    );
  }

  // Both work
  else {

    // GREEN
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
// BMP REFERENCE PRESSURE
// =====================================================

void calibrateReferencePressure() {

  if (!bmpAvailable) {

    referencePressure = 0.0f;

    Serial.println(
      "BMP390 unavailable - reference pressure = 0"
    );

    return;
  }

  Serial.println();
  Serial.println(
    "===== BMP390 CALIBRATION ====="
  );

  float pressure = 0.0f;

  bool fifthReadingValid = false;

  for (int i = 0; i < 5; i++) {

    if (bmp.performReading()) {

      pressure =
        bmp.pressure / 100.0f;

      Serial.print(
        "Pressure "
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

      // Fifth reading is reference
      if (i == 4) {

        referencePressure =
          pressure;

        fifthReadingValid = true;
      }

    } else {

      Serial.print(
        "Pressure "
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
    "Reference pressure: "
  );

  Serial.print(
    referencePressure,
    2
  );

  Serial.println(
    " hPa"
  );

  Serial.println(
    "=============================="
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

  if (!bmp.performReading()) {

    t.pressure = 0.0f;
    t.altitude = 0.0f;
    t.velocity = 0.0f;

    return;
  }

  // Pressure in hPa
  t.pressure =
    bmp.pressure / 100.0f;

  // Altitude
  if (referencePressure > 0.0f) {

    t.altitude =
      bmp.readAltitude(
        referencePressure
      );
  }
  else {

    t.altitude = 0.0f;
  }

  // ===================================================
  // VELOCITY
  // ===================================================

  uint32_t now = micros();

  if (previousAltitudeValid) {

    float dt =
      (now - previousAltitudeTime)
      / 1000000.0f;

    if (
      dt > 0.001f &&
      dt < 1.0f
    ) {

      t.velocity =
        (t.altitude - previousAltitude)
        / dt;
    }
    else {

      t.velocity = 0.0f;
    }

  }
  else {

    // First valid altitude reading
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

  t.orientationX =
    orientation.orientation.x;

  t.orientationY =
    orientation.orientation.y;

  t.orientationZ =
    orientation.orientation.z;
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
  // Packet number
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
// OPENLOG CSV
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
// API: /api/live
// =====================================================

void handleLiveAPI() {

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
// API: /api/status
// =====================================================

void handleStatusAPI() {

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
// 404
// =====================================================

void handleNotFound() {

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

  delay(1000);

  // ===================================================
  // SERIAL
  // ===================================================

  Serial.begin(
    115200
  );

  delay(500);

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "ESP32-S3 CANSAT TELEMETRY"
  );

  Serial.println(
    "16 Hz SENSOR / API"
  );

  Serial.println(
    "2 Hz OPENLOG"
  );

  Serial.println(
    "================================"
  );

  // ===================================================
  // RGB LED
  // ===================================================

  statusLED.begin();

  statusLED.setBrightness(
    40
  );

  statusLED.clear();

  statusLED.show();

  // ===================================================
  // I2C
  // ===================================================

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  // 400 kHz I2C
  Wire.setClock(
    400000
  );

  Serial.println(
    "I2C = 400 kHz"
  );

  // ===================================================
  // BMP390
  // ===================================================

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

  }
  else {

    bmpAvailable = false;

    Serial.println(
      "BMP390 NOT FOUND"
    );
  }

  // ===================================================
  // BNO055
  // ===================================================

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

  }
  else {

    bnoAvailable = false;

    Serial.println(
      "BNO055 NOT FOUND"
    );
  }

  // ===================================================
  // INA219
  // ===================================================

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

  }
  else {

    inaAvailable = false;

    Serial.println(
      "INA219 NOT FOUND"
    );
  }

  // ===================================================
  // LED STATUS
  // ===================================================

  updateStatusLED();

  // ===================================================
  // BMP CALIBRATION
  // ===================================================

  calibrateReferencePressure();

  // ===================================================
  // OPENLOG
  // ===================================================

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

  // ===================================================
  // INITIAL TELEMETRY
  // ===================================================

  latestTelemetry.packet = 0;

  latestTelemetry.pressure = 0.0f;
  latestTelemetry.altitude = 0.0f;
  latestTelemetry.velocity = 0.0f;

  latestTelemetry.orientationX = 0.0f;
  latestTelemetry.orientationY = 0.0f;
  latestTelemetry.orientationZ = 0.0f;

  latestTelemetry.batteryVoltage = 0.0f;

  // ===================================================
  // WIFI
  // ===================================================

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
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  // ===================================================
  // API ROUTES
  // ===================================================

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

  Serial.println();

  Serial.println(
    "SYSTEM READY"
  );

  // ===================================================
  // START 16 Hz TIMER
  // ===================================================

  nextSensorTime =
    micros();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // ===================================================
  // SERVICE HTTP
  // ===================================================

  server.handleClient();

  // ===================================================
  // 16 Hz SENSOR ACQUISITION
  // ===================================================

  uint32_t now =
    micros();

  if (
    (int32_t)(
      now - nextSensorTime
    ) >= 0
  ) {

    nextSensorTime +=
      SENSOR_INTERVAL_US;

    // -----------------------------------------------
    // Acquire newest packet
    // -----------------------------------------------

    acquireTelemetry();

    // -----------------------------------------------
    // Every 8th packet → OpenLog
    // -----------------------------------------------

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