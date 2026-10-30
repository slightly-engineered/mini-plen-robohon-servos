import { ServoRig } from "./servoRig.js";

export const ServoOrder = [
  "LEFT_SHOULDER",
  "RIGHT_SHOULDER",
  "NECK_TURN",
  "HEAD_NOD",
  "HEAD_TILT",
  "LEFT_ELBOW",
  "RIGHT_ELBOW",
  "LEFT_HIP",
  "LEFT_FOOT",
  "LEFT_ANKLE",
  "RIGHT_HIP",
  "RIGHT_FOOT"
];

export function importMotion(firmwareMotion) {
  const frames = firmwareMotion.frames.map((frame, index) => {
    const servos = { leg: {}, head: {}, shoulder: {} };

    for (const servoName of ServoOrder) {
      const raw = frame.angles[servoName] ?? 0;
      const rig = ServoRig[servoName];

      const busName =
        rig.bus === 0 ? "shoulder" :
        rig.bus === 1 ? "head" :
        "leg";

      servos[busName][rig.sid] = raw;
    }

    return {
      index,
      transition_time_ms: frame.transition,
      servos
    };
  });

  return {
    name: firmwareMotion.name ?? "Imported Motion",
    slot: firmwareMotion.slot ?? 0,
    version: "1.0",
    target: "RoBoHoN",
    codes: [],
    frames
  };
}
