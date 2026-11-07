#include "SystemState.h"
#include "DebugLogger.h"

SystemStateManager& SystemStateManager::getInstance() {
    static SystemStateManager instance;
    return instance;
}

SystemStateManager::SystemStateManager() : currentState_(SystemState::SETUP_MODE) {
    ConfigStore::getInstance().load(config_);
}

SystemState SystemStateManager::determineState() {
    currentState_ = config_.wifi.ssid.empty() ? SystemState::SETUP_MODE : SystemState::NORMAL_MODE;
    return currentState_;
}

void SystemStateManager::setState(SystemState state) {
    currentState_ = state;
    DebugLogger::getInstance().log(LogLevel::INFO, "System state set to: " + std::to_string((int)state));
}