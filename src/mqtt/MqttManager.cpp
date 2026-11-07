#include "MqttManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include "SensorManager.h"
#include "UpdateService.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "StringUtils.h"

// ------------------------------------------------------------------ Topics
static constexpr const char* TOPIC_STATUS          = "level-control/status";
static constexpr const char* TOPIC_SENSOR_STATE    = "level-control/sensor/state";
static constexpr const char* TOPIC_CFG_HOEHE_STATE = "level-control/config/behaelterhoehe/state";
static constexpr const char* TOPIC_CFG_HOEHE_SET   = "level-control/config/behaelterhoehe/set";
static constexpr const char* TOPIC_CFG_OFFSET_STATE = "level-control/config/offset/state";
static constexpr const char* TOPIC_CFG_OFFSET_SET  = "level-control/config/offset/set";
static constexpr const char* TOPIC_SYSTEM_STATE    = "level-control/system/state";
static constexpr const char* TOPIC_UPDATE_STATE      = "level-control/update/state";
static constexpr const char* TOPIC_UPDATE_INSTALL    = "level-control/update/install";
static constexpr const char* TOPIC_CFG_SAMPLE_STATE  = "level-control/config/sampleinterval/state";
static constexpr const char* TOPIC_CFG_SAMPLE_SET    = "level-control/config/sampleinterval/set";
static constexpr unsigned long kMetadataRepublishIntervalMs = 60000UL;

// ------------------------------------------------------------------ Singleton
MqttManager& MqttManager::getInstance() {
    static MqttManager instance;
    return instance;
}

MqttManager::MqttManager() : mqttClient_(netClient_) {
    mqttClient_.setCallback(mqttCallbackStatic);
}

// ------------------------------------------------------------------ Static callback shim
void MqttManager::mqttCallbackStatic(char* topic, byte* payload, unsigned int length) {
    std::string pl((char*)payload, length);
    MqttManager::getInstance().handleMessage(topic, pl);
}

// ------------------------------------------------------------------ Incoming message handler
void MqttManager::handleMessage(const char* topic, const std::string& payload) {
    DebugLogger::getInstance().log(LogLevel::DEBUG,
        std::string("MQTT rx [") + topic + "] " + payload);

    Config config;
    if (!ConfigStore::getInstance().load(config)) return;

    bool changed = false;
    const std::string topicName(topic);

    if (topicName == TOPIC_CFG_HOEHE_SET || topicName == TOPIC_CFG_OFFSET_SET || topicName == TOPIC_CFG_SAMPLE_SET) {
        char* parseEnd = nullptr;
        const double value = std::strtod(payload.c_str(), &parseEnd);
        const bool parsed = parseEnd != payload.c_str() && *parseEnd == '\0';
        if (!parsed) {
            DebugLogger::getInstance().log(LogLevel::WARN, "MQTT: ungültiger Wert [" + topicName + "]: " + payload);
        } else if (topicName == TOPIC_CFG_HOEHE_SET) {
            if (value >= ConfigLimits::kMinTankHeightCm && value <= ConfigLimits::kMaxTankHeightCm) {
                config.behaelterhoehe = static_cast<float>(value);
                SensorManager::getInstance().setBehaelterhoehe(config.behaelterhoehe);
                changed = true;
            }
        } else if (topicName == TOPIC_CFG_OFFSET_SET) {
            if (std::fabs(value) <= ConfigLimits::kMaxOffsetCm) {
                config.offset = static_cast<float>(value);
                SensorManager::getInstance().setOffset(config.offset);
                changed = true;
            }
        } else if (value >= 0.0 && value <= static_cast<double>(ConfigLimits::kMaxSampleIntervalSeconds)) {
            config.sampleIntervalSeconds = std::max(static_cast<unsigned long>(value), ConfigLimits::kMinSampleIntervalSeconds);
            SensorManager::getInstance().setSampleIntervalSeconds(config.sampleIntervalSeconds);
            changed = true;
        }
        if (changed) {
            DebugLogger::getInstance().log(LogLevel::INFO, "MQTT: " + topicName + " = " + payload);
        }
    } else if (topicName == TOPIC_UPDATE_INSTALL) {
        std::string requestedTarget = strutil::toLower(payload);
        if (requestedTarget.empty() || requestedTarget == "press" || requestedTarget == "install") {
            requestedTarget = "auto";
        }
        if (requestedTarget == "firmware") {
            requestedTarget = "app";
        }

        std::string error;
        if (!UpdateService::getInstance().requestRepoUpdate(requestedTarget, error)) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "MQTT: OTA-Start fehlgeschlagen: " + error);
        } else {
            publishUpdateState();
            DebugLogger::getInstance().log(LogLevel::INFO,
                "MQTT: OTA-Start ausgelöst via Home Assistant");
        }
    }

    if (changed) {
        ConfigStore::getInstance().save(config);
        publishConfig();
    }
}

