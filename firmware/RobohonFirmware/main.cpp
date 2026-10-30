#include <Arduino.h>
#include "config.h"
#include "servo_bus.h"
#include "motion_player.h"
#include "motion_storage.h"
#include "command_handler.h"
#include "wifi_ws.h"
#include "ble_uart.h"
#include "servo_bridge.h"
#include "esp_system.h"   // esp_reset_reason()
#include "esp_attr.h"     // RTC_NOINIT_ATTR

// 1 = start BLE UART, 0 = skip it (isolates BLE/WiFi radio contention)
#ifndef BLE_ENABLED
#define BLE_ENABLED 1
#endif

// =====================================================
// DEBUG: reset-reason history + heartbeat
//
// Why: a native-USB board drops off the USB bus whenever it resets, so a
// "disconnect" is usually a reset. The reset reason of the CRASHED boot is
// printed by the NEXT boot, which the editor's own port-open reboot can
// mask. So the last few boots are kept in RTC no-init memory (survives
// SW/WDT/brownout resets, cleared only on full power loss) and reprinted.
//
// Read it in the editor's DevTools console (lines prefixed "[ESP]").
// =====================================================
#ifndef DEBUG_HEARTBEAT
#define DEBUG_HEARTBEAT 1            // 0 = no periodic [HB] line (boot history stays)
#endif
#ifndef DEBUG_HB_INTERVAL_MS
#define DEBUG_HB_INTERVAL_MS 2000
#endif
#define BOOTLOG_MAGIC          0xB007D06Fu
#define BOOTLOG_DEPTH          6
#define BOOTLOG_REPRINT_MS     30000

struct BootRec {
    uint8_t  reason;          // esp_reset_reason_t that STARTED this boot
    uint8_t  prevStage;       // last setup stage the PREVIOUS boot reached
    uint32_t prevUptimeMs;    // how long the PREVIOUS boot ran
};

struct BootLog {
    uint32_t magic;
    uint32_t bootCount;
    uint32_t aliveMs;         // this boot's uptime, refreshed every ~100 ms
    uint8_t  stage;           // this boot's progress (see stageName)
    BootRec  rec[BOOTLOG_DEPTH];   // newest last
};

static RTC_NOINIT_ATTR BootLog g_bootLog;

static uint32_t s_lastAliveStore = 0;
static uint32_t s_lastHb         = 0;
static uint32_t s_lastHistPrint  = 0;
static uint32_t s_loopMaxUs      = 0;
static bool     s_histPrinted    = false;

static const char* resetReasonName(uint8_t r, char* buf, size_t n) {
    switch ((int)r) {
        case ESP_RST_POWERON:  return "POWERON (full power loss)";
        case ESP_RST_EXT:      return "EXT_PIN";
        case ESP_RST_SW:       return "SOFTWARE (esp_restart / USB-DTR)";
        case ESP_RST_PANIC:    return "PANIC (crash)";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO:     return "SDIO";
        case 11:               return "USB peripheral reset";   // IDF 5.x ESP_RST_USB
        case 12:               return "JTAG";
        case 13:               return "EFUSE";
        case 14:               return "POWER_GLITCH";
        case 15:               return "CPU_LOCKUP";
        default:
            snprintf(buf, n, "UNKNOWN(%d)", (int)r);
            return buf;
    }
}

static const char* stageName(uint8_t st) {
    switch (st) {
        case 0:  return "before servoBusInit";
        case 1:  return "servo buses up";
        case 2:  return "home frames sent";
        case 3:  return "servos released";
        case 4:  return "storage/player/commands up";
        case 5:  return "BLE up";
        case 6:  return "WiFi/WS up (setup done)";
        case 7:  return "loop() running";
        default: return "?";
    }
}

