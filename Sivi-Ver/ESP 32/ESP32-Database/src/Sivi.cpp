#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

#include <ESP32Servo.h>

// Insert your network credentials
#define WIFI_SSID     "Xiaomi"
#define WIFI_PASSWORD "G2706pls"

#define FIREBASE_DATABASE_URL "https://sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app"
#define API_KEY "AIzaSyBlxLdiakI8Zv5gtRWtAcfwxTcW-XtGbTw"

// Firebase Objects
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdoStream; // Dedicated object for the Real-time Servo Stream
FirebaseData fbdoSet;    // Dedicated object for the IR Sensor Uploads

// Hardware Pins
int servo_pin = 13;
int ir_sensor = 14; 
int lastStatus = -1;

// Timers for non-blocking polling
unsigned long lastMsgCheck  = 0;
unsigned long lastIRCheck   = 0;
#define MSG_POLL_MS  500   // check for web messages every 500ms
#define IR_POLL_MS    50   // IR sensor check every 50ms

Servo servo;

// --- CALLBACK FUNCTIONS ---

// This function runs automatically the instant the website slider moves
void streamCallback(FirebaseStream data) {
  if (data.dataType() == "int") {
    int servoValue = data.intData();
    
    // Only move if the value is different from the last known position
    static int lastServoPos = -1;
    if (servoValue != lastServoPos) {
      Serial.print("Moving Servo to: ");
      Serial.println(servoValue);
      servo.write(servoValue);
      lastServoPos = servoValue;
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("Stream timeout, resuming...");
}

// --- HELPER FUNCTIONS ---

void initWiFi(){
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected!");

  // Sync time
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

void uploadSensorData(int status) {
  delay(10); 
  
  if (Firebase.RTDB.setInt(&fbdoSet, "/sensor/obstacle", status)) {
    Serial.print("IR Status Uploaded: ");
    Serial.println(status);
  } else {
    Serial.print("IR Upload failed: ");
    Serial.println(fbdoSet.errorReason());
  }
}

// Sends a message typed in Serial Monitor to Firebase → website displays it
void sendSerialMessage(const String& text) {
  FirebaseJson msg;
  msg.set("text", text);
  msg.set("source", "esp32");  // website checks for source == "esp32" to display it

  if (Firebase.RTDB.setJSON(&fbdoSet, "/serial/message", &msg)) {
    Serial.print("[Sent to website] ");
    Serial.println(text);
  } else {
    Serial.print("Message send failed: ");
    Serial.println(fbdoSet.errorReason());
  }
}

// Reads message sent from website and prints it to Serial Monitor
void readWebMessage() {
  if (Firebase.RTDB.getJSON(&fbdoSet, "/serial/message")) {
    FirebaseJson &json = fbdoSet.jsonObject();
    FirebaseJsonData srcData, txtData;

    json.get(srcData, "source");
    json.get(txtData, "text");

    if (srcData.success && txtData.success) {
      String source = srcData.stringValue;
      String text   = txtData.stringValue;

      // Only print if it came from the website and is a new message
      static String lastWebMsg = "";
      if (source == "web" && text.length() > 0 && text != lastWebMsg) {
        lastWebMsg = text;
        Serial.print("[Web] ");
        Serial.println(text);
      }
    }
  }
}

// --- MAIN SETUP ---

void setup() {
  Serial.begin(115200);
  
  pinMode(ir_sensor, INPUT);
  servo.attach(servo_pin);
  servo.write(90); // Start at center
  
  initWiFi();

  config.api_key = API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("Firebase Auth OK");
  } else {
    Serial.printf("Auth Error: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  config.timeout.serverResponse = 60 * 1000;
  fbdoStream.keepAlive(1, 1, 1);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (!Firebase.RTDB.beginStream(&fbdoStream, "/servo/msgTitle")) {
    Serial.print("Stream begin error: ");
    Serial.println(fbdoStream.errorReason());
  }
  Firebase.RTDB.setStreamCallback(&fbdoStream, streamCallback, streamTimeoutCallback);

  Serial.println("Ready. Type a message and press Enter to send it to the website.");
}

// --- MAIN LOOP ---

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    initWiFi();
  }

  if (!Firebase.ready()) return;

  unsigned long now = millis();

  // 1. Upload IR sensor status if it changed (every 50ms)
  if (now - lastIRCheck >= IR_POLL_MS) {
    lastIRCheck = now;
    int currentStatus = digitalRead(ir_sensor);
    if (currentStatus != lastStatus) {
      uploadSensorData(currentStatus);
      lastStatus = currentStatus;
    }
  }

  // 2. Read Serial Monitor — sends typed message to website via Firebase
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      sendSerialMessage(input);
    }
  }

  // 3. Check Firebase for web messages — rate limited to every 500ms
  //    to avoid blocking the servo stream with constant HTTP requests
  if (now - lastMsgCheck >= MSG_POLL_MS) {
    lastMsgCheck = now;
    readWebMessage();
  }
}