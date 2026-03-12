#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// =========================
// USER SETTINGS
// =========================
#define WIFI_SSID     "Xiaomi"
#define WIFI_PASSWORD "G2706pls"

#define FIREBASE_DATABASE_URL "https://sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DATABASE_SECRET       "PASTE_YOUR_DATABASE_SECRET_HERE"

#define SERVO_PIN 13
#define IR_PIN    34

#define SERVO_POLL_MS 200
#define IR_UPLOAD_MS  500
#define MSG_CHECK_MS  300

// =========================
// SERVO PWM SETTINGS
// =========================
#define LEDC_CHANNEL 0
#define LEDC_FREQ_HZ 50
#define LEDC_BITS    16
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500

// =========================
// FIREBASE
// =========================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// =========================
// GLOBALS
// =========================
int currentAngle = 90;
String lastWebMsg = "";

unsigned long lastServoPoll = 0;
unsigned long lastIRUpload  = 0;
unsigned long lastMsgCheck  = 0;
unsigned long lastReadyMsg  = 0;

// =========================
// HELPER FUNCTIONS
// =========================
uint32_t angleToDuty(int angle) {
  angle = constrain(angle, 0, 180);
  long pulseUs = map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  return (uint32_t)(pulseUs * 65535UL / 20000UL);
}

void moveServo(int angle) {
  angle = constrain(angle, 0, 180);

  if (angle == currentAngle) return;   // avoid unnecessary writes

  currentAngle = angle;
  ledcWrite(LEDC_CHANNEL, angleToDuty(angle));

  Serial.print("Servo -> ");
  Serial.print(currentAngle);
  Serial.println(" deg");
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void sendSerialMessageToFirebase(const String& text) {
  FirebaseJson msg;
  msg.set("text", text);
  msg.set("source", "esp32");

  if (!Firebase.RTDB.setJSON(&fbdo, "/serial/message", &msg)) {
    Serial.print("Message upload failed: ");
    Serial.println(fbdo.errorReason());
  }
}

void uploadIRStatus() {
  int raw = digitalRead(IR_PIN);
  bool detected = (raw == LOW);

  FirebaseJson ir;
  ir.set("raw", raw);
  ir.set("detected", detected);

  if (!Firebase.RTDB.setJSON(&fbdo, "/ir", &ir)) {
    Serial.print("IR upload failed: ");
    Serial.println(fbdo.errorReason());
  }
}

void readServoFromFirebase() {
  if (Firebase.RTDB.getInt(&fbdo, "/servo/angle")) {
    int angle = fbdo.intData();
    moveServo(angle);
  } else {
    Serial.print("Servo read failed: ");
    Serial.println(fbdo.errorReason());
  }
}

void readWebMessageFromFirebase() {
  if (Firebase.RTDB.getJSON(&fbdo, "/serial/message")) {
    FirebaseJson &json = fbdo.jsonObject();
    FirebaseJsonData src, txt;

    json.get(src, "source");
    json.get(txt, "text");

    if (src.success && txt.success) {
      String source = src.stringValue;
      String text   = txt.stringValue;

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
  delay(1000);

  Serial.println();
  Serial.println("ESP32 Firebase Servo + IR + Message");

  pinMode(IR_PIN, INPUT_PULLUP);

  ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_BITS);
  ledcAttachPin(SERVO_PIN, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, angleToDuty(90));

  connectWiFi();

  config.database_url = FIREBASE_DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Push default starting angle once
  Firebase.RTDB.setInt(&fbdo, "/servo/angle", currentAngle);

  Serial.println("Setup complete.");
  Serial.println("Type in Serial Monitor and press Enter to send to web.");
}

// =========================
// LOOP
// =========================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  unsigned long now = millis();

  if (!Firebase.ready()) {
    if (now - lastReadyMsg > 3000) {
      Serial.println("Waiting for Firebase...");
      lastReadyMsg = now;
    }
    delay(100);
    return;
  }

  // 1. Read servo angle from Firebase
  if (now - lastServoPoll >= SERVO_POLL_MS) {
    lastServoPoll = now;
    readServoFromFirebase();
  }

  // 2. Upload IR status
  if (now - lastIRUpload >= IR_UPLOAD_MS) {
    lastIRUpload = now;
    uploadIRStatus();
  }

  // 3. Read web message
  if (now - lastMsgCheck >= MSG_CHECK_MS) {
    lastMsgCheck = now;
    readWebMessageFromFirebase();
  }

  // 4. Read Serial Monitor and send to Firebase
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      Serial.print("[You] ");
      Serial.println(input);
      sendSerialMessageToFirebase(input);
    }
  }
}