#include "ble_uart.h"
#include "command_handler.h"
#include "servo_bridge.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static BLEServer*         pServer  = nullptr;
static BLECharacteristic* pRxChar  = nullptr; // phone → ESP32
static BLECharacteristic* pTxChar  = nullptr; // ESP32 → phone

static String rxLineBuffer;

// ============================================================
// Raw-byte ring buffer for BLE → servo bridge
// ============================================================
static const int RB_SIZE = 512;
static uint8_t rbData[RB_SIZE];
static volatile int rbHead = 0;
static volatile int rbTail = 0;

static inline bool rbEmpty() {
    return rbHead == rbTail;
}

static inline bool rbFull() {
    return ((rbHead + 1) % RB_SIZE) == rbTail;
}

static inline void rbPush(uint8_t b) {
    if (!rbFull()) {
        rbData[rbHead] = b;
        rbHead = (rbHead + 1) % RB_SIZE;
    }
}

static inline uint8_t rbPop() {
    if (rbEmpty()) return 0;
    uint8_t b = rbData[rbTail];
    rbTail = (rbTail + 1) % RB_SIZE;
    return b;
}

// ============================================================
// RX Callback (BLE UART)
// ============================================================
class UartRxCallback : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override {

        String s = characteristic->getValue();
        int len = s.length();

        // Auto‑enable bridge when we see a 30‑byte frame (Motion Editor)
        if (len == 30) {
            servoBridgeEnable();
        }

        if (servoBridgeActive()) {
            for (int i = 0; i < len; i++) {
                rbPush((uint8_t)s[i]);
            }
            return;
        }

        // Robohon Connect JSON / upload path
        for (int i = 0; i < len; i++) {
            uint8_t b = (uint8_t)s[i];

            if (b == '\n') {
                bleUartOnLineReceived(rxLineBuffer);
                rxLineBuffer = "";
            }
            else if (b >= 32 && b <= 126) {
                rxLineBuffer += (char)b;
            }
        }
    }
};


// ============================================================
// Initialization
// ============================================================
void bleUartInit() {
    Serial.println("Starting BLE...");

    BLEDevice::init("RoBoHoN-C3");
    pServer = BLEDevice::createServer();

    BLEService* pService =
        pServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

    // RX characteristic (phone → ESP32)
    pRxChar = pService->createCharacteristic(
        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    pRxChar->setCallbacks(new UartRxCallback());

    // TX characteristic (ESP32 → phone)
    pTxChar = pService->createCharacteristic(
        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxChar->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();

    BLEAdvertisementData advData;
    advData.setName("RoBoHoN-C3");
    advData.setCompleteServices(BLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));

    adv->setAdvertisementData(advData);
    adv->setScanResponse(false);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);

    adv->start();

    BLEDevice::setMTU(512);

    Serial.println("BLE advertising started.");
}

void bleUartUpdate() {
    // No polling needed for ESP32 BLE
}

// ============================================================
// Send a line to the phone
// ============================================================
void bleUartSendLine(const String& line) {
    if (!pTxChar) return;

    String out = line + "\n";
    pTxChar->setValue(out.c_str());
    pTxChar->notify();
}

// ============================================================
// Incoming line handler
// ============================================================
void bleUartOnLineReceived(const String& line) {
    commandHandlerHandleLine(line);
}

// ============================================================
// Raw-byte API for servo bridge
// ============================================================
bool bleUartAvailable() {
    return !rbEmpty();
}

uint8_t bleUartRead() {
    return rbPop();
}

void bleUartWrite(const uint8_t* data, size_t len) {
    if (!pTxChar) return;

    const size_t CHUNK = 200;
    for (size_t i = 0; i < len; i += CHUNK) {
        size_t n = (len - i < CHUNK) ? (len - i) : CHUNK;

        // *** FIX FOR ESP32 ARDUINO CORE 3.0.6 ***
        pTxChar->setValue((uint8_t*)(data + i), n);

        pTxChar->notify();
    }
}
