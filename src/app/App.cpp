#include "App.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "SystemState.h"
#include "WifiManager.h"
#include "WebServerSetup.h"
#include "UpdateService.h"
#include "WebServerDashboard.h"
#include "SensorManager.h"
#include "MqttManager.h"
#include "PushNotificationManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include "HistoryManager.h"

namespace {
constexpr uint32_t kSetupFallbackRestartDelayMs = 30000;

// (Re-)initialisiert den MQTT-Client mit der gespeicherten Konfiguration und startet den Verbindungsaufbau.
void connectMqttFromConfig() {
    Config config;
    if (ConfigStore::getInstance().load(config) && !config.mqtt.server.empty()) {
        MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
        MqttManager::getInstance().connect();
    }
}
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

    // OTA-Rollback-Validierung so frueh wie moeglich ausfuehren.
    // Darf nicht vom WLAN-Status abhaengen, sonst kann ein frischer OTA-Boot
    // bei fehlendem WLAN unbestaetigt bleiben.
    esp_err_t otaMarkRes = esp_ota_mark_app_valid_cancel_rollback();
    if (otaMarkRes == ESP_OK) {
        DebugLogger::getInstance().log(LogLevel::INFO, "Aktuelles Image als gueltig markiert");
    } else if (otaMarkRes == ESP_ERR_NOT_FOUND || otaMarkRes == ESP_ERR_INVALID_STATE) {
        DebugLogger::getInstance().log(LogLevel::INFO, std::string("OTA-Validierung nicht noetig: ") + esp_err_to_name(otaMarkRes));
    } else {
        DebugLogger::getInstance().log(LogLevel::WARN, std::string("esp_ota_mark_app_valid_cancel_rollback(): ") + esp_err_to_name(otaMarkRes));
    }

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
            connectMqttFromConfig();
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
}

void App::loop() {
    WifiManager::getInstance().process();
    MqttManager::getInstance().loop();

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
    const bool updateInProgress = UpdateService::getInstance().isInProgress();

    if (updateInProgress) {
        delay(10);
        return;
    }

    if (lastMeasurement_ == 0 || now - lastMeasurement_ >= SensorManager::getInstance().getSampleIntervalMs()) {
        lastMeasurement_ = now;
        SensorManager::getInstance().measure();
        // Sofort nach jeder Messung Sensordaten übertragen, damit die UI ohne Verzögerung aktualisiert wird.
        WebServerDashboard::getInstance().broadcastSensorData();
    }

    PushNotificationManager::getInstance().process(
        SensorManager::getInstance().getDistancePercent(),
        SensorManager::getInstance().getDistanceCm(),
        SensorManager::getInstance().getRawDistance(),
        SensorManager::getInstance().hasValidReading()
    );

    // MQTT Reconnect alle 5 s prüfen
    if (now - lastMqttReconnect_ > 5000) {
        lastMqttReconnect_ = now;
        if (WifiManager::getInstance().isConnected() &&
            !MqttManager::getInstance().isConnected()) {
            connectMqttFromConfig();
        }
    }

    // MQTT Sensor-Daten alle 30 s senden
    if (now - lastMqttPublish_ > 30000) {
        lastMqttPublish_ = now;
        SensorManager& s = SensorManager::getInstance();
        const char* sensorStatus = s.hasValidReading() ? "ok" :
                                   (s.getLastPingUs() == 0 ? "timeout" : "out_of_range");
        MqttManager::getInstance().publishSensorState(
            s.getDistancePercent(),
            s.getDistanceCm(),
            s.getRawDistance(),
            s.getLastPingUs(),
            s.hasValidReading(),
            sensorStatus
        );
        MqttManager::getInstance().publishSystemState();
        MqttManager::getInstance().publishUpdateState();
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

