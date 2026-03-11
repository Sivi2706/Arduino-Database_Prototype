// Arduino Web Emulator
// Simulates:
// 1. Servo motor output
// 2. IR sensor input

(function () {

  // ----------------------------
  // GET HTML ELEMENTS
  // ----------------------------
  const simDot = document.getElementById("simDot");
  const simText = document.getElementById("simText");

  const angleSlider = document.getElementById("angleSlider");
  const angleValue = document.getElementById("angleValue");

  const servoArm = document.getElementById("servoArm");
  const servoAngle = document.getElementById("servoAngle");

  const irValue = document.getElementById("irValue");
  const irDetected = document.getElementById("irDetected");

  const threshold = document.getElementById("threshold");

  const canvas = document.getElementById("chart");
  const ctx = canvas.getContext("2d");



  // ----------------------------
  // SYSTEM STATE
  // ----------------------------
  let servoAngleValue = 90;
  let irSensorValue = 0;

  let irInterval = 100;
  let irRunning = true;

  let samples = [];
  const MAX_SAMPLES = 120;



  // ----------------------------
  // UTILITY FUNCTIONS
  // ----------------------------
  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }



  // ----------------------------
  // SIMULATION STATUS
  // ----------------------------
  function setSimulationStatus(running) {

    if (running) {
      simDot.style.background = "var(--success)";
      simText.innerText = "Simulation running";
    } else {
      simDot.style.background = "var(--danger)";
      simText.innerText = "Simulation paused";
    }
  }



  // ----------------------------
  // SERVO VISUAL UPDATE
  // ----------------------------
  function updateServo(angle) {

    servoAngleValue = clamp(angle, 0, 180);

    // Convert 0..180 → -90..+90 for display
    const rotation = servoAngleValue - 90;

    servoArm.style.transform =
      `translateX(-50%) rotate(${rotation}deg)`;

    servoAngle.innerText = servoAngleValue;
  }



  // ----------------------------
  // IR SENSOR DISPLAY
  // ----------------------------
  function updateIR(value) {

    irSensorValue = value;

    irValue.innerText = value;

    const limit = Number(threshold.value);

    if (value >= limit) {
      irDetected.innerText = "YES";
      irDetected.style.color = "var(--warn)";
    } else {
      irDetected.innerText = "NO";
      irDetected.style.color = "var(--muted)";
    }
  }



  // ----------------------------
  // STORE CHART DATA
  // ----------------------------
  function addSample(value) {

    samples.push(value);

    if (samples.length > MAX_SAMPLES) {
      samples.shift();
    }
  }



  // ----------------------------
  // DRAW CHART
  // ----------------------------
  function drawChart() {

    const w = canvas.width;
    const h = canvas.height;

    ctx.clearRect(0, 0, w, h);

    if (samples.length < 2) return;

    ctx.beginPath();

    for (let i = 0; i < samples.length; i++) {

      const x = (i / MAX_SAMPLES) * w;
      const y = h - (samples[i] / 1023) * h;

      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }

    ctx.strokeStyle = "blue";
    ctx.stroke();
  }



  // ----------------------------
  // IR SENSOR SIMULATION
  // ----------------------------
  function simulateIR() {

    // base signal
    const base = 450 + Math.sin(Date.now() / 1000) * 120;

    // random noise
    const noise = (Math.random() - 0.5) * 150;

    let value = Math.round(base + noise);

    value = clamp(value, 0, 1023);

    return value;
  }



  // ----------------------------
  // IR LOOP
  // ----------------------------
  let irTimer = null;

  function startIR() {

    if (irTimer) return;

    irRunning = true;
    setSimulationStatus(true);

    irTimer = setInterval(() => {

      const value = simulateIR();

      updateIR(value);
      addSample(value);
      drawChart();

    }, irInterval);
  }

  function stopIR() {

    irRunning = false;
    setSimulationStatus(false);

    clearInterval(irTimer);
    irTimer = null;
  }



  // ----------------------------
  // UI EVENTS
  // ----------------------------
  angleSlider.addEventListener("input", () => {

    angleValue.innerText = angleSlider.value;

    updateServo(Number(angleSlider.value));

  });



  // ----------------------------
  // INITIALIZE
  // ----------------------------
  updateServo(servoAngleValue);
  updateIR(0);

  startIR();

})();