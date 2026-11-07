#include <string>
#include "WebServerDashboard.h"
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "GeneratedVersion.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <mbedtls/version.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include "ConfigStore.h"
#include "HistoryManager.h"
#include "MqttManager.h"
#include "StringUtils.h"
#include "OtaTrustedRoots.h"
#include "PushNotificationManager.h"
#include "ReleaseSigningPublicKey.h"
#include "SensorManager.h"
#include "WifiManager.h"

#if MBEDTLS_VERSION_MAJOR >= 3
// Arduino-ESP32 3.x (mbedTLS 3) hat die *_ret-Varianten entfernt; die Basisfunktionen liefern jetzt int.
#define mbedtls_sha256_starts_ret mbedtls_sha256_starts
#define mbedtls_sha256_update_ret mbedtls_sha256_update
#define mbedtls_sha256_finish_ret mbedtls_sha256_finish
#endif

namespace {
constexpr char kLatestReleaseApiUrl[] = LEVEL_CONTROL_LATEST_RELEASE_API_URL;
constexpr char kLatestReleaseUrl[] = LEVEL_CONTROL_LATEST_RELEASE_URL;
constexpr char kReleaseTagBaseUrl[] = LEVEL_CONTROL_RELEASE_TAG_BASE_URL;
constexpr char kUpdateUserAgent[] = "Level-Control-OTA/1.0";
constexpr char kPasswordMask[] = "*****";
constexpr uint32_t kRestartDelayMs = 2500;
constexpr size_t kUploadBufferSize = 4096;
constexpr unsigned long kManifestCacheTtlMs = 300000;
constexpr unsigned long kUploadStallTimeoutMs = 300000;
constexpr unsigned long kAdminSessionTtlMs = 8UL * 60UL * 60UL * 1000UL;
constexpr size_t kMaxConfigBodyBytes = 2048;
constexpr size_t kMaxWifiBodyBytes = 4096;
constexpr size_t kMaxMqttBodyBytes = 2048;
constexpr size_t kMaxImportBodyBytes = 48 * 1024;
constexpr size_t kMaxUpdateRequestBytes = 512;
constexpr size_t kMaxAuthBodyBytes = 512;
constexpr size_t kMinAdminPasswordLength = 8;
constexpr size_t kEmbeddedSigBytes = 72;
constexpr size_t kEmbeddedSigTrailerSize = 8 + 1 + kEmbeddedSigBytes;
const uint8_t kEmbeddedSigMagic[8] = { 'L', 'C', 'S', 'I', 'G', 'V', '1', '!' };

bool mountLittleFsWithKnownLabels() {
    // Custom partitions often use label "littlefs"; Arduino default is "spiffs".
    if (LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        return true;
    }
    return LittleFS.begin(false, "/littlefs", 10, "spiffs");
}

struct RemoteUpdateContext {
    WebServerDashboard *dashboard;
    std::string target;
};

bool isMaskedPassword(const std::string& value) {
    return value == "***" || value == kPasswordMask;
}

std::string bytesToHex(const unsigned char *data, size_t length);

std::string sha256Hex(const std::string& value) {
    unsigned char digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts_ret(&context, 0) != 0
        || mbedtls_sha256_update_ret(&context,
            reinterpret_cast<const unsigned char*>(value.data()), value.size()) != 0
        || mbedtls_sha256_finish_ret(&context, digest) != 0) {
        mbedtls_sha256_free(&context);
        return "";
    }
    mbedtls_sha256_free(&context);
    return bytesToHex(digest, sizeof(digest));
}

bool parseVersionNumberPart(const std::string& text, size_t& pos, int& value) {
    if (pos >= text.size() || !isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }

    int parsed = 0;
    while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos]))) {
        const int digit = text[pos] - '0';
        // Prevent integer overflow while parsing malformed version strings.
        if (parsed > (INT_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
        ++pos;
    }

    value = parsed;
    return true;
}

bool parseVersion(const std::string& version, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    if (version.empty()) {
        return false;
    }

    size_t pos = 0;
    if (!parseVersionNumberPart(version, pos, major)) {
        return false;
    }
    if (pos >= version.size() || version[pos] != '.') {
        return false;
    }
    ++pos;

    if (!parseVersionNumberPart(version, pos, minor)) {
        return false;
    }
    if (pos >= version.size() || version[pos] != '.') {
        return false;
    }
    ++pos;

    if (!parseVersionNumberPart(version, pos, patch)) {
        return false;
    }

    return pos == version.size();
}

bool isHttpsUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0;
}

std::string normalizeVersionTag(std::string value) {
    if (!value.empty() && (value[0] == 'v' || value[0] == 'V')) {
        value.erase(0, 1);
    }
    return value;
}

bool hasBinSuffix(const std::string& name) {
    return name.size() >= 4 && name.rfind(".bin") == name.size() - 4;
}

bool isAppAssetName(const std::string& lowerName) {
    if (!hasBinSuffix(lowerName)) {
        return false;
    }
    if (lowerName.find("bootloader") != std::string::npos
        || lowerName.find("partition") != std::string::npos
        || lowerName.find("web-ui") != std::string::npos
        || lowerName.find("webui") != std::string::npos
        || lowerName.find("littlefs") != std::string::npos) {
        return false;
    }
    return lowerName.find("-app.bin") != std::string::npos
        || lowerName.find("firmware") != std::string::npos
        || lowerName.find("app") != std::string::npos;
}

bool isWebUiAssetName(const std::string& lowerName) {
    if (!hasBinSuffix(lowerName)) {
        return false;
    }
    return lowerName.find("-web-ui.bin") != std::string::npos
        || lowerName.find("webui") != std::string::npos
        || lowerName.find("littlefs") != std::string::npos;
}

std::string bytesToHex(const unsigned char *data, size_t length) {
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = data[index];
        result.push_back(hexChars[(value >> 4) & 0x0F]);
        result.push_back(hexChars[value & 0x0F]);
    }
    return result;
}

int compareVersions(const std::string& left, const std::string& right) {
    int leftMajor = 0;
    int leftMinor = 0;
    int leftPatch = 0;
    int rightMajor = 0;
    int rightMinor = 0;
    int rightPatch = 0;

    if (!parseVersion(left, leftMajor, leftMinor, leftPatch) ||
        !parseVersion(right, rightMajor, rightMinor, rightPatch)) {
        return left.compare(right);
    }

    if (leftMajor != rightMajor) {
        return leftMajor < rightMajor ? -1 : 1;
    }
    if (leftMinor != rightMinor) {
        return leftMinor < rightMinor ? -1 : 1;
    }
    if (leftPatch != rightPatch) {
        return leftPatch < rightPatch ? -1 : 1;
    }
    return 0;
}

// Sammelt einen Request-Body ueber mehrere Chunks in einem malloc-Puffer in request->_tempObject
// (die Library gibt ihn mit dem Request frei). Liefert den vollstaendigen, NUL-terminierten Body,
// sobald der letzte Chunk eingetroffen ist, sonst nullptr.
//
// WICHTIG: ESPAsyncWebServer ruft den Body-Handler fuer JEDEN Chunk auf - auch nachdem der erste Chunk
// abgelehnt wurde (Auth, Groesse). Der Aufrufer prueft Auth deshalb nur bei index == 0 und ruft diese
// Funktion danach auf; wurde Chunk 0 abgelehnt, existiert kein Puffer und alle Folge-Chunks werden
// verworfen. Es gibt bewusst keinen "Fallback", der einen Puffer erst bei Folge-Chunks anlegt.
const char* collectBody(AsyncWebServerRequest *request, const uint8_t *data, size_t len, size_t index,
                        size_t total, size_t maxBytes, const char *tooLargeJson) {
    if (index == 0) {
        if (total == 0 || total > maxBytes) {
            request->send(413, "application/json", tooLargeJson);
            return nullptr;
        }
        free(request->_tempObject);
        request->_tempObject = calloc(total + 1, 1);
        if (request->_tempObject == nullptr) {
            request->send(503, "application/json", "{\"error\":\"out_of_memory\"}");
            return nullptr;
        }
    }

    char *buffer = static_cast<char*>(request->_tempObject);
    if (buffer == nullptr || index + len > total) {
        return nullptr;
    }
    memcpy(buffer + index, data, len);
    return (index + len == total) ? buffer : nullptr;
}

