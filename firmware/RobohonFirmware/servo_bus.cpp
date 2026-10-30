#include "servo_bus.h"
#include "config.h"
#include <HardwareSerial.h>
#include "esp_rom_sys.h"

// ============================================================
// UART instances
// ============================================================
HardwareSerial LegUART(1);   // UART1 for LEG bus
// UART0 is Serial

// ============================================================
// Verified 30-byte HOME FRAMES (updated trailer = SID6 home)
// ============================================================
static const uint8_t LEG_HOME_FRAME[30] = {
    0x77,0x80,0x00,0x03,0x06,0x00,
    0x9F,0x05,0x40,0x01,
    0xE6,0xF6,0x40,0x02,
    0xDC,0xFF,0x40,0x03,
    0x1D,0x06,0x40,0x04,
    0x18,0xFB,0x40,0x05,
    0x6D,0x00,0x40,      // SID6 (R. Ankle)
    0x73
};

static const uint8_t HEAD_HOME_FRAME[30] = {
    0x77,0x80,0x00,0x03,0x06,0x00,
    0xEF,0x00,0x40,0x01,
    0x1E,0xF6,0x40,0x02,
    0xDE,0x08,0x40,0x03,
    0x1D,0x06,0x40,0x04,
    0x18,0xFB,0x40,0x05,
    0x6D,0x00,0x40,
    0xE5
};

static const uint8_t SHOULDER_HOME_FRAME[30] = {
    0x77,0x80,0x00,0x03,0x06,0x00,
    0x17,0x0B,0x40,0x01,
    0xE6,0xF6,0x40,0x02,
    0xDC,0xFF,0x40,0x03,
    0x1D,0x06,0x40,0x04,
    0x18,0xFB,0x40,0x05,
    0x6D,0x00,0x40,
    0xF5
};

// ============================================================
// Bit-bang UART for HEAD and SHOULDER buses
// ============================================================
static const uint32_t BIT_US = 8;

static inline void IRAM_ATTR bitBangByte(int txPin, uint8_t b) {
    noInterrupts();
    digitalWrite(txPin, LOW);
    esp_rom_delay_us(BIT_US);

    for (int i = 0; i < 8; i++) {
        digitalWrite(txPin, (b >> i) & 1);
        esp_rom_delay_us(BIT_US);
    }

    digitalWrite(txPin, HIGH);
    esp_rom_delay_us(BIT_US);
    interrupts();
}

static void IRAM_ATTR bitBangSend(int txPin, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++)
        bitBangByte(txPin, data[i]);
}

// ============================================================
// Checksum: sum bytes [6..28], then (256 - sum) & 0xFF
// ============================================================
static uint8_t calcChecksum(const uint8_t* frame) {
    uint32_t sum = 0;
    for (int i = 6; i < 29; i++) sum += frame[i];
    return (uint8_t)((256 - (sum & 0xFF)) & 0xFF);
}

// ============================================================
// Frame routing — always transmits with buf[2] = 0x00
// ============================================================
static void sendRawFrame(BusID bus, uint8_t* buf, size_t len) {
    buf[2] = 0x00;                  // Sharp servos require 0x00
    buf[len - 1] = calcChecksum(buf);

    switch (bus) {
        case BUS_HEAD_ID:     bitBangSend(HEAD_BUS_TX_PIN, buf, len); break;
        case BUS_LEG_ID:      LegUART.write(buf, len); LegUART.flush(); break;
        case BUS_SHOULDER_ID: bitBangSend(SHOULDER_BUS_TX_PIN, buf, len); break;
    }
}

