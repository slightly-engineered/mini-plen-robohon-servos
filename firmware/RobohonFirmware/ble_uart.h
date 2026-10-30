#pragma once
#include <Arduino.h>

void bleUartInit();
void bleUartUpdate();

// line-based JSON / command system
void bleUartSendLine(const String& line);
void bleUartOnLineReceived(const String& line);

// raw-byte API for servo bridge
bool    bleUartAvailable();
uint8_t bleUartRead();
void    bleUartWrite(const uint8_t* data, size_t len);
