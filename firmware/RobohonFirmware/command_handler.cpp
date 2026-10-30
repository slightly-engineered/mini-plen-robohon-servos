#include "command_handler.h"
#include <ArduinoJson.h>
#include "motion_parser.h"
#include "motion_storage.h"
#include "motion_player.h"
#include "servo_bus.h"
#include "config.h"
#include "ble_uart.h"
#include "wifi_ws.h"

// ------------------------------------------------------------
// State for text-based upload protocol: "UPLOAD <slot>" ... JSON ... "END"
// ------------------------------------------------------------
static bool uploadActive = false;
static uint8_t uploadSlot = 0;
static String uploadBuffer;

void commandHandlerInit() {
    // nothing yet
}

// ------------------------------------------------------------
// HYBRID RESPONSE SENDER (BLE + WiFi + USB)
// ------------------------------------------------------------
static void sendResponse(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);

    // BLE
    bleUartSendLine(out);

    // WiFi WebSocket
    wifiWsSend((const uint8_t*)out.c_str(), out.length());

    // USB Serial (optional debug)
    Serial.println(out);
}

static void sendHomeBurst() {
    servoSendHome(BUS_LEG_ID);
    servoSendHome(BUS_HEAD_ID);
    servoSendHome(BUS_SHOULDER_ID);
    servoRelease();
}

void commandHandlerHandleLine(const String& line) {

    // --------------------------------------------------------
    // 1) TEXT UPLOAD PROTOCOL (UPLOAD <slot> ... END)
    // --------------------------------------------------------

    if (line.startsWith("UPLOAD ")) {
        uploadActive = true;
        uploadBuffer = "";
        uploadSlot = line.substring(7).toInt();

        StaticJsonDocument<128> resp;
        resp["ok"] = true;
        resp["cmd"] = "upload_start";
        resp["slot"] = uploadSlot;
        sendResponse(resp);
        return;
    }

    if (uploadActive && line == "END") {
        uploadActive = false;

        Motion motion;
        if (!parseMotionJSON(uploadBuffer, motion)) {
            StaticJsonDocument<128> resp;
            resp["ok"] = false;
            resp["error"] = "parse_failed";
            sendResponse(resp);
            return;
        }

        if (!motionStorageSave(uploadSlot, motion)) {
            StaticJsonDocument<128> resp;
            resp["ok"] = false;
            resp["error"] = "save_failed";
            sendResponse(resp);
            return;
        }

        StaticJsonDocument<128> resp;
        resp["ok"] = true;
        resp["cmd"] = "upload";
        resp["slot"] = uploadSlot;
        sendResponse(resp);
        return;
    }

    if (uploadActive) {
        uploadBuffer += line;
        return;
    }

    // --------------------------------------------------------
    // 2) JSON COMMANDS
    // --------------------------------------------------------

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        StaticJsonDocument<256> resp;
        resp["ok"] = false;
        resp["error"] = "invalid_json";
        sendResponse(resp);
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) {
        StaticJsonDocument<256> resp;
        resp["ok"] = false;
        resp["error"] = "missing_cmd";
        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // upload: {"cmd":"upload","slot":3,"motion":{...}}
    // --------------------------------------------------------
    if (strcmp(cmd, "upload") == 0) {
        uint8_t slot = doc["slot"] | 0;
        JsonVariant motionJson = doc["motion"];

        if (motionJson.isNull()) {
            StaticJsonDocument<256> resp;
            resp["ok"] = false;
            resp["error"] = "missing_motion";
            sendResponse(resp);
            return;
        }

        String motionStr;
        serializeJson(motionJson, motionStr);

        Motion motion;
        if (!parseMotionJSON(motionStr, motion)) {
            StaticJsonDocument<256> resp;
            resp["ok"] = false;
            resp["error"] = "parse_failed";
            sendResponse(resp);
            return;
        }

        if (!motionStorageSave(slot, motion)) {
            StaticJsonDocument<256> resp;
            resp["ok"] = false;
            resp["error"] = "save_failed";
            sendResponse(resp);
            return;
        }

        StaticJsonDocument<256> resp;
        resp["ok"] = true;
        resp["cmd"] = "upload";
        resp["slot"] = slot;
        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // play: {"cmd":"play","slot":3}
    // --------------------------------------------------------
    if (strcmp(cmd, "play") == 0) {
        uint8_t slot = doc["slot"] | 0;

        Motion motion;
        if (!motionStorageLoad(slot, motion)) {
            StaticJsonDocument<256> resp;
            resp["ok"] = false;
            resp["error"] = "load_failed";
            resp["slot"] = slot;
            sendResponse(resp);
            return;
        }

        motionPlayerLoad(motion);
        motionPlayerPlay();

        StaticJsonDocument<256> resp;
        resp["ok"] = true;
        resp["cmd"] = "play";
        resp["slot"] = slot;
        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // stop: {"cmd":"stop"}
    // --------------------------------------------------------
    if (strcmp(cmd, "stop") == 0) {
        motionPlayerStop();
        sendHomeBurst();

        StaticJsonDocument<256> resp;
        resp["ok"] = true;
        resp["cmd"] = "stop";
        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // list: {"cmd":"list"}
    // --------------------------------------------------------
    if (strcmp(cmd, "list") == 0) {
        auto slots = motionStorageList();

        StaticJsonDocument<512> resp;
        resp["ok"] = true;
        resp["cmd"] = "list";

        JsonArray arr = resp.createNestedArray("slots");
        for (uint8_t s : slots) arr.add(s);

        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // frame: {"cmd":"frame","angles":{...}}
    // Live pose control (float degrees)
    // --------------------------------------------------------
    if (strcmp(cmd, "frame") == 0) {
        JsonObject angles = doc["angles"].as<JsonObject>();
        if (angles.isNull()) {
            StaticJsonDocument<256> resp;
            resp["ok"] = false;
            resp["error"] = "missing_angles";
            sendResponse(resp);
            return;
        }

        float allAngles[NUM_SERVOS];
        for (int i = 0; i < NUM_SERVOS; i++) {
            const char* name = servoNames[i];
            allAngles[i] = angles[name] | 0.0f;
        }

        int16_t frameShoulder[SERVO_COUNT];
        int16_t frameHead[SERVO_COUNT];
        int16_t frameLeg[SERVO_COUNT];

        buildBusFrame(BUS_SHOULDER_ID, allAngles, frameShoulder);
        buildBusFrame(BUS_HEAD_ID,     allAngles, frameHead);
        buildBusFrame(BUS_LEG_ID,      allAngles, frameLeg);

        servoWriteFrame(BUS_SHOULDER_ID, frameShoulder);
        servoWriteFrame(BUS_HEAD_ID,     frameHead);
        servoWriteFrame(BUS_LEG_ID,      frameLeg);

        StaticJsonDocument<256> resp;
        resp["ok"] = true;
        resp["cmd"] = "frame";
        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // home: {"cmd":"home"}
    // --------------------------------------------------------
    if (strcmp(cmd, "home") == 0) {

        sendHomeBurst();

        StaticJsonDocument<256> resp;
        resp["ok"] = true;
        resp["cmd"] = "home";
        sendResponse(resp);
        return;
    }

    // --------------------------------------------------------
    // Unknown command
    // --------------------------------------------------------
    StaticJsonDocument<256> resp;
    resp["ok"] = false;
    resp["error"] = "unknown_cmd";
    resp["cmd"] = cmd;
    sendResponse(resp);
}