// ============================================================
// Unified 30-byte frame builder (supports 6 servos)
// ============================================================
static void buildFrame(uint8_t* buf, const int16_t* positions, bool relax) {
    buf[0] = 0x77;
    buf[1] = 0x80;
    buf[2] = 0x00;
    buf[3] = 0x03;
    buf[4] = 0x06;   // 6 servos
    buf[5] = 0x00;

    int idx = 6;

    // SID1–SID5 (normal Sharp protocol)
    for (uint8_t sid = 1; sid <= 5; sid++) {
        if (relax) {
            buf[idx++] = 0x00;
            buf[idx++] = 0x00;
            buf[idx++] = 0xFF;
            buf[idx++] = sid;
        } else {
            int16_t p = positions[sid - 1];
            buf[idx++] = p & 0xFF;
            buf[idx++] = (p >> 8) & 0xFF;
            buf[idx++] = 0x40;
            buf[idx++] = sid;
        }
    }

    // SID6 (R. Ankle) — positional, no ID byte
    if (relax) {
        buf[26] = 0x00;
        buf[27] = 0x00;
        buf[28] = 0xFF;
    } else {
        int16_t p6 = positions[5];
        buf[26] = p6 & 0xFF;
        buf[27] = (p6 >> 8) & 0xFF;
        buf[28] = 0x40;
    }

    buf[29] = calcChecksum(buf);
}

// ============================================================
// Two-stage relax (correct Sharp behavior)
// ============================================================
static void buildFrameStage1Relax(uint8_t* buf) {
    buf[0] = 0x77;
    buf[1] = 0x80;
    buf[2] = 0x00;
    buf[3] = 0x03;
    buf[4] = 0x06;
    buf[5] = 0x00;

    int idx = 6;

    // SID1–SID5 relaxed
    for (uint8_t sid = 1; sid <= 5; sid++) {
        buf[idx++] = 0x00;
        buf[idx++] = 0x00;
        buf[idx++] = 0xFF;
        buf[idx++] = sid;
    }

    // SID6 still held at home (6D 00 40)
    buf[26] = 0x6D;
    buf[27] = 0x00;
    buf[28] = 0x40;

    buf[29] = calcChecksum(buf);
}

static void buildFrameFullRelax(uint8_t* buf) {
    buf[0] = 0x77;
    buf[1] = 0x80;
    buf[2] = 0x00;
    buf[3] = 0x03;
    buf[4] = 0x06;
    buf[5] = 0x00;

    int idx = 6;

    // SID1–SID5 relaxed
    for (uint8_t sid = 1; sid <= 5; sid++) {
        buf[idx++] = 0x00;
        buf[idx++] = 0x00;
        buf[idx++] = 0xFF;
        buf[idx++] = sid;
    }

    // SID6 relaxed (correct stage 2)
    buf[26] = 0x00;
    buf[27] = 0x00;
    buf[28] = 0xFF;

    buf[29] = calcChecksum(buf);
}

// ============================================================
// Public API
// ============================================================
void servoBusInit() {
    pinMode(LEG_BUS_TX_PIN, OUTPUT);
    digitalWrite(LEG_BUS_TX_PIN, HIGH);
    pinMode(HEAD_BUS_TX_PIN, OUTPUT);
    digitalWrite(HEAD_BUS_TX_PIN, HIGH);
    pinMode(SHOULDER_BUS_TX_PIN, OUTPUT);
    digitalWrite(SHOULDER_BUS_TX_PIN, HIGH);

    // RX pin is unused but required so UART1 actually attaches TX on ESP32-S3
    LegUART.setTxBufferSize(256);
    LegUART.begin(SERVO_UART_BAUD, SERIAL_8N1, LEG_BUS_RX_UNUSED_PIN, LEG_BUS_TX_PIN);

    Serial.printf("Servo buses: LEG UART1 TX GPIO %d, SHOULDER GPIO %d, HEAD GPIO %d\n",
                  LEG_BUS_TX_PIN, SHOULDER_BUS_TX_PIN, HEAD_BUS_TX_PIN);
}

void servoSendHome(BusID bus) {
    const uint8_t* frame = nullptr;

    switch (bus) {
        case BUS_LEG_ID:      frame = LEG_HOME_FRAME;      break;
        case BUS_HEAD_ID:     frame = HEAD_HOME_FRAME;     break;
        case BUS_SHOULDER_ID: frame = SHOULDER_HOME_FRAME; break;
    }

    uint8_t buf[30];
    for (int i = 0; i < 10; i++) {
        memcpy(buf, frame, 30);
        sendRawFrame(bus, buf, 30);
        delay(30);
    }
}

