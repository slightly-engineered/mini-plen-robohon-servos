// Nordic UART UUIDs
const NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_RX_UUID      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_TX_UUID      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

let rxChar = null;

const logEl = document.getElementById("log");
const statusDot = document.getElementById("statusDot");
const micButton = document.getElementById("micButton");

function log(...args) {
  logEl.textContent += args.join(" ") + "\n";
}

// ------------------------------------------------------------
// CONNECT
// ------------------------------------------------------------
async function connect() {
  try {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: "RoBoHoN-C3" }],
      optionalServices: [NUS_SERVICE_UUID]
    });

    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(NUS_SERVICE_UUID);

    rxChar = await service.getCharacteristic(NUS_RX_UUID);
    const txChar = await service.getCharacteristic(NUS_TX_UUID);

    await txChar.startNotifications();
    txChar.addEventListener("characteristicvaluechanged", e => {
      const text = new TextDecoder().decode(e.target.value);
      log("RX:", text.trim());
    });

    statusDot.classList.remove("gray");
    statusDot.classList.add("green");
    log("Connected");
  } catch (err) {
    log("Connect error:", err);
  }
}

// ------------------------------------------------------------
// SEND JSON COMMAND
// ------------------------------------------------------------
async function sendJson(obj) {
  if (!rxChar) return log("Not connected");
  const line = JSON.stringify(obj) + "\n";
  await rxChar.writeValue(new TextEncoder().encode(line));
  log("TX:", line.trim());
}

// ------------------------------------------------------------
// PLAY BUTTONS (slots 1–8)
// ------------------------------------------------------------
document.querySelectorAll(".icon-btn").forEach(btn => {
  btn.onclick = () => {
    const slot = Number(btn.dataset.slot);
    sendJson({ cmd: "play", slot });
  };
});

// ------------------------------------------------------------
// NEW: Play Slot 1 Button
// ------------------------------------------------------------
document.getElementById("playSlot1").onclick = () => {
  const slot = Number(document.getElementById("motionSlot").value);
  sendJson({ cmd: "play", slot });
};

// ------------------------------------------------------------
// MOTION UPLOAD
// ------------------------------------------------------------
async function uploadMotionToRobot(slot, jsonText) {
  if (!rxChar) return log("Not connected");

  const encoder = new TextEncoder();
  const CHUNK = 150;

  log(`Starting upload to slot ${slot}...`);

  // Start upload
  await rxChar.writeValue(encoder.encode(`UPLOAD ${slot}\n`));

  // Send JSON in chunks
  for (let i = 0; i < jsonText.length; i += CHUNK) {
    const chunk = jsonText.slice(i, i + CHUNK);
    await rxChar.writeValue(encoder.encode(chunk));
    log(`Sent chunk ${i / CHUNK + 1}`);
  }

  // End upload
  await rxChar.writeValue(encoder.encode("\nEND\n"));

  log("Upload complete");
}

document.getElementById("uploadMotion").onclick = async () => {
  const fileInput = document.getElementById("motionFile");
  const slot = Number(document.getElementById("motionSlot").value);

  if (!fileInput.files.length) return log("No file selected");

  const text = await fileInput.files[0].text();
  uploadMotionToRobot(slot, text);
};

// ------------------------------------------------------------
// VOICE RECOGNITION
// ------------------------------------------------------------
let recognition = null;

if ("webkitSpeechRecognition" in window) {
  recognition = new webkitSpeechRecognition();
  recognition.lang = "en-US";
  recognition.interimResults = false;

  recognition.onresult = (event) => {
    const text = event.results[0][0].transcript;
    log("Voice:", text);
    sendJson({ cmd: "voice", text });
  };
}

micButton.onmousedown = () => {
  if (recognition) recognition.start();
  micButton.style.background = "#ff4d4d";
};

micButton.onmouseup = () => {
  if (recognition) recognition.stop();
  micButton.style.background = "#d62828";
};

// ------------------------------------------------------------
// FOOTER BUTTONS
// ------------------------------------------------------------
document.getElementById("connect").onclick = connect;

document.getElementById("joystick").onclick = () =>
  alert("Joystick coming soon");

document.getElementById("advanced").onclick = () =>
  window.location.href = "../motion-editor/index.html";
