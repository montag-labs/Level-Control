#include "ConfigStore.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <cmath>
#include "StringUtils.h"

namespace {
// Kleine, bewusst nicht inlinebare Hilfsfunktionen: ArduinoJson-Templates pro Feld zu inlinen
// kostet im Flash ein Vielfaches (ConfigStore war vorher mit Abstand die groesste Objektdatei).
[[gnu::noinline]] std::string getStr(JsonVariantConst node, const char* key, const char* fallback) {
    const char* value = node[key] | fallback;
    return value;
}
[[gnu::noinline]] int getInt(JsonVariantConst node, const char* key, int fallback) {
    return node[key] | fallback;
}
[[gnu::noinline]] unsigned long getUlong(JsonVariantConst node, const char* key, unsigned long fallback) {
    return node[key] | fallback;
}
[[gnu::noinline]] float getFloat(JsonVariantConst node, const char* key, float fallback) {
    return node[key] | fallback;
}
[[gnu::noinline]] bool getBool(JsonVariantConst node, const char* key, bool fallback) {
    return node[key] | fallback;
}

[[gnu::noinline]] void putStr(JsonObject node, const char* key, const std::string& value) { node[key] = value; }
[[gnu::noinline]] void putInt(JsonObject node, const char* key, int value) { node[key] = value; }
[[gnu::noinline]] void putUlong(JsonObject node, const char* key, unsigned long value) { node[key] = value; }
[[gnu::noinline]] void putFloat(JsonObject node, const char* key, float value) { node[key] = value; }
[[gnu::noinline]] void putBool(JsonObject node, const char* key, bool value) { node[key] = value; }

bool isFiniteFloat(float value) {
    return std::isfinite(static_cast<double>(value));
}

bool validPort(int port) {
    return port >= 1 && port <= 65535;
}

void addReason(std::vector<std::string>* reasons, const char* text) {
    if (reasons != nullptr) {
        reasons->emplace_back(text);
    }
}

std::string joinReasons(const std::vector<std::string>& reasons) {
    std::string joined;
    for (const std::string& reason : reasons) {
        if (!joined.empty()) {
            joined += "; ";
        }
        joined += reason;
    }
    return joined;
}

const PushConfig& pushDefaults() {
    static const PushConfig defaults;
    return defaults;
}
}  // namespace

ConfigStore& ConfigStore::getInstance() {
    static ConfigStore instance;
    return instance;
}

std::string ConfigStore::normalizeReminderCycle(const std::string& value) {
    return (value == "week" || value == "month") ? value : "day";
}

int ConfigStore::normalizeReminderWeekday(int value) {
    return (value < 1 || value > 7) ? 1 : value;
}

std::string ConfigStore::reminderCycleFromLegacyMinutes(unsigned long cycleMinutes) {
    if (cycleMinutes >= 43200UL) {
        return "month";
    }
    return cycleMinutes >= 10080UL ? "week" : "day";
}

std::string ConfigStore::normalizePushEncryptionMode(const std::string& rawMode) {
    const std::string mode = strutil::toLower(rawMode);
    if (mode == "ssl" || mode == "tls") {
        return "ssl";
    }
    if (mode == "starttls" || mode == "start_tls" || mode == "start-tls") {
        return "starttls";
    }
    return "none";
}

std::string ConfigStore::derivePushEncryptionMode(bool useSsl, bool startTls) {
    if (useSsl) {
        return "ssl";
    }
    return startTls ? "starttls" : "none";
}

