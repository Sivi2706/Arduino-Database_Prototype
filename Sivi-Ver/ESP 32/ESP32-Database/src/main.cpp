/*
 * ============================================================
 *  Arduino Web Dashboard — ESP32 Firmware
 *  Firebase Realtime Database  (asia-southeast1)
 * ============================================================
 *
 *  PINOUT
 *  ──────────────────────────────────────────────────────────
 *  Component          ESP32 Pin   Notes
 *  ─────────────────  ─────────   ──────────────────────────
 *  Servo signal       GPIO 13     PWM via ESP32Servo lib
 *  Servo VCC          5 V (VIN)   Use external 5 V if needed
 *  Servo GND          GND
 *
 *  IR sensor OUT      GPIO 34     Digital input (HIGH = no obj,
 *                                 LOW  = object detected)
 *                                 GPIO 34 is input-only, good
 *                                 for digital sensors
 *  IR sensor VCC      3.3 V
 *  IR sensor GND      GND
 *
 *  LIBRARIES REQUIRED (install via Arduino Library Manager)
 *  ──────────────────────────────────────────────────────────
 *  - Firebase ESP Client  by Mobizt  (≥ 4.x)
 *  - ESP32Servo            by Kevin Harrington
 *
 *  BOARD: "ESP32 Dev Module" (or your specific board variant)
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>   // Firebase ESP Client by Mobizt
#include <ESP32Servo.h>

// ── Wi-Fi credentials ─────────────────────────────────────
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ── Firebase credentials ──────────────────────────────────
#define FIREBASE_HOST "sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "AIzaSyBlxLdiakI8Zv5gtRWtAcfwxTcW-XtGbTw"  // Web API key

// ── Pin definitions ───────────────────────────────────────
#define SERVO_PIN   13   // Servo signal wire
#define IR_PIN      34   // IR sensor digital output (active LOW)

// ── Firebase intervals ────────────────────────────────────
#define IR_UPLOAD_INTERVAL_MS    500   // Upload IR reading every 500 ms
#define SERVO_POLL_INTERVAL_MS   200   // Poll Firebase for new angle every 200 ms

// ── Globals ───────────────────────────────────────────────
FirebaseData   fbData;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

Servo servo;

int   currentAngle   = 90;
unsigned long lastIrUpload   = 0;
unsigned long lastServoPoll  = 0;

// ─────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
}

// ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Pins
  pinMode(IR_PIN, INPUT);
  servo.attach(SERVO_PIN, 500, 2400);  // 500–2400 µs pulse range
  servo.write(currentAngle);

  connectWiFi();

  // Firebase
  fbConfig.host             = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase connected.");
}

// ─────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── 1. Poll Firebase for servo angle ─────────────────
  if (now - lastServoPoll >= SERVO_POLL_INTERVAL_MS) {
    lastServoPoll = now;

    if (Firebase.getInt(fbData, "/servo/angle")) {
      int newAngle = fbData.intData();
      newAngle = constrain(newAngle, 0, 180);

      if (newAngle != currentAngle) {
        currentAngle = newAngle;
        servo.write(currentAngle);
        Serial.print("Servo → ");
        Serial.print(currentAngle);
        Serial.println("°");
      }
    } else {
      Serial.print("Firebase read error: ");
      Serial.println(fbData.errorReason());
    }
  }

  // ── 2. Read IR sensor & upload to Firebase ───────────
  if (now - lastIrUpload >= IR_UPLOAD_INTERVAL_MS) {
    lastIrUpload = now;

    // Digital IR sensor: LOW = object detected
    int  irRaw      = digitalRead(IR_PIN);
    bool detected   = (irRaw == LOW);

    FirebaseJson irJson;
    irJson.set("raw",       irRaw);
    irJson.set("detected",  detected);
    irJson.set("timestamp", (int)(now));  // millis uptime; swap for NTP if needed

    if (Firebase.setJSON(fbData, "/ir", irJson)) {
      Serial.print("IR uploaded — detected: ");
      Serial.println(detected ? "YES" : "NO");
    } else {
      Serial.print("IR upload error: ");
      Serial.println(fbData.errorReason());
    }
  }
}