#pragma once
#include <stdint.h>
#include <stddef.h>
#include "servo_bus.h"

// Enable/disable bridge mode
void servoBridgeEnable();
void servoBridgeDisable();
bool servoBridgeActive();

// Push a single raw byte from ANY transport (USB, BLE, WiFi)
void servoBridgePushByte(uint8_t b);

// Always drain USB: auto-enables bridge on 0x77 frame header or "bridge on"
void servoBridgePollUsb();

// Drain BLE + WiFi into the frame assembler (call when bridge is active)
void servoBridgePoll();