void ConfigStore::sanitize(Config& config, std::vector<std::string>* reasons) {
    const PushConfig& pushDefault = pushDefaults();
    const WifiConfig wifiDefault;

    config.wifi.deviceName = strutil::trim(config.wifi.deviceName);
    if (config.wifi.deviceName.empty()) {
        config.wifi.deviceName = wifiDefault.deviceName;
    }
    config.wifi.ntpServerPrimary = strutil::trim(config.wifi.ntpServerPrimary);
    if (config.wifi.ntpServerPrimary.empty()) {
        config.wifi.ntpServerPrimary = wifiDefault.ntpServerPrimary;
    }
    config.wifi.ntpServerSecondary = strutil::trim(config.wifi.ntpServerSecondary);
    if (config.wifi.ntpServerSecondary.empty()) {
        config.wifi.ntpServerSecondary = wifiDefault.ntpServerSecondary;
    }

    config.staticIp.ip = strutil::trim(config.staticIp.ip);
    config.staticIp.gateway = strutil::trim(config.staticIp.gateway);
    config.staticIp.subnet = strutil::trim(config.staticIp.subnet);
    config.staticIp.dns = strutil::trim(config.staticIp.dns);
    const bool anyStaticIp = !config.staticIp.ip.empty() || !config.staticIp.gateway.empty()
        || !config.staticIp.subnet.empty() || !config.staticIp.dns.empty();
    // Das Gateway ist optional (Web-UI, /api/wifi und WifiManager verlangen nur IP, Subnetz und DNS).
    const bool fullStaticIp = !config.staticIp.ip.empty()
        && !config.staticIp.subnet.empty() && !config.staticIp.dns.empty();
    if (anyStaticIp && !fullStaticIp) {
        config.staticIp = {};
        addReason(reasons, "unvollstaendige staticIp verworfen");
    }

    if (!validPort(config.mqtt.port)) {
        config.mqtt.port = 1883;
        addReason(reasons, "ungueltiger MQTT-Port auf 1883 gesetzt");
    }

    PushConfig& push = config.push;
    if (!validPort(push.smtpPort)) {
        push.smtpPort = pushDefault.smtpPort;
        addReason(reasons, "ungueltiger SMTP-Port auf 587 gesetzt");
    }
    if (push.useSsl && push.startTls) {
        push.startTls = false;
    }
    if (!isFiniteFloat(push.triggerPercent)) {
        push.triggerPercent = pushDefault.triggerPercent;
    }
    push.triggerPercent = std::fmin(std::fmax(push.triggerPercent, 0.0f), 100.0f);
    push.sendHour = push.sendHour < 0 ? 0 : (push.sendHour > 23 ? 23 : push.sendHour);
    push.sendMinute = push.sendMinute < 0 ? 0 : (push.sendMinute > 59 ? 59 : push.sendMinute);
    if (push.senderName.empty()) {
        push.senderName = pushDefault.senderName;
    }
    push.reminderCycle = normalizeReminderCycle(push.reminderCycle);
    push.reminderWeekday = normalizeReminderWeekday(push.reminderWeekday);
    if (push.subjectTemplate.empty()) {
        push.subjectTemplate = pushDefault.subjectTemplate;
    }
    if (push.bodyTemplate.empty()) {
        push.bodyTemplate = pushDefault.bodyTemplate;
    }

    if (!isFiniteFloat(config.behaelterhoehe)
        || config.behaelterhoehe < ConfigLimits::kMinTankHeightCm
        || config.behaelterhoehe > ConfigLimits::kMaxTankHeightCm) {
        config.behaelterhoehe = Config().behaelterhoehe;
        addReason(reasons, "ungueltige Behaelterhoehe auf 95 gesetzt");
    }
    if (!isFiniteFloat(config.offset) || std::fabs(config.offset) > ConfigLimits::kMaxOffsetCm) {
        config.offset = 0.0f;
        addReason(reasons, "ungueltiger Offset auf 0 gesetzt");
    }
    if (config.sampleIntervalSeconds < ConfigLimits::kMinSampleIntervalSeconds) {
        config.sampleIntervalSeconds = ConfigLimits::kMinSampleIntervalSeconds;
        addReason(reasons, "sampleIntervalSeconds < 5 auf 5 gesetzt");
    } else if (config.sampleIntervalSeconds > ConfigLimits::kMaxSampleIntervalSeconds) {
        config.sampleIntervalSeconds = ConfigLimits::kMaxSampleIntervalSeconds;
        addReason(reasons, "sampleIntervalSeconds auf Maximum begrenzt");
    }
    if (config.sensorType != "vl53l1x") {
        config.sensorType = "rcwl1670";
    }
}

