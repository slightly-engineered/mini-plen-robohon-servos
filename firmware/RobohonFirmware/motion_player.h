#pragma once
#include <Arduino.h>
#include "motion_storage.h"
#include "servo_bus.h"

// Initialize player state
void motionPlayerInit();

// Load a motion into the player
void motionPlayerLoad(const Motion& motion);

// Start playback
void motionPlayerPlay();

// Stop playback
void motionPlayerStop();

// Check if playing
bool motionPlayerIsPlaying();

// Update playback (call frequently)
void motionPlayerUpdate();

// Convert normalized angle (-1..+1) → raw servo position
int16_t angleToPosition(float norm, uint8_t sid, BusID bus);

// Build a full 6-slot frame for a specific bus.
// out MUST hold SERVO_COUNT (6) entries. Slots with no servo on this bus
// are filled with the verified Sharp filler values, never left uninitialized.
void buildBusFrame(BusID bus, const float* allAngles, int16_t out[SERVO_COUNT]);
