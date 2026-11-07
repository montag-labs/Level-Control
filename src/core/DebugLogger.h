#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <string>

enum class LogLevel {
    ERROR,
    WARN,
    INFO,
    DEBUG
};

// Schlanker Logger: schreibt nur in Debug-Builds (ohne NDEBUG) auf die serielle Schnittstelle.
class DebugLogger {
public:
    static DebugLogger& getInstance();

    void log(LogLevel level, const std::string& message);

private:
    DebugLogger() = default;
};

#endif // DEBUG_LOGGER_H