// Vergleich ohne vorzeitigen Abbruch (verhindert Timing-Angriffe auf Token/Hash).
bool constantTimeEquals(const char *left, size_t leftLen, const char *right, size_t rightLen) {
    unsigned char diff = leftLen == rightLen ? 0 : 1;
    const size_t common = leftLen < rightLen ? leftLen : rightLen;
    for (size_t index = 0; index < common; ++index) {
        diff |= static_cast<unsigned char>(left[index] ^ right[index]);
    }
    return diff == 0;
}

// Liest einen String; fehlt der Schluessel (oder ist er kein String), bleibt `fallback`.
[[gnu::noinline]] std::string jsonString(JsonVariantConst node, const char *key, const std::string& fallback) {
    const char *value = node[key] | static_cast<const char*>(nullptr);
    return value != nullptr ? std::string(value) : fallback;
}

// Uebernimmt ein Passwort nur, wenn es mitgesendet und nicht die Maske ist.
void applySecret(std::string& target, JsonVariantConst node, const char *key, bool allowEmpty) {
    const char *value = node[key] | static_cast<const char*>(nullptr);
    if (value == nullptr || isMaskedPassword(value) || (!allowEmpty && value[0] == '\0')) {
        return;
    }
    target = value;
}

void applyPushEncryptionModeToConfig(const std::string& mode, bool& useSsl, bool& startTls) {
    const std::string normalized = ConfigStore::normalizePushEncryptionMode(mode);
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
    if (sensor.getSensorType() != config.sensorType) {
        sensor.setSensorType(config.sensorType);
        sensor.init();
    }
}

bool validateUploadFilename(const String& filename, std::string& error) {
    const std::string lowerName = strutil::toLower(filename.c_str());
    if (lowerName.size() < 4 || lowerName.substr(lowerName.size() - 4) != ".bin") {
        error = "Es werden nur .bin-Dateien akzeptiert";
        return false;
    }
    if (lowerName.find("bootloader") != std::string::npos || lowerName.find("partition") != std::string::npos) {
        error = "Bootloader- oder Partitionsdateien dürfen hier nicht hochgeladen werden";
        return false;
    }
    return true;
}

const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "other";
    }
}

bool ensureConfiguredBootPartition(const esp_partition_t* targetPartition, std::string& error) {
    if (!targetPartition) {
        error = "Keine OTA-Zielpartition gefunden";
        return false;
    }

    esp_err_t setErr = esp_ota_set_boot_partition(targetPartition);
    if (setErr != ESP_OK) {
        error = std::string("Boot-Partition konnte nicht gesetzt werden: ") + esp_err_to_name(setErr);
        return false;
    }

    const esp_partition_t* configuredBoot = esp_ota_get_boot_partition();
    if (!configuredBoot || configuredBoot->address != targetPartition->address) {
        error = "Boot-Partition wurde nach OTA nicht korrekt übernommen";
        return false;
    }

    return true;
}
}

WebServerDashboard& WebServerDashboard::getInstance() {
    static WebServerDashboard instance;
    return instance;
}

bool WebServerDashboard::isAdminAuthenticated(AsyncWebServerRequest *request) const {
    // Billige Pruefungen zuerst; die Konfiguration (Passwort gesetzt?) wird nur bei passendem Token geladen.
    if (adminSessionToken_.empty() || (millis() - adminSessionIssuedMs_) > kAdminSessionTtlMs) {
        return false;
    }
    const AsyncWebHeader* header = request->getHeader("X-Admin-Token");
    if (header == nullptr
        || !constantTimeEquals(header->value().c_str(), header->value().length(),
                               adminSessionToken_.c_str(), adminSessionToken_.size())) {
        return false;
    }

    Config config;
    return ConfigStore::getInstance().load(config) && !config.adminPasswordHash.empty();
}

bool WebServerDashboard::requireAdmin(AsyncWebServerRequest *request) {
    if (isAdminAuthenticated(request)) {
        return true;
    }

    request->send(401, "application/json", "{\"error\":\"admin_auth_required\"}");
    return false;
}

void WebServerDashboard::sendSessionToken(AsyncWebServerRequest *request) {
    const std::string json = "{\"status\":\"ok\",\"token\":\"" + issueAdminSession() + "\"}";
    request->send(200, "application/json", json.c_str());
}

std::string WebServerDashboard::issueAdminSession() {
    char token[65];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()));
    adminSessionToken_ = token;
    adminSessionIssuedMs_ = millis();
    return adminSessionToken_;
}

WebServerDashboard::WebServerDashboard() : server_(80), ws_("/ws") {
    littleFsMounted_ = mountLittleFsWithKnownLabels();
    if (!littleFsMounted_) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "LittleFS mount failed");
    }
    server_.addHandler(&ws_);
}

bool WebServerDashboard::ensureLittleFsMounted() {
    if (littleFsMounted_) {
        return true;
    }

    littleFsMounted_ = mountLittleFsWithKnownLabels();
    if (littleFsMounted_) {
        DebugLogger::getInstance().log(LogLevel::INFO, "LittleFS remounted");
    } else {
        DebugLogger::getInstance().log(LogLevel::ERROR, "LittleFS remount failed");
    }
    return littleFsMounted_;
}

