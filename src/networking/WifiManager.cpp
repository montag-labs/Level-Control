#include "WifiManager.h"
#include "DebugLogger.h"
#include <esp_sntp.h>
#include <esp_netif.h>
#include <cctype>
#include "StringUtils.h"

namespace {
bool hasStaticIpConfig(const StaticIpConfig& config) {
    return !config.ip.empty() && !config.subnet.empty() && !config.dns.empty();
}

IPAddress parseIpOrDefault(const std::string& value) {
    IPAddress address(static_cast<uint32_t>(0U));
    if (!value.empty()) {
        address.fromString(value.c_str());
    }
    return address;
}

std::string normalizeDnsHostname(const std::string& deviceName) {
    std::string normalized;
    normalized.reserve(deviceName.size());

    bool previousWasHyphen = false;
    for (unsigned char rawChar : strutil::trim(deviceName)) {
        const char ch = static_cast<char>(std::tolower(rawChar));
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            normalized.push_back(ch);
            previousWasHyphen = false;
            continue;
        }

        if (!previousWasHyphen && !normalized.empty()) {
            normalized.push_back('-');
            previousWasHyphen = true;
        }
    }

    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }

    if (normalized.empty()) {
        normalized = "salzstand";
    }

    if (normalized.size() > 63) {
        normalized.resize(63);
        while (!normalized.empty() && normalized.back() == '-') {
            normalized.pop_back();
        }
    }

    return normalized.empty() ? "salzstand" : normalized;
}

bool applyStationHostname(const std::string& hostname, std::string& detail) {
    bool ok = true;

    if (!WiFi.setHostname(hostname.c_str())) {
        ok = false;
        detail += "WiFi.setHostname fehlgeschlagen";
    }

    esp_netif_t* staNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (staNetif == nullptr) {
        ok = false;
        if (!detail.empty()) {
            detail += "; ";
        }
        detail += "STA-Netif nicht gefunden";
        return false;
    }

    const esp_err_t setErr = esp_netif_set_hostname(staNetif, hostname.c_str());
    if (setErr != ESP_OK) {
        ok = false;
        if (!detail.empty()) {
            detail += "; ";
        }
        detail += std::string("esp_netif_set_hostname: ") + esp_err_to_name(setErr);
    }

    return ok;
}
}

WifiManager& WifiManager::getInstance() {
    static WifiManager instance;
    return instance;
}

WifiManager::WifiManager() {
    Config fullConfig;
    if (ConfigStore::getInstance().load(fullConfig)) {
        config_ = fullConfig.wifi;
        staticConfig_ = fullConfig.staticIp;
    }
}

void WifiManager::init() {
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    DebugLogger::getInstance().log(LogLevel::INFO, "WiFi powersave disabled for stable OTA/upload throughput");

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WiFi connected");
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        resetReconnectBackoff();
        std::string ip = WiFi.localIP().toString().c_str();
        DebugLogger::getInstance().log(LogLevel::INFO, std::string("WiFi got IP: ") + ip);
        // NTP synchronisieren (UTC, kein Sommer-/Winterzeit-Offset nötig –
        // das Dashboard rechnet im Browser in Ortszeit um)
        configTime(0, 0, config_.ntpServerPrimary.c_str(), config_.ntpServerSecondary.c_str());
        DebugLogger::getInstance().log(
            LogLevel::INFO,
            "NTP sync configured with primary=" + config_.ntpServerPrimary + ", secondary=" + config_.ntpServerSecondary
        );
        // Kein MQTT-Connect im WiFi-Event-Task: wird im main loop verarbeitet.
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        int reason = info.wifi_sta_disconnected.reason;
        DebugLogger::getInstance().log(LogLevel::WARN, "WiFi disconnected (reason=" + std::to_string(reason) + ")");
        scheduleReconnect();
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void WifiManager::process() {
    if (restartScheduled_) {
        const long remainingMs = static_cast<long>(restartAtMs_ - millis());
        if (remainingMs <= 0) {
            restartScheduled_ = false;
            DebugLogger::getInstance().log(LogLevel::INFO, "Scheduled restart");
            delay(50);
            ESP.restart();
            return;
        }
    }

    if (!reconnectPending_) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        reconnectPending_ = false;
        return;
    }

    unsigned long now = millis();
    if (now < nextReconnectAttemptMs_) {
        return;
    }

    reconnectPending_ = false;
    DebugLogger::getInstance().log(LogLevel::INFO, "WiFi reconnect attempt...");
    applyStationIdentity();
    WiFi.begin(config_.ssid.c_str(), config_.password.c_str());
}

bool WifiManager::connect() {
    if (config_.ssid.empty()) return false;

    reconnectPending_ = false;
    resetReconnectBackoff();
    applyIpConfig();

    applyStationIdentity();
    WiFi.begin(config_.ssid.c_str(), config_.password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    bool connected = (WiFi.status() == WL_CONNECTED);
    if (!connected) {
        scheduleReconnect();
    }
    return connected;
}

bool WifiManager::beginConnectAsync() {
    if (config_.ssid.empty()) {
        return false;
    }

    reconnectPending_ = false;
    resetReconnectBackoff();
    applyIpConfig();

    const std::string hostname = getDnsHostname();
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    std::string hostnameDetail;
    const bool hostnameOk = applyStationHostname(hostname, hostnameDetail);
    if (hostnameOk) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WiFi hostname set to " + hostname + " (AP+STA)");
    } else {
        DebugLogger::getInstance().log(LogLevel::WARN, "WiFi hostname not fully applied: " + hostnameDetail);
    }
    WiFi.begin(config_.ssid.c_str(), config_.password.c_str());
    return true;
}

