/*
========================================================
ESP32 FIREBASE DASHBOARD JAVASCRIPT
========================================================

This script does the following:

1. Connects to Firebase
2. Controls the servo slider
3. Displays IR sensor data
4. Allows message communication between Web and ESP32

--------------------------------------------------------
STEP 1 — CREATE A FIREBASE PROJECT
--------------------------------------------------------

Go to:
https://console.firebase.google.com

1. Click "Add Project"
2. Create a project name
3. Disable Google Analytics (optional)

--------------------------------------------------------
STEP 2 — CREATE REALTIME DATABASE
--------------------------------------------------------

Inside Firebase console:

Build → Realtime Database

Click:
Create Database

Choose:
Start in TEST MODE

--------------------------------------------------------
STEP 3 — ENABLE AUTHENTICATION
--------------------------------------------------------

Build → Authentication

Enable:
Anonymous authentication

--------------------------------------------------------
STEP 4 — GET FIREBASE CONFIG
--------------------------------------------------------

Project Settings → General

Scroll to:

Your Apps → Web App

Copy the configuration block and paste it below.

========================================================
*/


/* ============================================
   IMPORT FIREBASE LIBRARIES 
============================================ */

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js";
import { getAuth, signInAnonymously, onAuthStateChanged }
from "https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js";

import { getDatabase, ref, onValue, set }
from "https://www.gstatic.com/firebasejs/10.12.0/firebase-database.js";


/* ============================================
   FIREBASE CONFIGURATION
   REPLACE WITH YOUR OWN PROJECT SETTINGS
============================================ */

// const firebaseConfig = {

//   apiKey: "YOUR_API_KEY",

//   authDomain: "YOUR_PROJECT.firebaseapp.com",

//   databaseURL: "https://YOUR_PROJECT-default-rtdb.region.firebasedatabase.app",

//   projectId: "YOUR_PROJECT",

//   storageBucket: "YOUR_PROJECT.appspot.com",

//   messagingSenderId: "XXXX",

//   appId: "XXXX"

// };


// For Firebase JS SDK v7.20.0 and later, measurementId is optional
const firebaseConfig = {
  apiKey: "AIzaSyBlxLdiakI8Zv5gtRWtAcfwxTcW-XtGbTw",
  authDomain: "sivi-arduino-database.firebaseapp.com",
  databaseURL: "https://sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "sivi-arduino-database",
  storageBucket: "sivi-arduino-database.firebasestorage.app",
  messagingSenderId: "854752663108",
  appId: "1:854752663108:web:b80bad680b96bb63478e11",
  measurementId: "G-C1XE1J4ZJR"
};


/* ============================================
   INITIALIZE FIREBASE
============================================ */

const app = initializeApp(firebaseConfig);

const auth = getAuth(app);

const db = getDatabase(app);


/* ============================================
   GET HTML ELEMENTS
============================================ */

const statusEl    = document.getElementById("status");

const angleSlider = document.getElementById("angleSlider");

const angleValue  = document.getElementById("angleValue");

const servoAngle  = document.getElementById("servoAngle");

const irDetected  = document.getElementById("irDetected");

const irRaw       = document.getElementById("irRaw");

const messages    = document.getElementById("messages");

const msgInput    = document.getElementById("msgInput");

const sendBtn     = document.getElementById("sendBtn");


let lastEspMsg = "";


/* ============================================
   FUNCTION: DISPLAY MESSAGE
============================================ */

function addMessage(text, source) {

  if (messages.textContent === "No messages yet.") {
    messages.textContent = "";
  }

  messages.textContent += `[${source}] ${text}\n`;

  messages.scrollTop = messages.scrollHeight;

}


/* ============================================
   FUNCTION: SEND MESSAGE FROM WEB
============================================ */

function sendWebMessage() {

  const text = msgInput.value.trim();

  if (!text) return;

  addMessage(text, "Web");

  msgInput.value = "";

  set(ref(db, "serial/message"), {

    text: text,

    source: "web"

  });

}


/* ============================================
   SERVO SLIDER CONTROL
============================================ */

angleSlider.addEventListener("input", () => {

  const angle = Number(angleSlider.value);

  angleValue.textContent = angle;

  // Write value to Firebase
  set(ref(db, "servo/angle"), angle);

});


/* ============================================
   BUTTON EVENTS
============================================ */

sendBtn.addEventListener("click", sendWebMessage);

msgInput.addEventListener("keydown", (e) => {

  if (e.key === "Enter") {

    sendWebMessage();

  }

});


/* ============================================
   LOGIN TO FIREBASE
============================================ */

signInAnonymously(auth).catch(err => {

  console.error(err);

  statusEl.textContent = "Auth failed";

});


/* ============================================
   AFTER LOGIN
============================================ */

onAuthStateChanged(auth, (user) => {

  if (!user) {

    statusEl.textContent = "Connecting...";

    return;

  }

  statusEl.textContent = "Connected";


  /* -----------------------------------------
     SERVO DATA
  ----------------------------------------- */

  onValue(ref(db, "servo/angle"), (snap) => {

    const val = snap.val();

    if (val === null) return;

    angleSlider.value = val;

    angleValue.textContent = val;

    servoAngle.textContent = val;

  });


  /* -----------------------------------------
     IR SENSOR DATA
  ----------------------------------------- */

  onValue(ref(db, "ir"), (snap) => {

    const data = snap.val();

    if (!data) return;

    irRaw.textContent = data.raw ?? "--";

    irDetected.textContent = data.detected ? "YES" : "NO";

  });


  /* -----------------------------------------
     MESSAGE FROM ESP32
  ----------------------------------------- */

  onValue(ref(db, "serial/message"), (snap) => {

    const data = snap.val();

    if (!data || !data.text) return;

    if (data.source === "esp32" && data.text !== lastEspMsg) {

      lastEspMsg = data.text;

      addMessage(data.text, "ESP32");

    }

  });

});