// ------------------------------------------------------------------ Subscribe
void MqttManager::subscribeToTopics() {
    const bool okHoehe = mqttClient_.subscribe(TOPIC_CFG_HOEHE_SET);
    const bool okOffset = mqttClient_.subscribe(TOPIC_CFG_OFFSET_SET);
    const bool okSample = mqttClient_.subscribe(TOPIC_CFG_SAMPLE_SET);
    const bool okUpdate = mqttClient_.subscribe(TOPIC_UPDATE_INSTALL);

    if (okHoehe && okOffset && okSample && okUpdate) {
        DebugLogger::getInstance().log(LogLevel::INFO, "MQTT: config topics subscribed");
    } else {
        DebugLogger::getInstance().log(
            LogLevel::WARN,
            std::string("MQTT subscribe failed: hoehe=") + (okHoehe ? "1" : "0") +
            " offset=" + (okOffset ? "1" : "0") +
            " sample=" + (okSample ? "1" : "0") +
            " update=" + (okUpdate ? "1" : "0")
        );
    }
}

// ------------------------------------------------------------------ Init
void MqttManager::init(const char* server, uint16_t port) {
    server_ = server ? server : "";
    port_   = port;

    // Größeren Buffer für HA-Discovery Payloads (~400 Bytes)
    mqttClient_.setBufferSize(1024);
    mqttClient_.setServer(server_.c_str(), port_);
    mqttClient_.setKeepAlive(30);
    mqttClient_.setCallback(mqttCallbackStatic);
    state_ = MqttConnectionState::DISCONNECTED;

    // Eindeutige Geräte-ID aus MAC-Adresse
    std::string mac = WiFi.macAddress().c_str();
    deviceId_ = "level_control_";
    for (char c : mac) {
        if (c != ':') deviceId_ += c;
    }
    mqttClientId_ = deviceId_;

    Config config;
    if (ConfigStore::getInstance().load(config)) {
        username_ = config.mqtt.user;
        password_ = config.mqtt.password;
        if (!username_.empty()) {
            DebugLogger::getInstance().log(LogLevel::INFO, "MQTT auth: " + username_);
        }
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "MQTT init " + server_ + ":" + std::to_string(port_) +
        " id=" + deviceId_);
}

// ------------------------------------------------------------------ Connect
void MqttManager::connect() {
    unsigned long now = millis();
    if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < connectRetryIntervalMs_) {
        state_ = MqttConnectionState::BACKOFF;
        return;
    }
    if (mqttClient_.connected()) {
        state_ = MqttConnectionState::CONNECTED;
        return;
    }
    if (connecting_) {
        state_ = MqttConnectionState::CONNECTING;
        return;
    }
    if (server_.empty()) {
        state_ = MqttConnectionState::UNINITIALIZED;
        return;
    }

    connecting_ = true;
    state_ = MqttConnectionState::CONNECTING;
    lastConnectAttemptMs_ = now;

    // Verbinden mit LWT "offline"
    const char* user = username_.empty() ? nullptr : username_.c_str();
    const char* pass = password_.empty() ? nullptr : password_.c_str();
    bool ok = mqttClient_.connect(mqttClientId_.c_str(), user, pass,
                                  TOPIC_STATUS, 0, true, "offline");

    connecting_ = false;
    if (ok) {
        state_ = MqttConnectionState::CONNECTED;
        consecutiveFailures_ = 0;

        // Verfügbarkeit publizieren
        publish(TOPIC_STATUS, "online");

        // Config-Topics abonnieren
        subscribeToTopics();

        // Discovery + akt. Konfig nur wenn aktiviert
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            if (config.mqtt.discovery) {
                publishDiscovery();
            }
            publishConfig();
            publishSystemState();
            publishUpdateState();
        } else {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "MQTT: ConfigStore load failed during connect, skip discovery/config publish");
        }

        DebugLogger::getInstance().log(LogLevel::INFO, "MQTT connected");
    } else {
        state_ = MqttConnectionState::DISCONNECTED;
        consecutiveFailures_++;
        if (consecutiveFailures_ == 1 || (consecutiveFailures_ % 6) == 0) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "MQTT connect failed rc=" + std::to_string(mqttClient_.state()));
        }
    }
}