void WifiManager::scheduleRestart(uint32_t delayMs) {
    restartScheduled_ = true;
    restartAtMs_ = millis() + delayMs;
}

unsigned long WifiManager::getRestartRemainingMs() const {
    if (!restartScheduled_) {
        return 0;
    }

    const long remainingMs = static_cast<long>(restartAtMs_ - millis());
    return remainingMs > 0 ? static_cast<unsigned long>(remainingMs) : 0;
}

void WifiManager::scheduleReconnect() {
    if (config_.ssid.empty()) {
        reconnectPending_ = false;
        return;
    }

    unsigned long now = millis();
    nextReconnectAttemptMs_ = now + reconnectBackoffMs_;
    reconnectPending_ = true;

    DebugLogger::getInstance().log(
        LogLevel::WARN,
        "WiFi reconnect scheduled in " + std::to_string(reconnectBackoffMs_) + " ms"
    );

    if (reconnectBackoffMs_ < reconnectBackoffMaxMs_) {
        reconnectBackoffMs_ *= 2;
        if (reconnectBackoffMs_ > reconnectBackoffMaxMs_) {
            reconnectBackoffMs_ = reconnectBackoffMaxMs_;
        }
    }
}

void WifiManager::resetReconnectBackoff() {
    reconnectPending_ = false;
    reconnectBackoffMs_ = reconnectBackoffMinMs_;
}

void WifiManager::startAP(const std::string& ssid, const std::string& password, uint8_t maxConnections) {
    // AP+STA erlaubt Netzwerkscan ohne wiederholte Mode-Wechsel waehrend des Setups.
    WiFi.mode(WIFI_AP_STA);
    const bool started = WiFi.softAP(ssid.c_str(), password.c_str(), 1, 0, maxConnections);
    if (started) {
        DebugLogger::getInstance().log(
            LogLevel::INFO,
            "AP started: " + ssid + " (max clients=" + std::to_string(maxConnections) + ")"
        );
    } else {
        DebugLogger::getInstance().log(LogLevel::ERROR, "Failed to start AP: " + ssid);
    }
}

std::vector<WifiNetwork> WifiManager::scanNetworks() {
    WiFi.scanDelete();
    // Passiver Scan reduziert Unterbrechungen von AP-Clients waehrend der Suche.
    int n = WiFi.scanNetworks(false, true, true, 120);
    if (n < 0) {
        delay(250);
        WiFi.scanDelete();
        n = WiFi.scanNetworks(false, true, true, 120);
    }

    std::vector<WifiNetwork> networks;
    if (n <= 0) {
        return networks;
    }

    for (int i = 0; i < n; ++i) {
        networks.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i)});
    }
    WiFi.scanDelete();
    return networks;
}

bool WifiManager::setConfig(const WifiConfig& config, const StaticIpConfig& staticConfig) {
    Config fullConfig;
    ConfigStore::getInstance().load(fullConfig);  // ohne gespeicherte Konfiguration bleiben die Defaults
    fullConfig.wifi = config;
    fullConfig.staticIp = staticConfig;
    ConfigStore::sanitize(fullConfig);
    if (!ConfigStore::getInstance().save(fullConfig)) {
        return false;
    }
    config_ = fullConfig.wifi;
    staticConfig_ = fullConfig.staticIp;
    return true;
}

WifiConfig WifiManager::getConfig() const {
    return config_;
}

std::string WifiManager::getDnsHostname() const {
    return normalizeDnsHostname(config_.deviceName);
}

std::string WifiManager::getLocalUrl() const {
    return std::string("http://") + getDnsHostname() + "/";
}

void WifiManager::applyIpConfig() {
    if (hasStaticIpConfig(staticConfig_)) {
        WiFi.config(parseIpOrDefault(staticConfig_.ip), parseIpOrDefault(staticConfig_.gateway),
                    parseIpOrDefault(staticConfig_.subnet), parseIpOrDefault(staticConfig_.dns));
    } else {
        const IPAddress emptyIp(static_cast<uint32_t>(0U));
        WiFi.config(emptyIp, emptyIp, emptyIp, emptyIp);
    }
}

void WifiManager::applyStationIdentity() {
    const std::string hostname = getDnsHostname();
    // Ein laufender Setup-AP (Fallback nach fehlgeschlagenem Verbindungsaufbau) darf beim erneuten
    // Verbindungsversuch nicht abgeschaltet werden, sonst ist das Geraet bei falschem Passwort unerreichbar.
    WiFi.mode((WiFi.getMode() & WIFI_AP) != 0 ? WIFI_AP_STA : WIFI_STA);
    WiFi.setSleep(false);
    std::string hostnameDetail;
    const bool hostnameOk = applyStationHostname(hostname, hostnameDetail);
    if (hostnameOk) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WiFi hostname set to " + hostname);
    } else {
        DebugLogger::getInstance().log(LogLevel::WARN, "WiFi hostname not fully applied: " + hostnameDetail);
    }
}