void WebServerDashboard::init() {
    setupRoutes();
    setupUpdateRoutes();
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

std::string WebServerDashboard::getInstalledVersion() const {
    return LEVEL_CONTROL_VERSION;
}

void WebServerDashboard::refreshManifestInBackgroundIfStale() {
    const bool cacheExpired = lastManifestCheckMs_ == 0 || (millis() - lastManifestCheckMs_) >= kManifestCacheTtlMs;
    if (cacheExpired && !manifestFetchInProgress_ && WiFi.status() == WL_CONNECTED) {
        startBackgroundManifestFetch();
    }
}

// Wird u. a. aus der Hauptschleife (MQTT) aufgerufen: liefert nur den Cache und holt veraltete Daten im
// Hintergrund-Task nach, statt die Hauptschleife mit einem blockierenden HTTPS-Request anzuhalten.
std::string WebServerDashboard::getAvailableVersion() {
    refreshManifestInBackgroundIfStale();
    return cachedManifest_.valid ? cachedManifest_.version : "";
}

std::string WebServerDashboard::getLatestReleaseUrl() {
    refreshManifestInBackgroundIfStale();
    return cachedManifest_.valid && !cachedManifest_.releaseUrl.empty() ? cachedManifest_.releaseUrl : kLatestReleaseUrl;
}

bool WebServerDashboard::isUpdateInProgress() const {
    return updateState_.inProgress;
}

int WebServerDashboard::getUpdateProgressPercent() const {
    if (updateState_.total == 0) {
        return 0;
    }
    return static_cast<int>(std::min<size_t>(100, (updateState_.received * 100) / updateState_.total));
}

bool WebServerDashboard::requestRepoUpdate(const std::string& target, std::string& error) {
    recoverStuckUploadIfNeeded();

    if (updateState_.inProgress || uploadActive_) {
        error = "Update läuft bereits";
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        error = "Kein WLAN für Repo-Update verfügbar";
        return false;
    }

    if (!refreshManifestCache(true, error)) {
        return false;
    }

    const std::string resolvedTarget = resolveUpdateTarget(cachedManifest_, target, error);
    if (resolvedTarget.empty()) {
        return false;
    }

    startRemoteUpdateTask(resolvedTarget);
    return true;
}

void WebServerDashboard::setupRoutes() {
    server_.on("/api/auth/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config config;
        const bool configured = ConfigStore::getInstance().load(config) && !config.adminPasswordHash.empty();
        JsonDocument doc;
        doc["configured"] = configured;
        doc["authenticated"] = configured && isAdminAuthenticated(request);
        doc["sessionTtlMs"] = kAdminSessionTtlMs;
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
        adminSessionToken_.clear();
        adminSessionIssuedMs_ = 0;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/auth/change", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0 && !requireAdmin(request)) return;
            const char *body = collectBody(request, data, len, index, total, kMaxAuthBodyBytes,
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
            sendSessionToken(request);
        });

    for (const char* route : {"/api/auth/setup", "/api/auth/login"}) {
        const bool isSetup = std::strcmp(route, "/api/auth/setup") == 0;
        server_.on(route, HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
            [this, isSetup](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                const char *body = collectBody(request, data, len, index, total, kMaxAuthBodyBytes,
                                               "{\"error\":\"auth_payload_too_large\"}");
                if (body == nullptr) return;

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
                    || !constantTimeEquals(passwordHash.c_str(), passwordHash.size(),
                                           config.adminPasswordHash.c_str(), config.adminPasswordHash.size())) {
                    request->send(401, "application/json", "{\"error\":\"admin_login_failed\"}");
                    return;
                }
                sendSessionToken(request);
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
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"Config load failed\"}");
        }
    });

    server_.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = collectBody(request, data, len, index, total, kMaxConfigBodyBytes,
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
        ConfigStore::sanitize(config);  // normalisiert u. a. den Sensortyp
        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"sensor_config_save_failed\"}");
            return;
        }
        applySensorSettings(config);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
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

    server_.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = collectBody(request, data, len, index, total, kMaxWifiBodyBytes,
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

    server_.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
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

    server_.on("/api/mqtt", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = collectBody(request, data, len, index, total, kMaxMqttBodyBytes,
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
        ConfigStore::sanitize(config);
        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"mqtt_config_save_failed\"}");
            return;
        }

        // Re-init/connect MQTT with the new settings
        if (!config.mqtt.server.empty()) {
            MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
            MqttManager::getInstance().connect();
        }

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
        Config config;
        if (ConfigStore::getInstance().load(config) && !config.mqtt.server.empty()) {
            MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
            MqttManager::getInstance().connect();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/push", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            JsonDocument doc;
            doc["enabled"] = config.push.enabled;
            doc["smtpServer"] = config.push.smtpServer;
            doc["smtpPort"] = config.push.smtpPort;
            doc["useSsl"] = config.push.useSsl;
            doc["startTls"] = config.push.startTls;
            doc["encryption"] = ConfigStore::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
            doc["security"] = ConfigStore::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
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
            push.reminderCycle = ConfigStore::reminderCycleFromLegacyMinutes(doc["cycleMinutes"].as<unsigned long>());
        }
        push.reminderWeekday = doc["reminderWeekday"] | push.reminderWeekday;
        push.subjectTemplate = jsonString(doc, "subjectTemplate", push.subjectTemplate);
        push.bodyTemplate = jsonString(doc, "bodyTemplate", push.bodyTemplate);

        ConfigStore::sanitize(config);
        if (push.useSsl && push.smtpPort == 587) push.smtpPort = 465;
        if (push.startTls && push.smtpPort == 465) push.smtpPort = 587;

        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"push_config_save_failed\"}");
            return;
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    server_.addHandler(pushPostHandler);

    server_.on("/api/push/test", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        std::string error;
        if (PushNotificationManager::getInstance().sendTestEmail(error)) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }

        if (error.empty()) {
            error = "Unbekannter SMTP-Fehler";
        }

        JsonDocument doc;
        doc["error"] = error;
        std::string json;
        serializeJson(doc, json);
        request->send(500, "application/json", json.c_str());
    });

    server_.on("/api/push/smtp-check", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        SmtpDiagResult diagResult = PushNotificationManager::getInstance().smtpDiagnostic();

        JsonDocument doc;
        doc["success"] = diagResult.success;
        JsonArray steps = doc["steps"].to<JsonArray>();
        for (const auto& step : diagResult.steps) {
            JsonObject s = steps.add<JsonObject>();
            s["name"] = step.name;
            s["ok"] = step.ok;
            s["detail"] = step.detail;
        }
        std::string json;
        serializeJson(doc, json);
        const int statusCode = diagResult.success ? 200 : 503;
        request->send(statusCode, "application/json", json.c_str());
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
        pushJ["encryption"] = ConfigStore::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
        pushJ["security"] = ConfigStore::derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
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
    server_.on("/api/import", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = collectBody(request, data, len, index, total, kMaxImportBodyBytes,
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

            ConfigStore::sanitize(config);  // Backups koennen veraltet oder manipuliert sein
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
        scheduleRestart(kRestartDelayMs);  // Antwort vor dem Neustart ausliefern, ohne den Async-Task zu blockieren
    });

    server_.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        scheduleRestart(1000);
    });

    // Root explizit bedienen; eigentliche Datei-Auslieferung macht der Static-Handler.
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/index.html");
    });

    // Explizite Route fuer index.html als robuste Absicherung gegen Static-Fallback-Probleme.
    server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ensureLittleFsMounted()) {
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
            if (!ensureLittleFsMounted()) {
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

void WebServerDashboard::setupUpdateRoutes() {
    server_.on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendUpdateStatus(request);
    });

    server_.on("/api/update/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        recoverStuckUploadIfNeeded();

        if (updateState_.inProgress && updateState_.source != "upload") {
            request->send(409, "application/json", "{\"error\":\"Aktives Repo-Update kann nicht manuell zurückgesetzt werden\"}");
            return;
        }

        if (updateState_.rebootPending) {
            request->send(409, "application/json", "{\"error\":\"Neustart steht bereits aus\"}");
            return;
        }

        clearUploadSession(true);
        resetUpdateState("", "");
        updateState_.message = "Update-Status wurde zurückgesetzt";
        touchUpdateActivity();
        DebugLogger::getInstance().log(LogLevel::INFO, "Update status reset requested via API");
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update-Status wurde zurückgesetzt\"}");
    });

    server_.on("/api/update/manifest", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendUpdateManifest(request);
    });

    for (const char* target : {"app", "webui"}) {
        const String route = String("/api/update/signature/") + target;
        const bool isApp = std::strcmp(target, "app") == 0;
        server_.on(route.c_str(), HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
            [this, isApp](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                if (index == 0 && !requireAdmin(request)) return;
                const char *body = collectBody(request, data, len, index, total, kEmbeddedSigBytes,
                                               "{\"error\":\"Ungueltige Signaturdatei\"}");
                if (body == nullptr) return;
                auto& signature = isApp ? uploadAppSignature_ : uploadWebUiSignature_;
                signature.assign(reinterpret_cast<const uint8_t*>(body), reinterpret_cast<const uint8_t*>(body) + total);
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            });
    }

    server_.on("/api/update/repo", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        const char *body = collectBody(request, data, len, index, total, kMaxUpdateRequestBytes,
                                       "{\"error\":\"update_request_too_large\"}");
        if (body == nullptr) return;

        recoverStuckUploadIfNeeded();

        if (updateState_.inProgress || uploadActive_) {
            request->send(409, "application/json", "{\"error\":\"Update läuft bereits\"}");
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, body, total) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"Ungültiger Request-Body\"}");
            return;
        }

        std::string target = jsonString(doc, "target", "");
        if (target == "firmware") {
            target = "app";
        }
        if (target.empty()) {
            target = "auto";
        }
        if (target != "app" && target != "webui" && target != "full" && target != "auto") {
            request->send(400, "application/json", "{\"error\":\"Ungültiges Update-Ziel\"}");
            return;
        }

        std::string error;
        if (!requestRepoUpdate(target, error)) {
            request->send(WiFi.status() == WL_CONNECTED ? 409 : 503, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
            return;
        }

        request->send(202, "application/json", "{\"status\":\"started\"}");
    });

    // "firmware" ist ein Alias fuer "app" (aeltere Clients).
    for (const char* route : {"/api/update/upload/app", "/api/update/upload/firmware", "/api/update/upload/webui"}) {
        const bool isWebUi = std::strcmp(route, "/api/update/upload/webui") == 0;
        server_.on(route, HTTP_POST,
            [](AsyncWebServerRequest *request) {},
            [this, isWebUi](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
                if (index == 0 && !requireAdmin(request)) return;
                handleUpload(request, isWebUi ? "webui" : "app", filename, index, data, len, final);
            });
    }
}

