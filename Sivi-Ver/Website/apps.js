// ================================================================
// app.js — Arduino Web Dashboard logic
//
// HOW THIS FILE IS ORGANISED:
//   1. Firebase setup      — connect to the database
//   2. Get HTML elements   — grab the parts of the page we'll update
//   3. Helper functions    — small reusable tools
//   4. Display functions   — update what the user sees
//   5. Firebase listeners  — watch the database for live changes
//   6. User actions        — what happens when the user does something
//   7. Start               — kick off the listeners immediately
//
// NOTE: No authentication is needed here because the Firebase
// database rules are set to { ".read": true, ".write": true }.
// ================================================================


// ----------------------------------------------------------------
// 1. FIREBASE SETUP
//
// Firebase is Google's real-time database service.
// We import only the parts we need from their SDK.
// ----------------------------------------------------------------

import { initializeApp }
  from "https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js";

import { getDatabase, ref, onValue, set }
  from "https://www.gstatic.com/firebasejs/10.12.0/firebase-database.js";

// Your project's unique Firebase settings
const firebaseConfig = {
  apiKey:            "AIzaSyBlxLdiakI8Zv5gtRWtAcfwxTcW-XtGbTw",
  authDomain:        "sivi-arduino-database.firebaseapp.com",
  databaseURL:       "https://sivi-arduino-database-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId:         "sivi-arduino-database",
  storageBucket:     "sivi-arduino-database.firebasestorage.app",
  messagingSenderId: "854752663108",
  appId:             "1:854752663108:web:b80bad680b96bb63478e11"
};

// Initialise Firebase and get a handle to the database
const app = initializeApp(firebaseConfig);
const db  = getDatabase(app);


// ----------------------------------------------------------------
// 2. GET HTML ELEMENTS
//
// document.getElementById("id") finds an element in index.html
// by its id="..." attribute so we can read or change it.
// ----------------------------------------------------------------

const simDot          = document.getElementById("simDot");          // coloured dot in topbar
const simText         = document.getElementById("simText");          // status text in topbar
const angleSlider     = document.getElementById("angleSlider");      // servo slider
const angleValue      = document.getElementById("angleValue");       // slider angle number
const servoArm        = document.getElementById("servoArm");         // rotating arm graphic
const servoAngle      = document.getElementById("servoAngle");       // confirmed angle from ESP32
const irDetected      = document.getElementById("irDetected");       // YES/NO badge
const irValue         = document.getElementById("irValue");          // raw IR number
const serialMessages  = document.getElementById("serialMessages");   // message list
const serialContainer = document.getElementById("serialContainer");  // scrollable box
const clearBtn        = document.getElementById("clearBtn");         // Clear button
const msgInput        = document.getElementById("msgInput");         // text input box
const sendBtn         = document.getElementById("sendBtn");          // Send button


// ----------------------------------------------------------------
// 3. HELPER FUNCTIONS
// ----------------------------------------------------------------

// clamp() keeps a number inside a min/max range
// Example: clamp(200, 0, 180) returns 180
function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}


// ----------------------------------------------------------------
// 4. DISPLAY FUNCTIONS
//
// These update what the user sees on the page.
// ----------------------------------------------------------------

// setStatus() changes the coloured dot and text in the top bar
// Possible states: "connecting", "waiting", "connected"
function setStatus(state) {
  const states = {
    connecting: { color: "var(--warn)",    text: "Connecting to Firebase…" },
    waiting:    { color: "var(--warn)",    text: "Waiting for ESP32…"      },
    connected:  { color: "var(--success)", text: "Connected to ESP32"      },
  };

  const s = states[state] || states.connecting;
  simDot.style.background = s.color;
  simText.innerText        = s.text;
}


// applyServoUI() rotates the on-screen arm and updates the numbers
// angle goes from 0° to 180°
// We subtract 90 so 90° points straight up (0° tilt = 90° servo)
function applyServoUI(angle) {
  const a = clamp(Number(angle), 0, 180);
  servoArm.style.transform = `translateX(-50%) rotate(${a - 90}deg)`;
  servoAngle.innerText     = a;
  angleSlider.value        = a;
  angleValue.innerText     = a;
}


