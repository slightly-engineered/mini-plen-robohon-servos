#pragma once
#include <Arduino.h>
#include "motion_storage.h"

// Parse a JSON motion string into a Motion struct.
// Returns true on success, false on parse error.
bool parseMotionJSON(const String& json, Motion& outMotion);