bool ConfigStore::load(Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cacheValid_) {
        config = cache_;
        return true;
    }

    std::string jsonStr;
    {
        Preferences prefs;
        if (!prefs.begin(nvsNamespace_, true)) return false;  // Read-only
        jsonStr = prefs.getString("config", "").c_str();
        prefs.end();
    }
    if (jsonStr.empty()) return false;

    Config parsed;
    JsonDocument doc;
    if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) {
        loadFallbackApplied_ = true;
        loadFallbackMessage_ = "Konfiguration war ungueltig und wurde auf Defaults zurueckgesetzt";
    } else {
        const JsonVariantConst root = doc.as<JsonVariantConst>();
        const JsonVariantConst wifi = root["wifi"];
        const JsonVariantConst staticIp = root["staticIp"];
        const JsonVariantConst mqtt = root["mqtt"];
        const JsonVariantConst push = root["push"];
        const PushConfig& pushDefault = pushDefaults();

        parsed.version = getInt(root, "version", 1);
        parsed.wifi.ssid = getStr(wifi, "ssid", "");
        parsed.wifi.password = getStr(wifi, "password", "");
        parsed.wifi.deviceName = getStr(wifi, "deviceName", parsed.wifi.deviceName.c_str());
        parsed.wifi.ntpServerPrimary = getStr(wifi, "ntpServerPrimary", parsed.wifi.ntpServerPrimary.c_str());
        parsed.wifi.ntpServerSecondary = getStr(wifi, "ntpServerSecondary", parsed.wifi.ntpServerSecondary.c_str());
        parsed.staticIp.ip = getStr(staticIp, "ip", "");
        parsed.staticIp.gateway = getStr(staticIp, "gateway", "");
        parsed.staticIp.subnet = getStr(staticIp, "subnet", "");
        parsed.staticIp.dns = getStr(staticIp, "dns", "");
        parsed.mqtt.server = getStr(mqtt, "server", "");
        parsed.mqtt.port = getInt(mqtt, "port", parsed.mqtt.port);
        parsed.mqtt.user = getStr(mqtt, "user", "");
        parsed.mqtt.password = getStr(mqtt, "password", "");
        parsed.mqtt.discovery = getBool(mqtt, "discovery", parsed.mqtt.discovery);

        PushConfig& p = parsed.push;
        p.enabled = getBool(push, "enabled", false);
        p.smtpServer = getStr(push, "smtpServer", "");
        p.smtpPort = getInt(push, "smtpPort", pushDefault.smtpPort);
        p.useSsl = getBool(push, "useSsl", false);
        p.startTls = getBool(push, "startTls", false);
        // Rueckwaertskompatibilitaet: altes NVS ohne das Feld -> Zertifikatspruefung deaktivieren,
        // damit bestehende SMTP-Konfigurationen nicht sofort brechen.
        p.smtpSkipCertVerify = getBool(push, "smtpSkipCertVerify", true);
        p.authUser = getStr(push, "authUser", "");
        p.authPassword = getStr(push, "authPassword", "");
        p.senderName = getStr(push, "senderName", pushDefault.senderName.c_str());
        p.senderEmail = getStr(push, "senderEmail", "");
        p.recipientEmail = getStr(push, "recipientEmail", "");
        p.triggerPercent = getFloat(push, "triggerPercent", pushDefault.triggerPercent);
        p.sendHour = getInt(push, "sendHour", pushDefault.sendHour);
        p.sendMinute = getInt(push, "sendMinute", pushDefault.sendMinute);
        // Aeltere Firmware speicherte nur cycleMinutes statt reminderCycle.
        const std::string storedCycle = getStr(push, "reminderCycle", "");
        p.reminderCycle = storedCycle.empty()
            ? reminderCycleFromLegacyMinutes(getUlong(push, "cycleMinutes", 1440UL))
            : storedCycle;
        p.reminderWeekday = getInt(push, "reminderWeekday", pushDefault.reminderWeekday);
        p.subjectTemplate = getStr(push, "subjectTemplate", pushDefault.subjectTemplate.c_str());
        p.bodyTemplate = getStr(push, "bodyTemplate", pushDefault.bodyTemplate.c_str());

        parsed.behaelterhoehe = getFloat(root, "behaelterhoehe", parsed.behaelterhoehe);
        parsed.offset = getFloat(root, "offset", parsed.offset);
        parsed.sampleIntervalSeconds = getUlong(root, "sampleIntervalSeconds", parsed.sampleIntervalSeconds);
        parsed.sensorType = getStr(root, "sensorType", parsed.sensorType.c_str());
        parsed.adminPasswordHash = getStr(root, "adminPasswordHash", "");

        std::vector<std::string> fallbackReasons;
        sanitize(parsed, &fallbackReasons);
        if (!fallbackReasons.empty()) {
            loadFallbackApplied_ = true;
            loadFallbackMessage_ = joinReasons(fallbackReasons);
        }
    }

    cache_ = parsed;
    cacheValid_ = true;
    config = parsed;
    return true;
}