// addMessage() adds a new line to the serial monitor box
// source = "esp32" → cyan colour
// source = "web"   → green colour
function addMessage(text, source) {
  // Remove the placeholder text on first message
  const placeholder = serialMessages.querySelector(".serial-placeholder");
  if (placeholder) placeholder.remove();

  // Create a new div for this message
  const div = document.createElement("div");
  div.className   = "serial-message " + (source === "web" ? "from-web" : "from-esp");
  div.textContent = (source === "web" ? "[Web] " : "[ESP32] ") + text;

  // Add it to the bottom of the list
  serialMessages.appendChild(div);

  // Auto-scroll to the newest message
  serialContainer.scrollTop = serialContainer.scrollHeight;
}


// ----------------------------------------------------------------
// 5. FIREBASE LISTENERS
//
// onValue() watches a database path and runs a function
// every time the value changes — this is how we get live updates.
// ----------------------------------------------------------------

// lastEspMsg stores the last ESP32 message we displayed
// so we don't show the same message twice
let lastEspMsg = "";

function setupListeners() {

  // --- Watch /servo/angle ---
  // Fires whenever the ESP32 writes its confirmed position
  onValue(ref(db, "servo/angle"), (snapshot) => {
    const angle = snapshot.val();
    if (angle === null || angle === undefined) return;

    setStatus("connected"); // we received data = ESP32 is online
    applyServoUI(angle);    // update the visual arm
  });


  // --- Watch /ir ---
  // Fires whenever the ESP32 uploads a new IR sensor reading
  onValue(ref(db, "ir"), (snapshot) => {
    const data = snapshot.val();
    if (!data) return;

    // Update raw value display
    irValue.innerText = data.raw ?? "—";

    // Update the YES/NO detection badge
    if (data.detected) {
      irDetected.textContent = "YES";
      irDetected.className   = "ir-badge active"; // turns red
    } else {
      irDetected.textContent = "NO";
      irDetected.className   = "ir-badge";        // grey again
    }
  });


  // --- Watch /serial/message ---
  // Fires when either the ESP32 or the web page writes a message.
  // We only show ESP32 messages here — web messages are already
  // shown immediately when the user clicks Send.
  onValue(ref(db, "serial/message"), (snapshot) => {
    const data = snapshot.val();
    if (!data || !data.text) return;

    // Only show it if it came from ESP32 and is a new message
    if (data.source === "esp32" && data.text !== lastEspMsg) {
      lastEspMsg = data.text;
      addMessage(data.text, "esp32");
    }
  });
}


// ----------------------------------------------------------------
// 6. USER ACTIONS
// ----------------------------------------------------------------

// --- Servo slider ---
// When the user drags the slider, update the display immediately.
// We wait 80ms after they stop dragging before writing to Firebase
// to avoid flooding the database with every tiny movement.
let writeTimer = null;

angleSlider.addEventListener("input", () => {
  const angle = Number(angleSlider.value);
  angleValue.innerText = angle;   // update the number shown
  applyServoUI(angle);            // rotate the arm on screen

  clearTimeout(writeTimer);       // cancel any previous pending write
  writeTimer = setTimeout(() => {
    // Write the final angle to Firebase — ESP32 will read this
    set(ref(db, "servo/angle"), angle)
      .catch(err => console.error("Firebase write error:", err));
  }, 80); // wait 80ms after the user stops dragging
});


// --- Clear button ---
// Wipes all messages from the serial monitor display
clearBtn.addEventListener("click", () => {
  serialMessages.innerHTML =
    '<div class="serial-placeholder">No messages yet. Type below to send one.</div>';
});


// --- Send message ---
// Writes the typed message to Firebase so the ESP32 can read it
function sendWebMessage() {
  const text = msgInput.value.trim(); // remove extra spaces
  if (!text) return;                  // do nothing if input is empty

  msgInput.value = "";                // clear the input box

  // Show the message in the monitor right away (green)
  addMessage(text, "web");

  // Write to Firebase — the ESP32 polls this path and will print it
  set(ref(db, "serial/message"), { text: text, source: "web" })
    .catch(err => console.error("Send error:", err));
}

sendBtn.addEventListener("click", sendWebMessage);

// Also send when the user presses the Enter key
msgInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") sendWebMessage();
});


// ----------------------------------------------------------------
// 7. START — connect directly (no auth needed with open rules)
// ----------------------------------------------------------------

// Show amber "connecting" status while Firebase initialises
setStatus("connecting");

// Start listening to the database immediately
// onValue() will fire as soon as the first data arrives
setupListeners();