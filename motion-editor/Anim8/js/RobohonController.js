// js/RobohonController.js  (Anim8 edition)
// Drives RoBoHoN servo buses from Anim8's rig.
//
//  - Anim8 angles are 0.1 deg ints (same as PlenUsbController used).
//  - core/robohonMap.js maps each GLB node to a bus + SID and converts
//    angle -> raw servo position.
//  - Wire protocol is identical to the RoBoHoN Motion Editor: one 30-byte
//    "77 80" frame per bus, sent over USB (bridge on) / BLE NUS / WiFi WebSocket.
import {
    HW, Buses, BUS_HOME_DEFAULTS, activeBuses, busLookup, angleToRaw, clamp16
} from "../core/robohonMap.js";

const NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

export class RobohonController {

    constructor() {
        // USB transport
        this.port   = null;
        this.writer = null;

        // BLE transport
        this.bleDevice  = null;
        this.bleServer  = null;
        this.bleService = null;
        this.bleRxChar  = null;
        this.bleTxChar  = null;

        // Wi-Fi WebSocket transport
        this.socket = null;

        // Device debug output (USB)
        this._reader            = null;
        this._rxLines           = [];
        this._handledPorts      = new WeakSet();
        this.debugBridgeFrames  = false;

        // Frame routing / latest-state pump
        this._currentBusId = null;
        this._pending      = new Set();   // bus ids waiting to be sent
        this._sending      = false;

        // State
        this.lastAngles = {};   // anim8 name -> angle (0.1 deg)
        this.rawHold    = {};   // anim8 name -> raw override (calibration nudges)
    }

    /* =========================================================
       SERIAL CONNECT (WebSerial)
       ========================================================= */
    async connectSerial() {
        this.port = await navigator.serial.requestPort();
        await this._openSerialAndEnableBridge();
        console.log("Connected to ESP32 bridge (WebSerial OK)");
    }

    async _openSerialAndEnableBridge() {
        const openOpts = { baudRate: 115200, bufferSize: 1024, flowControl: "none" };

        await this.port.open(openOpts);
        try {
            await this.port.setSignals({ dataTerminalReady: true, requestToSend: false });
        } catch (_) { /* not all adapters support this */ }

        this._attachPortHandlers(this.port);

        this.writer = this.port.writable.getWriter();
        this._startSerialReader();

        // Opening the port often pulses DTR and reboots the ESP32.
        await new Promise((r) => setTimeout(r, 2500));

        const enable = new TextEncoder().encode("bridge on\n");
        try {
            await this.writer.write(enable);
        } catch (e) {
            console.warn("USB write after open failed, retrying (device may have re-enumerated):", e);
            try { this.writer.releaseLock(); } catch (_) {}
            await this._stopSerialReader();
            try { await this.port.close(); } catch (_) {}
            await new Promise((r) => setTimeout(r, 1500));

            const ports = await navigator.serial.getPorts();
            this.port = ports[0];
            if (!this.port) throw e;
            await this.port.open(openOpts);
            this._attachPortHandlers(this.port);
            this.writer = this.port.writable.getWriter();
            this._startSerialReader();
            await new Promise((r) => setTimeout(r, 500));
            await this.writer.write(enable);
        }

        await this.sendHomePose("USB");
    }

    /* =========================================================
       USB DEBUG: device Serial text -> console, disconnect dump
       ========================================================= */
    _stamp() {
        return new Date().toISOString().slice(11, 23);   // HH:MM:SS.mmm
    }

    _attachPortHandlers(port) {
        if (!port || this._handledPorts.has(port)) return;
        this._handledPorts.add(port);

        port.addEventListener("disconnect", () => {
            console.warn(`USB disconnected at ${this._stamp()}`);
            console.warn("Last device lines before disconnect:\n" +
                (this._rxLines.slice(-25).join("\n") || "(none received)"));
            if (this.port === port) {
                this.writer = null;
                this.port   = null;
            }
        });
    }