// ------------------------------------------------------------------ Loop
void MqttManager::loop() {
    if (mqttClient_.connected()) {
        mqttClient_.loop();
        state_ = MqttConnectionState::CONNECTED;

        static unsigned long lastMetadataRepublishMs = 0;
        const unsigned long now = millis();
        if (lastMetadataRepublishMs == 0 || now - lastMetadataRepublishMs >= kMetadataRepublishIntervalMs) {
            lastMetadataRepublishMs = now;
            Config config;
            if (ConfigStore::getInstance().load(config)) {
                publishConfig();
                if (config.mqtt.discovery) {
                    publishDiscovery();
                }
            } else {
                DebugLogger::getInstance().log(LogLevel::WARN,
                    "MQTT: ConfigStore load failed during periodic metadata republish");
            }
        }
    }
}

// ------------------------------------------------------------------ Disconnect
void MqttManager::disconnect() {
    if (isConnected()) {
        publish(TOPIC_STATUS, "offline");
    }
    connecting_ = false;
    mqttClient_.disconnect();
    state_ = MqttConnectionState::DISCONNECTED;
}

// ------------------------------------------------------------------ Publish generic
void MqttManager::publish(const char* topic, const char* payload, bool retain) {
    if (isConnected()) {
        if (!mqttClient_.publish(topic, payload, retain)) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                std::string("MQTT publish failed: ") + topic);
        }
    }
}

// ------------------------------------------------------------------ Publish sensor state
void MqttManager::publishSensorState(float fillLevel, float distanceCm,
                                      float rawDistanceM, unsigned int pingUs,
                                      bool valid, const char* status) {
    if (!isConnected()) return;
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"fill_level\":%.1f,\"distance_cm\":%.1f,"
        "\"raw_distance_m\":%.4f,\"ping_us\":%u,\"valid\":%s,\"status\":\"%s\"}",
        fillLevel, distanceCm, rawDistanceM, pingUs,
        valid ? "true" : "false",
        status ? status : "ok");
    publish(TOPIC_SENSOR_STATE, buf);
}

// ------------------------------------------------------------------ Publish config state
void MqttManager::publishConfig() {
    if (!isConnected()) return;
    Config config;
    if (!ConfigStore::getInstance().load(config)) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", config.behaelterhoehe);
    publish(TOPIC_CFG_HOEHE_STATE, buf);
    snprintf(buf, sizeof(buf), "%.1f", config.offset);
    publish(TOPIC_CFG_OFFSET_STATE, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)config.sampleIntervalSeconds);
    publish(TOPIC_CFG_SAMPLE_STATE, buf);
}

// ------------------------------------------------------------------ Publish system state
void MqttManager::publishSystemState() {
    if (!isConnected()) return;
    char buf[384];
    snprintf(buf, sizeof(buf),
        "{"
        "\"ip\":\"%s\","
        "\"ssid\":\"%s\","
        "\"rssi\":%d,"
        "\"uptime_s\":%lu,"
        "\"free_heap\":%u,"
        "\"min_free_heap\":%u,"
        "\"cpu_freq_mhz\":%u,"
        "\"flash_size_kb\":%u,"
        "\"sketch_size_kb\":%u,"
        "\"chip_rev\":%u"
        "}",
        WiFi.localIP().toString().c_str(),
        WiFi.SSID().c_str(),
        (int)WiFi.RSSI(),
        millis() / 1000UL,
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getMinFreeHeap(),
        (unsigned)ESP.getCpuFreqMHz(),
        (unsigned)(ESP.getFlashChipSize() / 1024),
        (unsigned)(ESP.getSketchSize() / 1024),
        (unsigned)ESP.getChipRevision()
    );
    publish(TOPIC_SYSTEM_STATE, buf);
}

