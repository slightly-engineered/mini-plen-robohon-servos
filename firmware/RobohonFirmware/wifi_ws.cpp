#include "wifi_ws.h"
#include "servo_bridge.h"
#include "command_handler.h"
#include "config.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>

#include "esp_wifi.h"      // Required for country + protocol fixes
#include "esp_mac.h"       // esp_read_mac()

// ============================================================
// WS2812 Status LED (GPIO 10 on ESP32‑C3 Zero)
// ============================================================
static Adafruit_NeoPixel statusLed(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

static unsigned long ledTimer = 0;
static bool ledState = false;
static int wsClientCount = 0;

// ============================================================
// WebSocket server
// ============================================================
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ============================================================
// RX ring buffer for binary data (WiFi → servo bridge)
// ============================================================
static const size_t RX_BUF_SIZE = 2048;
static uint8_t rxBuf[RX_BUF_SIZE];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static inline bool rxBufferPush(uint8_t b) {
    size_t head = __atomic_load_n(&rxHead, __ATOMIC_ACQUIRE);
    size_t next = (head + 1) % RX_BUF_SIZE;
    if (next == __atomic_load_n(&rxTail, __ATOMIC_ACQUIRE)) return false; // overflow
    rxBuf[head] = b;
    __atomic_store_n(&rxHead, next, __ATOMIC_RELEASE);
    return true;
}

bool wifiWsReadByte(uint8_t* out) {
    size_t tail = __atomic_load_n(&rxTail, __ATOMIC_ACQUIRE);
    if (tail == __atomic_load_n(&rxHead, __ATOMIC_ACQUIRE)) return false; // empty
    *out = rxBuf[tail];
    __atomic_store_n(&rxTail, (tail + 1) % RX_BUF_SIZE, __ATOMIC_RELEASE);
    return true;
}

// ============================================================
// Build-time switches (override with -D or edit here)
// ============================================================
#ifndef WIFI_DIAG
#define WIFI_DIAG 1            // 1 = boot banner, LED self-test, network scan, event logging
#endif
#ifndef WIFI_FORCE_BGN
#define WIFI_FORCE_BGN 1       // 1 = lock STA to 11b/g/n (original behaviour); 0 = let the chip negotiate (C6 adds 11ax)
#endif
#ifndef WIFI_DISABLE_SLEEP
#define WIFI_DISABLE_SLEEP 1   // 1 = WiFi.setSleep(false); if BLE is running, the coex layer may override this
#endif
#ifndef WIFI_TX_POWER_ENABLE
#define WIFI_TX_POWER_ENABLE 1 // 1 = cap TX power (test for weak supply / antenna mismatch: AP hears scan but never answers auth)
#endif
#ifndef WIFI_TX_POWER_LEVEL
#define WIFI_TX_POWER_LEVEL WIFI_POWER_8_5dBm
#endif

// ============================================================
// Diagnostics helpers
// ============================================================
static volatile uint8_t lastDisconnectReason = 0;

static const char* reasonText(uint8_t r) {
    switch (r) {
        case 0:   return "none";
        case 2:   return "AUTH_EXPIRE";
        case 3:   return "AUTH_LEAVE";
        case 4:   return "ASSOC_EXPIRE";
        case 8:   return "ASSOC_LEAVE";
        case 15:  return "4WAY_HANDSHAKE_TIMEOUT (usually password or WPA3/PMF mismatch)";
        case 200: return "BEACON_TIMEOUT";
        case 201: return "NO_AP_FOUND (SSID not heard: RF path, band, or hidden SSID)";
        case 202: return "AUTH_FAIL";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        case 205: return "CONNECTION_FAIL";
        case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
        case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
        case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
        default:  return "other (see wifi_err_reason_t)";
    }
}

static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("[WIFI] STA started");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.printf("[WIFI] associated on channel %u\n", (unsigned)info.wifi_sta_connected.channel);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WIFI] got IP %s\n", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            lastDisconnectReason = info.wifi_sta_disconnected.reason;
            Serial.printf("[WIFI] disconnected, reason %u: %s\n",
                          (unsigned)lastDisconnectReason, reasonText(lastDisconnectReason));
            break;
        default:
            break;
    }
}