bool ConfigStore::wasFallbackApplied() const {
    return loadFallbackApplied_;
}

std::string ConfigStore::getFallbackMessage() const {
    return loadFallbackMessage_;
}

bool ConfigStore::save(const Config& input) {
    Config config = input;
    sanitize(config);

    JsonDocument doc;
    const JsonObject root = doc.to<JsonObject>();
    putInt(root, "version", config.version);

    const JsonObject wifi = root["wifi"].to<JsonObject>();
    putStr(wifi, "ssid", config.wifi.ssid);
    putStr(wifi, "password", config.wifi.password);
    putStr(wifi, "deviceName", config.wifi.deviceName);
    putStr(wifi, "ntpServerPrimary", config.wifi.ntpServerPrimary);
    putStr(wifi, "ntpServerSecondary", config.wifi.ntpServerSecondary);

    const JsonObject staticIp = root["staticIp"].to<JsonObject>();
    putStr(staticIp, "ip", config.staticIp.ip);
    putStr(staticIp, "gateway", config.staticIp.gateway);
    putStr(staticIp, "subnet", config.staticIp.subnet);
    putStr(staticIp, "dns", config.staticIp.dns);

    const JsonObject mqtt = root["mqtt"].to<JsonObject>();
    putStr(mqtt, "server", config.mqtt.server);
    putInt(mqtt, "port", config.mqtt.port);
    putStr(mqtt, "user", config.mqtt.user);
    putStr(mqtt, "password", config.mqtt.password);
    putBool(mqtt, "discovery", config.mqtt.discovery);

    const JsonObject push = root["push"].to<JsonObject>();
    const PushConfig& p = config.push;
    putBool(push, "enabled", p.enabled);
    putStr(push, "smtpServer", p.smtpServer);
    putInt(push, "smtpPort", p.smtpPort);
    putBool(push, "useSsl", p.useSsl);
    putBool(push, "startTls", p.startTls);
    putBool(push, "smtpSkipCertVerify", p.smtpSkipCertVerify);
    putStr(push, "authUser", p.authUser);
    putStr(push, "authPassword", p.authPassword);
    putStr(push, "senderName", p.senderName);
    putStr(push, "senderEmail", p.senderEmail);
    putStr(push, "recipientEmail", p.recipientEmail);
    putFloat(push, "triggerPercent", p.triggerPercent);
    putInt(push, "sendHour", p.sendHour);
    putInt(push, "sendMinute", p.sendMinute);
    putStr(push, "reminderCycle", p.reminderCycle);
    putInt(push, "reminderWeekday", p.reminderWeekday);
    // Aeltere Firmware-Staende lesen noch cycleMinutes statt reminderCycle.
    putUlong(push, "cycleMinutes", p.reminderCycle == "month" ? 43200UL : (p.reminderCycle == "week" ? 10080UL : 1440UL));
    putStr(push, "subjectTemplate", p.subjectTemplate);
    putStr(push, "bodyTemplate", p.bodyTemplate);

    putFloat(root, "behaelterhoehe", config.behaelterhoehe);
    putFloat(root, "offset", config.offset);
    putUlong(root, "sampleIntervalSeconds", config.sampleIntervalSeconds);
    putStr(root, "sensorType", config.sensorType);
    putStr(root, "adminPasswordHash", config.adminPasswordHash);

    std::string jsonStr;
    serializeJson(doc, jsonStr);

    std::lock_guard<std::mutex> lock(mutex_);
    Preferences prefs;
    if (!prefs.begin(nvsNamespace_, false)) return false;  // Read-write
    const bool success = prefs.putString("config", jsonStr.c_str()) > 0;
    prefs.end();

    if (success) {
        cache_ = config;
        cacheValid_ = true;
    } else {
        cacheValid_ = false;
    }
    return success;
}

void ConfigStore::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    Preferences prefs;
    if (prefs.begin(nvsNamespace_, false)) {
        prefs.clear();
        prefs.end();
    }
    cacheValid_ = false;
}
