// core/robohonMap.js
// ------------------------------------------------------------
// Anim8 3D-model node  ->  RoBoHoN servo (bus + SID) mapping.
//
// Anim8 keeps its own rig (core/servoRig.js: node names, axes, +/-90 deg
// limits, 0.1 deg units). This file is the ONLY place that knows about the
// RoBoHoN hardware. Edit it to re-assign servos as the mini PLEN grows.
//
// Firmware bus IDs (config.h):  0 = LEG (GPIO4/UART1)
//                               1 = HEAD/ELBOW (GPIO6 bit-bang)
//                               2 = SHOULDER (GPIO5 bit-bang)
// ------------------------------------------------------------

export const Buses = { LEG: 0, HEAD: 1, SHOULDER: 2 };

// Raw servo counts per degree of joint rotation.
// Derived from the bench sweep: 64,000 raw counts ~= 330 deg of shaft travel.
// PROVISIONAL - tune per servo with `rawPerDeg` once measured.
export const RAW_PER_DEG = 194;

// Raw value sent for any SID on a bus that has no Anim8 joint assigned
// (R1 home captures; SID6 is the trailer channel). Keeps unused slots parked.
export const BUS_HOME_DEFAULTS = {
    [Buses.LEG]:      { 1: 1439, 2: -2330, 3: -36, 4: 1565, 5: -1256, 6: 109 },
    [Buses.HEAD]:     { 1: 239,  2: -2530, 3: 2270, 4: 1565, 5: -1256, 6: 109 },
    [Buses.SHOULDER]: { 1: 2839, 2: -2330, 3: -36, 4: 1565, 5: -1256, 6: 109 }
};

// ------------------------------------------------------------
// Hardware map. Key = GLB node / Anim8 ServoRig name.
//   tag      : tracker tag (R1 = Robohon 1 donor)
//   robohon  : firmware servo name of the donor actuator
//   bus, sid : where the donor actuator lives
//   home     : raw value at model angle 0   (PROVISIONAL = R1 home)
//   dir      : +1 / -1 hardware direction    (PROVISIONAL = +1, verify)
//   min,max  : raw clamp (R1 firmware limits; pitch servos = home +/- 90 deg)
//
// Donor sides are mounted reversed: PLEN left = R1 right servos and vice versa.
// Joints not listed here (legs) are virtual only - nothing is sent for them.
// ------------------------------------------------------------
export const HW = {
    neck_yaw: {
        tag: "RIN", robohon: "NECK_TURN",
        bus: Buses.HEAD, sid: 1,
        home: 239, dir: 1, min: -15361, max: 14839
    },

    // Shoulder PITCH: head-bus donors (Head Nod / Head Tilt).
    // R1 head limits are far too small for an arm swing, so the clamp is
    // home +/- 90 deg (17,460 raw) - matches Anim8's own +/-90 deg joint limit.
    left_shoulder_pitch: {
        tag: "RIHN", robohon: "HEAD_NOD",
        bus: Buses.HEAD, sid: 2,
        home: -2530, dir: 1, min: -19990, max: 14930
    },
    right_shoulder_pitch: {
        tag: "RIHT", robohon: "HEAD_TILT",
        bus: Buses.HEAD, sid: 3,
        home: 2270, dir: 1, min: -15190, max: 19730
    },

    // Shoulder ROLL: shoulder-bus donors, side-swapped.
    left_shoulder_roll: {
        tag: "RIRS", robohon: "RIGHT_SHOULDER",
        bus: Buses.SHOULDER, sid: 2,
        home: -2330, dir: 1, min: -24330, max: 24070
    },
    right_shoulder_roll: {
        tag: "RILS", robohon: "LEFT_SHOULDER",
        bus: Buses.SHOULDER, sid: 1,
        home: 2839, dir: 1, min: -24161, max: 2439
    },

    // Elbows: head-bus donors, side-swapped.
    left_elbow_roll: {
        tag: "RIRE", robohon: "RIGHT_ELBOW",
        bus: Buses.HEAD, sid: 5,
        home: -1256, dir: 1, min: -16256, max: 1744
    },
    right_elbow_roll: {
        tag: "RILE", robohon: "LEFT_ELBOW",
        bus: Buses.HEAD, sid: 4,
        home: 1565, dir: 1, min: -1835, max: 15965
    }
};

// Factory values, captured before any saved calibration is applied.
const FACTORY = {};
for (const n in HW) FACTORY[n] = { home: HW[n].home, dir: HW[n].dir };

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

// Buses that currently have at least one Anim8 joint assigned.
export function activeBuses() {
    return [...new Set(Object.values(HW).map(c => c.bus))];
}

// { sid: anim8Name } for one bus
export function busLookup(bus) {
    const out = {};
    for (const n in HW) if (HW[n].bus === bus) out[HW[n].sid] = n;
    return out;
}

const clamp16 = v => Math.max(-32768, Math.min(32767, Math.round(v)));

// Anim8 angle (0.1 deg units) -> raw servo position
export function angleToRaw(name, angle) {
    const c = HW[name];
    if (!c) return 0;
    const deg = (angle ?? 0) / 10;
    let raw = c.home + c.dir * deg * (c.rawPerDeg ?? RAW_PER_DEG);
    const lo = Math.min(c.min, c.home);     // never clamp "home" itself away
    const hi = Math.max(c.max, c.home);
    raw = Math.max(lo, Math.min(hi, raw));
    return clamp16(raw);
}

export { clamp16 };

// ------------------------------------------------------------
// Calibration persistence (home + direction only), via localStorage.
// ------------------------------------------------------------
const CAL_KEY = "anim8_robohon_cal_v1";

export function loadCalibration() {
    try {
        const saved = JSON.parse(localStorage.getItem(CAL_KEY) || "{}");
        for (const n in saved) {
            if (!HW[n]) continue;
            if (Number.isFinite(saved[n].home)) HW[n].home = saved[n].home;
            if (saved[n].dir === 1 || saved[n].dir === -1) HW[n].dir = saved[n].dir;
        }
    } catch (e) {
        console.warn("Calibration load failed:", e);
    }
}

export function saveCalibration() {
    const out = {};
    for (const n in HW) out[n] = { home: HW[n].home, dir: HW[n].dir };
    try { localStorage.setItem(CAL_KEY, JSON.stringify(out)); }
    catch (e) { console.warn("Calibration save failed:", e); }
}

export function resetCalibration() {
    for (const n in HW) { HW[n].home = FACTORY[n].home; HW[n].dir = FACTORY[n].dir; }
    try { localStorage.removeItem(CAL_KEY); } catch (_) {}
}

// Paste-ready text for this file's HW table (home + dir only)
export function calibrationDump() {
    return Object.keys(HW)
        .map(n => `${n}: home ${HW[n].home}, dir ${HW[n].dir}`)
        .join("\n");
}
