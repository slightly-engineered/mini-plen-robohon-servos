#pragma once
#include <Arduino.h>

// Initialize command handler
void commandHandlerInit();

// Process a single incoming command line (JSON or UPLOAD protocol)
void commandHandlerHandleLine(const String& line);