void WebServerDashboard::resetUpdateState(const std::string& source, const std::string& target) {
    updateState_ = {};
    updateState_.source = source;
    updateState_.target = target;
    updateState_.phase = "idle";
    restartScheduled_ = false;
    touchUpdateActivity();
}

void WebServerDashboard::touchUpdateActivity() {
    updateState_.lastActivityMs = millis();
}

void WebServerDashboard::setUpdatePhase(const std::string& phase, const std::string& message, size_t received, size_t total) {
    updateState_.phase = phase;
    updateState_.message = message;
    updateState_.received = received;
    updateState_.total = total;
    touchUpdateActivity();
}

void WebServerDashboard::clearUploadSession(bool abortUpdate) {
    const bool shouldRemountFs = uploadTarget_ == "webui";

    if (abortUpdate && (uploadActive_ || (updateState_.source == "upload" && updateState_.inProgress))) {
        Update.abort();
    }

    uploadActive_ = false;
    uploadFailed_ = false;
    uploadRequest_ = nullptr;
    uploadTarget_ = "";
    uploadFilename_ = "";
    uploadDetachedMode_ = false;
    uploadAppSignature_.clear();
    uploadWebUiSignature_.clear();
    uploadTailBuffer_.clear();
    uploadTargetPartition_ = nullptr;
    uploadLastChunkMs_ = 0;
    if (uploadShaActive_) {
        mbedtls_sha256_free(&uploadShaContext_);
        uploadShaActive_ = false;
    }

    if (shouldRemountFs) {
        ensureLittleFsMounted();
    }
}

void WebServerDashboard::recoverStuckUploadIfNeeded() {
    if (!uploadActive_ || updateState_.source != "upload" || !updateState_.inProgress) {
        return;
    }

    const unsigned long referenceMs = uploadLastChunkMs_ != 0 ? uploadLastChunkMs_ : updateState_.lastActivityMs;
    const unsigned long now = millis();
    if (referenceMs == 0 || (now - referenceMs) < kUploadStallTimeoutMs) {
        return;
    }

    DebugLogger::getInstance().log(
        LogLevel::ERROR,
        std::string("Upload stalled, resetting state after ") + std::to_string(now - referenceMs) + " ms");
    clearUploadSession(true);
    markUpdateFailed("Upload wurde wegen Inaktivität abgebrochen. Bitte erneut versuchen.");
}

void WebServerDashboard::markUpdateFailed(const std::string& message) {
    if (updateState_.source == "upload") {
        clearUploadSession(false);
    }
    updateState_.inProgress = false;
    updateState_.success = false;
    updateState_.rebootPending = false;
    restartScheduled_ = false;
    setUpdatePhase("failed", message, updateState_.received, updateState_.total);
    DebugLogger::getInstance().log(LogLevel::ERROR, std::string("OTA failed: ") + message);
}

void WebServerDashboard::markUpdateSucceeded(const std::string& message) {
    if (updateState_.source == "upload") {
        clearUploadSession(false);
    }
    updateState_.inProgress = false;
    updateState_.success = true;
    updateState_.rebootPending = true;
    setUpdatePhase("done", message, updateState_.total, updateState_.total);
    DebugLogger::getInstance().log(LogLevel::INFO, std::string("OTA success: ") + message);
}

void WebServerDashboard::scheduleRestart(uint32_t delayMs) {
    if (restartScheduled_) {
        return;
    }

    restartScheduled_ = true;
    struct RestartContext {
        uint32_t delayMs;
    };

    auto *context = new RestartContext{ delayMs };
    const BaseType_t created = xTaskCreate([](void *param) {
        auto *ctx = static_cast<RestartContext*>(param);
        vTaskDelay(pdMS_TO_TICKS(ctx->delayMs));
        delete ctx;
        ESP.restart();
    }, "level-control-restart", 4096, context, 1, nullptr);

    if (created != pdPASS) {
        delete context;
        restartScheduled_ = false;
        DebugLogger::getInstance().log(LogLevel::ERROR, "Restart-Task konnte nicht erstellt werden, starte sofort neu");
        ESP.restart();
    }
}

void WebServerDashboard::sendUpdateStatus(AsyncWebServerRequest *request) {
    recoverStuckUploadIfNeeded();

    JsonDocument doc;
    doc["inProgress"] = updateState_.inProgress;
    doc["success"] = updateState_.success;
    doc["rebootPending"] = updateState_.rebootPending;
    doc["source"] = updateState_.source;
    doc["target"] = updateState_.target;
    doc["phase"] = updateState_.phase;
    doc["message"] = updateState_.message;
    doc["installedVersion"] = getInstalledVersion();
    doc["availableVersion"] = updateState_.availableVersion;
    doc["pendingVersion"] = (updateState_.success && updateState_.rebootPending
        && (updateState_.target == "app" || updateState_.target == "full"))
        ? updateState_.availableVersion
        : "";
    doc["manifestError"] = manifestError_;
    doc["received"] = updateState_.received;
    doc["total"] = updateState_.total;
    doc["uploadActive"] = uploadActive_;
    doc["activityAgeMs"] = updateState_.lastActivityMs == 0 ? 0UL : (millis() - updateState_.lastActivityMs);
    doc["uploadStallTimeoutMs"] = kUploadStallTimeoutMs;
    doc["canReset"] = !updateState_.rebootPending
        && updateState_.source == "upload"
        && (!updateState_.inProgress || updateState_.phase == "failed");
    doc["configFallbackApplied"] = ConfigStore::getInstance().wasFallbackApplied();
    doc["configFallbackMessage"] = ConfigStore::getInstance().getFallbackMessage();
    doc["appMaxSize"] = getAppPartitionSize();
    doc["firmwareMaxSize"] = getAppPartitionSize();
    doc["webuiMaxSize"] = getFilesystemPartitionSize();
    doc["manifestUrl"] = kLatestReleaseApiUrl;
    doc["releaseTagBaseUrl"] = kReleaseTagBaseUrl;

    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    if (runningPartition) {
        doc["runningPartitionLabel"] = runningPartition->label;
        doc["runningPartitionAddress"] = runningPartition->address;
        doc["runningPartitionSize"] = runningPartition->size;
    } else {
        doc["runningPartitionLabel"] = "";
        doc["runningPartitionAddress"] = 0;
        doc["runningPartitionSize"] = 0;
    }

    if (bootPartition) {
        doc["configuredBootPartitionLabel"] = bootPartition->label;
        doc["configuredBootPartitionAddress"] = bootPartition->address;
        doc["configuredBootPartitionSize"] = bootPartition->size;
    } else {
        doc["configuredBootPartitionLabel"] = "";
        doc["configuredBootPartitionAddress"] = 0;
        doc["configuredBootPartitionSize"] = 0;
    }

    const esp_reset_reason_t resetReason = esp_reset_reason();
    doc["lastResetReason"] = resetReasonToString(resetReason);
    doc["lastResetReasonCode"] = static_cast<int>(resetReason);

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

void WebServerDashboard::sendUpdateManifest(AsyncWebServerRequest *request) {
    // Non-blocking: Liefert Cache-Daten sofort und startet bei Bedarf einen Background-Fetch.
    const unsigned long now = millis();
    const bool cacheExpired = lastManifestCheckMs_ == 0 || (now - lastManifestCheckMs_) >= kManifestCacheTtlMs;

    if (cacheExpired && !manifestFetchInProgress_) {
        startBackgroundManifestFetch();
    }

    if (!cachedManifest_.valid && !manifestError_.empty()) {
        JsonDocument doc;
        doc["error"] = manifestError_;
        doc["manifestUrl"] = kLatestReleaseApiUrl;
        doc["fetching"] = manifestFetchInProgress_;
        std::string json;
        serializeJson(doc, json);
        request->send(502, "application/json", json.c_str());
        return;
    }

    if (!cachedManifest_.valid) {
        JsonDocument doc;
        doc["error"] = manifestFetchInProgress_ ? "Release-Informationen werden geladen" : "Kein Release-Manifest verfügbar";
        doc["manifestUrl"] = kLatestReleaseApiUrl;
        doc["fetching"] = manifestFetchInProgress_;
        std::string json;
        serializeJson(doc, json);
        request->send(manifestFetchInProgress_ ? 202 : 502, "application/json", json.c_str());
        return;
    }

    JsonDocument doc;
    doc["fetching"] = manifestFetchInProgress_;
    doc["version"] = cachedManifest_.version;
    doc["releaseUrl"] = cachedManifest_.releaseUrl;

    JsonObject assets = doc["assets"].to<JsonObject>();
    if (!cachedManifest_.app.url.empty()) {
        JsonObject app = assets["app"].to<JsonObject>();
        app["name"] = cachedManifest_.app.name;
        app["url"] = cachedManifest_.app.url;
        app["size"] = cachedManifest_.app.size;
    }
    if (!cachedManifest_.webui.url.empty()) {
        JsonObject webui = assets["webui"].to<JsonObject>();
        webui["name"] = cachedManifest_.webui.name;
        webui["url"] = cachedManifest_.webui.url;
        webui["size"] = cachedManifest_.webui.size;
    }

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

bool WebServerDashboard::fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error) {
    if (!isHttpsUrl(kLatestReleaseApiUrl)) {
        error = "Release-API-URL muss HTTPS verwenden";
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);

    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, kLatestReleaseApiUrl)) {
        error = "Release-Information konnte nicht geladen werden";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", kUpdateUserAgent);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Release-Request fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }

    rawManifest = http.getString().c_str();
    http.end();

    return parseLatestReleaseInfo(rawManifest, manifest, error);
}

