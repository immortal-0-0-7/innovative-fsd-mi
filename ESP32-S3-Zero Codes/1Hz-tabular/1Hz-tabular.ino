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

const char* WIFI_SSID = "ESPTest";
const char* WIFI_PASSWORD = "ESPpassword";

// =====================================================
// ESP32-S3 ZERO PINS
// =====================================================

// I2C
#define SDA_PIN 1
#define SCL_PIN 2

// OpenLog UART1
#define OPENLOG_RX 13
#define OPENLOG_TX 5

// Onboard RGB LED
#define LED_PIN 21
#define LED_COUNT 1

// =====================================================
// OBJECTS
// =====================================================

WebServer server(80);

Adafruit_BMP3XX bmp;

Adafruit_BNO055 bno =
  Adafruit_BNO055(55, 0x28, &Wire);

Adafruit_INA219 ina219(0x40);

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
// BMP390 REFERENCE PRESSURE
// =====================================================

float referencePressure = 0.0;

// =====================================================
// TELEMETRY STRUCTURE
// =====================================================

struct Telemetry {

  unsigned long packet;
  unsigned long time;

  // BMP390
  float pressure;
  float temperature;
  float altitude;

  // BNO055
  float accelX;
  float accelY;
  float accelZ;

  float gyroX;
  float gyroY;
  float gyroZ;

  float pitch;
  float roll;
  float yaw;

  // INA219
  float voltage;
  float current;
  float power;
};

// =====================================================
// TELEMETRY HISTORY
// =====================================================

const int MAX_HISTORY = 20;

Telemetry history[MAX_HISTORY];

int telemetryCount = 0;

unsigned long packetNumber = 0;

// =====================================================
// RGB STATUS LED
// =====================================================

