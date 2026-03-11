/*
 * ================================================================
 * main.cpp — ESP32 Firebase Controller
 *
 * WHAT THIS PROGRAM DOES:
 *   1. Connects to Wi-Fi
 *   2. Connects to Firebase (Google's real-time database)
 *   3. Reads /servo/angle from Firebase → moves the servo motor
 *   4. Reads /ir every 500ms → uploads sensor state to Firebase
 *   5. Reads messages typed on the website → prints to Serial Monitor
 *   6. Reads messages typed in Serial Monitor → sends to the website
 *
 * WIRING:
 *   Servo  Red    → ESP32 VIN  (5V — NOT 3.3V)
 *   Servo  Brown  → ESP32 GND
 *   Servo  Orange → ESP32 GPIO 13
 *   IR     VCC    → ESP32 3.3V
 *   IR     GND    → ESP32 GND
 *   IR     OUT    → ESP32 GPIO 34
 *
 * BEFORE FLASHING:
 *   1. Replace DATABASE_SECRET below with your secret from:
 *      Firebase Console → Project Settings → Service Accounts
 *      → Database Secrets → Show
 *   2. Set Firebase rules to { ".read": true, ".write": true }
 * ================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>


// ----------------------------------------------------------------
// SETTINGS — change these to match your setup
// ----------------------------------------------------------------

// Wi-Fi network name and password
#define WIFI_SSID     "Xiaomi"
#define WIFI_PASSWORD "G2706pls"

// Firebase database URL and secret key
#define FIREBASE_DATABASE_URL "https://sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DATABASE_SECRET       "PASTE_YOUR_DATABASE_SECRET_HERE"

// GPIO pin numbers
#define SERVO_PIN 13   // servo signal wire
#define IR_PIN    34   // IR sensor output wire

// How often (in milliseconds) to check Firebase and the IR sensor
#define SERVO_POLL_MS 100   // check for new servo angle every 100ms
#define IR_UPLOAD_MS  500   // upload IR reading every 500ms
#define MSG_CHECK_MS  300   // check for web messages every 300ms


// ----------------------------------------------------------------
// SERVO SETTINGS (LEDC = ESP32's built-in PWM hardware)
//
// Standard servo PWM:
//   Frequency = 50 Hz (one pulse every 20ms)
//   0°  = 500 µs pulse width
//   90° = 1500 µs pulse width
//   180° = 2500 µs pulse width
// ----------------------------------------------------------------
#define LEDC_CHANNEL  0
#define LEDC_FREQ_HZ  50
#define LEDC_BITS     16
#define SERVO_MIN_US  500
#define SERVO_MAX_US  2500


// ----------------------------------------------------------------
// FIREBASE OBJECTS
//
// We use 3 separate FirebaseData objects so that one operation
// never blocks another:
//   fbStream → listens for servo angle changes (stream)
//   fbPoll   → reads servo angle as a fallback every 100ms
//   fbWrite  → uploads IR data and messages
// ----------------------------------------------------------------
FirebaseData   fbStream;
FirebaseData   fbPoll;
FirebaseData   fbWrite;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;


// ----------------------------------------------------------------
// GLOBAL VARIABLES
// ----------------------------------------------------------------
int  currentAngle = 90;   // last known servo angle
bool streamReady  = false; // true once the Firebase stream is active

unsigned long lastServoPoll = 0;
unsigned long lastIRUpload  = 0;
unsigned long lastMsgCheck  = 0;
unsigned long lastFBCheck   = 0;

String lastWebMsg = ""; // track last web message so we don't repeat it


// ================================================================
// SERVO FUNCTIONS
// ================================================================

// Convert a 0–180° angle into a LEDC duty cycle value
// This is the maths that turns an angle into a pulse width
uint32_t angleToDuty(int angle) {
  angle   = constrain(angle, 0, 180);
  long us = map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  return (uint32_t)(us * 65536UL / 20000UL);
}

// Move the servo to a given angle (0–180)
void moveServo(int angle) {
  angle = constrain(angle, 0, 180);
  currentAngle = angle;
  ledcWrite(LEDC_CHANNEL, angleToDuty(angle));
  Serial.print("Servo → ");
  Serial.print(currentAngle);
  Serial.println("°");
}


// ================================================================
// STREAM CALLBACK
//
// This function is called automatically by the Firebase library
// whenever the value at /servo/angle changes in the database.
// It gives us the fastest possible response to slider movements.
// ================================================================
void onAngleStream(FirebaseStream data) {
  String type = data.dataType();
  if (type == "int" || type == "float" || type == "number") {
    moveServo(data.intData()); // move immediately on stream event
  }
}

// Called if the stream connection drops — we mark it as not ready
// so the loop() will reconnect it automatically
void onStreamTimeout(bool timedOut) {
  if (timedOut) {
    Serial.println("Stream timed out — will reconnect.");
    streamReady = false;
  }
}


// ================================================================
// WI-FI
// ================================================================
void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected — IP: ");
    Serial.println(WiFi.localIP());
  } else {
    // Couldn't connect — restart the ESP32 and try again
    Serial.println("\nWi-Fi failed. Restarting...");
    delay(2000);
    ESP.restart();
  }
}


// ================================================================
// SETUP — runs once when the ESP32 powers on
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 Starting ===");
  Serial.println("Type a message and press Enter to send it to the website.");

  // Set up servo using LEDC (ESP32 hardware PWM — no library needed)
  ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_BITS);
  ledcAttachPin(SERVO_PIN, LEDC_CHANNEL);
  moveServo(90); // start at centre position
  Serial.println("Servo ready on GPIO 13.");

  // Set up IR sensor pin as digital input
  pinMode(IR_PIN, INPUT_PULLUP);
  Serial.println("IR sensor ready on GPIO 34.");

  // Connect to Wi-Fi
  connectWiFi();

  // Configure Firebase with the database URL and secret key
  // The legacy token (secret) is the simplest auth method for ESP32
  fbConfig.database_url               = FIREBASE_DATABASE_URL;
  fbConfig.signer.tokens.legacy_token = DATABASE_SECRET;
  fbConfig.timeout.serverResponse     = 8000;
  fbConfig.timeout.socketConnection   = 20000;

  // Set memory size for each connection
  fbStream.setResponseSize(4096); // stream needs more memory
  fbPoll.setResponseSize(512);    // poll only reads one number
  fbWrite.setResponseSize(1024);  // writes don't need much

  // Connect to Firebase
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase connected.");
  Serial.println("=== Ready ===\n");
}


// ================================================================
// LOOP — runs over and over forever after setup()
// ================================================================
void loop() {

  // --- Check Wi-Fi is still connected ---
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi lost. Reconnecting...");
    connectWiFi();
    streamReady = false;
    delay(1000);
    return; // restart the loop
  }

  unsigned long now = millis(); // milliseconds since boot

  // --- Wait until Firebase is authenticated and ready ---
  if (!Firebase.ready()) {
    if (now - lastFBCheck > 3000) {
      Serial.println("Waiting for Firebase...");
      lastFBCheck = now;
    }
    delay(100);
    return;
  }

  // --- Start (or restart) the Firebase stream ---
  // The stream is the fastest way to receive servo angle updates.
  // If it drops, we restart it here.
  if (!streamReady) {
    Serial.println("Starting Firebase stream...");
    if (Firebase.RTDB.beginStream(&fbStream, "/servo/angle")) {
      Firebase.RTDB.setStreamCallback(&fbStream, onAngleStream, onStreamTimeout);
      streamReady = true;
      Serial.println("Stream started.");

      // Write our current angle back to Firebase so the website syncs
      Firebase.RTDB.setInt(&fbWrite, "/servo/angle", currentAngle);
    } else {
      Serial.print("Stream failed: ");
      Serial.println(fbStream.errorReason());
      delay(2000);
      return;
    }
  }

  // --- Poll servo angle as a fallback every 100ms ---
  // Even if the stream drops, the servo will still respond
  // because we directly read the angle from Firebase here.
  if (now - lastServoPoll >= SERVO_POLL_MS) {
    lastServoPoll = now;
    if (Firebase.RTDB.getInt(&fbPoll, "/servo/angle")) {
      moveServo(fbPoll.intData());
    }
  }

  // --- Read Serial Monitor input from the user ---
  // If the user types something and presses Enter, send it to Firebase
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim(); // remove leading/trailing whitespace

    if (input.length() > 0) {
      Serial.print("[You]: ");
      Serial.println(input);

      // Write to /serial/message so the website can display it
      FirebaseJson msg;
      msg.set("text",   input);
      msg.set("source", "esp32");
      Firebase.RTDB.setJSON(&fbWrite, "/serial/message", &msg);
    }
  }

  // --- Check if the website sent us a message ---
  // The website writes to /serial/message with source:"web"
  // We read it here and print it to the Serial Monitor
  if (now - lastMsgCheck >= MSG_CHECK_MS) {
    lastMsgCheck = now;

    if (Firebase.RTDB.getJSON(&fbWrite, "/serial/message")) {
      FirebaseJson&    json   = fbWrite.jsonObject();
      FirebaseJsonData srcData, txtData;

      json.get(srcData, "source");
      json.get(txtData, "text");

      if (srcData.success && txtData.success) {
        String source = srcData.stringValue;
        String text   = txtData.stringValue;

        // Only print if it's from the web and is a new message
        if (source == "web" && text != lastWebMsg && text.length() > 0) {
          lastWebMsg = text;
          Serial.print("[Web]: ");
          Serial.println(text);
        }
      }
    }
  }

  // --- Upload IR sensor reading every 500ms ---
  if (now - lastIRUpload >= IR_UPLOAD_MS) {
    lastIRUpload = now;

    int  irRaw    = digitalRead(IR_PIN);
    bool detected = (irRaw == LOW); // LOW means object detected

    FirebaseJson irData;
    irData.set("raw",      irRaw);
    irData.set("detected", detected);

    if (!Firebase.RTDB.setJSON(&fbWrite, "/ir", &irData)) {
      Serial.print("IR upload error: ");
      Serial.println(fbWrite.errorReason());
    }
  }
}