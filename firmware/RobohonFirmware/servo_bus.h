#pragma once
#include <Arduino.h>

// ============================================================
// Servo count (LEG bus now has 6 servos)
// ============================================================
#define SERVO_COUNT 6

// ============================================================
// Bus IDs
// ============================================================
enum BusID {
    BUS_LEG_ID = 0,
    BUS_HEAD_ID = 1,
    BUS_SHOULDER_ID = 2
};

// ============================================================
// Servo ID constants (recommended)
// ============================================================
enum {
    SID_LEFT_HIP     = 1,
    SID_LEFT_FOOT    = 2,
    SID_LEFT_ANKLE   = 3,
    SID_RIGHT_HIP    = 4,
    SID_RIGHT_FOOT   = 5,
    SID_RIGHT_ANKLE  = 6   // NEW — trailer channel promoted
};

// ============================================================
// Public API (servo bus only)
// ============================================================
void servoBusInit();
int16_t servoClamp(uint8_t sid, BusID bus, int16_t val);

void servoSendHome(BusID bus);
void servoSendHomeOnce();
void servoRelease(BusID bus);
void servoRelease();

// Updated to explicitly require 6‑servo arrays
void servoWriteFrame(BusID bus, const int16_t positions[SERVO_COUNT]);
void servoWriteFrame(const int16_t positions[SERVO_COUNT]);

void servoSendRawFrame(BusID bus, uint8_t* buf, size_t len);