void MqttManager::publishUpdateState() {
    if (!isConnected()) return;

    UpdateService& updates = UpdateService::getInstance();
    const std::string installedVersion = updates.installedVersion();
    std::string latestVersion = updates.availableVersion();
    if (latestVersion.empty()) {
        latestVersion = installedVersion;
    }

    JsonDocument doc;
    doc["installed_version"] = installedVersion;
    doc["latest_version"] = latestVersion;
    doc["title"] = "Level-Control OTA";
    doc["release_url"] = updates.latestReleaseUrl();
    if (updates.isInProgress()) {
        doc["in_progress"] = true;
        doc["update_percentage"] = updates.progressPercent();
    }

    std::string payload;
    serializeJson(doc, payload);
    publish(TOPIC_UPDATE_STATE, payload.c_str());
}

// ------------------------------------------------------------------ Home Assistant Discovery
namespace {
// Beschreibt eine Home-Assistant-Entitaet (sensor/number); der Update-Entitaet ist unten eigens.
struct DiscoveryEntity {
    const char* domain;        // "sensor" oder "number"
    const char* objectId;      // Teil des Discovery-Topics
    const char* uniqueSuffix;  // wird an die Geraete-ID angehaengt
    const char* name;
    const char* stateTopic;
    const char* commandTopic;   // nullptr = nur lesbar
    const char* valueTemplate;  // nullptr = Topic liefert direkt den Wert
    const char* unit;
    const char* deviceClass;
    const char* icon;
    const char* category;  // nullptr, "diagnostic" oder "config"
    bool hasRange;         // nur fuer "number"
    float min;
    float max;
    float step;
};

const DiscoveryEntity kDiscoveryEntities[] = {
    {"sensor", "fill_level", "_fill_level", "Füllstand", TOPIC_SENSOR_STATE, nullptr,
     "{{ value_json.fill_level | round(1) }}", "%", nullptr, "mdi:cup-water", nullptr, false, 0, 0, 0},
    {"sensor", "distance_cm", "_distance_cm", "Level-Control", TOPIC_SENSOR_STATE, nullptr,
     "{{ value_json.distance_cm | round(1) }}", "cm", nullptr, "mdi:ruler", nullptr, false, 0, 0, 0},
    {"sensor", "raw_distance", "_raw_distance", "Rohdistanz", TOPIC_SENSOR_STATE, nullptr,
     "{{ value_json.raw_distance_m | round(3) }}", "m", nullptr, "mdi:tape-measure", "diagnostic", false, 0, 0, 0},
    {"sensor", "ping_us", "_ping_us", "Ultraschall Pingzeit", TOPIC_SENSOR_STATE, nullptr,
     "{{ value_json.ping_us }}", "µs", nullptr, "mdi:timer-outline", "diagnostic", false, 0, 0, 0},
    {"number", "behaelterhoehe", "_behaelterhoehe", "Behälterhöhe", TOPIC_CFG_HOEHE_STATE, TOPIC_CFG_HOEHE_SET,
     nullptr, "cm", nullptr, "mdi:arrow-expand-vertical", "config", true,
     ConfigLimits::kMinTankHeightCm, ConfigLimits::kMaxTankHeightCm, 0.5f},
    {"number", "offset", "_offset", "Sensor Offset", TOPIC_CFG_OFFSET_STATE, TOPIC_CFG_OFFSET_SET,
     nullptr, "cm", nullptr, "mdi:tune-vertical", "config", true,
     -ConfigLimits::kMaxOffsetCm, ConfigLimits::kMaxOffsetCm, 0.5f},
    {"number", "sample_interval", "_sample_interval", "Abtastrate Ultraschall", TOPIC_CFG_SAMPLE_STATE,
     TOPIC_CFG_SAMPLE_SET, nullptr, "s", nullptr, "mdi:timer-sand", "config", true,
     static_cast<float>(ConfigLimits::kMinSampleIntervalSeconds),
     static_cast<float>(ConfigLimits::kMaxSampleIntervalSeconds), 1.0f},
    {"sensor", "rssi", "_rssi", "WiFi Signal", TOPIC_SYSTEM_STATE, nullptr,
     "{{ value_json.rssi }}", "dBm", "signal_strength", "mdi:wifi", "diagnostic", false, 0, 0, 0},
    {"sensor", "ip_address", "_ip", "IP-Adresse", TOPIC_SYSTEM_STATE, nullptr,
     "{{ value_json.ip }}", nullptr, nullptr, "mdi:ip-network", "diagnostic", false, 0, 0, 0},
    {"sensor", "ssid", "_ssid", "SSID", TOPIC_SYSTEM_STATE, nullptr,
     "{{ value_json.ssid }}", nullptr, nullptr, "mdi:access-point", "diagnostic", false, 0, 0, 0},
    {"sensor", "uptime", "_uptime", "Betriebszeit", TOPIC_SYSTEM_STATE, nullptr,
     "{{ value_json.uptime_s }}", "s", "duration", "mdi:clock-outline", "diagnostic", false, 0, 0, 0},
    {"sensor", "free_heap", "_free_heap", "Freier Heap", TOPIC_SYSTEM_STATE, nullptr,
     "{{ value_json.free_heap }}", "B", nullptr, "mdi:memory", "diagnostic", false, 0, 0, 0},
    {"sensor", "cpu_freq", "_cpu_freq", "CPU-Frequenz", TOPIC_SYSTEM_STATE, nullptr,
     "{{ value_json.cpu_freq_mhz }}", "MHz", nullptr, "mdi:cpu-32-bit", "diagnostic", false, 0, 0, 0},
};

void publishDiscoveryDoc(MqttManager& mqtt, const char* domain, const char* objectId, JsonDocument& doc) {
    // Device-Block wird in jede Entity eingebettet
    const JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = mqtt.getDeviceId();
    device["name"] = "Level-Control";
    device["manufacturer"] = "DIY";
    device["model"] = "ESP32-C3";

    std::string payload;
    serializeJson(doc, payload);
    const std::string topic = std::string("homeassistant/") + domain + "/" + mqtt.getDeviceId() + "/" + objectId + "/config";
    mqtt.publish(topic.c_str(), payload.c_str());
}
}  // namespace