bool WebServerDashboard::parseLatestReleaseInfo(const std::string& rawManifest, ReleaseManifest& manifest, std::string& error) const {
    manifest = {};
    JsonDocument doc;
    if (deserializeJson(doc, rawManifest) != DeserializationError::Ok) {
        error = "Release-Antwort ist kein gültiges JSON";
        return false;
    }

    manifest.version = normalizeVersionTag(doc["tag_name"] | "");
    manifest.releaseUrl = doc["html_url"] | kLatestReleaseUrl;

    ManifestAsset detachedApp;
    ManifestAsset detachedWebUi;
    ManifestAsset legacyApp;
    ManifestAsset legacyWebUi;
    const auto hasSuffix = [](const std::string& value, const char* suffix) {
        const size_t suffixLength = strlen(suffix);
        return value.size() >= suffixLength
            && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
    };

    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
        const std::string name = asset["name"] | "";
        const std::string lowerName = strutil::toLower(name);
        const std::string url = asset["browser_download_url"] | "";
        const size_t size = asset["size"] | 0;

        if (hasSuffix(lowerName, "-image.bin")) {
            detachedApp.name = name;
            detachedApp.url = url;
            detachedApp.size = size;
        } else if (hasSuffix(lowerName, "-image.sig")) {
            detachedApp.signatureUrl = url;
        } else if (hasSuffix(lowerName, "-filesystem.bin")) {
            detachedWebUi.name = name;
            detachedWebUi.url = url;
            detachedWebUi.size = size;
        } else if (hasSuffix(lowerName, "-filesystem.sig")) {
            detachedWebUi.signatureUrl = url;
        } else if (isAppAssetName(lowerName)) {
            legacyApp.name = name;
            legacyApp.url = url;
            legacyApp.size = size;
        } else if (isWebUiAssetName(lowerName)) {
            legacyWebUi.name = name;
            legacyWebUi.url = url;
            legacyWebUi.size = size;
        }
    }

    manifest.app = !detachedApp.url.empty() && !detachedApp.signatureUrl.empty()
        ? detachedApp
        : legacyApp;
    manifest.webui = !detachedWebUi.url.empty() && !detachedWebUi.signatureUrl.empty()
        ? detachedWebUi
        : legacyWebUi;

    manifest.valid = !manifest.version.empty() && (!manifest.app.url.empty() || !manifest.webui.url.empty());

    if (!manifest.valid) {
        error = "Release enthält keine unterstützten OTA-Assets";
        return false;
    }

    return true;
}
bool WebServerDashboard::verifyDetachedFileSignature(const unsigned char* hash, size_t hashLen, const uint8_t* signature, size_t signatureLen, std::string& error) const {
    mbedtls_pk_context publicKey;
    mbedtls_pk_init(&publicKey);

    const int parseResult = mbedtls_pk_parse_public_key(
        &publicKey,
        reinterpret_cast<const unsigned char*>(kReleaseSigningPublicKeyPem),
        strlen(kReleaseSigningPublicKeyPem) + 1);
    if (parseResult != 0) {
        mbedtls_pk_free(&publicKey);
        error = "Öffentlicher Release-Schlüssel konnte nicht geladen werden";
        return false;
    }

    const int verifyResult = mbedtls_pk_verify(
        &publicKey,
        MBEDTLS_MD_SHA256,
        hash,
        hashLen,
        signature,
        signatureLen);
    mbedtls_pk_free(&publicKey);

    if (verifyResult != 0) {
        error = "Dateisignatur ungültig";
        return false;
    }

    return true;
}

bool WebServerDashboard::refreshManifestCache(bool forceRefresh, std::string& error) {
    const unsigned long now = millis();
    const bool cacheFresh = !forceRefresh && lastManifestCheckMs_ != 0 && (now - lastManifestCheckMs_) < kManifestCacheTtlMs;
    if (cacheFresh) {
        error = manifestError_;
        return cachedManifest_.valid;
    }

    ReleaseManifest manifest;
    std::string rawManifest;
    if (!fetchLatestManifest(manifest, rawManifest, error)) {
        // Keep the last valid manifest to survive transient network/release endpoint issues.
        if (cachedManifest_.valid) {
            manifestError_ = error;
            lastManifestCheckMs_ = now;
            return true;
        }

        cachedManifest_ = {};
        manifestError_ = error;
        lastManifestCheckMs_ = now;
        return false;
    }

    cachedManifest_ = manifest;
    manifestError_.clear();
    lastManifestCheckMs_ = now;
    return true;
}

void WebServerDashboard::startBackgroundManifestFetch() {
    if (manifestFetchInProgress_) {
        return;
    }
    manifestFetchInProgress_ = true;

    if (xTaskCreate([](void *param) {
            auto *dashboard = static_cast<WebServerDashboard*>(param);
            dashboard->runManifestFetchTask();
            vTaskDelete(nullptr);
        }, "manifest-fetch", 8192, this, 1, nullptr) != pdPASS) {
        manifestFetchInProgress_ = false;
        manifestError_ = "Manifest-Task konnte nicht gestartet werden";
        DebugLogger::getInstance().log(LogLevel::ERROR, manifestError_);
    }
}