void servoSendHomeOnce() {
    uint8_t buf[30];
    memcpy(buf, LEG_HOME_FRAME, 30);
    sendRawFrame(BUS_LEG_ID, buf, 30);
    memcpy(buf, HEAD_HOME_FRAME, 30);
    sendRawFrame(BUS_HEAD_ID, buf, 30);
    memcpy(buf, SHOULDER_HOME_FRAME, 30);
    sendRawFrame(BUS_SHOULDER_ID, buf, 30);
}

void servoRelease(BusID bus) {
    uint8_t buf[30];

    // Stage 1: release SID1–SID5
    for (int i = 0; i < 3; i++) {
        buildFrameStage1Relax(buf);
        sendRawFrame(bus, buf, 30);
        delay(30);
    }

    // Stage 2: release SID6 (R. Ankle)
    for (int i = 0; i < 7; i++) {
        buildFrameFullRelax(buf);
        sendRawFrame(bus, buf, 30);
        delay(30);
    }
}

void servoRelease() {
    servoRelease(BUS_LEG_ID);
    servoRelease(BUS_HEAD_ID);
    servoRelease(BUS_SHOULDER_ID);
}

void servoWriteFrame(BusID bus, const int16_t* positions) {
    uint8_t buf[30];
    buildFrame(buf, positions, false);
    sendRawFrame(bus, buf, 30);
}

void servoWriteFrame(const int16_t* positions) {
    servoWriteFrame(BUS_LEG_ID, positions);
}

// ============================================================
// Public raw frame sender (for bridge passthrough)
// ============================================================
void servoSendRawFrame(BusID bus, uint8_t* buf, size_t len) {
    sendRawFrame(bus, buf, len);
}

// ============================================================
// Clamp implementation
// ============================================================
int16_t servoClamp(uint8_t sid, BusID bus, int16_t val) {
    struct LimitEntry {
        BusID   bus;
        uint8_t sid;
        int16_t mn;
        int16_t mx;
    };

    const LimitEntry limits[] = {
        { BUS_LEG_ID,      1,  LIMIT_LEFT_HIP_MIN,       LIMIT_LEFT_HIP_MAX      },
        { BUS_LEG_ID,      2,  LIMIT_LEFT_FOOT_MIN,      LIMIT_LEFT_FOOT_MAX     },
        { BUS_LEG_ID,      3,  LIMIT_LEFT_ANKLE_MIN,     LIMIT_LEFT_ANKLE_MAX    },
        { BUS_LEG_ID,      4,  LIMIT_RIGHT_HIP_MIN,      LIMIT_RIGHT_HIP_MAX     },
        { BUS_LEG_ID,      5,  LIMIT_RIGHT_FOOT_MIN,     LIMIT_RIGHT_FOOT_MAX    },
        { BUS_LEG_ID,      6,  LIMIT_RIGHT_ANKLE_MIN,    LIMIT_RIGHT_ANKLE_MAX   },

        { BUS_HEAD_ID,     1,  LIMIT_NECK_TURN_MIN,      LIMIT_NECK_TURN_MAX     },
        { BUS_HEAD_ID,     2,  LIMIT_HEAD_NOD_MIN,       LIMIT_HEAD_NOD_MAX      },
        { BUS_HEAD_ID,     3,  LIMIT_HEAD_TILT_MIN,      LIMIT_HEAD_TILT_MAX     },
        { BUS_HEAD_ID,     4,  LIMIT_LEFT_ELBOW_MIN,     LIMIT_LEFT_ELBOW_MAX    },
        { BUS_HEAD_ID,     5,  LIMIT_RIGHT_ELBOW_MIN,    LIMIT_RIGHT_ELBOW_MAX   },

        { BUS_SHOULDER_ID, 1,  LIMIT_LEFT_SHOULDER_MIN,  LIMIT_LEFT_SHOULDER_MAX },
        { BUS_SHOULDER_ID, 2,  LIMIT_RIGHT_SHOULDER_MIN, LIMIT_RIGHT_SHOULDER_MAX}
    };

    for (const auto& l : limits) {
        if (l.bus == bus && l.sid == sid) {
            if (val < l.mn) return l.mn;
            if (val > l.mx) return l.mx;
            return val;
        }
    }

    return val;
}
