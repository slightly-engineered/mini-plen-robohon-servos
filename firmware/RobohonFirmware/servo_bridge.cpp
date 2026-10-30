#include "servo_bridge.h"
#include "servo_bus.h"
#include "ble_uart.h"
#include "wifi_ws.h"
#include <Arduino.h>
#include <string.h>

static bool g_bridgeMode = false;
static bool g_sendHomeOnEnable = false;

static uint8_t frameBuf[30];
static int frameIdx = 0;

void servoBridgeEnable() {
    if (!g_bridgeMode) g_sendHomeOnEnable = true;
    g_bridgeMode = true;
    frameIdx = 0;
    Serial.println("[BRIDGE] ENABLED");
}

void servoBridgeDisable() {
    g_bridgeMode = false;
    frameIdx = 0;
    g_sendHomeOnEnable = false;
    Serial.println("[BRIDGE] DISABLED");
}

bool servoBridgeActive() {
    return g_bridgeMode;
}

static bool frameValid(const uint8_t* f) {
    if (f[0] != 0x77 || f[1] != 0x80) return false;
    uint32_t sum = 0;
    for (int i = 6; i < 29; i++) sum += f[i];
    uint8_t expected = (uint8_t)((256 - (sum & 0xFF)) & 0xFF);
    return f[29] == expected;
}

void servoBridgePushByte(uint8_t b) {
    if (!g_bridgeMode) return;

    if (frameIdx == 0) {
        if (b != 0x77) return;
        frameBuf[frameIdx++] = b;
        return;
    }

    if (frameIdx == 1) {
        if (b != 0x80) {
            frameIdx = (b == 0x77) ? 1 : 0;
            frameBuf[0] = 0x77;
            return;
        }
        frameBuf[frameIdx++] = b;
        return;
    }

    frameBuf[frameIdx++] = b;
    if (frameIdx < 30) return;

    if (!frameValid(frameBuf)) {
        Serial.println("[BRIDGE] drop bad frame, hunting 77 80");
        int keep = 0;
        for (int i = 1; i < 30; i++) {
            if (frameBuf[i] == 0x77 && (i + 1 >= 30 || frameBuf[i + 1] == 0x80 || i + 1 == 30)) {
                keep = 30 - i;
                memmove(frameBuf, frameBuf + i, keep);
                break;
            }
        }
        frameIdx = keep;
        return;
    }

    frameIdx = 0;

    BusID bus = (BusID)frameBuf[2];
    if (bus > BUS_SHOULDER_ID) bus = BUS_LEG_ID;

    static uint32_t okCount = 0;
    if ((++okCount % 15) == 1) {
        Serial.printf("[BRIDGE] frame #%lu bus=%u (0=LEG/GPIO4 1=HEAD/GPIO6 2=SHOULDER/GPIO5)\n",
                      (unsigned long)okCount, (unsigned)bus);
    }

    servoSendRawFrame(bus, frameBuf, 30);
}

static String usbLine;

void servoBridgePollUsb() {
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();

        if (g_bridgeMode) {
            servoBridgePushByte(b);
            continue;
        }

        if (b == 0x77) {
            servoBridgeEnable();
            servoBridgePushByte(b);
            continue;
        }

        if (b == '\n' || b == '\r') {
            usbLine.trim();
            if (usbLine.equalsIgnoreCase("bridge on")) {
                servoBridgeEnable();
            } else if (usbLine.equalsIgnoreCase("bridge off")) {
                servoBridgeDisable();
            }
            usbLine = "";
        } else if (b >= 32 && b <= 126) {
            if (usbLine.length() < 64) usbLine += (char)b;
        } else {
            usbLine = "";
        }
    }
}

void servoBridgePoll() {
    if (g_sendHomeOnEnable) {
        g_sendHomeOnEnable = false;
        Serial.println("[BRIDGE] TX verified home frames (LEG GPIO4, HEAD GPIO6, SHOULDER GPIO5)");
        servoSendHomeOnce();
    }

    if (!g_bridgeMode) return;

    while (bleUartAvailable()) {
        servoBridgePushByte(bleUartRead());
    }

    uint8_t b;
    while (wifiWsReadByte(&b)) {
        servoBridgePushByte(b);
    }
}