void WebServerDashboard::runManifestFetchTask() {
    ReleaseManifest manifest;
    std::string rawManifest;
    std::string error;

    if (fetchLatestManifest(manifest, rawManifest, error)) {
        cachedManifest_ = manifest;
        manifestError_.clear();
    } else {
        if (!cachedManifest_.valid) {
            cachedManifest_ = {};
        }
        manifestError_ = error;
    }

    lastManifestCheckMs_ = millis();
    manifestFetchInProgress_ = false;
}

std::string WebServerDashboard::resolveUpdateTarget(const ReleaseManifest& manifest, const std::string& requestedTarget, std::string& error) const {
    const bool hasApp = !manifest.app.url.empty();
    const bool hasWebUi = !manifest.webui.url.empty();
    const bool newerVersion = compareVersions(manifest.version, getInstalledVersion()) > 0;

    if (requestedTarget == "auto" || requestedTarget.empty()) {
        if (!newerVersion) {
            error = "Keine neuere Version verfügbar";
            return "";
        }
        if (hasApp && hasWebUi) {
            return "full";
        }
        if (hasApp) {
            return "app";
        }
        if (hasWebUi) {
            return "webui";
        }

        error = "Manifest enthält keine verfügbaren Update-Assets";
        return "";
    }

    if (requestedTarget == "app") {
        if (hasApp) {
            return "app";
        }
        error = "Im Manifest ist kein App-Update vorhanden";
        return "";
    }

    if (requestedTarget == "webui") {
        if (hasWebUi) {
            return "webui";
        }
        error = "Im Manifest ist kein Web-UI-Update vorhanden";
        return "";
    }

    if (requestedTarget == "full") {
        if (hasApp && hasWebUi) {
            return "full";
        }
        error = "Für ein Komplett-Update fehlen Assets im Manifest";
        return "";
    }

    error = "Ungültiges Update-Ziel";
    return "";
}

void WebServerDashboard::startRemoteUpdateTask(const std::string& target) {
    resetUpdateState("repo", target);
    updateState_.inProgress = true;
    setUpdatePhase("queued", "Repo-Update wird vorbereitet");

    auto *context = new RemoteUpdateContext{ this, target };
    if (xTaskCreate([](void *param) {
            auto *ctx = static_cast<RemoteUpdateContext*>(param);
            ctx->dashboard->runRemoteUpdateTask(ctx->target);
            delete ctx;
            vTaskDelete(nullptr);
        }, "level-control-ota", 16384, context, 1, nullptr) != pdPASS) {
        delete context;
        markUpdateFailed("OTA-Task konnte nicht gestartet werden");
    }
}

