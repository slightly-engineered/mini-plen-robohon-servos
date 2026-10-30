#include <Arduino.h>
#include <ArduinoJson.h>
#include "motion_parser.h"
#include "motion_storage.h"
#include "config.h"   // for servoNames[]

bool parseMotionJSON(const String& json, Motion& outMotion)
{
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, json);

    if (err) {
        Serial.println("JSON parse error");
        return false;
    }

    JsonArray frames = doc["frames"];
    if (frames.isNull() || frames.size() == 0) {
        Serial.println("No frames in JSON");
        return false;
    }

    outMotion.frameCount = frames.size();
    outMotion.frames.clear();
    outMotion.frames.reserve(outMotion.frameCount);

    // Loop metadata
    outMotion.loop      = doc["loop"]["enabled"] | false;
    outMotion.loopStart = doc["loop"]["begin"]   | 0;
    outMotion.loopEnd   = doc["loop"]["end"]     | 0;
    outMotion.loopCount = doc["loop"]["count"]   | 0;

    // Parse each frame
    for (JsonObject f : frames) {

        Frame frame;
        frame.transition = f["transition"] | 300;

        JsonObject angles = f["angles"];
        if (angles.isNull()) {
            Serial.println("Frame missing angles");
            return false;
        }

        // Read all servo angles by name
        for (int i = 0; i < NUM_SERVOS; i++) {
            const char* name = servoNames[i];
            frame.angles[i] = angles[name] | 0.0f;
        }

        outMotion.frames.push_back(frame);
    }

    return true;
}
