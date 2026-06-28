
/* // this is ai generated code i generated using chat gpt,im not trying to claim this as my code
 * ARNA_Robot.ino
 * Single-file firmware for ESP32 NodeMCU robot
 *
 * Hardware:
 *  - ESP32 NodeMCU (WROOM-32)
 *  - L298N motor driver
 *  - 2x N20 motors with encoders
 *  - VL53L0X ToF sensor
 *  - ADXL345 accelerometer
 *  - ESP32-CAM streams directly to PC
 *
 * Libraries:
 *  - WiFi
 *  - PubSubClient
 *  - ArduinoJson
 *  - Adafruit_VL53L0X
 *  - Adafruit_ADXL345_U
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_ADXL345_U.h>

/******************** USER SETTINGS ********************/
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASSWORD";

const char* MQTT_SERVER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "ARNA_NODE_01";

const char* TOPIC_CMD   = "arna/cmd_vel";
const char* TOPIC_TEL   = "arna/telemetry";
const char* TOPIC_STAT  = "arna/status";

/******************** PIN DEFINITIONS ********************/
#define LEFT_EN   25
#define LEFT_IN1  26
#define LEFT_IN2  27

#define RIGHT_EN  14
#define RIGHT_IN3 12
#define RIGHT_IN4 13

#define LEFT_ENC_A  34
#define LEFT_ENC_B  35
#define RIGHT_ENC_A 32
#define RIGHT_ENC_B 33

#define SDA_PIN 21
#define SCL_PIN 22

#define STATUS_LED 2
#define BATTERY_PIN 36

/******************** ROBOT PARAMETERS ********************/
const float WHEEL_DIAMETER = 0.065f;      // meters
const float WHEEL_BASE     = 0.145f;      // meters
const int   TICKS_PER_REV  = 620;

const float MAX_LINEAR  = 0.45f;          // m/s
const float MAX_ANGULAR = 2.0f;           // rad/s

/******************** GLOBALS ********************/
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

Adafruit_VL53L0X tof = Adafruit_VL53L0X();
Adafruit_ADXL345_Unified adxl(12345);

volatile long leftTicks = 0;
volatile long rightTicks = 0;

float leftRPM = 0;
float rightRPM = 0;

float targetLeftRPM = 0;
float targetRightRPM = 0;

float cmdLinear = 0;
float cmdAngular = 0;

unsigned long lastCmdTime = 0;
unsigned long lastRPMTime = 0;
unsigned long lastTelemetry = 0;

/******************** PID ********************/
struct PID {
  float kp, ki, kd;
  float integral;
  float prevError;
};

PID leftPID  = {1.2f, 0.08f, 0.01f, 0, 0};
PID rightPID = {1.2f, 0.08f, 0.01f, 0, 0};

/******************** ENCODER ISRs ********************/
void IRAM_ATTR leftISR() {
  if (digitalRead(LEFT_ENC_B)) leftTicks++;
  else leftTicks--;
}

void IRAM_ATTR rightISR() {
  if (digitalRead(RIGHT_ENC_B)) rightTicks++;
  else rightTicks--;
}

/******************** MOTOR CONTROL ********************/
void setLeftMotor(int pwm) {
  pwm = constrain(pwm, -255, 255);

  if (pwm > 0) {
    digitalWrite(LEFT_IN1, HIGH);
    digitalWrite(LEFT_IN2, LOW);
    ledcWrite(0, pwm);
  } else if (pwm < 0) {
    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, HIGH);
    ledcWrite(0, -pwm);
  } else {
    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, LOW);
    ledcWrite(0, 0);
  }
}

void setRightMotor(int pwm) {
  pwm = constrain(pwm, -255, 255);

  if (pwm > 0) {
    digitalWrite(RIGHT_IN3, HIGH);
    digitalWrite(RIGHT_IN4, LOW);
    ledcWrite(1, pwm);
  } else if (pwm < 0) {
    digitalWrite(RIGHT_IN3, LOW);
    digitalWrite(RIGHT_IN4, HIGH);
    ledcWrite(1, -pwm);
  } else {
    digitalWrite(RIGHT_IN3, LOW);
    digitalWrite(RIGHT_IN4, LOW);
    ledcWrite(1, 0);
  }
}

void stopMotors() {
  setLeftMotor(0);
  setRightMotor(0);
}

/******************** PID COMPUTE ********************/
float pidCompute(PID &pid, float target, float measured, float dt) {
  float error = target - measured;

  pid.integral += error * dt;
  pid.integral = constrain(pid.integral, -200, 200);

  float derivative = (error - pid.prevError) / dt;
  pid.prevError = error;

  float output =
      pid.kp * error +
      pid.ki * pid.integral +
      pid.kd * derivative;

  return constrain(output, -255, 255);
}

