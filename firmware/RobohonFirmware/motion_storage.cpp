#include "motion_storage.h"
#include <LittleFS.h>
#include "config.h"

static String slotPath(uint8_t slot) {
    return "/motion_" + String(slot) + ".bin";
}

bool motionStorageInit() {
    return LittleFS.begin(true);  // format on fail
}

bool motionStorageSave(uint8_t slot, const Motion& motion) {
    String path = slotPath(slot);
    File f = LittleFS.open(path, "w");
    if (!f) return false;

    // Write frame count
    f.write(motion.frameCount);

    // Write loop metadata
    f.write(motion.loop ? 1 : 0);
    f.write(motion.loopStart);
    f.write(motion.loopEnd);
    f.write(motion.loopCount);

    // Write each frame
    for (const Frame& fr : motion.frames) {

        // transition (uint16_t)
        f.write((uint8_t)(fr.transition & 0xFF));
        f.write((uint8_t)((fr.transition >> 8) & 0xFF));

        // angles (float32, 4 bytes each)
        for (int i = 0; i < NUM_SERVOS; i++) {
            float a = fr.angles[i];
            uint8_t* p = (uint8_t*)&a;
            f.write(p[0]);
            f.write(p[1]);
            f.write(p[2]);
            f.write(p[3]);
        }
    }

    f.close();
    return true;
}

bool motionStorageLoad(uint8_t slot, Motion& outMotion) {
    String path = slotPath(slot);
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    outMotion.frameCount = f.read();

    // Read loop metadata
    outMotion.loop       = f.read() != 0;
    outMotion.loopStart  = f.read();
    outMotion.loopEnd    = f.read();
    outMotion.loopCount  = f.read();

    outMotion.frames.clear();
    outMotion.frames.reserve(outMotion.frameCount);

    for (int i = 0; i < outMotion.frameCount; i++) {
        Frame fr;

        // transition
        uint8_t lo = f.read();
        uint8_t hi = f.read();
        fr.transition = (hi << 8) | lo;

        // angles (float32)
        for (int s = 0; s < NUM_SERVOS; s++) {
            uint8_t b0 = f.read();
            uint8_t b1 = f.read();
            uint8_t b2 = f.read();
            uint8_t b3 = f.read();

            float a;
            uint8_t* p = (uint8_t*)&a;
            p[0] = b0;
            p[1] = b1;
            p[2] = b2;
            p[3] = b3;

            fr.angles[s] = a;
        }

        outMotion.frames.push_back(fr);
    }

    f.close();
    return true;
}

std::vector<uint8_t> motionStorageList() {
    std::vector<uint8_t> slots;

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (file) {
        String name = file.name();
        if (name.startsWith("/motion_")) {
            int slot = name.substring(8).toInt();
            slots.push_back(slot);
        }
        file = root.openNextFile();
    }

    return slots;
}

bool motionStorageDelete(uint8_t slot) {
    return LittleFS.remove(slotPath(slot));
}