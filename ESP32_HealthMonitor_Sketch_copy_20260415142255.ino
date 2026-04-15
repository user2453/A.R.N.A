#include <WiFiManager.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PulseSensorPlayground.h>
#include <SPIFFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <math.h>

// OLED DISPLAY
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin (not used)

// Define OLED I2C pins explicitly
#define OLED_SDA 21
#define OLED_SCL 22
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins
#define ONE_WIRE_BUS 4
#define PULSE_PIN 34
#define TRIG_PIN 12
#define ECHO_PIN 14
#define PROXIMITY_PIN 35  // Analog input for horizontal proximity sensor

// Sensor angle in degrees and mount height in cm
#define SENSOR_ANGLE_DEG 60.0
#define MOUNT_HEIGHT_CM 150.0  // Adjust as per your mounting height

// Forward declarations of web handler functions
void handleRoot();
void handleData();

// Globals
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);
PulseSensorPlayground pulseSensor;

float temperature = -127.0; // default error value
int pulseRaw = 0;
int bpm = 0;
float height = 0.0;
float proximityDistance = 0.0;
bool systemActive = true; // Always on (no proximity sensor gating)

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize I2C with defined pins
  Wire.begin(OLED_SDA, OLED_SCL);

  // OLED INIT
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Default I2C address
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Halt here if OLED init fails
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("ESP32 Health Monitor");
  display.display();
  delay(1000);

  // Initialize DS18B20 sensor and verify detection
  Serial.println("🔎 Initializing DS18B20 sensor...");
  sensors.begin();

  int sensorCount = sensors.getDeviceCount();
  Serial.print("✅ DS18B20 sensor count: ");
  Serial.println(sensorCount);

  if (sensorCount == 0) {
    Serial.println("❌ No DS18B20 sensors detected! Check wiring and pull-up resistor (4.7kΩ on data line).");
  }

  SPIFFS.begin(true);

  // Setup pulse sensor
  pulseSensor.analogInput(PULSE_PIN);
  pulseSensor.setThreshold(550);
  pulseSensor.begin();

  // WiFi manager
  WiFiManager wm;
  if (!wm.autoConnect("ESP32-SETUP", "12345678")) {
    Serial.println("WiFi failed, restarting...");
    ESP.restart();
  }

  Serial.println("Connected: " + WiFi.localIP().toString());

  // Web routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  if (systemActive) {
    sensors.requestTemperatures();
    float tempRead = sensors.getTempCByIndex(0);

    if (tempRead == -127) {
      // Error reading sensor
      Serial.println("❗ DS18B20 read error -127°C detected.");
    } else {
      temperature = tempRead;
      Serial.print("🌡️ Temperature reading: ");
      Serial.println(temperature);
    }

    pulseRaw = analogRead(PULSE_PIN);
    bpm = pulseSensor.getBeatsPerMinute();

    proximityDistance = analogRead(PROXIMITY_PIN); // Raw ADC value
    const int PROXIMITY_THRESHOLD_RAW = 1000; // Adjust threshold properly after calibration

    if (proximityDistance < PROXIMITY_THRESHOLD_RAW) {
      height = getHeightFromAngledUltrasonic();
    } else {
      height = -1; // No object close enough horizontally
    }

    logData();
    updateOLED();
  }

  server.handleClient();
}

float getHeightFromAngledUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30 ms timeout
  if (duration == 0)
    return -1;

  float distance = duration * 0.0343 / 2.0; // distance in cm

  float angleRad = SENSOR_ANGLE_DEG * PI / 180.0;
  float vertical_drop = distance * sin(angleRad);

  float calcHeight = MOUNT_HEIGHT_CM - vertical_drop;
  if (calcHeight < 0)
    calcHeight = 0; // Clamp to zero minimum

  return calcHeight;
}

void logData() {
  File logFile = SPIFFS.open("/log.csv", FILE_APPEND);
  if (logFile) {
    logFile.printf("%lu,%.2f,%d,%d,%.2f,%.0f\n", millis(), temperature, pulseRaw, bpm, height, proximityDistance);
    logFile.close();
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("ESP32 Health Monitor");
  display.print("Temp: ");
  if (temperature == -127)
    display.println("--");
  else
    display.println(String(temperature, 1) + " C");

  display.print("BPM:  ");
  display.println(bpm);

  display.print("Raw:  ");
  display.println(pulseRaw);

  display.print("Height:");
  if (height >= 0)
    display.println(String(height, 1) + " cm");
  else
    display.println("--");

  display.print("Prox: ");
  display.println(proximityDistance);

  display.display();
}

// Web server root handler
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8" />
      <title>ESP32 Health Monitor</title>
      <meta name="viewport" content="width=device-width, initial-scale=1" />
      <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
      <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
        h1 { text-align: center; }
        .container { display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; }
        .values { flex: 1 1 300px; background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        .label { font-size: 1.2em; margin: 15px 0; }
        .graphs { flex: 2 1 600px; background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        canvas { width: 100% !important; height: auto !important; }
      </style>
    </head>
    <body>
      <h1>📟 ESP32 Health Monitor</h1>
      <div class="container">
        <div class="values">
          <div class="label">🌡️ Temperature: <span id="temp">--</span> °C</div>
          <div class="label">❤️ BPM: <span id="bpm">--</span></div>
          <div class="label">🫀 Raw Pulse: <span id="pulseRaw">--</span></div>
          <div class="label">📏 Height: <span id="heightVal">--</span> cm</div>
          <div class="label">📡 Proximity: <span id="proximityVal">--</span></div>
        </div>
        <div class="graphs">
          <canvas id="myChart"></canvas>
        </div>
      </div>
      <script>
        let labels = [], tempData = [], bpmData = [];
        const ctx = document.getElementById('myChart').getContext('2d');
        const chart = new Chart(ctx, {
          type: 'line',
          data: {
            labels: labels,
            datasets: [
              { label: 'Temperature (°C)', data: tempData, borderColor: 'red', borderWidth: 2, fill: false },
              { label: 'BPM', data: bpmData, borderColor: 'blue', borderWidth: 2, fill: false }
            ]
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: { x: { display: false } }
          }
        });
        function updateData() {
          fetch('/data').then(res => res.json()).then(data => {
            document.getElementById('temp').innerText = data.temperature === -127 ? '--' : data.temperature.toFixed(1);
            document.getElementById('bpm').innerText = data.bpm;
            document.getElementById('pulseRaw').innerText = data.pulseRaw;
            document.getElementById('heightVal').innerText = data.height >= 0 ? data.height.toFixed(1) : '--';
            document.getElementById('proximityVal').innerText = data.proximity;
            const time = new Date().toLocaleTimeString();
            if (labels.length > 20) {
              labels.shift(); tempData.shift(); bpmData.shift();
            }
            labels.push(time);
            tempData.push(data.temperature);
            bpmData.push(data.bpm);
            chart.update();
          });
        }
        setInterval(updateData, 2000);
        window.onload = updateData;
      </script>
    </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Web server JSON data handler
void handleData() {
  String json = "{";
  json += "\"temperature\":" + String(temperature, 2) + ",";
  json += "\"pulseRaw\":" + String(pulseRaw) + ",";
  json += "\"bpm\":" + String(bpm) + ",";
  json += "\"height\":" + String(height, 2) + ",";
  json += "\"proximity\":" + String(proximityDistance);
  json += "}";
  server.send(200, "application/json", json);
}