void WebServerDashboard::runRemoteUpdateTask(const std::string& target) {
    ReleaseManifest manifest;
    std::string rawManifest;
    std::string error;

    setUpdatePhase("release", "Release-Informationen werden geladen");
    if (!fetchLatestManifest(manifest, rawManifest, error)) {
        manifestError_ = error;
        lastManifestCheckMs_ = millis();
        markUpdateFailed(error);
        return;
    }


    cachedManifest_ = manifest;
    manifestError_.clear();
    lastManifestCheckMs_ = millis();
    updateState_.availableVersion = manifest.version;

    if ((target == "webui" || target == "full") && !manifest.webui.url.empty()) {
        if (!applyRemoteAsset(manifest.webui, U_SPIFFS, "webui", manifest.version, error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if ((target == "app" || target == "full") && !manifest.app.url.empty()) {
        if (!applyRemoteAsset(manifest.app, U_FLASH, "app", manifest.version, error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if (target == "full") {
        markUpdateSucceeded("App und Web-UI wurden erfolgreich aktualisiert");
    } else if (target == "app") {
        markUpdateSucceeded("App wurde erfolgreich aktualisiert");
    } else {
        markUpdateSucceeded("Web-UI wurde erfolgreich aktualisiert");
    }

    scheduleRestart(kRestartDelayMs);
}

bool WebServerDashboard::applyDetachedRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error) {
    if (!isHttpsUrl(asset.url) || !isHttpsUrl(asset.signatureUrl)) {
        error = "Asset- und Signatur-URL müssen HTTPS verwenden";
        return false;
    }

    WiFiClientSecure signatureClient;
    signatureClient.setCACert(kOtaTrustedRootCAs);
    HTTPClient signatureHttp;
    signatureHttp.setTimeout(15000);
    signatureHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!signatureHttp.begin(signatureClient, asset.signatureUrl.c_str())) {
        error = "Signatur konnte nicht geöffnet werden";
        return false;
    }
    signatureHttp.addHeader("User-Agent", kUpdateUserAgent);
    if (signatureHttp.GET() != HTTP_CODE_OK) {
        error = "Signatur-Download fehlgeschlagen";
        signatureHttp.end();
        return false;
    }
    const String signatureBody = signatureHttp.getString();
    signatureHttp.end();
    const size_t signatureLength = signatureBody.length();
    if (signatureLength == 0 || signatureLength > kEmbeddedSigBytes) {
        error = "Signatur hat eine ungültige Länge";
        return false;
    }
    std::vector<uint8_t> signature(
        reinterpret_cast<const uint8_t*>(signatureBody.c_str()),
        reinterpret_cast<const uint8_t*>(signatureBody.c_str()) + signatureLength);

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);
    HTTPClient http;
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, asset.url.c_str())) {
        error = "Asset konnte nicht geöffnet werden";
        return false;
    }
    http.addHeader("User-Agent", kUpdateUserAgent);
    if (http.GET() != HTTP_CODE_OK) {
        error = "Asset-Download fehlgeschlagen";
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    if (contentLength <= 0) {
        error = "Asset liefert keine gültige Dateigröße";
        http.end();
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(contentLength);
    if (command == U_SPIFFS) {
        LittleFS.end();
    }

    if (!Update.begin(expectedSize, command)) {
        error = Update.errorString();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
        error = "SHA256-Kontext konnte nicht initialisiert werden";
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    setUpdatePhase(phase, "Download läuft", 0, expectedSize);
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[kUploadBufferSize];
    size_t received = 0;
    while (received < expectedSize) {
        const size_t available = stream->available();
        if (available == 0) {
            if (!http.connected()) {
                break;
            }
            delay(1);
            continue;
        }

        const size_t chunkSize = stream->readBytes(buffer, std::min<size_t>(available, sizeof(buffer)));
        if (chunkSize == 0) {
            continue;
        }
        if (received + chunkSize > expectedSize
            || mbedtls_sha256_update_ret(&shaContext, buffer, chunkSize) != 0
            || Update.write(buffer, chunkSize) != chunkSize) {
            error = Update.errorString();
            Update.abort();
            mbedtls_sha256_free(&shaContext);
            if (command == U_SPIFFS) {
                LittleFS.begin();
            }
            http.end();
            return false;
        }
        received += chunkSize;
        setUpdatePhase(phase, "Download läuft", received, expectedSize);
    }

    if (received != expectedSize) {
        error = "Asset-Download wurde vorzeitig beendet";
        Update.abort();
        mbedtls_sha256_free(&shaContext);
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    unsigned char shaResult[32];
    if (mbedtls_sha256_finish_ret(&shaContext, shaResult) != 0
        || !verifyDetachedFileSignature(shaResult, sizeof(shaResult), signature.data(), signature.size(), error)) {
        if (error.empty()) {
            error = "Signaturprüfung fehlgeschlagen";
        }
        Update.abort();
        mbedtls_sha256_free(&shaContext);
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }
    mbedtls_sha256_free(&shaContext);
    setUpdatePhase(phase, "Signatur geprüft", received, expectedSize);

    if (!Update.end(true)) {
        error = Update.errorString();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    if (command == U_FLASH) {
        const esp_partition_t* targetPartition = esp_ota_get_next_update_partition(nullptr);
        if (!ensureConfiguredBootPartition(targetPartition, error)) {
            http.end();
            return false;
        }
    }
    if (command == U_SPIFFS) {
        ensureLittleFsMounted();
    }
    http.end();
    return true;
}

bool WebServerDashboard::applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error) {
    if (!asset.signatureUrl.empty()) {
        return applyDetachedRemoteAsset(asset, command, phase, version, error);
    }

    if (!isHttpsUrl(asset.url)) {
        error = "Asset-URL muss HTTPS verwenden";
        return false;
    }

    DebugLogger::getInstance().log(
        LogLevel::INFO,
        std::string("OTA asset start: phase=") + phase + ", version=" + version + ", url=" + asset.url);

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);

    HTTPClient http;
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, asset.url.c_str())) {
        error = "Asset konnte nicht geöffnet werden";
        return false;
    }

    http.addHeader("User-Agent", kUpdateUserAgent);
    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Asset-Download fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    if (contentLength > 0 && static_cast<size_t>(contentLength) <= kEmbeddedSigTrailerSize) {
        error = "Asset ist zu klein oder enthält keinen Signatur-Trailer";
        http.end();
        return false;
    }
    const size_t expectedSize = contentLength > 0
        ? static_cast<size_t>(contentLength) - kEmbeddedSigTrailerSize
        : UPDATE_SIZE_UNKNOWN;

    if (command == U_SPIFFS) {
        LittleFS.end();
    }

    const esp_partition_t* targetPartition = nullptr;
    if (command == U_FLASH) {
        targetPartition = esp_ota_get_next_update_partition(nullptr);
    }

    if (!Update.begin(expectedSize, command)) {
        error = Update.errorString();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[kUploadBufferSize];
    size_t received = 0;
    size_t streamReceived = 0;
    bool firstPayloadChunk = true;
    std::vector<uint8_t> tailBuffer;
    tailBuffer.reserve(kEmbeddedSigTrailerSize + kUploadBufferSize);

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
        error = "SHA256-Kontext konnte nicht initialisiert werden";
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    setUpdatePhase(phase, "Download läuft", 0, expectedSize);

    while (http.connected() && (contentLength < 0 || streamReceived < static_cast<size_t>(contentLength))) {
        const size_t available = stream->available();
        if (available == 0) {
            delay(1);
            continue;
        }

        const size_t chunkSize = stream->readBytes(buffer, std::min<size_t>(available, sizeof(buffer)));
        if (chunkSize == 0) {
            continue;
        }

        streamReceived += chunkSize;
        tailBuffer.insert(tailBuffer.end(), buffer, buffer + chunkSize);

        while (tailBuffer.size() > kEmbeddedSigTrailerSize) {
            const size_t processLen = tailBuffer.size() - kEmbeddedSigTrailerSize;
            std::vector<uint8_t> payloadChunk(tailBuffer.begin(), tailBuffer.begin() + processLen);
            tailBuffer.erase(tailBuffer.begin(), tailBuffer.begin() + processLen);

            if (firstPayloadChunk && !payloadChunk.empty()) {
                if ((command == U_FLASH && payloadChunk[0] != 0xE9) || (command == U_SPIFFS && payloadChunk[0] == 0xE9)) {
                    Update.abort();
                    if (command == U_SPIFFS) {
                        LittleFS.begin();
                    }
                    http.end();
                    error = command == U_FLASH ? "App-Asset ist kein gültiges ESP32-Image" : "Web-UI-Asset sieht wie eine App aus";
                    mbedtls_sha256_free(&shaContext);
                    return false;
                }
                firstPayloadChunk = false;
            }

            if (mbedtls_sha256_update_ret(&shaContext, payloadChunk.data(), payloadChunk.size()) != 0) {
                Update.abort();
                if (command == U_SPIFFS) {
                    LittleFS.begin();
                }
                http.end();
                error = "SHA256-Berechnung fehlgeschlagen";
                mbedtls_sha256_free(&shaContext);
                return false;
            }
            if (Update.write(payloadChunk.data(), payloadChunk.size()) != payloadChunk.size()) {
                Update.abort();
                if (command == U_SPIFFS) {
                    LittleFS.begin();
                }
                http.end();
                error = Update.errorString();
                mbedtls_sha256_free(&shaContext);
                return false;
            }

            received += payloadChunk.size();
        }

        setUpdatePhase(phase, "Download läuft", received, expectedSize);
    }

    if (tailBuffer.size() != kEmbeddedSigTrailerSize) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "Signatur-Trailer fehlt oder ist unvollständig";
        return false;
    }

    if ((command == U_FLASH && received < 65536) || (command == U_SPIFFS && received < 4096)) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = command == U_FLASH ? "App-Asset ist unplausibel klein" : "Web-UI-Asset ist unplausibel klein";
        return false;
    }

    if (memcmp(tailBuffer.data(), kEmbeddedSigMagic, sizeof(kEmbeddedSigMagic)) != 0) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "Asset-Signatur-Trailer ist ungültig";
        return false;
    }
    const size_t sigLen = tailBuffer[sizeof(kEmbeddedSigMagic)];
    if (sigLen == 0 || sigLen > kEmbeddedSigBytes) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "Asset-Signaturlänge ist ungültig";
        return false;
    }

    unsigned char shaResult[32];
    if (mbedtls_sha256_finish_ret(&shaContext, shaResult) != 0) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "SHA256-Abschluss fehlgeschlagen";
        return false;
    }
    mbedtls_sha256_free(&shaContext);

    if (!verifyDetachedFileSignature(
            shaResult,
            sizeof(shaResult),
            tailBuffer.data() + sizeof(kEmbeddedSigMagic) + 1,
            sigLen,
            error)) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    if (!Update.end(true)) {
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        error = Update.errorString();
        return false;
    }

    if (command == U_FLASH) {
        // Nach erfolgreichem Flashen die neue Partition als Boot-Partition setzen und verifizieren.
        if (!ensureConfiguredBootPartition(targetPartition, error)) {
            http.end();
            return false;
        }
    }

    if (command == U_SPIFFS) {
        ensureLittleFsMounted();
    }

    http.end();
    return true;
}

bool WebServerDashboard::validateUploadChunk(const String& target, const uint8_t *data, size_t len, size_t index, std::string& error) const {
    if (index == 0 && len > 0) {
        if (target == "app" && data[0] != 0xE9) {
            error = "Die hochgeladene App ist kein gültiges ESP32-Binary";
            return false;
        }
        if (target == "webui" && data[0] == 0xE9) {
            error = "Die Web-UI-Datei sieht wie eine App aus";
            return false;
        }
    }
    return true;
}