#if WIFI_TX_POWER_ENABLE
static void applyTxPower() {
    WiFi.setTxPower(WIFI_TX_POWER_LEVEL);
    // raw value is in 0.25 dBm units (34 = 8.5 dBm)
    Serial.printf("[WIFI] TX power now %d (x0.25 dBm)\n", (int)WiFi.getTxPower());
}
#endif

#if WIFI_DIAG
// auth: 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA/WPA2, 5 WPA2-Ent, 6 WPA3, 7 WPA2/WPA3
static void wifiScanReport() {
    Serial.println("[SCAN] scanning...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.printf("[SCAN] result %d (0 with a router nearby = RF path/antenna/power; negative = scan error)\n", n);
        return;
    }
    Serial.printf("[SCAN] %d networks\n", n);
    for (int i = 0; i < n; i++) {
        bool ours = false;
        for (int k = 0; k < WIFI_COUNT; k++) {
            if (WiFi.SSID(i) == WIFI_SSIDS[k]) ours = true;
        }
        Serial.printf("[SCAN] %-32s ch%-2d %4d dBm auth=%d %s\n",
                      WiFi.SSID(i).c_str(), (int)WiFi.channel(i), (int)WiFi.RSSI(i),
                      (int)WiFi.encryptionType(i), ours ? "<-- configured" : "");
    }
    WiFi.scanDelete();
}
#endif

// ============================================================
// WiFi + WebSocket Init (MULTI‑SSID VERSION)
// ============================================================
void wifiWsInit() {

    // Init WS2812 LED
    statusLed.begin();
    statusLed.setBrightness(50);
    statusLed.clear();
    statusLed.show();

#if WIFI_DIAG
    Serial.printf("[BOOT] chip=%s rev=%d sdk=%s heap=%u\n",
                  ESP.getChipModel(), (int)ESP.getChipRevision(),
                  ESP.getSdkVersion(), (unsigned)ESP.getFreeHeap());

    // LED self-test: proves STATUS_LED_PIN is right for this board
    statusLed.setPixelColor(0, statusLed.Color(255, 255, 255));
    statusLed.show();
    delay(300);
    statusLed.clear();
    statusLed.show();
    Serial.printf("[BOOT] status LED on GPIO %d (no white flash = wrong pin for this board)\n", STATUS_LED_PIN);
#endif

    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_STA);

#if WIFI_DIAG
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);   // factory MAC; WiFi.macAddress() returns zeros before the STA starts
        Serial.printf("[BOOT] STA MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
#endif

#if WIFI_DISABLE_SLEEP
    WiFi.setSleep(false);
#endif

#if WIFI_TX_POWER_ENABLE
    applyTxPower();
#endif

    // Country code: US, channels 1-11
    wifi_country_t country_config = {
        .cc = "US",
        .schan = 1,
        .nchan = 11,
        .policy = WIFI_COUNTRY_POLICY_AUTO
    };
    esp_wifi_set_country(&country_config);

#if WIFI_FORCE_BGN
    // Force 2.4 GHz b/g/n (Optimum routers need this). On the C6 this excludes 11ax.
    esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B |
        WIFI_PROTOCOL_11G |
        WIFI_PROTOCOL_11N
    );
#endif

#if WIFI_DIAG
    wifiScanReport();
