#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ESP32Servo.h>

// =========================
// CREDENTIALS
// =========================
#define WIFI_SSID             "Xiaomi"
#define WIFI_PASSWORD         "G2706pls"
#define FIREBASE_DATABASE_URL "https://sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app"
#define API_KEY               "AIzaSyBlxLdiakI8Zv5gtRWtAcfwxTcW-XtGbTw"

// =========================
// FIREBASE OBJECTS
// ONE object per role — never share between stream and read/write
// =========================
FirebaseAuth     auth;
FirebaseConfig   config;
FirebaseData     fbdoStream;  // STREAM ONLY  — never used for get/set
FirebaseData     fbdoWrite;   // WRITE ONLY   — IR upload, send message
FirebaseData     fbdoRead;    // READ ONLY    — read web message

// =========================
// HARDWARE
// =========================
#define SERVO_PIN  13
#define IR_PIN     14

Servo servo;
int lastIRStatus = -1;

// =========================
// TIMERS
// =========================
unsigned long lastIRCheck  = 0;
unsigned long lastMsgCheck = 0;
#define IR_POLL_MS   50    // IR sensor: every 50ms
#define MSG_POLL_MS  500   // Web message poll: every 500ms

// =========================
// STREAM CALLBACK
// Fires instantly when /servo/msgTitle changes — no polling needed
// =========================
void streamCallback(FirebaseStream data) {
  if (data.dataType() == "int") {
    int angle = data.intData();
    static int lastAngle = -1;
    if (angle != lastAngle) {
      lastAngle = angle;
      servo.write(angle);
      Serial.print("Servo -> ");
      Serial.println(angle);
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout — reconnecting...");
    // The library auto-resumes; no manual action needed
  }
}

// =========================
// WIFI
// =========================
void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nConnected!");

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized.");
}

// =========================
// WRITE: IR sensor → Firebase
// Uses fbdoWrite — never touches the stream object
// =========================
void uploadIR(int status) {
  if (Firebase.RTDB.setInt(&fbdoWrite, "/sensor/obstacle", status)) {
    Serial.print("IR Uploaded: ");
    Serial.println(status);
  } else {
    Serial.print("IR Upload failed: ");
    Serial.println(fbdoWrite.errorReason());
  }
}

// =========================
// WRITE: Serial Monitor → Firebase → Website
// Uses fbdoWrite — never touches the stream object
// =========================
void sendSerialMessage(const String& text) {
  FirebaseJson msg;
  msg.set("text",   text);
  msg.set("source", "esp32");

  if (Firebase.RTDB.setJSON(&fbdoWrite, "/serial/message", &msg)) {
    Serial.print("[Sent to website] ");
    Serial.println(text);
  } else {
    Serial.print("Send failed: ");
    Serial.println(fbdoWrite.errorReason());
  }
}

// =========================
// READ: Website message → Serial Monitor
// Uses fbdoRead — dedicated read object, never touches stream or write
// =========================
void readWebMessage() {
  if (Firebase.RTDB.getJSON(&fbdoRead, "/serial/message")) {
    FirebaseJson& json = fbdoRead.jsonObject();
    FirebaseJsonData srcData, txtData;

    json.get(srcData, "source");
    json.get(txtData, "text");

    if (srcData.success && txtData.success) {
      String source = srcData.stringValue;
      String text   = txtData.stringValue;

      static String lastWebMsg = "";
      if (source == "web" && text.length() > 0 && text != lastWebMsg) {
        lastWebMsg = text;
        Serial.print("[Web] ");
        Serial.println(text);
      }
    }
  }
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);

  pinMode(IR_PIN, INPUT);
  servo.attach(SERVO_PIN);
  servo.write(90);

  initWiFi();

  config.api_key      = API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Auth OK");
  } else {
    Serial.printf("Auth Error: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback   = tokenStatusCallback;
  config.timeout.serverResponse  = 10 * 1000; // 10s response timeout

  // keepAlive ONLY on the stream object
  fbdoStream.keepAlive(5, 5, 1);

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Begin stream on its dedicated object — never use fbdoStream for anything else
  if (!Firebase.RTDB.beginStream(&fbdoStream, "/servo/msgTitle")) {
    Serial.print("Stream error: ");
    Serial.println(fbdoStream.errorReason());
  }
  Firebase.RTDB.setStreamCallback(&fbdoStream, streamCallback, streamTimeoutCallback);

  Serial.println("Ready. Type a message and press Enter to send to website.");
}

// =========================
// LOOP
// =========================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    initWiFi();
  }

  if (!Firebase.ready()) return;

  unsigned long now = millis();

  // 1. IR sensor — every 50ms, upload only on change
  if (now - lastIRCheck >= IR_POLL_MS) {
    lastIRCheck = now;
    int currentStatus = digitalRead(IR_PIN);
    if (currentStatus != lastIRStatus) {
      lastIRStatus = currentStatus;
      uploadIR(currentStatus);
    }
  }

  // 2. Serial Monitor → website
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      sendSerialMessage(input);
    }
  }

  // 3. Website → Serial Monitor — every 500ms
  if (now - lastMsgCheck >= MSG_POLL_MS) {
    lastMsgCheck = now;
    readWebMessage();
  }
}