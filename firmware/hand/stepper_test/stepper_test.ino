/*
 * Robot Hand Actuator — HOME (open) and CLOSE (closed)
 * ESP32-C3 + DRV8825
 *
 * Commands:
 *   h  = home (fully open)
 *   c  = close (fully closed)
 */

#include <Arduino.h>

static const int PIN_STEP = 4;
static const int PIN_DIR  = 5;
static const int PIN_EN   = 9;   // active LOW

// Timing
static const unsigned STEP_HIGH_US = 4;
static const unsigned STEP_PERIOD_US = 4000;  // 250 steps/s

// Full stroke (closed position)
static const int strokeSteps = 1500;

// Homing overtravel (same as stroke for symmetry)
static const int homeSteps = 1500;

// Position counter
static int currentPos = 0;

void enableDriver(bool on) {
    digitalWrite(PIN_EN, on ? LOW : HIGH);
}

void pulseStep() {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(STEP_HIGH_US);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(STEP_PERIOD_US - STEP_HIGH_US);
}

void moveSteps(int n, bool forward) {
    digitalWrite(PIN_DIR, forward ? HIGH : LOW);
    delayMicroseconds(5);

    for (int i = 0; i < n; i++) {
        pulseStep();
        currentPos += forward ? 1 : -1;

        if (Serial.available()) return;
    }
}

void printHelp() {
    Serial.println();
    Serial.println(F("Commands:"));
    Serial.println(F("  h     home (fully open hand)"));
    Serial.println(F("  c     close (fully closed hand)"));
    Serial.println(F("  ?     help"));
    Serial.printf("currentPos = %d\n", currentPos);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    pinMode(PIN_STEP, OUTPUT);
    pinMode(PIN_DIR, OUTPUT);
    pinMode(PIN_EN, OUTPUT);

    digitalWrite(PIN_STEP, LOW);
    digitalWrite(PIN_DIR, LOW);
    enableDriver(true);

    Serial.println();
    Serial.println(F("Robot Hand — HOME & CLOSE (1500 step stroke)"));
    printHelp();
}

void loop() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    char c = line.charAt(0);

    switch (c) {

        case 'h':   // HOME = fully open
        case 'H':
            Serial.println(F("Homing (open hand)..."));
            enableDriver(true);
            moveSteps(homeSteps, false);   // guaranteed full travel
            currentPos = 0;
            Serial.println(F("Hand OPEN — position = 0"));
            break;

        case 'c':   // CLOSE = fully closed
        case 'C':
            Serial.println(F("Closing hand..."));
            enableDriver(true);
            moveSteps(strokeSteps, true);
            Serial.printf("Hand CLOSED — position = %d\n", currentPos);
            break;

        default:
            printHelp();
            break;
    }
}
