#include "App.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "ConfigStore.h"
#include "DebugLogger.h"
#include "HistoryManager.h"
#include "NetworkWorker.h"
#include "SensorManager.h"
#include "SystemState.h"
#include "UpdateService.h"
#include "WebServerDashboard.h"
#include "WebServerSetup.h"
#include "WifiManager.h"

namespace {
constexpr uint32_t kSetupFallbackRestartDelayMs = 30000;
// Ein frisch per OTA installiertes Image gilt erst nach dieser Laufzeit als gut. Stuerzt es vorher ab,
// startet der Bootloader beim naechsten Reset wieder das vorherige Image (Rollback).
constexpr unsigned long kImageValidationDelayMs = 20000UL;
}  // namespace

App& App::getInstance() {
    static App instance;
    return instance;
}

void App::init() {
    Serial.begin(115200);
    delay(100);
#ifndef NDEBUG
    Serial.println("[Level-Control] Starting...");
#endif
    DebugLogger::getInstance().log(LogLevel::INFO, "Starting Level-Control Sensor");

    SystemStateManager& ssm = SystemStateManager::getInstance();
    SystemState state = ssm.determineState();

    WifiManager::getInstance().init();

    if (state == SystemState::SETUP_MODE) {
        WifiManager::getInstance().startAP("Level-Control-Setup", "");
        WebServerSetup::getInstance().init();
        WebServerSetup::getInstance().start();
    } else {
        const bool connected = WifiManager::getInstance().connect();

        if (connected) {
            WebServerDashboard::getInstance().init();
            WebServerDashboard::getInstance().start();
        } else {
            // Nach 20 fehlgeschlagenen Versuchen -> Setup-AP starten
            DebugLogger::getInstance().log(LogLevel::WARN,
                "WiFi connect failed, switching to SETUP_MODE");
            ssm.setState(SystemState::SETUP_MODE);
            WifiManager::getInstance().startAP("Level-Control-Setup", "");
            WebServerSetup::getInstance().init();
            WebServerSetup::getInstance().start();
        }
    }

    SensorManager::getInstance().init();
    // HistoryManager nach dem LittleFS-Mount (WebServerDashboard::init) initialisieren
    HistoryManager::getInstance().init();

    // MQTT und E-Mail laufen in einem eigenen Task (siehe NetworkWorker).
    NetworkWorker::start();
}

void App::markImageValidWhenStable() {
    if (imageValidated_ || millis() < kImageValidationDelayMs) {
        return;
    }
    imageValidated_ = true;
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) {
        DebugLogger::getInstance().log(LogLevel::INFO, "Aktuelles Image als gueltig markiert");
    } else if (result != ESP_ERR_NOT_FOUND && result != ESP_ERR_INVALID_STATE) {
        DebugLogger::getInstance().log(LogLevel::WARN,
            std::string("esp_ota_mark_app_valid_cancel_rollback(): ") + esp_err_to_name(result));
    }
}

void App::loop() {
    WifiManager::getInstance().process();
    markImageValidWhenStable();

    // Setup-Fallback (z. B. Router nach Stromausfall noch nicht erreichbar beim Boot): Sobald das
    // gespeicherte WLAN wieder verbunden ist, neu starten, damit das Dashboard statt der Setup-Seite laeuft.
    // Beim manuellen Setup ist der Neustart bereits ueber /connect-status geplant.
    if (SystemStateManager::getInstance().getCurrentState() == SystemState::SETUP_MODE
        && WifiManager::getInstance().isConnected()
        && !WifiManager::getInstance().isRestartScheduled()) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WLAN im Setup-Modus wieder verbunden, starte neu");
        WifiManager::getInstance().scheduleRestart(kSetupFallbackRestartDelayMs);
    }

    const unsigned long now = millis();

    if (UpdateService::getInstance().isInProgress()) {
        delay(10);
        return;
    }

    if (lastMeasurement_ == 0 || now - lastMeasurement_ >= SensorManager::getInstance().getSampleIntervalMs()) {
        lastMeasurement_ = now;
        SensorManager::getInstance().measure();
        // Sofort nach jeder Messung Sensordaten übertragen, damit die UI ohne Verzögerung aktualisiert wird.
        WebServerDashboard::getInstance().broadcastSensorData();
    }

    // Level-Control-Verlauf alle 60 s prüfen; HistoryManager entscheidet intern ob 6h vergangen
    if (lastHistoryCheck_ == 0 || now - lastHistoryCheck_ >= 60000UL) {
        lastHistoryCheck_ = now;
        if (SensorManager::getInstance().hasValidReading()) {
            HistoryManager::getInstance().addSample(
                SensorManager::getInstance().getDistancePercent());
        }
    }

    // WebSocket Broadcast (WiFi, Uptime, MQTT-Status) alle 5 s
    // Sensor-Daten werden direkt nach jeder Messung gesendet (siehe oben).
    if (now - lastBroadcast_ > 5000) {
        lastBroadcast_ = now;
        WebServerDashboard::getInstance().broadcastWifiData();
        WebServerDashboard::getInstance().broadcastUptime();
        WebServerDashboard::getInstance().broadcastMqttState();
    }

    delay(100);
}
