#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <string>
#include <WiFiClient.h>
#include <PubSubClient.h>

enum class MqttConnectionState {
    UNINITIALIZED,
    DISCONNECTED,
    BACKOFF,
    CONNECTING,
    CONNECTED
};

class MqttManager {
public:
    static MqttManager& getInstance();

    // Wird zyklisch vom Netzwerk-Task aufgerufen: Verbindungsaufbau/-ueberwachung, Nachrichten,
    // periodische Zustandsmeldungen. Alle MQTT-Zugriffe laufen in diesem einen Task.
    void service();
    // Aus beliebigen Tasks aufrufbar (z. B. nach geaenderter Konfiguration): baut die Verbindung neu auf.
    void requestReconnect() { reconnectRequested_ = true; }

    // Publish sensor data (alle Messwerte auf einmal)
    void publishSensorState(float fillLevel, float distanceCm, float rawDistanceM,
                            unsigned int pingUs, bool valid, const char* status = "ok");

    // Publish WiFi, Uptime & ESP32-Systemwerte
    void publishSystemState();

    // Publish OTA/Version state for Home Assistant Update entity
    void publishUpdateState();

    // Publish aktuellen Konfig-Zustand (behaelterhoehe, offset)
    void publishConfig();

    // Home Assistant MQTT Discovery
    void publishDiscovery();

    // Generic raw publish
    void publish(const char* topic, const char* payload, bool retain = true);

    bool isConnected() { return mqttClient_.connected(); }
    MqttConnectionState getState() const { return state_; }
    const std::string& getDeviceId() const { return deviceId_; }

private:
    MqttManager();

    void init(const char* server, uint16_t port);
    void connect();
    void loop();
    void disconnect();
    void reconfigureFromStoredConfig();
    void publishPeriodicState();

    WiFiClient netClient_;
    PubSubClient mqttClient_;
    std::string mqttClientId_;
    std::string deviceId_;   // MAC-based unique device ID
    std::string server_;
    uint16_t port_ = 1883;
    std::string username_;
    std::string password_;
    bool connecting_ = false;
    unsigned long lastConnectAttemptMs_ = 0;
    static constexpr unsigned long connectRetryIntervalMs_ = 5000;
    MqttConnectionState state_ = MqttConnectionState::UNINITIALIZED;
    int consecutiveFailures_ = 0;
    volatile bool reconnectRequested_ = false;
    unsigned long lastReconnectCheckMs_ = 0;
    unsigned long lastPublishMs_ = 0;

    static void mqttCallbackStatic(char* topic, byte* payload, unsigned int length);
    void handleMessage(const char* topic, const std::string& payload);
    void subscribeToTopics();
};

#endif // MQTT_MANAGER_H