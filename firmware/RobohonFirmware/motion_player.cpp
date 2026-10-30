#include "motion_player.h"
#include "servo_bus.h"
#include "config.h"
#include <Arduino.h>
#include "motion_storage.h"

// ============================================================
// Internal State
// ============================================================

static Motion currentMotion;
static bool playing = false;

static uint8_t currentFrame = 0;
static uint8_t nextFrame = 1;

static unsigned long frameStartTime = 0;

static int loopRemaining = 0;   // -1 = infinite, 0 = done, >0 = count remaining

// ============================================================
// Per-servo spec (normalized -1..+1)
// ============================================================

struct ServoSpec {
    BusID   bus;
    uint8_t sid;
    int16_t home;
    int32_t dPos;
    int32_t dNeg;
};

static const ServoSpec SERVO_SPECS[NUM_SERVOS] = {

    // SHOULDER BUS
    { BUS_SHOULDER_ID, 1,  2839,  21800, -27000 },
    { BUS_SHOULDER_ID, 2, -2330,  26400, -22000 },

    // HEAD / ELBOW BUS
    { BUS_HEAD_ID,     1,   239,  14600, -15400 },
    { BUS_HEAD_ID,     2, -2530,   3000,   -200 },
    { BUS_HEAD_ID,     3,  2270,   5400,  -4400 },
    { BUS_HEAD_ID,     4,  1565,  14400,  -3400 },
    { BUS_HEAD_ID,     5, -1256,   3000, -15000 },

    // LEG BUS (now 6 servos)
    { BUS_LEG_ID,      1,  1439,  20000, -19400 },
    { BUS_LEG_ID,      2, -2330,  15000, -18200 },
    { BUS_LEG_ID,      3,   -36,   1800,  -5600 },
    { BUS_LEG_ID,      4,  1565,  17400, -22400 },
    { BUS_LEG_ID,      5, -1256,  18200, -14000 },

    // NEW: Right Ankle (SID6)
    { BUS_LEG_ID,      6,   109,   5400,  -5600 }   // range based on your sweep
};

// ============================================================
// Normalized angle → raw servo position
// ============================================================

int16_t angleToPosition(float norm, uint8_t sid, BusID bus)
{
    for (int i = 0; i < NUM_SERVOS; i++) {
        const ServoSpec& s = SERVO_SPECS[i];

        if (s.bus == bus && s.sid == sid) {

            float delta = (norm >= 0.0f)
                ? norm * float(s.dPos)
                : norm * float(-s.dNeg);

            int16_t raw = s.home + int16_t(delta);
            return servoClamp(sid, bus, raw);
        }
    }

    return 0;
}

// ============================================================
// Build frame for a specific bus
// SHOULDER: 2 servos (SID1-2)
// HEAD:     5 servos (SID1-5)
// LEG:      6 servos (SID1-6)
//
// out[] ALWAYS holds SERVO_COUNT (6) entries. Slots without a servo on
// this bus carry the verified Sharp filler values (same values as the
// home frames in servo_bus.cpp and SHARP_FILLER in exporter.js).
// SID1/SID2 exist on every bus, so their filler is never transmitted.
// ============================================================

static const int16_t SLOT_FILLER[SERVO_COUNT] = { 0, 0, -36, 1565, -1256, 109 };

void buildBusFrame(BusID bus, const float* allAngles, int16_t* out)
{
    for (int i = 0; i < SERVO_COUNT; i++) out[i] = SLOT_FILLER[i];

    for (int i = 0; i < NUM_SERVOS; i++) {
        if (SERVO_SPECS[i].bus != bus) continue;

        uint8_t sid = SERVO_SPECS[i].sid;
        if (sid < 1 || sid > SERVO_COUNT) continue;

        out[sid - 1] = angleToPosition(allAngles[i], sid, bus);
    }
}

// ============================================================
// Internal: send home frames + relax
// ============================================================

static void sendHomeSequence()
{
    servoSendHome(BUS_LEG_ID);
    servoSendHome(BUS_HEAD_ID);
    servoSendHome(BUS_SHOULDER_ID);

    servoRelease();
}

// ============================================================
// Public API
// ============================================================

void motionPlayerInit() {
    playing = false;
}

void motionPlayerLoad(const Motion& motion) {
    currentMotion = motion;
    currentFrame = 0;
    nextFrame = (motion.frameCount > 1) ? 1 : 0;
}

void motionPlayerPlay() {
    if (currentMotion.frameCount == 0) return;

    playing = true;
    currentFrame = 0;
    nextFrame = (currentMotion.frameCount > 1) ? 1 : 0;
    frameStartTime = millis();

    // Set up loop counter
    if (currentMotion.loop) {
        loopRemaining = (currentMotion.loopCount == 0)
            ? -1                          // 0 = infinite
            : currentMotion.loopCount;
    } else {
        loopRemaining = 0;
    }

    Serial.printf("[PLAYER] Playing %d frames, loop=%d loopStart=%d loopEnd=%d loopCount=%d\n",
        currentMotion.frameCount,
        currentMotion.loop,
        currentMotion.loopStart,
        currentMotion.loopEnd,
        currentMotion.loopCount);
}

void motionPlayerStop() {
    playing = false;
    loopRemaining = 0;
    sendHomeSequence();
}

bool motionPlayerIsPlaying() {
    return playing;
}

void motionPlayerUpdate() {
    if (!playing) return;
    if (currentMotion.frameCount < 2) return;

    unsigned long now = millis();

    Frame& f0 = currentMotion.frames[currentFrame];
    Frame& f1 = currentMotion.frames[nextFrame];

    float t = float(now - frameStartTime) / float(f0.transition);

    if (t >= 1.0f) {
        currentFrame = nextFrame;
        nextFrame++;

        bool atLoopEnd = currentMotion.loop && (currentFrame >= currentMotion.loopEnd);
        bool atMotionEnd = (nextFrame >= currentMotion.frameCount);

        Serial.printf("[LOOP] currentFrame=%d loopEnd=%d loopRemaining=%d atLoopEnd=%d atMotionEnd=%d\n",
            currentFrame, currentMotion.loopEnd, loopRemaining, atLoopEnd, atMotionEnd);

        if (atLoopEnd) {
            if (loopRemaining > 0) loopRemaining--;

            if (loopRemaining == 0) {
                playing = false;
                sendHomeSequence();
                return;
            } else {
                nextFrame = currentMotion.loopStart;
            }
        } else if (atMotionEnd) {
            playing = false;
            sendHomeSequence();
            return;
        }

        frameStartTime = now;
        t = 0.0f;
    }

    // Interpolate
    float interpAngles[NUM_SERVOS];
    for (int i = 0; i < NUM_SERVOS; i++) {
        float a0 = f0.angles[i];
        float a1 = f1.angles[i];
        interpAngles[i] = a0 + (a1 - a0) * t;
    }

    int16_t frameShoulder[SERVO_COUNT];
    int16_t frameHead[SERVO_COUNT];
    int16_t frameLeg[SERVO_COUNT];

    buildBusFrame(BUS_SHOULDER_ID, interpAngles, frameShoulder);
    buildBusFrame(BUS_HEAD_ID,     interpAngles, frameHead);
    buildBusFrame(BUS_LEG_ID,      interpAngles, frameLeg);

    servoWriteFrame(BUS_SHOULDER_ID, frameShoulder);
    servoWriteFrame(BUS_HEAD_ID,     frameHead);
    servoWriteFrame(BUS_LEG_ID,      frameLeg);
}
