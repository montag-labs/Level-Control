#include "DebugLogger.h"
#include <Arduino.h>

DebugLogger& DebugLogger::getInstance() {
    static DebugLogger instance;
    return instance;
}

void DebugLogger::log(LogLevel level, const std::string& message) {
#ifndef NDEBUG
    static const char* const kLevelNames[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    Serial.printf("[%lu][%s] %s\n", millis(), kLevelNames[static_cast<int>(level)], message.c_str());
#else
    (void)level;
    (void)message;
#endif
}
