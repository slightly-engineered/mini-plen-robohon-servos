#pragma once
#include <Arduino.h>
#include <vector>

// ============================================================
// Total number of servos in the robot
// SHOULDER: 2
// HEAD/ELBOW: 5
// LEG: 6 (including new SID6: Right Ankle)
// ============================================================
#define NUM_SERVOS 13

// One frame of motion
struct Frame {
    uint16_t transition;        // milliseconds
    float angles[NUM_SERVOS];   // normalized angles (-1..+1 or degrees)
};

// One full motion
struct Motion {
    uint8_t frameCount = 0;
    std::vector<Frame> frames;

    bool loop = false;
    uint8_t loopStart = 0;
    uint8_t loopEnd = 0;
    uint8_t loopCount = 0;      // 0 = infinite
};

// Initialize filesystem (LittleFS)
bool motionStorageInit();

// Save a Motion into a slot
bool motionStorageSave(uint8_t slot, const Motion& motion);

// Load a Motion from a slot
bool motionStorageLoad(uint8_t slot, Motion& outMotion);

// List all slots that contain motions
std::vector<uint8_t> motionStorageList();

// Delete a slot
bool motionStorageDelete(uint8_t slot);
