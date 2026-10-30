#pragma once
#include <stdint.h>
#include "sdkconfig.h"   // CONFIG_IDF_TARGET_* macros

// ============================================================
// Hardware Configuration for ESP32‑S3 / ESP32‑C3 / ESP32‑C6
// ============================================================
#if CONFIG_IDF_TARGET_ESP32C6
  #define STATUS_LED_PIN  8     // C6 DevKitC-1 / C6-Zero onboard WS2812: verify against your board's pinout
#else
  #define STATUS_LED_PIN  10    // C3 Zero onboard WS2812
#endif
#define STATUS_LED_COUNT  1

// ============================================================
// Wi‑Fi Credentials (Multiple Networks Supported)
// ============================================================
// Wi-Fi credentials live in secrets.h (git-ignored). Copy secrets.example.h to secrets.h.
#include "secrets.h"

static const int WIFI_COUNT = sizeof(WIFI_SSIDS) / sizeof(WIFI_SSIDS[0]);

// ============================================================
// Servo buses (TX-only, idle HIGH, 8N1 ~115200)
// LEG      -> UART1 TX on GPIO 4
// SHOULDER -> bit-bang TX on GPIO 5
// HEAD     -> bit-bang TX on GPIO 6
// ============================================================
#define LEG_BUS_TX_PIN        4
#define SHOULDER_BUS_TX_PIN   5
#define HEAD_BUS_TX_PIN       6
#define LEG_BUS_RX_UNUSED_PIN 7   // UART1 driver needs an RX pin; not wired to servos
#define SERVO_UART_BAUD       115200

// ============================================================
// Servo Layout
// ============================================================

// 13 servos total:
// 2 shoulders
// 5 head/elbow
// 6 legs
#define NUM_SERVOS  13
#define NUM_BUSES   3

// Bus indices — MUST match servo_bus.h
#define BUS_LEG         0
#define BUS_HEAD        1
#define BUS_SHOULDER    2

// Timing
#define BUS_SWITCH_DELAY_US  50
#define FRAME_GAP_US         200

// Protocol
#define SERVO_FLAG_MOVE 0x40
#define SERVO_FLAG_FREE 0xFF
#define SERVO_FRAME_LEN 30

// ============================================================
// Home Positions (raw)
// ============================================================

// LEG
#define HOME_LEFT_HIP        1439
#define HOME_LEFT_FOOT      -2330
#define HOME_LEFT_ANKLE       -36
#define HOME_RIGHT_HIP       1565
#define HOME_RIGHT_FOOT     -1256
#define HOME_RIGHT_ANKLE      109

// SHOULDER
#define HOME_LEFT_SHOULDER    2839
#define HOME_RIGHT_SHOULDER  -2330

// HEAD / ELBOW
#define HOME_NECK_TURN         239
#define HOME_HEAD_NOD        -2530
#define HOME_HEAD_TILT        2270
#define HOME_LEFT_ELBOW       1565
#define HOME_RIGHT_ELBOW     -1256

// ============================================================
// Mechanical Limits (raw)
// ============================================================

// LEG
#define LIMIT_LEFT_HIP_MIN    -18235
#define LIMIT_LEFT_HIP_MAX     21439
#define LIMIT_LEFT_FOOT_MIN   -20530
#define LIMIT_LEFT_FOOT_MAX    12670
#define LIMIT_LEFT_ANKLE_MIN   -5636
#define LIMIT_LEFT_ANKLE_MAX    1764
#define LIMIT_RIGHT_HIP_MIN   -20835
#define LIMIT_RIGHT_HIP_MAX    18965
#define LIMIT_RIGHT_FOOT_MIN  -15256
#define LIMIT_RIGHT_FOOT_MAX   16944
#define LIMIT_RIGHT_ANKLE_MIN  -5600
#define LIMIT_RIGHT_ANKLE_MAX   5400

// SHOULDER
#define LIMIT_LEFT_SHOULDER_MIN   -24161
#define LIMIT_LEFT_SHOULDER_MAX    2439
#define LIMIT_RIGHT_SHOULDER_MIN  -24330
#define LIMIT_RIGHT_SHOULDER_MAX   24070

// HEAD / ELBOW
#define LIMIT_NECK_TURN_MIN      -15361
#define LIMIT_NECK_TURN_MAX       14839
#define LIMIT_HEAD_NOD_MIN        -2330
#define LIMIT_HEAD_NOD_MAX          470
#define LIMIT_HEAD_TILT_MIN       -2130
#define LIMIT_HEAD_TILT_MAX        7670
#define LIMIT_LEFT_ELBOW_MIN      -1835
#define LIMIT_LEFT_ELBOW_MAX      15965
#define LIMIT_RIGHT_ELBOW_MIN    -16256
#define LIMIT_RIGHT_ELBOW_MAX      1744

// ============================================================
// Servo Mapping (extern — defined in config.cpp)
// ============================================================
extern const uint8_t SERVO_BUS_MAP[NUM_SERVOS];
extern const uint8_t SERVO_ID_MAP[NUM_SERVOS];
extern const char*   servoNames[NUM_SERVOS];