void MqttManager::publishDiscovery() {
    if (!isConnected() || deviceId_.empty()) return;

    for (const DiscoveryEntity& entity : kDiscoveryEntities) {
        JsonDocument doc;
        doc["unique_id"] = deviceId_ + entity.uniqueSuffix;
        doc["name"] = entity.name;
        doc["state_topic"] = entity.stateTopic;
        doc["availability_topic"] = TOPIC_STATUS;
        doc["icon"] = entity.icon;
        if (entity.commandTopic) doc["command_topic"] = entity.commandTopic;
        if (entity.valueTemplate) doc["value_template"] = entity.valueTemplate;
        if (entity.unit) doc["unit_of_measurement"] = entity.unit;
        if (entity.deviceClass) doc["device_class"] = entity.deviceClass;
        if (entity.category) doc["entity_category"] = entity.category;
        if (entity.hasRange) {
            doc["min"] = entity.min;
            doc["max"] = entity.max;
            doc["step"] = entity.step;
        }
        publishDiscoveryDoc(*this, entity.domain, entity.objectId, doc);
    }

    // Update-Entität für Home Assistant OTA
    JsonDocument updateDoc;
    updateDoc["unique_id"] = deviceId_ + "_ota_update";
    updateDoc["name"] = "Level-Control OTA";
    updateDoc["title"] = "Level-Control Firmware";
    updateDoc["state_topic"] = TOPIC_UPDATE_STATE;
    updateDoc["command_topic"] = TOPIC_UPDATE_INSTALL;
    updateDoc["payload_install"] = "install";
    updateDoc["device_class"] = "firmware";
    updateDoc["availability_topic"] = TOPIC_STATUS;
    updateDoc["icon"] = "mdi:update";
    publishDiscoveryDoc(*this, "update", "ota", updateDoc);

    DebugLogger::getInstance().log(LogLevel::INFO,
        "MQTT HA Discovery published (" + deviceId_ + ")");
}
