#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PulseSensorPlayground.h>
#include "bp.h"
#include "patient.h"
#include "sheets.h"
#include "portal.h"

// ===========================
// PIN DEFINITIONS
// ===========================

// RFID RC522
#define RFID_SS 5
#define RFID_RST 14

// LCD
#define LCD_SDA 21
#define LCD_SCL 22

// DS18B20
#define TEMP_PIN 4

// Pulse Sensor
#define PULSE_PIN 34

// BioAmp EXG
#define EEG_PIN 35

// Joystick
#define JOY_X 32
#define JOY_Y 33
#define JOY_SW 25

// ===========================
// WIFI
// ===========================

const char* WIFI_SSID = "abc123";
const char* WIFI_PASS = "12345678";

// ===========================
// OBJECTS
// ===========================

LiquidCrystal_I2C lcd(0x27, 20, 4);

MFRC522 mfrc522(RFID_SS, RFID_RST);

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

PulseSensorPlayground pulseSensor;

// ===========================
// SENSOR VARIABLES
// ===========================

float temperature = 0;
int bpm = 0;
int eegValue = 0;


float spo2 = 98;

// ===========================
// TIMERS
// ===========================

unsigned long lastVitalsUpload = 0;
unsigned long lastScreenUpdate = 0;

// ===========================
// FUNCTIONS
// ===========================

void connectWiFi()
{
    lcd.clear();
    lcd.print("Connecting WiFi");

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("WiFi Connected");
}

void readTemperature()
{
    tempSensor.requestTemperatures();
    temperature =
        tempSensor.getTempCByIndex(0);
}

void readPulse()
{
    if (pulseSensor.sawStartOfBeat())
    {
        bpm =
            pulseSensor.getBeatsPerMinute();
    }
}

void readEEG()
{
    eegValue =
        analogRead(EEG_PIN);
}

void updateLCD()
{
    lcd.clear();

    lcd.setCursor(0,0);
    lcd.print("HR:");
    lcd.print(bpm);

    lcd.setCursor(10,0);
    lcd.print("T:");
    lcd.print(temperature,1);

    lcd.setCursor(0,1);
    lcd.print("EEG:");
    lcd.print(eegValue);

    lcd.setCursor(0,2);

    if(currentPatient.loaded)
    {
        lcd.print(currentPatient.name);
    }
    else
    {
        lcd.print("No Patient");
    }

    lcd.setCursor(0,3);

    if(WiFi.status()==WL_CONNECTED)
    {
        lcd.print("Cloud OK");
    }
    else
    {
        lcd.print("WiFi Lost");
    }
}

String readRFID()
{
    if(!mfrc522.PICC_IsNewCardPresent())
    {
        return "";
    }

    if(!mfrc522.PICC_ReadCardSerial())
    {
        return "";
    }

    String uid = "";

    for(byte i=0;i<mfrc522.uid.size;i++)
    {
        if(mfrc522.uid.uidByte[i] < 0x10)
        {
            uid += "0";
        }

        uid += String(
            mfrc522.uid.uidByte[i],
            HEX
        );
    }

    uid.toUpperCase();

    mfrc522.PICC_HaltA();

    return uid;
}

void processRFID()
{
    String uid =
        readRFID();

    if(uid == "")
    {
        return;
    }

    Serial.println("Card Detected");
    Serial.println(uid);

    lcd.clear();
    lcd.print("Checking Card");

    if(lookupPatient(uid))
    {
        Serial.println("Patient Found");

        lcd.clear();
        lcd.print("Welcome");

        lcd.setCursor(0,1);
        lcd.print(currentPatient.name);

        delay(2000);
    }
    else
    {
        Serial.println("Unknown Card");

        lcd.clear();
        lcd.print("New Patient");

        delay(1000);

        startRegistrationPortal(uid);
    }
}

// ===========================
// SETUP
// ===========================

void setup()
{
    Serial.begin(115200);

    Wire.begin(
        LCD_SDA,
        LCD_SCL
    );

    lcd.init();
    lcd.backlight();

    connectWiFi();

    SPI.begin(
        18,
        19,
        23,
        RFID_SS
    );

    mfrc522.PCD_Init();

    tempSensor.begin();

    pulseSensor.analogInput(PULSE_PIN);
    pulseSensor.setThreshold(550);
    pulseSensor.begin();

    lcd.clear();
    lcd.print("A.R.N.A Ready");

    delay(2000);
    initBP();
}

// ===========================
// LOOP
// ===========================

void loop()
{
    handlePortal();

    processRFID();

    readTemperature();
    readPulse();
    readEEG();
    updateBP();

    if(millis() - lastScreenUpdate > 1000)
    {
        updateLCD();

        lastScreenUpdate =
            millis();
    }

    if(currentPatient.loaded)
    {
        if(millis() - lastVitalsUpload > 30000)
        {
            uploadVitals(
                currentPatient.uid,
                currentPatient.name,

                bpm,
                temperature,
                eegValue,

                systolicBP,
                diastolicBP,
                spo2
            );

            lastVitalsUpload =
                millis();
        }
    }
}