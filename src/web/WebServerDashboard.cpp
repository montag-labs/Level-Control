#include <string>
#include "WebServerDashboard.h"
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ConfigStore.h"
#include "DebugLogger.h"
#include "HistoryManager.h"
#include "MqttManager.h"
#include "PushNotificationManager.h"
#include "SensorManager.h"
#include "StringUtils.h"
#include "UpdateService.h"
#include "WebSupport.h"
#include "WifiManager.h"

namespace {
constexpr uint32_t kRestartDelayMs = 2500;
constexpr size_t kMaxConfigBodyBytes = 2048;
constexpr size_t kMaxWifiBodyBytes = 4096;
constexpr size_t kMaxMqttBodyBytes = 2048;
constexpr size_t kMaxImportBodyBytes = 48 * 1024;
constexpr size_t kMaxAuthBodyBytes = 512;
constexpr size_t kMinAdminPasswordLength = 8;

using web::applySecret;
using web::jsonString;
using web::kPasswordMask;
using web::sha256Hex;

void applyPushEncryptionModeToConfig(const std::string& mode, bool& useSsl, bool& startTls) {
    const std::string normalized = ConfigRules::normalizePushEncryptionMode(mode);
    useSsl = normalized == "ssl";
    startTls = normalized == "starttls";
}

const char* mqttStateName(MqttConnectionState state) {
    switch (state) {
        case MqttConnectionState::CONNECTED:    return "connected";
        case MqttConnectionState::CONNECTING:   return "connecting";
        case MqttConnectionState::BACKOFF:      return "backoff";
        case MqttConnectionState::DISCONNECTED: return "disconnected";
        default:                                return "uninitialized";
    }
}

// Uebernimmt Sensor-Einstellungen in den Messbetrieb; ein geaenderter Sensortyp wird neu initialisiert.
void applySensorSettings(const Config& config) {
    SensorManager& sensor = SensorManager::getInstance();
    sensor.setBehaelterhoehe(config.behaelterhoehe);
    sensor.setOffset(config.offset);
    sensor.setSampleIntervalSeconds(config.sampleIntervalSeconds);
    if (sensor.getSensorType() != config.sensorType || sensor.getTofRoiSize() != config.tofRoiSize) {
        sensor.setSensorType(config.sensorType);
        sensor.setTofRoiSize(config.tofRoiSize);
        sensor.init();
    }
}
}  // namespace

WebServerDashboard& WebServerDashboard::getInstance() {
    static WebServerDashboard instance;
    return instance;
}

bool WebServerDashboard::requireAdmin(AsyncWebServerRequest *request) {
    return admin_.require(request);
}

WebServerDashboard::WebServerDashboard() : server_(80), ws_("/ws") {
    web::ensureLittleFsMounted();
    server_.addHandler(&ws_);
}

void WebServerDashboard::init() {
    setupRoutes();
    UpdateService::getInstance().registerRoutes(server_, [this](AsyncWebServerRequest *request) {
        return requireAdmin(request);
    });
}

void WebServerDashboard::start() {
    server_.begin();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerDashboard started");
}