void WebServerDashboard::handleUpload(AsyncWebServerRequest *request, const String& target, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    recoverStuckUploadIfNeeded();

    if (index == 0) {
        if (updateState_.inProgress || uploadActive_) {
            request->send(409, "application/json", "{\"error\":\"Update läuft bereits\"}");
            return;
        }

        std::string error;
        if (!validateUploadFilename(filename, error)) {
            request->send(400, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
            return;
        }

        uploadActive_ = true;
        uploadFailed_ = false;
        uploadRequest_ = request;
        uploadTarget_ = target;
        uploadFilename_ = filename;
        const std::string lowerFilename = strutil::toLower(filename.c_str());
        uploadDetachedMode_ = lowerFilename.find("-image.bin") != std::string::npos
            || lowerFilename.find("-filesystem.bin") != std::string::npos;
        const auto& detachedSignature = target == "app" ? uploadAppSignature_ : uploadWebUiSignature_;
        if (uploadDetachedMode_ && detachedSignature.empty()) {
            request->send(400, "application/json", "{\"error\":\"Fuer dieses Detached-Asset zuerst die .sig-Datei waehlen\"}");
            uploadActive_ = false;
            return;
        }
        uploadTailBuffer_.clear();
        uploadTailBuffer_.reserve(kEmbeddedSigTrailerSize + kUploadBufferSize);
        uploadLastChunkMs_ = millis();
        resetUpdateState("upload", target.c_str());
        updateState_.inProgress = true;
        setUpdatePhase("validating", "Upload wird geprüft", 0, 0);
        DebugLogger::getInstance().log(
            LogLevel::INFO,
            std::string("Manual upload started: target=") + target.c_str()
                + ", file=" + filename.c_str()
                + ", size=" + std::to_string(request->contentLength()));

        if (target == "webui") {
            LittleFS.end();
        }

        mbedtls_sha256_init(&uploadShaContext_);
        if (mbedtls_sha256_starts_ret(&uploadShaContext_, 0) != 0) {
            uploadFailed_ = true;
            mbedtls_sha256_free(&uploadShaContext_);
            markUpdateFailed("SHA256-Kontext konnte nicht initialisiert werden");
            request->send(500, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
            return;
        }
        uploadShaActive_ = true;

        // Für App-Uploads die Ziel-OTA-Partition bereits vor Beginn ermitteln
        uploadTargetPartition_ = nullptr;
        if (target == "app") {
            uploadTargetPartition_ = esp_ota_get_next_update_partition(nullptr);
        }

        const size_t partitionLimit = target == "app" ? getAppPartitionSize() : getFilesystemPartitionSize();
        const size_t beginSize = partitionLimit > 0 ? partitionLimit : UPDATE_SIZE_UNKNOWN;
        if (!Update.begin(beginSize, target == "app" ? U_FLASH : U_SPIFFS)) {
            uploadFailed_ = true;
            markUpdateFailed(Update.errorString());
            request->send(500, "application/json", (std::string("{\"error\":\"") + Update.errorString() + "\"}").c_str());
            return;
        }
    }

    // Nur der Request, der den Upload gestartet (und damit authentifiziert) hat, darf Daten liefern.
    if (!uploadActive_ || uploadTarget_ != target || uploadRequest_ != request) {
        return;
    }

    std::string error;
    if (!uploadFailed_ && !validateUploadChunk(target, data, len, index, error)) {
        uploadFailed_ = true;
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(error);
    }

    if (!uploadFailed_ && len > 0) {
        uploadLastChunkMs_ = millis();
        const size_t limit = target == "app" ? getAppPartitionSize() : getFilesystemPartitionSize();
        if (uploadDetachedMode_) {
            const size_t nextPayloadSize = updateState_.received + len;
            if ((limit > 0 && nextPayloadSize > limit)
                || (uploadShaActive_ && mbedtls_sha256_update_ret(&uploadShaContext_, data, len) != 0)
                || Update.write(data, len) != len) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed(Update.errorString());
            } else {
                setUpdatePhase("uploading", "Upload läuft", nextPayloadSize, 0);
            }
        } else {
        uploadTailBuffer_.insert(uploadTailBuffer_.end(), data, data + len);
        while (!uploadFailed_ && uploadTailBuffer_.size() > kEmbeddedSigTrailerSize) {
            const size_t processLen = uploadTailBuffer_.size() - kEmbeddedSigTrailerSize;
            std::vector<uint8_t> payloadChunk(uploadTailBuffer_.begin(), uploadTailBuffer_.begin() + processLen);
            uploadTailBuffer_.erase(uploadTailBuffer_.begin(), uploadTailBuffer_.begin() + processLen);

            const size_t nextPayloadSize = updateState_.received + payloadChunk.size();
            if (limit > 0 && nextPayloadSize > limit) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed(target == "app"
                    ? "App-Datei überschreitet die verfügbare App-Partition"
                    : "Web-UI-Datei überschreitet die LittleFS-Partition");
                break;
            }

            if (uploadShaActive_ && mbedtls_sha256_update_ret(&uploadShaContext_, payloadChunk.data(), payloadChunk.size()) != 0) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed("SHA256-Berechnung fehlgeschlagen");
                break;
            }

            if (Update.write(payloadChunk.data(), payloadChunk.size()) != payloadChunk.size()) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed(Update.errorString());
                break;
            }

            setUpdatePhase("uploading", "Upload läuft", nextPayloadSize, 0);
        }
        }
    }

    if (!final) {
        return;
    }

    if (uploadFailed_) {
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    const size_t payloadSize = updateState_.received;
    if ((target == "app" && payloadSize < 65536) || (target == "webui" && payloadSize < 4096)) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(target == "app"
            ? "App-Datei ist für ein ESP32-Image unplausibel klein"
            : "Web-UI-Datei ist unplausibel klein");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    if (!uploadDetachedMode_ && (uploadTailBuffer_.size() != kEmbeddedSigTrailerSize
        || memcmp(uploadTailBuffer_.data(), kEmbeddedSigMagic, sizeof(kEmbeddedSigMagic)) != 0)) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed("Signatur-Trailer fehlt oder ist ungültig");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }
    const auto& detachedSignature = target == "app" ? uploadAppSignature_ : uploadWebUiSignature_;
    const size_t sigLen = uploadDetachedMode_
        ? detachedSignature.size()
        : uploadTailBuffer_[sizeof(kEmbeddedSigMagic)];
    if (sigLen == 0 || sigLen > kEmbeddedSigBytes) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed("Signaturlänge im Trailer ist ungültig");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    setUpdatePhase("verifying", "Signatur wird geprüft", payloadSize, 0);
    unsigned char uploadSha[32];
    if (!uploadShaActive_ || mbedtls_sha256_finish_ret(&uploadShaContext_, uploadSha) != 0) {
        if (uploadShaActive_) {
            mbedtls_sha256_free(&uploadShaContext_);
            uploadShaActive_ = false;
        }
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed("Signaturprüfung fehlgeschlagen");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }
    mbedtls_sha256_free(&uploadShaContext_);
    uploadShaActive_ = false;

    std::string signatureError;
    if (!verifyDetachedFileSignature(
            uploadSha,
            sizeof(uploadSha),
            uploadDetachedMode_
                ? detachedSignature.data()
                : uploadTailBuffer_.data() + sizeof(kEmbeddedSigMagic) + 1,
            sigLen,
            signatureError)) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(std::string("Signaturprüfung fehlgeschlagen: ") + signatureError);
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    if (!Update.end(true)) {
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(Update.errorString());
        request->send(500, "application/json", (std::string("{\"error\":\"") + Update.errorString() + "\"}").c_str());
        return;
    }

    if (target == "app") {
        // Falls wir vor dem Upload die Ziel-Partition ermittelt haben, diese jetzt als Boot-Partition setzen.
        std::string bootPartitionError;
        if (!ensureConfiguredBootPartition(uploadTargetPartition_, bootPartitionError)) {
            uploadTargetPartition_ = nullptr;
            if (target == "webui") {
                LittleFS.begin();
            }
            markUpdateFailed(bootPartitionError);
            request->send(500, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
            return;
        }
        uploadTargetPartition_ = nullptr;
    }

    if (target == "webui") {
        ensureLittleFsMounted();
    }

    const std::string successMessage = target == "app"
        ? "App wurde erfolgreich hochgeladen"
        : "Web-UI wurde erfolgreich hochgeladen";
    const bool deferReboot = request->hasParam("deferReboot")
        && (request->getParam("deferReboot")->value() == "1"
            || request->getParam("deferReboot")->value().equalsIgnoreCase("true"));

    if (deferReboot) {
        markUpdateSucceeded(successMessage + " (Neustart ausstehend)");
        updateState_.rebootPending = false;
        request->send(200, "application/json", (std::string("{\"status\":\"ok\",\"message\":\"") + successMessage + "\",\"deferredReboot\":true}").c_str());
        return;
    }

    markUpdateSucceeded(successMessage);
    scheduleRestart(kRestartDelayMs);
    request->send(200, "application/json", (std::string("{\"status\":\"ok\",\"message\":\"") + successMessage + "\"}").c_str());
}

size_t WebServerDashboard::getAppPartitionSize() const {
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    return partition ? partition->size : 0;
}

size_t WebServerDashboard::getFilesystemPartitionSize() const {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr);
    return partition ? partition->size : 0;
}
