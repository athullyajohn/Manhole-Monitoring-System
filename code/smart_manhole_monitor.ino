#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// 🔐 Replace with your Wi-Fi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// 🔐 Replace with your Telegram Bot Token and Chat ID
const char* botToken = "YOUR_BOT_TOKEN";
const char* chatID = "YOUR_CHAT_ID";

// Tilt & RFID
#define TILT_SENSOR_PIN 36  
#define SERVO_PIN 13        
#define SS_PIN 5            
#define RST_PIN 22          

// Ultrasonic Sensor
#define TRIG_PIN 26
#define ECHO_PIN 25
#define MANHOLE_DEPTH 50  

// Flow Sensor
#define FLOW_SENSOR_PIN 27
#define FLOW_RATE_THRESHOLD 0.5 
volatile int flowPulseCount = 0;
float flowRate;

// Gas Sensor
#define GAS_SENSOR_PIN 34   
#define GAS_THRESHOLD 2500   

Servo lidServo;
MFRC522 mfrc522(SS_PIN, RST_PIN);
bool lidClosed = false;
const char* authorizedUID = "6391CB30";  

// GPS Module
TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // RX = 16, TX = 17

void IRAM_ATTR countFlowPulses() {
    flowPulseCount++;
}

void sendTelegramAlert(String message) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(botToken) +
                 "/sendMessage?chat_id=" + String(chatID) +
                 "&text=" + message;
    http.begin(url);
    http.GET();
    http.end();
}

void sendGPSLocation() {
    if (gps.location.isValid()) {
        float lat = gps.location.lat();
        float lng = gps.location.lng();
        String locationMsg = "📍 GPS Location: " + String(lat, 6) + ", " + String(lng, 6);
        sendTelegramAlert(locationMsg);
    }
}

void setup() {
    Serial.begin(115200);

    // WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    sendTelegramAlert("✅ System Ready!");

    // GPS
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17

    // Sensors
    pinMode(TILT_SENSOR_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    pinMode(GAS_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countFlowPulses, RISING);

    lidServo.attach(SERVO_PIN);
    lidServo.write(0);
    SPI.begin();
    mfrc522.PCD_Init();
}

void loop() {
    while (SerialGPS.available()) {
        gps.encode(SerialGPS.read());
    }

    checkTiltSensor();
    checkWaterLevel();
    checkFlowRate();
    checkGasLevel();

    delay(1000);
}

void checkTiltSensor() {
    int tiltValue = analogRead(TILT_SENSOR_PIN);
    
    if (tiltValue < 1000 && !lidClosed) {
        lidServo.write(90);
        lidClosed = true;
        sendTelegramAlert("🚨 Tilt detected! Closing secondary lid...");
        sendGPSLocation();
    } 
    else if (lidClosed) {
        String scannedUID = readRFID();
        if (scannedUID == authorizedUID) {
            lidServo.write(0);
            lidClosed = false;
            sendTelegramAlert("✅ Authorized RFID scanned! Opening lid...");
            sendGPSLocation();
        }
    }
}

void checkWaterLevel() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    float distance = duration * 0.034 / 2;
    if (distance <= 0 || distance > MANHOLE_DEPTH) distance = MANHOLE_DEPTH;

    float waterLevel = MANHOLE_DEPTH - distance;

    if (distance >= 40) {
        Serial.println("💧 Manhole is EMPTY.");
    } 
    else if (distance > 20 && distance < 40) {
        sendTelegramAlert("⚠ Manhole is HALF-FILLED.");
        sendGPSLocation();
    } 
    else {
        sendTelegramAlert("🚨 Manhole is FULLY FILLED! Overflow Warning!");
        sendGPSLocation();
    }
}

void checkFlowRate() {
    static unsigned long lastTime = 0;
    unsigned long currentTime = millis();
    if (currentTime - lastTime >= 1000) {
        flowRate = (flowPulseCount / 7.5);
        if (flowRate >= FLOW_RATE_THRESHOLD) {
            String msg = "💧 Flow Rate: " + String(flowRate) + " L/min";
            sendTelegramAlert(msg);
            sendGPSLocation();
        }
        flowPulseCount = 0;
        lastTime = currentTime;
    }
}

void checkGasLevel() {
    int gasValue = analogRead(GAS_SENSOR_PIN);
    if (gasValue > GAS_THRESHOLD) {
        sendTelegramAlert("🚨 Toxic gases detected! Unsafe for workers!");
        sendGPSLocation();
    }
}

String readRFID() {
    if (!mfrc522.PICC_IsNewCardPresent()) return "";
    if (!mfrc522.PICC_ReadCardSerial()) return "";

    String uid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        uid += String(mfrc522.uid.uidByte[i], HEX);
    }

    uid.toUpperCase();
    mfrc522.PICC_HaltA();  
    return uid;
}
