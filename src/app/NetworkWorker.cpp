#include "NetworkWorker.h"

#include <Arduino.h>

#include "DebugLogger.h"
#include "MqttManager.h"
#include "PushNotificationManager.h"
#include "UpdateService.h"

namespace {
// SMTP ueber TLS braucht deutlich mehr Stack als die Standard-Tasks.
constexpr uint32_t kStackBytes = 12288;
constexpr uint32_t kPeriodMs = 100;

void workerTask(void*) {
    for (;;) {
        // Waehrend eines Updates ruht der Task, damit Heap und WLAN-Bandbreite dem Download gehoeren.
        if (!UpdateService::getInstance().isInProgress()) {
            MqttManager::getInstance().service();
            PushNotificationManager::getInstance().service();
        }
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
}
}  // namespace

namespace NetworkWorker {

void start() {
    if (xTaskCreate(workerTask, "lc-net", kStackBytes, nullptr, 1, nullptr) != pdPASS) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "Netzwerk-Task konnte nicht gestartet werden");
    }
}

}  // namespace NetworkWorker
