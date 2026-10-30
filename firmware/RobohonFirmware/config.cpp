#include "config.h"

// Servo index order (13):
//  0: LEFT_SHOULDER
//  1: RIGHT_SHOULDER
//  2: NECK_TURN
//  3: HEAD_NOD
//  4: HEAD_TILT
//  5: LEFT_ELBOW
//  6: RIGHT_ELBOW
//  7: LEFT_HIP
//  8: LEFT_FOOT
//  9: LEFT_ANKLE
// 10: RIGHT_HIP
// 11: RIGHT_FOOT
// 12: RIGHT_ANKLE   <-- NEW

// ============================================================
// Bus mapping for each servo (which bus it belongs to)
// ============================================================
const uint8_t SERVO_BUS_MAP[NUM_SERVOS] = {
    BUS_SHOULDER,   // LEFT_SHOULDER
    BUS_SHOULDER,   // RIGHT_SHOULDER

    BUS_HEAD,       // NECK_TURN
    BUS_HEAD,       // HEAD_NOD
    BUS_HEAD,       // HEAD_TILT
    BUS_HEAD,       // LEFT_ELBOW
    BUS_HEAD,       // RIGHT_ELBOW

    BUS_LEG,        // LEFT_HIP
    BUS_LEG,        // LEFT_FOOT
    BUS_LEG,        // LEFT_ANKLE
    BUS_LEG,        // RIGHT_HIP
    BUS_LEG,        // RIGHT_FOOT
    BUS_LEG         // RIGHT_ANKLE (NEW)
};

// ============================================================
// Servo ID mapping (SID number on each bus)
// ============================================================
// These must match the Sharp protocol:
//  - Shoulder bus: SIDs 1–2
//  - Head bus:     SIDs 1–5
//  - Leg bus:      SIDs 1–6 (NEW SID6 = Right Ankle)
const uint8_t SERVO_ID_MAP[NUM_SERVOS] = {
    1,  // LEFT_SHOULDER
    2,  // RIGHT_SHOULDER

    1,  // NECK_TURN
    2,  // HEAD_NOD
    3,  // HEAD_TILT
    4,  // LEFT_ELBOW
    5,  // RIGHT_ELBOW

    1,  // LEFT_HIP
    2,  // LEFT_FOOT
    3,  // LEFT_ANKLE
    4,  // RIGHT_HIP
    5,  // RIGHT_FOOT
    6   // RIGHT_ANKLE (NEW)
};

// ============================================================
// Human-readable servo names (used in JSON motion files)
// ============================================================
const char* servoNames[NUM_SERVOS] = {
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
    "RIGHT_FOOT",
    "RIGHT_ANKLE"   // NEW
};