void WebServerDashboard::broadcastSensorData() {
    JsonDocument doc;
    doc["type"] = "sensor";
    doc["ping_us"] = SensorManager::getInstance().getLastPingUs();
    doc["valid"] = SensorManager::getInstance().hasValidReading();
    doc["rohdistanz"] = SensorManager::getInstance().getRawDistance();
    doc["salzstandCm"] = SensorManager::getInstance().getDistanceCm();
    doc["salzstandPercent"] = SensorManager::getInstance().getDistancePercent();
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::broadcastWifiData() {
    JsonDocument doc;
    doc["type"] = "wifi";
    doc["signal"] = WiFi.RSSI();
    String ip = WiFi.localIP().toString();
    String ssid = WiFi.SSID();
    String bssid = WiFi.BSSIDstr();
    doc["ip"] = ip.c_str();
    doc["ssid"] = ssid.c_str();
    doc["bssid"] = bssid.c_str();
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::broadcastUptime() {
    ws_.cleanupClients();  // Verbindungen getrennter Browser freigeben (laeuft alle 5 s ueber App::loop)
    JsonDocument doc;
    doc["type"] = "uptime";
    doc["uptime"] = millis() / 1000;
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::broadcastMqttState() {
    JsonDocument doc;
    doc["type"] = "mqtt";
    doc["state"] = mqttStateName(MqttManager::getInstance().getState());
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::setupRoutes() {
    server_.on("/api/auth/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config config;
        const bool configured = ConfigStore::getInstance().load(config) && !config.adminPasswordHash.empty();
        JsonDocument doc;
        doc["configured"] = configured;
        doc["authenticated"] = configured && admin_.isAuthenticated(request);
        doc["sessionTtlMs"] = AdminSession::kSessionTtlMs;
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
        admin_.logout();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/auth/change", HTTP_POST, web::answerIfPending, nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0 && !requireAdmin(request)) return;
            const char *body = web::collectBody(request, data, len, index, total, kMaxAuthBodyBytes,
                                           "{\"error\":\"auth_payload_too_large\"}");
            if (body == nullptr) return;

            JsonDocument doc;
            const bool validJson = deserializeJson(doc, body, total) == DeserializationError::Ok;
            const std::string password = validJson ? std::string(doc["password"] | "") : "";
            Config config;
            if (!validJson || password.size() < kMinAdminPasswordLength || !ConfigStore::getInstance().load(config)) {
                request->send(400, "application/json", "{\"error\":\"admin_password_invalid\"}");
                return;
            }
            config.adminPasswordHash = sha256Hex(password);
            if (!ConfigStore::getInstance().save(config)) {
                request->send(500, "application/json", "{\"error\":\"admin_password_save_failed\"}");
                return;
            }
            admin_.sendToken(request);
        });

    for (const char* route : {"/api/auth/setup", "/api/auth/login"}) {
        const bool isSetup = std::strcmp(route, "/api/auth/setup") == 0;
        server_.on(route, HTTP_POST, web::answerIfPending, nullptr,
            [this, isSetup](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                const char *body = web::collectBody(request, data, len, index, total, kMaxAuthBodyBytes,
                                               "{\"error\":\"auth_payload_too_large\"}");
                if (body == nullptr) return;
                if (!isSetup && admin_.rejectIfLoginLocked(request)) return;

                JsonDocument doc;
                const bool validJson = deserializeJson(doc, body, total) == DeserializationError::Ok;
                const std::string password = validJson ? std::string(doc["password"] | "") : "";
                Config config;
                const bool configLoaded = ConfigStore::getInstance().load(config);
                if (!validJson || !configLoaded || password.size() < kMinAdminPasswordLength) {
                    request->send(400, "application/json", "{\"error\":\"admin_password_invalid\"}");
                    return;
                }

                const std::string passwordHash = sha256Hex(password);
                if (isSetup) {
                    if (!config.adminPasswordHash.empty()) {
                        request->send(409, "application/json", "{\"error\":\"admin_already_configured\"}");
                        return;
                    }
                    config.adminPasswordHash = passwordHash;
                    if (!ConfigStore::getInstance().save(config)) {
                        request->send(500, "application/json", "{\"error\":\"admin_password_save_failed\"}");
                        return;
                    }
                } else if (config.adminPasswordHash.empty()
                    || !web::constantTimeEquals(passwordHash.c_str(), passwordHash.size(),
                                           config.adminPasswordHash.c_str(), config.adminPasswordHash.size())) {
                    admin_.recordLoginFailure();
                    request->send(401, "application/json", "{\"error\":\"admin_login_failed\"}");
                    return;
                }
                admin_.resetLoginFailures();
                admin_.sendToken(request);
            });
    }

    server_.on("/api/laser-version", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["version"] = SensorManager::getInstance().getLaserVersion();
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            JsonDocument doc;
            doc["behaelterhoehe"] = config.behaelterhoehe;
            doc["offset"] = config.offset;
            doc["sampleIntervalSeconds"] = config.sampleIntervalSeconds;
            doc["sensorType"] = config.sensorType;
            doc["tofRoiSize"] = config.tofRoiSize;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"Config load failed\"}");
        }
    });

    server_.on("/api/config", HTTP_POST, web::answerIfPending, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = web::collectBody(request, data, len, index, total, kMaxConfigBodyBytes,
                                       "{\"error\":\"config_payload_too_large\"}");
        if (body == nullptr) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"invalid_config_payload\"}");
            return;
        }

        Config config;
        ConfigStore::getInstance().load(config);  // aktuelle Werte gelten fuer nicht gesendete Felder
        const float height = doc["behaelterhoehe"] | config.behaelterhoehe;
        const float offset = doc["offset"] | config.offset;
        const unsigned long intervalSeconds = doc["sampleIntervalSeconds"] | config.sampleIntervalSeconds;
        if (!(height >= ConfigLimits::kMinTankHeightCm && height <= ConfigLimits::kMaxTankHeightCm)) {
            request->send(400, "application/json", "{\"error\":\"invalid_tank_height\"}");
            return;
        }
        if (!(std::fabs(offset) <= ConfigLimits::kMaxOffsetCm)) {
            request->send(400, "application/json", "{\"error\":\"invalid_offset\"}");
            return;
        }
        if (intervalSeconds < ConfigLimits::kMinSampleIntervalSeconds) {
            request->send(400, "application/json", "{\"error\":\"sample_interval_too_small\"}");
            return;
        }
        if (intervalSeconds > ConfigLimits::kMaxSampleIntervalSeconds) {
            request->send(400, "application/json", "{\"error\":\"sample_interval_too_large\"}");
            return;
        }

        config.behaelterhoehe = height;
        config.offset = offset;
        config.sampleIntervalSeconds = intervalSeconds;
        config.sensorType = jsonString(doc, "sensorType", config.sensorType);
        config.tofRoiSize = doc["tofRoiSize"] | config.tofRoiSize;
        ConfigRules::sanitize(config);  // normalisiert u. a. den Sensortyp
        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"sensor_config_save_failed\"}");
            return;
        }
        applySensorSettings(config);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/wifi", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        Config config;
        const bool loaded = ConfigStore::getInstance().load(config);

        const std::string configuredSsid = loaded ? config.wifi.ssid : "";
        const std::string runtimeSsid = WiFi.SSID().c_str();
        const std::string runtimePsk = WiFi.psk().c_str();

        bool wifiHasPassword = loaded && !config.wifi.password.empty();
        if (!wifiHasPassword && !runtimePsk.empty()) {
            // Fallback: WLAN-Stack kennt ein PSK, auch wenn es nicht (mehr) in NVS liegt.
            wifiHasPassword = true;
        }

        JsonDocument doc;
        doc["ssid"] = !configuredSsid.empty() ? configuredSsid : runtimeSsid;
        doc["password"] = wifiHasPassword ? kPasswordMask : "";
        doc["hasPassword"] = wifiHasPassword;
        doc["deviceName"] = loaded ? config.wifi.deviceName : WifiManager::getInstance().getConfig().deviceName;
        doc["ntpServerPrimary"] = loaded ? config.wifi.ntpServerPrimary : WifiManager::getInstance().getConfig().ntpServerPrimary;
        doc["ntpServerSecondary"] = loaded ? config.wifi.ntpServerSecondary : WifiManager::getInstance().getConfig().ntpServerSecondary;
        doc["dnsHostname"] = WifiManager::getInstance().getDnsHostname();
        doc["localUrl"] = WifiManager::getInstance().getLocalUrl();
        doc["useStaticIp"] = loaded && (!config.staticIp.ip.empty() || !config.staticIp.subnet.empty() || !config.staticIp.dns.empty());
        doc["staticIp"]["ip"] = loaded ? config.staticIp.ip : "";
        doc["staticIp"]["gateway"] = loaded ? config.staticIp.gateway : "";
        doc["staticIp"]["subnet"] = loaded ? config.staticIp.subnet : "";
        doc["staticIp"]["dns"] = loaded ? config.staticIp.dns : "";

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        std::vector<WifiNetwork> networks = WifiManager::getInstance().scanNetworks();
        std::sort(networks.begin(), networks.end(), [](const WifiNetwork& left, const WifiNetwork& right) {
            return left.rssi > right.rssi;
        });

        JsonDocument doc;
        JsonArray items = doc["networks"].to<JsonArray>();
        for (const auto& network : networks) {
            if (network.ssid.empty()) {
                continue;
            }

            bool duplicate = false;
            for (JsonVariant item : items) {
                if (std::string(item["ssid"] | "") == network.ssid) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            JsonObject entry = items.add<JsonObject>();
            entry["ssid"] = network.ssid;
            entry["rssi"] = network.rssi;
        }

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/wifi", HTTP_POST, web::answerIfPending, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = web::collectBody(request, data, len, index, total, kMaxWifiBodyBytes,
                                       "{\"error\":\"wifi_payload_too_large\"}");
        if (body == nullptr) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"invalid_wifi_payload\"}");
            return;
        }

        Config config;
        ConfigStore::getInstance().load(config);  // aktuelle Werte gelten fuer nicht gesendete Felder
        config.wifi.ssid = strutil::trim(jsonString(doc, "ssid", config.wifi.ssid));
        if (config.wifi.ssid.empty()) {
            request->send(400, "application/json", "{\"error\":\"ssid_required\"}");
            return;
        }
        config.wifi.deviceName = jsonString(doc, "deviceName", config.wifi.deviceName);
        config.wifi.ntpServerPrimary = jsonString(doc, "ntpServerPrimary", config.wifi.ntpServerPrimary);
        config.wifi.ntpServerSecondary = jsonString(doc, "ntpServerSecondary", config.wifi.ntpServerSecondary);
        applySecret(config.wifi.password, doc, "password", false);

        if (doc["useStaticIp"] | false) {
            config.staticIp.ip = jsonString(doc["staticIp"], "ip", "");
            config.staticIp.gateway = jsonString(doc["staticIp"], "gateway", "");
            config.staticIp.subnet = jsonString(doc["staticIp"], "subnet", "");
            config.staticIp.dns = jsonString(doc["staticIp"], "dns", "");

            if (config.staticIp.ip.empty() || config.staticIp.subnet.empty() || config.staticIp.dns.empty()) {
                request->send(400, "application/json", "{\"error\":\"static_ip_requires_ip_subnet_dns\"}");
                return;
            }
        } else {
            config.staticIp = {};
        }

        if (!WifiManager::getInstance().setConfig(config.wifi, config.staticIp)) {
            request->send(500, "application/json", "{\"error\":\"wifi_config_save_failed\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/mqtt", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            JsonDocument doc;
            doc["server"] = config.mqtt.server;
            doc["port"] = config.mqtt.port;
            doc["user"] = config.mqtt.user;
            doc["password"] = config.mqtt.password.empty() ? "" : kPasswordMask;
            doc["hasPassword"] = !config.mqtt.password.empty();
            doc["discovery"] = config.mqtt.discovery;
            doc["device_id"] = MqttManager::getInstance().getDeviceId();
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"MQTT config load failed\"}");
        }
    });

    server_.on("/api/mqtt", HTTP_POST, web::answerIfPending, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = web::collectBody(request, data, len, index, total, kMaxMqttBodyBytes,
                                       "{\"error\":\"mqtt_payload_too_large\"}");
        if (body == nullptr) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"invalid_mqtt_payload\"}");
            return;
        }

        Config config;
        ConfigStore::getInstance().load(config);  // aktuelle Werte gelten fuer nicht gesendete Felder
        config.mqtt.server = jsonString(doc, "server", config.mqtt.server);
        config.mqtt.port = doc["port"] | config.mqtt.port;
        config.mqtt.user = jsonString(doc, "user", config.mqtt.user);
        applySecret(config.mqtt.password, doc, "password", false);
        config.mqtt.discovery = doc["discovery"] | config.mqtt.discovery;
        ConfigRules::sanitize(config);
        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"mqtt_config_save_failed\"}");
            return;
        }

        // Der Netzwerk-Task baut die Verbindung mit den neuen Einstellungen neu auf.
        MqttManager::getInstance().requestReconnect();

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/mqtt/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        ConfigStore::getInstance().load(config);
        JsonDocument doc;
        doc["state"] = mqttStateName(MqttManager::getInstance().getState());
        doc["server"] = config.mqtt.server;
        doc["port"] = config.mqtt.port;
        doc["deviceId"] = MqttManager::getInstance().getDeviceId();
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/mqtt/reconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        MqttManager::getInstance().requestReconnect();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/push", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            JsonDocument doc;
            doc["enabled"] = config.push.enabled;
            doc["smtpServer"] = config.push.smtpServer;
            doc["smtpPort"] = config.push.smtpPort;
            doc["useSsl"] = config.push.useSsl;
            doc["startTls"] = config.push.startTls;
            doc["encryption"] = ConfigRules::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
            doc["security"] = ConfigRules::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
            doc["smtpSkipCertVerify"] = config.push.smtpSkipCertVerify;
            doc["authUser"] = config.push.authUser;
            doc["authPassword"] = config.push.authPassword.empty() ? "" : kPasswordMask;
            doc["hasAuthPassword"] = !config.push.authPassword.empty();
            doc["senderName"] = config.push.senderName;
            doc["senderEmail"] = config.push.senderEmail;
            doc["recipientEmail"] = config.push.recipientEmail;
            doc["triggerPercent"] = config.push.triggerPercent;
            doc["sendHour"] = config.push.sendHour;
            doc["sendMinute"] = config.push.sendMinute;
            doc["reminderCycle"] = config.push.reminderCycle;
            doc["reminderWeekday"] = config.push.reminderWeekday;
            doc["subjectTemplate"] = config.push.subjectTemplate;
            doc["bodyTemplate"] = config.push.bodyTemplate;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"Push config load failed\"}");
        }
    });

    auto* pushPostHandler = new AsyncCallbackJsonWebHandler("/api/push", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!requireAdmin(request)) return;
        const JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            request->send(400, "application/json", "{\"error\":\"invalid_push_payload\"}");
            return;
        }

        Config config;
        ConfigStore::getInstance().load(config);  // aktuelle Werte gelten fuer nicht gesendete Felder
        PushConfig& push = config.push;

        push.enabled = doc["enabled"] | push.enabled;
        push.smtpServer = jsonString(doc, "smtpServer", push.smtpServer);
        push.smtpPort = doc["smtpPort"] | push.smtpPort;
        push.useSsl = doc["useSsl"] | push.useSsl;
        push.startTls = doc["startTls"] | push.startTls;
        if (!push.useSsl) {
            push.useSsl = doc["ssl"] | false;  // aeltere Clients
        }
        const std::string encryptionMode = jsonString(doc, "encryption", jsonString(doc, "security", ""));
        if (!encryptionMode.empty()) {
            applyPushEncryptionModeToConfig(encryptionMode, push.useSsl, push.startTls);
        }
        push.smtpSkipCertVerify = doc["smtpSkipCertVerify"] | push.smtpSkipCertVerify;
        push.authUser = jsonString(doc, "authUser", push.authUser);
        applySecret(push.authPassword, doc, "authPassword", false);
        push.senderName = jsonString(doc, "senderName", push.senderName);
        push.senderEmail = jsonString(doc, "senderEmail", push.senderEmail);
        push.recipientEmail = jsonString(doc, "recipientEmail", push.recipientEmail);
        push.triggerPercent = doc["triggerPercent"] | push.triggerPercent;
        push.sendHour = doc["sendHour"] | push.sendHour;
        push.sendMinute = doc["sendMinute"] | push.sendMinute;
        const std::string reminderCycle = jsonString(doc, "reminderCycle", "");
        if (!reminderCycle.empty()) {
            push.reminderCycle = reminderCycle;
        } else if (doc["cycleMinutes"].is<unsigned long>()) {
            push.reminderCycle = ConfigRules::reminderCycleFromLegacyMinutes(doc["cycleMinutes"].as<unsigned long>());
        }
        push.reminderWeekday = doc["reminderWeekday"] | push.reminderWeekday;
        push.subjectTemplate = jsonString(doc, "subjectTemplate", push.subjectTemplate);
        push.bodyTemplate = jsonString(doc, "bodyTemplate", push.bodyTemplate);

        ConfigRules::sanitize(config);
        if (push.useSsl && push.smtpPort == 587) push.smtpPort = 465;
        if (push.startTls && push.smtpPort == 465) push.smtpPort = 587;

        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"push_config_save_failed\"}");
            return;
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    server_.addHandler(pushPostHandler);

    // Test-Mail und SMTP-Diagnose laufen als Auftrag im Netzwerk-Task: POST startet (202), GET /api/push/job liefert das Ergebnis.
    server_.on("/api/push/test", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        if (!PushNotificationManager::getInstance().requestTestEmail()) {
            request->send(409, "application/json", "{\"error\":\"push_job_running\"}");
            return;
        }
        request->send(202, "application/json", "{\"status\":\"started\"}");
    });

    server_.on("/api/push/smtp-check", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        if (!PushNotificationManager::getInstance().requestDiagnostic()) {
            request->send(409, "application/json", "{\"error\":\"push_job_running\"}");
            return;
        }
        request->send(202, "application/json", "{\"status\":\"started\"}");
    });

    server_.on("/api/push/job", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        const PushJobStatus job = PushNotificationManager::getInstance().jobStatus();
        JsonDocument doc;
        switch (job.state) {
            case PushJobStatus::State::Pending:
            case PushJobStatus::State::Running: doc["state"] = "running"; break;
            case PushJobStatus::State::Done:    doc["state"] = "done"; break;
            default:                            doc["state"] = "idle"; break;
        }
        doc["kind"] = job.kind;
        doc["success"] = job.success;
        doc["error"] = job.error;
        JsonArray steps = doc["steps"].to<JsonArray>();
        for (const auto& step : job.diagnostic.steps) {
            JsonObject entry = steps.add<JsonObject>();
            entry["name"] = step.name;
            entry["ok"] = step.ok;
            entry["detail"] = step.detail;
        }
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    // GET /api/history – liefert alle gespeicherten Level-Control-Verläufe als JSON-Array
    server_.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
        const auto entries = HistoryManager::getInstance().getHistory();
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->print('[');
        bool first = true;
        for (const auto& e : entries) {
            if (!first) resp->print(',');
            first = false;
            char buf[40];
            snprintf(buf, sizeof(buf), "{\"ts\":%lu,\"v\":%.1f}",
                     static_cast<unsigned long>(e.timestamp), e.value);
            resp->print(buf);
        }
        resp->print(']');
        request->send(resp);
    });

    // DELETE /api/history – löscht alle gespeicherten Verlaufsdaten
    server_.on("/api/history", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        HistoryManager::getInstance().clear();
        request->send(204);
    });

    // GET /api/export – Konfiguration und Verlaufsdaten als JSON-Backup herunterladen
    server_.on("/api/export", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        Config config;
        if (!ConfigStore::getInstance().load(config)) {
            request->send(500, "application/json", "{\"error\":\"config_load_failed\"}");
            return;
        }

        const auto entries = HistoryManager::getInstance().getHistory();
        const time_t t = time(nullptr);
        struct tm* tmInfo = localtime(&t);
        char filename[64];
        strftime(filename, sizeof(filename), "level-control-backup-%Y-%m-%d.json", tmInfo);
        const bool includeSecrets = request->hasParam("includeSecrets")
            && request->getParam("includeSecrets")->value() == "1";

        JsonDocument cfgDoc;
        cfgDoc["behaelterhoehe"] = config.behaelterhoehe;
        cfgDoc["offset"] = config.offset;
        cfgDoc["sampleIntervalSeconds"] = config.sampleIntervalSeconds;
        cfgDoc["sensorType"] = config.sensorType;
        cfgDoc["tofRoiSize"] = config.tofRoiSize;
        JsonObject wifiJ = cfgDoc["wifi"].to<JsonObject>();
        wifiJ["ssid"] = config.wifi.ssid;
        wifiJ["password"] = includeSecrets ? config.wifi.password : kPasswordMask;
        wifiJ["deviceName"] = config.wifi.deviceName;
        wifiJ["ntpServerPrimary"] = config.wifi.ntpServerPrimary;
        wifiJ["ntpServerSecondary"] = config.wifi.ntpServerSecondary;
        JsonObject sipJ = cfgDoc["staticIp"].to<JsonObject>();
        sipJ["ip"] = config.staticIp.ip;
        sipJ["gateway"] = config.staticIp.gateway;
        sipJ["subnet"] = config.staticIp.subnet;
        sipJ["dns"] = config.staticIp.dns;
        JsonObject mqttJ = cfgDoc["mqtt"].to<JsonObject>();
        mqttJ["server"] = config.mqtt.server;
        mqttJ["port"] = config.mqtt.port;
        mqttJ["user"] = config.mqtt.user;
        mqttJ["password"] = includeSecrets ? config.mqtt.password : kPasswordMask;
        mqttJ["discovery"] = config.mqtt.discovery;
        JsonObject pushJ = cfgDoc["push"].to<JsonObject>();
        pushJ["enabled"] = config.push.enabled;
        pushJ["smtpServer"] = config.push.smtpServer;
        pushJ["smtpPort"] = config.push.smtpPort;
        pushJ["useSsl"] = config.push.useSsl;
        pushJ["startTls"] = config.push.startTls;
        pushJ["encryption"] = ConfigRules::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
        pushJ["security"] = ConfigRules::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
        pushJ["smtpSkipCertVerify"] = config.push.smtpSkipCertVerify;
        pushJ["authUser"] = config.push.authUser;
        pushJ["authPassword"] = includeSecrets ? config.push.authPassword : kPasswordMask;
        pushJ["senderName"] = config.push.senderName;
        pushJ["senderEmail"] = config.push.senderEmail;
        pushJ["recipientEmail"] = config.push.recipientEmail;
        pushJ["triggerPercent"] = config.push.triggerPercent;
        pushJ["sendHour"] = config.push.sendHour;
        pushJ["sendMinute"] = config.push.sendMinute;
        pushJ["reminderCycle"] = config.push.reminderCycle;
        pushJ["reminderWeekday"] = config.push.reminderWeekday;
        pushJ["subjectTemplate"] = config.push.subjectTemplate;
        pushJ["bodyTemplate"] = config.push.bodyTemplate;
        std::string cfgJson;
        serializeJson(cfgDoc, cfgJson);

        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Content-Disposition",
            String("attachment; filename=\"") + filename + "\"");
        resp->printf("{\"exportVersion\":1,\"exportedAt\":%lu,\"config\":",
            static_cast<unsigned long>(t));
        resp->print(cfgJson.c_str());
        resp->print(",\"history\":[");
        bool firstEntry = true;
        for (const auto& e : entries) {
            if (!firstEntry) resp->print(',');
            firstEntry = false;
            char buf[40];
            snprintf(buf, sizeof(buf), "{\"ts\":%lu,\"v\":%.1f}",
                static_cast<unsigned long>(e.timestamp), e.value);
            resp->print(buf);
        }
        resp->print("]}");
        request->send(resp);
    });

    // POST /api/import – Konfiguration und Verlaufsdaten aus JSON-Backup wiederherstellen
    server_.on("/api/import", HTTP_POST, web::answerIfPending, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = web::collectBody(request, data, len, index, total, kMaxImportBodyBytes,
                                       "{\"error\":\"import_payload_too_large\"}");
        if (body == nullptr) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"invalid_json\"}");
            return;
        }
        if ((doc["exportVersion"] | 0) != 1) {
            request->send(400, "application/json", "{\"error\":\"unsupported_export_version\"}");
            return;
        }

        bool configRestored = false;
        bool historyRestored = false;

        const JsonVariantConst cfg = doc["config"];
        const bool hasConfig = !cfg.isNull();
        if (hasConfig) {
            Config config;
            ConfigStore::getInstance().load(config);
            config.behaelterhoehe = cfg["behaelterhoehe"] | config.behaelterhoehe;
            config.offset = cfg["offset"] | config.offset;
            config.sampleIntervalSeconds = cfg["sampleIntervalSeconds"] | config.sampleIntervalSeconds;
            config.sensorType = jsonString(cfg, "sensorType", config.sensorType);
            config.tofRoiSize = cfg["tofRoiSize"] | config.tofRoiSize;

            const JsonVariantConst wifi = cfg["wifi"];
            config.wifi.ssid = jsonString(wifi, "ssid", config.wifi.ssid);
            applySecret(config.wifi.password, wifi, "password", true);
            config.wifi.deviceName = jsonString(wifi, "deviceName", config.wifi.deviceName);
            config.wifi.ntpServerPrimary = jsonString(wifi, "ntpServerPrimary", config.wifi.ntpServerPrimary);
            config.wifi.ntpServerSecondary = jsonString(wifi, "ntpServerSecondary", config.wifi.ntpServerSecondary);

            const JsonVariantConst staticIp = cfg["staticIp"];
            config.staticIp.ip = jsonString(staticIp, "ip", config.staticIp.ip);
            config.staticIp.gateway = jsonString(staticIp, "gateway", config.staticIp.gateway);
            config.staticIp.subnet = jsonString(staticIp, "subnet", config.staticIp.subnet);
            config.staticIp.dns = jsonString(staticIp, "dns", config.staticIp.dns);

            const JsonVariantConst mqtt = cfg["mqtt"];
            config.mqtt.server = jsonString(mqtt, "server", config.mqtt.server);
            config.mqtt.port = mqtt["port"] | config.mqtt.port;
            config.mqtt.user = jsonString(mqtt, "user", config.mqtt.user);
            applySecret(config.mqtt.password, mqtt, "password", true);
            config.mqtt.discovery = mqtt["discovery"] | config.mqtt.discovery;

            const JsonVariantConst push = cfg["push"];
            PushConfig& p = config.push;
            p.enabled = push["enabled"] | p.enabled;
            p.smtpServer = jsonString(push, "smtpServer", p.smtpServer);
            p.smtpPort = push["smtpPort"] | p.smtpPort;
            p.useSsl = push["useSsl"] | p.useSsl;
            p.startTls = push["startTls"] | p.startTls;
            p.smtpSkipCertVerify = push["smtpSkipCertVerify"] | p.smtpSkipCertVerify;
            p.authUser = jsonString(push, "authUser", p.authUser);
            applySecret(p.authPassword, push, "authPassword", true);
            p.senderName = jsonString(push, "senderName", p.senderName);
            p.senderEmail = jsonString(push, "senderEmail", p.senderEmail);
            p.recipientEmail = jsonString(push, "recipientEmail", p.recipientEmail);
            p.triggerPercent = push["triggerPercent"] | p.triggerPercent;
            p.sendHour = push["sendHour"] | p.sendHour;
            p.sendMinute = push["sendMinute"] | p.sendMinute;
            p.reminderCycle = jsonString(push, "reminderCycle", p.reminderCycle);
            p.reminderWeekday = push["reminderWeekday"] | p.reminderWeekday;
            p.subjectTemplate = jsonString(push, "subjectTemplate", p.subjectTemplate);
            p.bodyTemplate = jsonString(push, "bodyTemplate", p.bodyTemplate);

            ConfigRules::sanitize(config);  // Backups koennen veraltet oder manipuliert sein
            configRestored = ConfigStore::getInstance().save(config);
            if (configRestored) {
                applySensorSettings(config);
            }
        }

        if (doc["history"].is<JsonArrayConst>()) {
            const JsonArrayConst arr = doc["history"];
            std::vector<HistoryEntry> entries;
            entries.reserve(std::min(arr.size(), static_cast<size_t>(HistoryManager::kCapacity)));
            for (JsonVariantConst item : arr) {
                const uint32_t ts = item["ts"] | 0U;
                if (ts > 0) entries.push_back({ts, item["v"] | 0.0f});
            }
            HistoryManager::getInstance().restore(entries);
            historyRestored = true;
        }

        if (hasConfig && !configRestored) {
            request->send(500, "application/json", "{\"error\":\"config_restore_failed\",\"configRestored\":false}");
            return;
        }

        char respBuf[100];
        snprintf(respBuf, sizeof(respBuf),
            "{\"status\":\"ok\",\"configRestored\":%s,\"historyRestored\":%s}",
            configRestored ? "true" : "false",
            historyRestored ? "true" : "false");
        request->send(200, "application/json", respBuf);
    });

    // POST /api/factory-reset – setzt Konfiguration und Historie auf Werkseinstellungen zurueck
    server_.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        ConfigStore::getInstance().reset();
        if (!ConfigStore::getInstance().save(Config())) {
            request->send(500, "application/json", "{\"error\":\"factory_reset_failed\"}");
            return;
        }

        HistoryManager::getInstance().clear();

        // Gespeicherte WLAN-Credentials aus der WiFi-Stack-NVS loeschen.
        WiFi.disconnect(true, true);

        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"factory_reset_started\"}");
        web::scheduleRestart(kRestartDelayMs);  // Antwort vor dem Neustart ausliefern, ohne den Async-Task zu blockieren
    });

    server_.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        web::scheduleRestart(1000);
    });

    // Root explizit bedienen; eigentliche Datei-Auslieferung macht der Static-Handler.
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/index.html");
    });

    // Explizite Route fuer index.html als robuste Absicherung gegen Static-Fallback-Probleme.
    server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!web::ensureLittleFsMounted()) {
            request->send(503, "application/json", "{\"error\":\"ui_not_available\"}");
            return;
        }

        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
        response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        request->send(response);
    });

    // Serve static files from LittleFS (nach API-Routen registrieren)
    // no-store: verhindert, dass der Browser JS/CSS nach einem OTA-Update aus dem Cache laed
    // und so die alte UI anzeigt obwohl neue Dateien geflasht wurden.
    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-store");

    server_.onNotFound([this](AsyncWebServerRequest *request) {
        const String url = request->url();
        const bool isApiRoute = url.startsWith("/api/");
        const bool isWsRoute = url == "/ws";
        const int lastSlash = url.lastIndexOf('/');
        const int lastDot = url.lastIndexOf('.');
        const bool hasExtension = lastDot > lastSlash;

        // SPA-Fallback nur fuer URL-Pfade ohne Dateiendung (z. B. /dashboard).
        // Fuer Asset-Dateien (/assets/*.js, /favicon.svg, ...) bleibt 404 korrekt,
        // damit Browser kein HTML als Script/CSS bekommt.
        if (request->method() == HTTP_GET && !isApiRoute && !isWsRoute && !hasExtension) {
            if (!web::ensureLittleFsMounted()) {
                request->send(503, "application/json", "{\"error\":\"ui_not_available\"}");
                return;
            }

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
            response->addHeader("Pragma", "no-cache");
            response->addHeader("Expires", "0");
            request->send(response);
            return;
        }

        JsonDocument doc;
        doc["error"] = "route_not_found";
        doc["url"] = request->url();
        doc["method"] = request->methodToString();
        std::string json;
        serializeJson(doc, json);
        request->send(404, "application/json", json.c_str());
    });
}