void updateStatusLED() {

  uint32_t color;

  // Both sensors missing
  if (!bmpAvailable && !bnoAvailable) {

    // RED
    color = statusLED.Color(
      255,
      0,
      0
    );
  }

  // BNO works, BMP doesn't
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

  // BMP works, BNO doesn't
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
// BMP390 REFERENCE PRESSURE CALIBRATION
// =====================================================

void calibrateReferencePressure() {

  if (!bmpAvailable) {

    referencePressure = 0;

    Serial.println();
    Serial.println(
      "BMP390 unavailable."
    );

    Serial.println(
      "Reference pressure = 0"
    );

    return;
  }

  Serial.println();
  Serial.println(
    "================================="
  );

  Serial.println(
    "BMP390 PRESSURE CALIBRATION"
  );

  Serial.println(
    "Taking 5 readings..."
  );

  Serial.println(
    "================================="
  );

  float pressure = 0;

  for (int i = 0; i < 5; i++) {

    if (bmp.performReading()) {

      pressure =
        bmp.pressure / 100.0;

      Serial.print(
        "Pressure reading "
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
      }

    } else {

      Serial.print(
        "Pressure reading "
      );

      Serial.print(
        i + 1
      );

      Serial.println(
        " FAILED"
      );
    }

    // 5 readings within approximately 1 second
    if (i < 4) {

      delay(200);
    }
  }

  Serial.println();

  Serial.print(
    "REFERENCE PRESSURE: "
  );

  Serial.print(
    referencePressure,
    2
  );

  Serial.println(
    " hPa"
  );

  Serial.println(
    "================================="
  );
}

// =====================================================
// READ TELEMETRY
// =====================================================

Telemetry readTelemetry() {

  Telemetry t;

  // ===================================================
  // DEFAULT EVERYTHING TO ZERO
  // ===================================================

  t.packet = ++packetNumber;

  t.time = millis();

  // BMP390
  t.pressure = 0;
  t.temperature = 0;
  t.altitude = 0;

  // BNO055
  t.accelX = 0;
  t.accelY = 0;
  t.accelZ = 0;

  t.gyroX = 0;
  t.gyroY = 0;
  t.gyroZ = 0;

  t.pitch = 0;
  t.roll = 0;
  t.yaw = 0;

  // INA219
  t.voltage = 0;
  t.current = 0;
  t.power = 0;

  // ===================================================
  // BMP390
  // ===================================================

  if (bmpAvailable) {

    if (bmp.performReading()) {

      // Pressure in hPa
      t.pressure =
        bmp.pressure / 100.0;

      // Temperature in Celsius
      t.temperature =
        bmp.temperature;

      // Altitude using fifth startup reading
      if (referencePressure > 0) {

        t.altitude =
          bmp.readAltitude(
            referencePressure
          );
      }
      else {

        t.altitude = 0;
      }
    }
  }

  // ===================================================
  // BNO055
  // ===================================================

  if (bnoAvailable) {

    sensors_event_t orientationData;

    sensors_event_t linearAccelData;

    sensors_event_t gyroData;

    // Orientation
    bno.getEvent(
      &orientationData,
      Adafruit_BNO055::VECTOR_EULER
    );

    // Linear acceleration
    bno.getEvent(
      &linearAccelData,
      Adafruit_BNO055::VECTOR_LINEARACCEL
    );

    // Gyroscope
    bno.getEvent(
      &gyroData,
      Adafruit_BNO055::VECTOR_GYROSCOPE
    );

    // Euler angles

    t.yaw =
      orientationData.orientation.x;

    t.pitch =
      orientationData.orientation.y;

    t.roll =
      orientationData.orientation.z;

    // Acceleration

    t.accelX =
      linearAccelData.acceleration.x;

    t.accelY =
      linearAccelData.acceleration.y;

    t.accelZ =
      linearAccelData.acceleration.z;

    // Gyroscope

    t.gyroX =
      gyroData.gyro.x;

    t.gyroY =
      gyroData.gyro.y;

    t.gyroZ =
      gyroData.gyro.z;
  }

  // ===================================================
  // INA219
  // ===================================================

  if (inaAvailable) {

    // Bus voltage in Volts
    t.voltage =
      ina219.getBusVoltage_V();

    // Current in mA
    t.current =
      ina219.getCurrent_mA();

    // Power in mW
    t.power =
      ina219.getPower_mW();
  }

  return t;
}

// =====================================================
// ADD TELEMETRY TO HISTORY
// =====================================================

void addTelemetry(
  Telemetry t
) {

  // Shift old packets down

  for (
    int i = MAX_HISTORY - 1;
    i > 0;
    i--
  ) {

    history[i] =
      history[i - 1];
  }

  // Newest packet at top

  history[0] = t;

  if (
    telemetryCount <
    MAX_HISTORY
  ) {

    telemetryCount++;
  }
}

// =====================================================
// SAVE TO OPENLOG
// =====================================================

void saveToOpenLog(
  Telemetry t
) {

  OpenLog.print(t.packet);
  OpenLog.print(",");

  OpenLog.print(t.time);
  OpenLog.print(",");

  OpenLog.print(
    t.pressure,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.temperature,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.altitude,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.accelX,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.accelY,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.accelZ,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.gyroX,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.gyroY,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.gyroZ,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.pitch,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.roll,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.yaw,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.voltage,
    3
  );

  OpenLog.print(",");

  OpenLog.print(
    t.current,
    2
  );

  OpenLog.print(",");

  OpenLog.print(
    t.power,
    2
  );

  OpenLog.println();
}

// =====================================================
// TELEMETRY → JSON
// =====================================================

String telemetryToJSON(
  Telemetry t
) {

  String json = "{";

  json += "\"packet\":";
  json += t.packet;

  json += ",\"time\":";
  json += t.time;

  // BMP

  json += ",\"pressure\":";
  json += String(
    t.pressure,
    2
  );

  json += ",\"temperature\":";
  json += String(
    t.temperature,
    2
  );

  json += ",\"altitude\":";
  json += String(
    t.altitude,
    2
  );

  // BNO

  json += ",\"accelX\":";
  json += String(
    t.accelX,
    3
  );

  json += ",\"accelY\":";
  json += String(
    t.accelY,
    3
  );

  json += ",\"accelZ\":";
  json += String(
    t.accelZ,
    3
  );

  json += ",\"gyroX\":";
  json += String(
    t.gyroX,
    3
  );

  json += ",\"gyroY\":";
  json += String(
    t.gyroY,
    3
  );

  json += ",\"gyroZ\":";
  json += String(
    t.gyroZ,
    3
  );

  json += ",\"pitch\":";
  json += String(
    t.pitch,
    2
  );

  json += ",\"roll\":";
  json += String(
    t.roll,
    2
  );

  json += ",\"yaw\":";
  json += String(
    t.yaw,
    2
  );

  // INA219

  json += ",\"voltage\":";
  json += String(
    t.voltage,
    3
  );

  json += ",\"current\":";
  json += String(
    t.current,
    2
  );

  json += ",\"power\":";
  json += String(
    t.power,
    2
  );

  json += "}";

  return json;
}

// =====================================================
// /api/telemetry
// =====================================================

void handleTelemetryAPI() {

  String json = "[";

  for (
    int i = 0;
    i < telemetryCount;
    i++
  ) {

    if (i > 0) {

      json += ",";
    }

    json +=
      telemetryToJSON(
        history[i]
      );
  }

  json += "]";

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// /api/status
// =====================================================

void handleStatusAPI() {

  String json = "{";

  json += "\"bmp390\":";
  json +=
    bmpAvailable
      ? "true"
      : "false";

  json += ",\"bno055\":";
  json +=
    bnoAvailable
      ? "true"
      : "false";

  json += ",\"ina219\":";
  json +=
    inaAvailable
      ? "true"
      : "false";

  json += ",\"referencePressure\":";

  json += String(
    referencePressure,
    2
  );

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// WEB DASHBOARD
// =====================================================

void handleRoot() {

  String html = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<title>ESP32-S3 Telemetry</title>

<style>

body {

  font-family: Arial;

  margin: 20px;

  background: #f4f4f4;
}

h1 {

  text-align: center;
}

.status {

  text-align: center;

  margin-bottom: 20px;

  font-size: 18px;
}

.container {

  overflow-x: auto;
}

table {

  width: 100%;

  border-collapse: collapse;

  background: white;
}

th, td {

  padding: 10px;

  border: 1px solid #ddd;

  text-align: center;

  white-space: nowrap;
}

th {

  background: #222;

  color: white;
}

tr:first-child {

  background: #dff0ff;

  font-weight: bold;
}

</style>

</head>

<body>

<h1>ESP32-S3 Telemetry</h1>

<div class="status">

BMP390:
<span id="bmp">---</span>

&nbsp;&nbsp;

BNO055:
<span id="bno">---</span>

&nbsp;&nbsp;

INA219:
<span id="ina">---</span>

&nbsp;&nbsp;

Reference Pressure:
<span id="reference">---</span>
hPa

</div>

<div class="container">

<table>

<thead>

<tr>

<th>Packet</th>
<th>Time (ms)</th>

<th>Pressure (hPa)</th>
<th>Temperature (°C)</th>
<th>Altitude (m)</th>

<th>Acc X</th>
<th>Acc Y</th>
<th>Acc Z</th>

<th>Gyro X</th>
<th>Gyro Y</th>
<th>Gyro Z</th>

<th>Pitch</th>
<th>Roll</th>
<th>Yaw</th>

<th>Voltage (V)</th>
<th>Current (mA)</th>
<th>Power (mW)</th>

</tr>

</thead>

<tbody id="telemetry">

</tbody>

</table>

</div>

<script>

// =====================================================
// UPDATE TELEMETRY
// =====================================================

async function updateTelemetry() {

  try {

    const response =
      await fetch(
        '/api/telemetry'
      );

    const data =
      await response.json();

    let table = "";

    data.forEach(
      (t) => {

        table += "<tr>";

        table +=
          "<td>" +
          t.packet +
          "</td>";

        table +=
          "<td>" +
          t.time +
          "</td>";

        table +=
          "<td>" +
          t.pressure.toFixed(2) +
          "</td>";

        table +=
          "<td>" +
          t.temperature.toFixed(2) +
          "</td>";

        table +=
          "<td>" +
          t.altitude.toFixed(2) +
          "</td>";

        table +=
          "<td>" +
          t.accelX.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.accelY.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.accelZ.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.gyroX.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.gyroY.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.gyroZ.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.pitch.toFixed(2) +
          "</td>";

        table +=
          "<td>" +
          t.roll.toFixed(2) +
          "</td>";

        table +=
          "<td>" +
          t.yaw.toFixed(2) +
          "</td>";

        // INA219

        table +=
          "<td>" +
          t.voltage.toFixed(3) +
          "</td>";

        table +=
          "<td>" +
          t.current.toFixed(2) +
          "</td>";

        table +=
          "<td>" +
          t.power.toFixed(2) +
          "</td>";

        table += "</tr>";
      }
    );

    document.getElementById(
      "telemetry"
    ).innerHTML = table;

  }

  catch(error) {

    console.log(error);

  }
}

// =====================================================
// UPDATE STATUS
// =====================================================

async function updateStatus() {

  try {

    const response =
      await fetch(
        '/api/status'
      );

    const data =
      await response.json();

    document.getElementById(
      "bmp"
    ).innerText =
      data.bmp390
        ? "OK"
        : "MISSING";

    document.getElementById(
      "bno"
    ).innerText =
      data.bno055
        ? "OK"
        : "MISSING";

    document.getElementById(
      "ina"
    ).innerText =
      data.ina219
        ? "OK"
        : "MISSING";

    document.getElementById(
      "reference"
    ).innerText =
      Number(
        data.referencePressure
      ).toFixed(2);

  }

  catch(error) {

    console.log(error);

  }
}

// =====================================================
// INITIAL UPDATE
// =====================================================

updateTelemetry();

updateStatus();

// =====================================================
// PERIODIC UPDATE
// =====================================================

setInterval(
  updateTelemetry,
  1000
);

setInterval(
  updateStatus,
  2000
);

</script>

</body>

</html>

)rawliteral";

  server.send(
    200,
    "text/html",
    html
  );
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  delay(2000);

  // ===================================================
  // SERIAL
  // ===================================================

  Serial.begin(115200);

  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "ESP32-S3 CANSAT TELEMETRY SYSTEM"
  );

  Serial.println(
    "======================================"
  );

  // ===================================================
  // RGB LED
  // ===================================================

  statusLED.begin();

  statusLED.setBrightness(40);

  statusLED.clear();

  statusLED.show();

  // ===================================================
  // I2C
  // ===================================================

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Serial.println(
    "I2C initialized."
  );

  // ===================================================
  // BMP390
  // ===================================================

  Serial.println();

  Serial.println(
    "Checking BMP390..."
  );

  if (
    bmp.begin_I2C(0x77)
  ) {

    bmpAvailable = true;

    Serial.println(
      "BMP390 detected."
    );

    bmp.setTemperatureOversampling(
      BMP3_OVERSAMPLING_8X
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
      "BMP390 NOT detected."
    );

    Serial.println(
      "BMP values will remain 0."
    );
  }

  // ===================================================
  // BNO055
  // ===================================================

  Serial.println();

  Serial.println(
    "Checking BNO055..."
  );

  if (
    bno.begin()
  ) {

    bnoAvailable = true;

    Serial.println(
      "BNO055 detected."
    );

    delay(1000);

    bno.setExtCrystalUse(true);
  }

  else {

    bnoAvailable = false;

    Serial.println(
      "BNO055 NOT detected."
    );

    Serial.println(
      "BNO055 values will remain 0."
    );
  }

  // ===================================================
  // INA219
  // ===================================================

  Serial.println();

  Serial.println(
    "Checking INA219..."
  );

  if (
    ina219.begin()
  ) {

    inaAvailable = true;

    Serial.println(
      "INA219 detected."
    );

    // Default INA219 calibration.
    // Suitable for normal bus-voltage/current
    // measurements.

    ina219.setCalibration_32V_2A();

  }

  else {

    inaAvailable = false;

    Serial.println(
      "INA219 NOT detected."
    );

    Serial.println(
      "Voltage/current/power will remain 0."
    );
  }

  // ===================================================
  // RGB STATUS
  // ===================================================

  updateStatusLED();

  // ===================================================
  // BMP REFERENCE PRESSURE
  // ===================================================

  calibrateReferencePressure();

  // ===================================================
  // OPENLOG
  // ===================================================

  Serial.println();

  Serial.println(
    "Starting OpenLog..."
  );

  OpenLog.begin(
    9600,
    SERIAL_8N1,
    OPENLOG_RX,
    OPENLOG_TX
  );

  delay(1000);

  OpenLog.println(
    "packet,time,pressure,temperature,"
    "altitude,accelX,accelY,accelZ,"
    "gyroX,gyroY,gyroZ,"
    "pitch,roll,yaw,"
    "voltage,current,power"
  );

  Serial.println(
    "OpenLog UART initialized."
  );

  // ===================================================
  // WIFI
  // ===================================================

  Serial.println();

  Serial.println(
    "Connecting to Wi-Fi..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print(
    "Connecting"
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println(
    "Wi-Fi connected!"
  );

  Serial.print(
    "ESP32 IP Address: "
  );

  Serial.println(
    WiFi.localIP()
  );

  // ===================================================
  // HTTP ROUTES
  // ===================================================

  server.on(
    "/",
    handleRoot
  );

  server.on(
    "/api/telemetry",
    handleTelemetryAPI
  );

  server.on(
    "/api/status",
    handleStatusAPI
  );

  server.begin();

  Serial.println();

  Serial.println(
    "HTTP server started!"
  );

  Serial.print(
    "Open: http://"
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "SYSTEM READY"
  );

  Serial.println(
    "======================================"
  );
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // Handle HTTP requests

  server.handleClient();

  // ===================================================
  // TELEMETRY EVERY 1 SECOND
  // ===================================================

  static unsigned long lastTelemetry = 0;

  if (
    millis() - lastTelemetry >= 1000
  ) {

    lastTelemetry = millis();

    // Read all sensors

    Telemetry t =
      readTelemetry();

    // Newest packet goes to top

    addTelemetry(t);

    // Save to SD through OpenLog

    saveToOpenLog(t);

    // =================================================
    // SERIAL DEBUG
    // =================================================

    Serial.print("Packet ");
    Serial.print(t.packet);

    Serial.print(" | Pressure: ");
    Serial.print(t.pressure, 2);
    Serial.print(" hPa");

    Serial.print(" | Altitude: ");
    Serial.print(t.altitude, 2);
    Serial.print(" m");

    Serial.print(" | Temp: ");
    Serial.print(t.temperature, 2);
    Serial.print(" C");

    Serial.print(" | Roll: ");
    Serial.print(t.roll, 2);

    Serial.print(" | Pitch: ");
    Serial.print(t.pitch, 2);

    Serial.print(" | Yaw: ");
    Serial.print(t.yaw, 2);

    Serial.print(" | Voltage: ");
    Serial.print(t.voltage, 3);
    Serial.print(" V");

    Serial.print(" | Current: ");
    Serial.print(t.current, 2);
    Serial.print(" mA");

    Serial.print(" | Power: ");
    Serial.print(t.power, 2);
    Serial.println(" mW");
  }
}