// Call FIRST in setup(): archives how the previous boot ended.
static void bootLogBegin() {
    esp_reset_reason_t r = esp_reset_reason();

    if (g_bootLog.magic != BOOTLOG_MAGIC) {      // RTC no-init is garbage after power loss
        memset(&g_bootLog, 0, sizeof(g_bootLog));
        g_bootLog.magic = BOOTLOG_MAGIC;
    }

    g_bootLog.bootCount++;
    for (int i = 0; i < BOOTLOG_DEPTH - 1; i++) g_bootLog.rec[i] = g_bootLog.rec[i + 1];
    g_bootLog.rec[BOOTLOG_DEPTH - 1].reason       = (uint8_t)r;
    g_bootLog.rec[BOOTLOG_DEPTH - 1].prevStage    = g_bootLog.stage;
    g_bootLog.rec[BOOTLOG_DEPTH - 1].prevUptimeMs = g_bootLog.aliveMs;

    g_bootLog.stage   = 0;
    g_bootLog.aliveMs = 0;

    s_lastAliveStore = s_lastHb = s_lastHistPrint = s_loopMaxUs = 0;
    s_histPrinted = false;
}

static inline void bootStage(uint8_t st) {
    g_bootLog.stage = st;
}

static void bootLogPrint() {
    char nb[24];
    Serial.printf("[BOOT] this is boot #%lu; history, newest last:\n",
                  (unsigned long)g_bootLog.bootCount);
    for (int i = 0; i < BOOTLOG_DEPTH; i++) {
        long n = (long)g_bootLog.bootCount - (BOOTLOG_DEPTH - 1 - i);
        if (n < 1) continue;
        const BootRec& b = g_bootLog.rec[i];
        Serial.printf("[BOOT]   #%ld started by %s | previous boot ran %lu ms, last stage: %s\n",
                      n, resetReasonName(b.reason, nb, sizeof(nb)),
                      (unsigned long)b.prevUptimeMs, stageName(b.prevStage));
    }
}

// Call at the END of every loop() with that iteration's duration.
static void debugTick(uint32_t loopUs) {
    uint32_t now = millis();
    if (loopUs > s_loopMaxUs) s_loopMaxUs = loopUs;

    if (now - s_lastAliveStore >= 100) {
        s_lastAliveStore = now;
        g_bootLog.aliveMs = now;
        g_bootLog.stage   = 7;
    }

    if (now - s_lastHb >= DEBUG_HB_INTERVAL_MS) {
        s_lastHb = now;
#if DEBUG_HEARTBEAT
        Serial.printf("[HB] boot#%lu up=%lus heap=%u minHeap=%u loopMax=%luus bridge=%d\n",
                      (unsigned long)g_bootLog.bootCount, (unsigned long)(now / 1000),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                      (unsigned long)s_loopMaxUs, (int)servoBridgeActive());
#endif
        s_loopMaxUs = 0;

        // History on the first heartbeat (setup's own copy may have been lost
        // while the port re-enumerated) and every 30 s after that.
        if (!s_histPrinted || now - s_lastHistPrint >= BOOTLOG_REPRINT_MS) {
            s_histPrinted = true;
            s_lastHistPrint = now;
            bootLogPrint();
        }
    }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
    bootLogBegin();
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);  // don't stall if USB CDC host is gone (ESP32-S3)
    delay(300);
    Serial.println();
    Serial.printf("RoBoHoN firmware booting on %s...\n", ESP.getChipModel());
    bootLogPrint();

    // Initialize servo buses
    servoBusInit();
    bootStage(1);

    // Send home frames to all buses
    Serial.println("Sending home frames...");
    servoSendHome(BUS_HEAD_ID);
    servoSendHome(BUS_LEG_ID);
    servoSendHome(BUS_SHOULDER_ID);
    bootStage(2);

    // Release all servos
    Serial.println("Releasing servos...");
    servoRelease();
    bootStage(3);

    // Initialize storage
    if (!motionStorageInit()) {
        Serial.println("LittleFS init failed");
    }

    // Initialize motion player + command handler
    motionPlayerInit();
    commandHandlerInit();
    bootStage(4);

    // Initialize BLE UART (Connect App)
#if BLE_ENABLED
    bleUartInit();
#else
    Serial.println("BLE disabled (BLE_ENABLED 0)");
#endif
    bootStage(5);

    // Initialize WiFi + WebSocket server (Motion Editor)
    Serial.println("Starting WiFi + WebSocket...");
    wifiWsInit();
    bootStage(6);

    Serial.println("Setup complete.");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
    uint32_t t0 = micros();

    servoBridgePollUsb();
    wifiWsUpdate();
    bleUartUpdate();
    motionPlayerUpdate();
    servoBridgePoll();

    debugTick(micros() - t0);
}