    _startSerialReader() {
        const port = this.port;
        if (!port?.readable || this._reader) return;

        let reader;
        try {
            reader = port.readable.getReader();
        } catch (e) {
            console.warn("Could not start serial reader:", e?.message || e);
            return;
        }
        this._reader = reader;

        const decoder = new TextDecoder();
        let partial = "";

        (async () => {
            try {
                while (true) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    if (!value) continue;

                    partial += decoder.decode(value, { stream: true });
                    let nl;
                    while ((nl = partial.indexOf("\n")) >= 0) {
                        this._onDeviceLine(partial.slice(0, nl).replace(/\r$/, ""));
                        partial = partial.slice(nl + 1);
                    }
                    if (partial.length > 4096) partial = "";   // binary-noise guard
                }
            } catch (e) {
                console.warn(`Serial read ended at ${this._stamp()}:`, e?.message || e);
            } finally {
                try { reader.releaseLock(); } catch (_) {}
                if (this._reader === reader) this._reader = null;
            }
        })();
    }

    async _stopSerialReader() {
        const r = this._reader;
        if (!r) return;
        try { await r.cancel(); } catch (_) {}
    }

    _onDeviceLine(line) {
        if (!line) return;
        if (!this.debugBridgeFrames && line.startsWith("[BRIDGE] frame #")) return;

        this._rxLines.push(`${this._stamp()} ${line}`);
        if (this._rxLines.length > 60) this._rxLines.shift();
        console.log("[ESP]", line);
    }

    isTransportReady() {
        const usbReady  = !!this.writer;
        const bleReady  = !!(this.bleRxChar && this.bleDevice?.gatt?.connected);
        const wifiReady = this.socket?.readyState === WebSocket.OPEN;
        return usbReady || bleReady || wifiReady;
    }

    async sendHomePose(via) {
        const zeros = {};
        for (const name in HW) zeros[name] = 0;
        console.log("Sending home pose via", via);
        await this.sendFullFrame(zeros);
    }

    /* =========================================================
       BLE CONNECT (WebBluetooth, NUS)
       ========================================================= */
    async connectBle() {
        this.bleDevice = await navigator.bluetooth.requestDevice({
            filters: [{ namePrefix: "RoBoHoN" }],
            optionalServices: [NUS_SERVICE_UUID]
        });

        this.bleDevice.addEventListener("gattserverdisconnected", () => {
            console.warn("BLE disconnected");
            this.bleServer  = null;
            this.bleService = null;
            this.bleRxChar  = null;
            this.bleTxChar  = null;
        });

        this.bleServer  = await this.bleDevice.gatt.connect();
        this.bleService = await this.bleServer.getPrimaryService(NUS_SERVICE_UUID);

        this.bleRxChar = await this.bleService.getCharacteristic(NUS_RX_CHAR_UUID);

        try {
            this.bleTxChar = await this.bleService.getCharacteristic(NUS_TX_CHAR_UUID);
            await this.bleTxChar.startNotifications();
            this.bleTxChar.addEventListener("characteristicvaluechanged", (ev) => {
                const v = ev.target.value;
                const text = new TextDecoder().decode(v);
                console.log("BLE RX line:", text.trim());
            });
        } catch (e) {
            console.warn("No TX characteristic or notifications not available:", e);
        }

        console.log("Connected to ESP32 bridge over BLE (NUS)");
    }

    /* =========================================================
       Wi‑Fi WebSocket CONNECT
       ========================================================= */
    async connectWifi(ip) {
        return new Promise((resolve, reject) => {
            const url = `ws://${ip}/ws`;
            console.log("Connecting WebSocket:", url);

            this.socket = new WebSocket(url);
            this.socket.binaryType = "arraybuffer";

            this.socket.onopen = () => {
                console.log("Connected to ESP32 bridge over Wi‑Fi WebSocket");
                resolve();
            };

            this.socket.onerror = (err) => {
                console.error("WebSocket error:", err);
                reject(err);
            };

            this.socket.onclose = () => {
                console.warn("WebSocket closed");
                this.socket = null;
            };

            this.socket.onmessage = (event) => {
                if (typeof event.data === "string") {
                    console.log("WS JSON:", event.data);
                } else {
                    console.log("WS binary:", new Uint8Array(event.data));
                }
            };
        });
    }

    /* =========================================================
       LOW-LEVEL WRITE (USB or BLE or Wi‑Fi)
       ========================================================= */
    async writePacket(packet) {
        const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);

        if ((this._txLogs || 0) < 9) {
            this._txLogs = (this._txLogs || 0) + 1;
            const hex = [...bytes].map((b) => b.toString(16).padStart(2, "0")).join(" ");
            console.log(`TX bus=${this._currentBusId} ${hex}`);
        }

        if (this.socket && this.socket.readyState === WebSocket.OPEN) {
            try {
                this.socket.send(bytes);
                return;
            } catch (e) {
                console.warn("WS write failed:", e);
            }
        }

        if (this.bleRxChar && this.bleDevice?.gatt?.connected) {
            try {
                await this.bleRxChar.writeValueWithoutResponse(bytes);
                return;
            } catch (e) {
                console.warn("BLE write failed:", e);
            }
        }

        if (this.writer) {
            try {
                await this.writer.write(bytes);
                return;
            } catch (e) {
                console.warn("USB write failed:", e);
            }
        }

        console.warn("writePacket: no active transport (WS, BLE, or USB)");
    }

    /* =========================================================
       RAW POSITION FOR ONE JOINT
       ========================================================= */
    getRaw(name) {
        if (this.rawHold[name] !== undefined) return this.rawHold[name];
        return angleToRaw(name, this.lastAngles[name] ?? 0);
    }

    /* =========================================================
       BUILD A 30-BYTE FRAME FOR ONE BUS
       Header 77 80 <bus> 03 06 00, five [pos_lo pos_hi 40 sid] slots,
       SID6 trailer [pos_lo pos_hi 40], checksum = (256 - sum(6..28)) & FF
       ========================================================= */
    buildBusFrame(busId) {
        const lookup   = busLookup(busId);
        const defaults = BUS_HOME_DEFAULTS[busId];
        const buf      = new Uint8Array(30);

        buf[0] = 0x77;
        buf[1] = 0x80;
        buf[2] = busId;      // 0=LEG, 1=HEAD, 2=SHOULDER
        buf[3] = 0x03;
        buf[4] = 0x06;       // ALWAYS 6 servos (Sharp protocol)
        buf[5] = 0x00;

        const posFor = (sid) => {
            const name = lookup[sid];
            return name ? this.getRaw(name) : defaults[sid];
        };

        let idx = 6;
        for (let sid = 1; sid <= 5; sid++) {
            const pos = posFor(sid);
            buf[idx++] = pos & 0xFF;
            buf[idx++] = (pos >> 8) & 0xFF;
            buf[idx++] = 0x40;
            buf[idx++] = sid;
        }

        const pos6 = posFor(6);
        buf[26] = pos6 & 0xFF;
        buf[27] = (pos6 >> 8) & 0xFF;
        buf[28] = 0x40;      // SID6 has no ID byte

        let sum = 0;
        for (let i = 6; i < 29; i++) sum += buf[i];
        buf[29] = (256 - (sum & 0xFF)) & 0xFF;

        return buf;
    }

    /* =========================================================
       PUBLIC SENDERS
       Only buses that have an Anim8 joint assigned are ever sent.
       ========================================================= */

    // One joint changed (live drag). The rest of its bus is re-sent at its
    // last known position, since the protocol always carries the whole bus.
    sendSingle(name, angle) {
        const c = HW[name];
        if (!c) return;                       // virtual joint (legs): ignore
        this.lastAngles[name] = angle;
        delete this.rawHold[name];
        this._queue([c.bus]);
    }

    // Whole pose (playback / frame select). angleMap = { anim8Name: angle }
    sendFullFrame(angleMap) {
        for (const name in angleMap) {
            if (!HW[name]) continue;
            this.lastAngles[name] = angleMap[name];
            delete this.rawHold[name];
        }
        this._queue(activeBuses());
    }

    // Calibration: drive one joint to an exact raw value
    sendRaw(name, raw) {
        const c = HW[name];
        if (!c) return;
        this.rawHold[name] = clamp16(raw);
        this._queue([c.bus]);
    }

    // Drop the calibration hold and go back to the model angle
    releaseRaw(name) {
        delete this.rawHold[name];
        const c = HW[name];
        if (c) this._queue([c.bus]);
    }

    _queue(buses) {
        for (const b of buses) this._pending.add(b);
        this._pump();
    }

    // Latest-state pump: coalesces rapid updates into one frame per bus.
    async _pump() {
        if (this._sending) return;
        this._sending = true;
        try {
            while (this._pending.size) {
                // SHOULDER, HEAD, LEG - same order as the Robohon editor
                const order = [Buses.SHOULDER, Buses.HEAD, Buses.LEG];
                const buses = order.filter(b => this._pending.has(b));
                this._pending.clear();

                for (const b of buses) {
                    this._currentBusId = b;
                    await this.writePacket(this.buildBusFrame(b));
                }
            }
        } finally {
            this._sending = false;
        }
    }
}