/******************** MQTT ********************/
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[128];
  if (length >= sizeof(msg)) return;

  memcpy(msg, payload, length);
  msg[length] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, msg)) return;

  cmdLinear  = doc["linear"]  | 0.0f;
  cmdAngular = doc["angular"] | 0.0f;

  lastCmdTime = millis();
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting...");

    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println("OK");
      mqtt.subscribe(TOPIC_CMD);
      mqtt.publish(TOPIC_STAT, "ONLINE");
    } else {
      Serial.print("Failed: ");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

/******************** WIFI ********************/
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

/******************** RPM UPDATE ********************/
void updateRPM() {
  unsigned long now = millis();
  float dt = (now - lastRPMTime) / 60000.0f; // minutes

  if (dt <= 0) return;

  static long prevLeft = 0;
  static long prevRight = 0;

  long l = leftTicks;
  long r = rightTicks;

  leftRPM  = ((l - prevLeft)  / (float)TICKS_PER_REV) / dt;
  rightRPM = ((r - prevRight) / (float)TICKS_PER_REV) / dt;

  prevLeft = l;
  prevRight = r;
  lastRPMTime = now;
}

/******************** VELOCITY TO RPM ********************/
void computeTargets() {
  // Safety timeout
  if (millis() - lastCmdTime > 1000) {
    cmdLinear = 0;
    cmdAngular = 0;
  }

  float vLeft  = cmdLinear - (cmdAngular * WHEEL_BASE * 0.5f);
  float vRight = cmdLinear + (cmdAngular * WHEEL_BASE * 0.5f);

  float wheelCirc = PI * WHEEL_DIAMETER;

  targetLeftRPM  = (vLeft  / wheelCirc) * 60.0f;
  targetRightRPM = (vRight / wheelCirc) * 60.0f;
}

/******************** TELEMETRY ********************/
void publishTelemetry() {
  if (millis() - lastTelemetry < 50) return;
  lastTelemetry = millis();

  // ToF
  int tofMM = -1;
  VL53L0X_RangingMeasurementData_t measure;
  tof.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    tofMM = measure.RangeMilliMeter;
  }

  // ADXL345
  sensors_event_t event;
  adxl.getEvent(&event);

  // Battery (voltage divider required)
  int raw = analogRead(BATTERY_PIN);
  float battery = (raw / 4095.0f) * 3.3f * 2.0f; // assumes 1:1 divider

  JsonDocument doc;
  doc["leftTicks"]  = leftTicks;
  doc["rightTicks"] = rightTicks;
  doc["leftRPM"]    = leftRPM;
  doc["rightRPM"]   = rightRPM;
  doc["tof"]        = tofMM;
  doc["ax"]         = event.acceleration.x;
  doc["ay"]         = event.acceleration.y;
  doc["az"]         = event.acceleration.z;
  doc["battery"]    = battery;
  doc["wifi"]       = WiFi.RSSI();

  char buffer[256];
  size_t n = serializeJson(doc, buffer);

  mqtt.publish(TOPIC_TEL, buffer, n);
}

/******************** SETUP ********************/
void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);

  // Motor pins
  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN3, OUTPUT);
  pinMode(RIGHT_IN4, OUTPUT);

  // PWM
  ledcSetup(0, 20000, 8);
  ledcAttachPin(LEFT_EN, 0);

  ledcSetup(1, 20000, 8);
  ledcAttachPin(RIGHT_EN, 1);

  stopMotors();

  // Encoders
  pinMode(LEFT_ENC_A, INPUT_PULLUP);
  pinMode(LEFT_ENC_B, INPUT_PULLUP);
  pinMode(RIGHT_ENC_A, INPUT_PULLUP);
  pinMode(RIGHT_ENC_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(LEFT_ENC_A), leftISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RIGHT_ENC_A), rightISR, CHANGE);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Sensors
  if (!tof.begin()) {
    Serial.println("VL53L0X not found");
  }

  if (!adxl.begin()) {
    Serial.println("ADXL345 not found");
  }

  // Network
  connectWiFi();

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  lastRPMTime = millis();
  lastCmdTime = millis();

  Serial.println("ARNA Robot Ready");
}

/******************** LOOP ********************/
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqtt.connected()) {
    connectMQTT();
  }

  mqtt.loop();

  // 20ms control loop
  static unsigned long lastControl = 0;
  if (millis() - lastControl >= 20) {
    lastControl = millis();

    updateRPM();
    computeTargets();

    float dt = 0.02f;

    int leftPWM  = pidCompute(leftPID,  targetLeftRPM,  leftRPM,  dt);
    int rightPWM = pidCompute(rightPID, targetRightRPM, rightRPM, dt);

    setLeftMotor(leftPWM);
    setRightMotor(rightPWM);
  }

  publishTelemetry();

  // Status LED
  digitalWrite(STATUS_LED, mqtt.connected());
}
