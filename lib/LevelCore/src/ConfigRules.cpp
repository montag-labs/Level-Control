#include "ConfigRules.h"

#include <cmath>

#include "StringUtils.h"

namespace {
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
const PushConfig& pushDefaults() {
    static const PushConfig defaults;
    return defaults;
}
}  // namespace

namespace ConfigRules {

std::string normalizeReminderCycle(const std::string& value) {
    return (value == "week" || value == "month") ? value : "day";
}

int normalizeReminderWeekday(int value) {
    return (value < 1 || value > 7) ? 1 : value;
}

std::string reminderCycleFromLegacyMinutes(unsigned long cycleMinutes) {
    if (cycleMinutes >= 43200UL) {
        return "month";
    }
    return cycleMinutes >= 10080UL ? "week" : "day";
}

std::string normalizePushEncryptionMode(const std::string& rawMode) {
    const std::string mode = strutil::toLower(rawMode);
    if (mode == "ssl" || mode == "tls") {
        return "ssl";
    }
    if (mode == "starttls" || mode == "start_tls" || mode == "start-tls") {
        return "starttls";
    }
    return "none";
}

std::string derivePushEncryptionMode(bool useSsl, bool startTls) {
    if (useSsl) {
        return "ssl";
    }
    return startTls ? "starttls" : "none";
}

void sanitize(Config& config, std::vector<std::string>* reasons) {
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
    if (config.tofRoiSize != ConfigLimits::kTofRoiFull && config.tofRoiSize != ConfigLimits::kTofRoiMedium
        && config.tofRoiSize != ConfigLimits::kTofRoiNarrow) {
        config.tofRoiSize = ConfigLimits::kTofRoiFull;
    }
}

}  // namespace ConfigRules
