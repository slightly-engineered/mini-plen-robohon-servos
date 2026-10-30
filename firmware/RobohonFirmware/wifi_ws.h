#pragma once
#include <Arduino.h>

void wifiWsInit();          // start WiFi + WS server
void wifiWsUpdate();        // optional, if you need housekeeping
bool wifiWsSend(const uint8_t *data, size_t len);  // for JSON replies, etc.
bool wifiWsReadByte(uint8_t* out);