#endif

    Serial.println("Connecting to WiFi...");
    Serial.println("Configured networks:");
    for (int i = 0; i < WIFI_COUNT; i++) {
        Serial.printf("  %s\n", WIFI_SSIDS[i]);
    }

    bool connected = false;

    // Try each SSID in config.h
    for (int i = 0; i < WIFI_COUNT; i++) {

        Serial.printf("Trying SSID: %s\n", WIFI_SSIDS[i]);

        // Clean state before each attempt so a stalled join doesn't bleed into the next
        lastDisconnectReason = 0;
        WiFi.disconnect();
        delay(100);
        WiFi.begin(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);
#if WIFI_TX_POWER_ENABLE
        applyTxPower();   // re-apply: some cores reset TX power inside begin()
#endif

        unsigned long start = millis();

        // Try for 15 seconds per network
        while (millis() - start < 15000) {

            wl_status_t st = WiFi.status();

            if (st == WL_CONNECTED) {
                connected = true;
                break;
            }

            // Print status codes for debugging
            if (millis() % 1000 < 50) {
                Serial.printf("Status: %d\n", st);
            }

            // Fast blink while connecting
            unsigned long now = millis();
            if (now - ledTimer >= 100) {
                ledTimer = now;
                ledState = !ledState;
                if (ledState)
                    statusLed.setPixelColor(0, statusLed.Color(255, 255, 0)); // yellow
                else
                    statusLed.clear();
                statusLed.show();
            }

            delay(50);
            Serial.print(".");
            servoBridgePollUsb();  // USB live control still works during STA wait
        }

        Serial.println();

        if (connected) break;
    }

    if (!connected) {
        Serial.println("WiFi failed on all networks!");
        Serial.printf("Last disconnect reason: %u (%s)\n",
                      (unsigned)lastDisconnectReason, reasonText(lastDisconnectReason));
        return;
    }

    Serial.println("WiFi connected.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP().toString());

    // --------------------------------------------------------
    // WebSocket event handler
    // --------------------------------------------------------
    ws.onEvent([](AsyncWebSocket *server,
                  AsyncWebSocketClient *client,
                  AwsEventType type,
                  void *arg,
                  uint8_t *data,
                  size_t len)
    {
        if (type == WS_EVT_CONNECT) {
            wsClientCount++;
            servoBridgeEnable();
            return;
        }

        if (type == WS_EVT_DISCONNECT) {
            if (wsClientCount > 0) wsClientCount--;
            // Keep bridge on so USB/BLE still work after the WS client drops
            return;
        }

        if (type != WS_EVT_DATA) return;

        AwsFrameInfo *info = (AwsFrameInfo*)arg;

        // ----------------------------------------------------
        // BINARY FRAME → push bytes into RX ring buffer
        // ----------------------------------------------------
        if (info->opcode == WS_BINARY) {
            for (size_t i = 0; i < len; i++) {
                rxBufferPush(data[i]);
            }
            return;
        }

        // ----------------------------------------------------
        // TEXT FRAME → JSON command
        // ----------------------------------------------------
        if (info->opcode == WS_TEXT) {
            String line = String((char*)data).substring(0, len);
            commandHandlerHandleLine(line);
            return;
        }
    });

    server.addHandler(&ws);
    server.begin();
}

// ============================================================
// LED + WebSocket update
// ============================================================
void wifiWsUpdate() {
    unsigned long now = millis();

    // WiFi disconnected → LED OFF
    if (WiFi.status() != WL_CONNECTED) {
        statusLed.clear();
        statusLed.show();
        return;
    }

    // Solid green if WS client connected
    if (wsClientCount > 0) {
        statusLed.setPixelColor(0, statusLed.Color(0, 255, 0)); // green
        statusLed.show();
        return;
    }

    // Slow blink (connected but idle)
    if (now - ledTimer >= 500) {
        ledTimer = now;
        ledState = !ledState;

        if (ledState)
            statusLed.setPixelColor(0, statusLed.Color(0, 0, 255)); // blue
        else
            statusLed.clear();

        statusLed.show();
    }
}

// ============================================================
// Send binary data to ALL connected WS clients
// ============================================================
bool wifiWsSend(const uint8_t *data, size_t len) {
    ws.binaryAll(data, len);
    return true;
}
