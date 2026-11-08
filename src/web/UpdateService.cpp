#include "UpdateService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "ConfigStore.h"
#include "DebugLogger.h"
#include "GeneratedVersion.h"
#include "OtaTrustedRoots.h"
#include "StringUtils.h"
#include "Version.h"
#include "WebSupport.h"

namespace {
constexpr char kLatestReleaseApiUrl[] = LEVEL_CONTROL_LATEST_RELEASE_API_URL;
constexpr char kLatestReleaseUrl[] = LEVEL_CONTROL_LATEST_RELEASE_URL;
constexpr char kReleaseTagBaseUrl[] = LEVEL_CONTROL_RELEASE_TAG_BASE_URL;
constexpr char kUserAgent[] = "Level-Control-OTA/1.0";
constexpr uint32_t kRestartDelayMs = 2500;
constexpr size_t kDownloadBufferSize = 4096;
constexpr unsigned long kManifestCacheTtlMs = 300000;
constexpr unsigned long kUploadStallTimeoutMs = 300000;
constexpr size_t kMaxUpdateRequestBytes = 512;

bool isHttpsUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0;
}

bool hasSuffix(const std::string& value, const char* suffix) {
    const size_t suffixLength = strlen(suffix);
    return value.size() >= suffixLength && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
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

// Antwortet mit {"error": "..."}; ArduinoJson uebernimmt das Escaping.
void sendError(AsyncWebServerRequest *request, int code, const std::string& message) {
    JsonDocument doc;
    doc["error"] = message;
    std::string json;
    serializeJson(doc, json);
    request->send(code, "application/json", json.c_str());
}

bool validateUploadFilename(const String& filename, std::string& error) {
    const std::string lowerName = strutil::toLower(filename.c_str());
    if (!hasSuffix(lowerName, ".bin")) {
        error = "Es werden nur .bin-Dateien akzeptiert";
        return false;
    }
    if (lowerName.find("bootloader") != std::string::npos || lowerName.find("partition") != std::string::npos) {
        error = "Bootloader- oder Partitionsdateien dürfen hier nicht hochgeladen werden";
        return false;
    }
    return true;
}

}  // namespace

UpdateService& UpdateService::getInstance() {
    static UpdateService instance;
    return instance;
}

// ------------------------------------------------------------------ ManifestCache

UpdateService::ReleaseManifest UpdateService::ManifestCache::manifest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manifest_;
}

std::string UpdateService::ManifestCache::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool UpdateService::ManifestCache::isStale(unsigned long ttlMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastCheckMs_ == 0 || (millis() - lastCheckMs_) >= ttlMs;
}

void UpdateService::ManifestCache::storeSuccess(const ReleaseManifest& manifest) {
    std::lock_guard<std::mutex> lock(mutex_);
    manifest_ = manifest;
    error_.clear();
    lastCheckMs_ = millis();
}

void UpdateService::ManifestCache::storeFailure(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manifest_.valid) {
        manifest_ = {};
    }
    error_ = error;
    lastCheckMs_ = millis();
}

// ------------------------------------------------------------------ oeffentliche Abfragen

std::string UpdateService::installedVersion() const {
    return LEVEL_CONTROL_VERSION;
}

void UpdateService::refreshManifestInBackgroundIfStale() {
    if (manifestCache_.isStale(kManifestCacheTtlMs) && !manifestFetchInProgress_ && WiFi.status() == WL_CONNECTED) {
        startBackgroundManifestFetch();
    }
}

// Wird u. a. aus der Hauptschleife (MQTT) aufgerufen: liefert nur den Cache und holt veraltete Daten im
// Hintergrund-Task nach, statt die Hauptschleife mit einem blockierenden HTTPS-Request anzuhalten.
std::string UpdateService::availableVersion() {
    refreshManifestInBackgroundIfStale();
    const ReleaseManifest manifest = manifestCache_.manifest();
    return manifest.valid ? manifest.version : "";
}

std::string UpdateService::latestReleaseUrl() {
    refreshManifestInBackgroundIfStale();
    const ReleaseManifest manifest = manifestCache_.manifest();
    return manifest.valid && !manifest.releaseUrl.empty() ? manifest.releaseUrl : kLatestReleaseUrl;
}

int UpdateService::progressPercent() const {
    if (state_.total == 0) {
        return 0;
    }
    return static_cast<int>(std::min<size_t>(100, (state_.received * 100) / state_.total));
}

// Startet das Repo-Update. Es wird bewusst kein HTTPS-Request ausgefuehrt: der Aufrufer ist ein
// Async-TCP-Handler oder die Hauptschleife. Das Manifest wird im OTA-Task frisch geladen und dort
// gegen das Ziel aufgeloest. Liegt ein aktueller Cache vor, werden offensichtliche Fehler (kein neueres
// Release, Asset fehlt) vorab synchron gemeldet.
bool UpdateService::requestRepoUpdate(const std::string& target, std::string& error) {
    recoverStuckUploadIfNeeded();

    if (state_.inProgress || upload_) {
        error = "Update läuft bereits";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        error = "Kein WLAN für Repo-Update verfügbar";
        return false;
    }

    const ReleaseManifest cached = manifestCache_.manifest();
    if (cached.valid && !manifestCache_.isStale(kManifestCacheTtlMs) && resolveTarget(cached, target, error).empty()) {
        return false;
    }

    startRemoteUpdateTask(target);
    return true;
}

// ------------------------------------------------------------------ Zustand

void UpdateService::resetState(const std::string& source, const std::string& target) {
    state_ = {};
    state_.source = source;
    state_.target = target;
    state_.phase = "idle";
    state_.lastActivityMs = millis();
}

void UpdateService::setPhase(const std::string& phase, const std::string& message, size_t received, size_t total) {
    state_.phase = phase;
    state_.message = message;
    state_.received = received;
    state_.total = total;
    state_.lastActivityMs = millis();
}

void UpdateService::markFailed(const std::string& message) {
    state_.inProgress = false;
    state_.success = false;
    state_.rebootPending = false;
    setPhase("failed", message, state_.received, state_.total);
    DebugLogger::getInstance().log(LogLevel::ERROR, "OTA failed: " + message);
}

void UpdateService::markSucceeded(const std::string& message) {
    state_.inProgress = false;
    state_.success = true;
    state_.rebootPending = true;
    setPhase("done", message, state_.total, state_.total);
    DebugLogger::getInstance().log(LogLevel::INFO, "OTA success: " + message);
}

void UpdateService::clearUpload() {
    upload_.reset();  // bricht eine unfertige Flash-Sitzung ab
    uploadRequest_ = nullptr;
    uploadFailed_ = false;
    uploadLastChunkMs_ = 0;
}

void UpdateService::recoverStuckUploadIfNeeded() {
    if (!upload_ || state_.source != "upload" || !state_.inProgress) {
        return;
    }
    const unsigned long referenceMs = uploadLastChunkMs_ != 0 ? uploadLastChunkMs_ : state_.lastActivityMs;
    const unsigned long now = millis();
    if (referenceMs == 0 || (now - referenceMs) < kUploadStallTimeoutMs) {
        return;
    }

    DebugLogger::getInstance().log(LogLevel::ERROR,
        std::string("Upload stalled, resetting state after ") + std::to_string(now - referenceMs) + " ms");
    clearUpload();
    markFailed("Upload wurde wegen Inaktivität abgebrochen. Bitte erneut versuchen.");
}

// ------------------------------------------------------------------ Routen

void UpdateService::registerRoutes(AsyncWebServer& server, AuthCheck requireAdmin) {
    server.on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendStatus(request);
    });

    server.on("/api/update/manifest", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendManifest(request);
    });

    server.on("/api/update/reset", HTTP_POST, [this, requireAdmin](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        recoverStuckUploadIfNeeded();

        if (state_.inProgress && state_.source != "upload") {
            sendError(request, 409, "Aktives Repo-Update kann nicht manuell zurückgesetzt werden");
            return;
        }
        if (state_.rebootPending) {
            sendError(request, 409, "Neustart steht bereits aus");
            return;
        }

        clearUpload();
        resetState("", "");
        state_.message = "Update-Status wurde zurückgesetzt";
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update-Status wurde zurückgesetzt\"}");
    });

    server.on("/api/update/repo", HTTP_POST, web::answerIfPending, nullptr,
        [this, requireAdmin](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0 && !requireAdmin(request)) return;
            const char *body = web::collectBody(request, data, len, index, total, kMaxUpdateRequestBytes,
                                                "{\"error\":\"update_request_too_large\"}");
            if (body == nullptr) return;

            recoverStuckUploadIfNeeded();
            if (state_.inProgress || upload_) {
                sendError(request, 409, "Update läuft bereits");
                return;
            }

            JsonDocument doc;
            if (deserializeJson(doc, body, total) != DeserializationError::Ok) {
                sendError(request, 400, "Ungültiger Request-Body");
                return;
            }

            std::string target = web::jsonString(doc, "target", "");
            if (target == "firmware") {
                target = "app";
            }
            if (target.empty()) {
                target = "auto";
            }
            if (target != "app" && target != "webui" && target != "full" && target != "auto") {
                sendError(request, 400, "Ungültiges Update-Ziel");
                return;
            }

            std::string error;
            if (!requestRepoUpdate(target, error)) {
                sendError(request, WiFi.status() == WL_CONNECTED ? 409 : 503, error);
                return;
            }
            request->send(202, "application/json", "{\"status\":\"started\"}");
        });

    // "firmware" ist ein Alias fuer "app" (aeltere Clients).
    for (const char* route : {"/api/update/upload/app", "/api/update/upload/firmware", "/api/update/upload/webui"}) {
        const bool isWebUi = std::strcmp(route, "/api/update/upload/webui") == 0;
        server.on(route, HTTP_POST, web::answerIfPending,
            [this, requireAdmin, isWebUi](AsyncWebServerRequest *request, const String& filename, size_t index,
                                          uint8_t *data, size_t len, bool final) {
                if (index == 0 && !requireAdmin(request)) return;
                handleUpload(request, isWebUi ? FlashSession::Target::WebUi : FlashSession::Target::App,
                             filename, index, data, len, final);
            });
    }
}

void UpdateService::sendStatus(AsyncWebServerRequest *request) {
    recoverStuckUploadIfNeeded();

    JsonDocument doc;
    doc["inProgress"] = state_.inProgress;
    doc["success"] = state_.success;
    doc["rebootPending"] = state_.rebootPending;
    doc["source"] = state_.source;
    doc["target"] = state_.target;
    doc["phase"] = state_.phase;
    doc["message"] = state_.message;
    doc["installedVersion"] = installedVersion();
    doc["availableVersion"] = state_.availableVersion;
    doc["pendingVersion"] = (state_.success && state_.rebootPending
        && (state_.target == "app" || state_.target == "full"))
        ? state_.availableVersion
        : "";
    doc["manifestError"] = manifestCache_.error();
    doc["received"] = state_.received;
    doc["total"] = state_.total;
    doc["uploadActive"] = upload_ != nullptr;
    doc["activityAgeMs"] = state_.lastActivityMs == 0 ? 0UL : (millis() - state_.lastActivityMs);
    doc["uploadStallTimeoutMs"] = kUploadStallTimeoutMs;
    doc["canReset"] = !state_.rebootPending
        && state_.source == "upload"
        && (!state_.inProgress || state_.phase == "failed");
    doc["configFallbackApplied"] = ConfigStore::getInstance().wasFallbackApplied();
    doc["configFallbackMessage"] = ConfigStore::getInstance().getFallbackMessage();
    const size_t appMaxSize = FlashSession::partitionSize(FlashSession::Target::App);
    doc["appMaxSize"] = appMaxSize;
    doc["firmwareMaxSize"] = appMaxSize;
    doc["webuiMaxSize"] = FlashSession::partitionSize(FlashSession::Target::WebUi);
    doc["manifestUrl"] = kLatestReleaseApiUrl;
    doc["releaseTagBaseUrl"] = kReleaseTagBaseUrl;

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    doc["runningPartitionLabel"] = running ? running->label : "";
    doc["runningPartitionAddress"] = running ? running->address : 0;
    doc["runningPartitionSize"] = running ? running->size : 0;
    doc["configuredBootPartitionLabel"] = boot ? boot->label : "";
    doc["configuredBootPartitionAddress"] = boot ? boot->address : 0;
    doc["configuredBootPartitionSize"] = boot ? boot->size : 0;

    const esp_reset_reason_t resetReason = esp_reset_reason();
    doc["lastResetReason"] = resetReasonToString(resetReason);
    doc["lastResetReasonCode"] = static_cast<int>(resetReason);

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

void UpdateService::sendManifest(AsyncWebServerRequest *request) {
    // Non-blocking: Liefert Cache-Daten sofort und startet bei Bedarf einen Background-Fetch.
    if (manifestCache_.isStale(kManifestCacheTtlMs) && !manifestFetchInProgress_) {
        startBackgroundManifestFetch();
    }

    const ReleaseManifest manifest = manifestCache_.manifest();
    const std::string manifestError = manifestCache_.error();
    JsonDocument doc;

    if (!manifest.valid) {
        const bool fetching = manifestFetchInProgress_;
        doc["error"] = !manifestError.empty() ? manifestError
            : (fetching ? "Release-Informationen werden geladen" : "Kein Release-Manifest verfügbar");
        doc["manifestUrl"] = kLatestReleaseApiUrl;
        doc["fetching"] = fetching;
        std::string json;
        serializeJson(doc, json);
        request->send(manifestError.empty() && fetching ? 202 : 502, "application/json", json.c_str());
        return;
    }

    doc["fetching"] = manifestFetchInProgress_;
    doc["version"] = manifest.version;
    doc["releaseUrl"] = manifest.releaseUrl;

    JsonObject assets = doc["assets"].to<JsonObject>();
    for (const auto& entry : {std::make_pair("app", &manifest.app), std::make_pair("webui", &manifest.webui)}) {
        if (entry.second->url.empty()) continue;
        JsonObject asset = assets[entry.first].to<JsonObject>();
        asset["name"] = entry.second->name;
        asset["url"] = entry.second->url;
        asset["size"] = entry.second->size;
    }

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

// ------------------------------------------------------------------ Release-Manifest

bool UpdateService::fetchLatestManifest(ReleaseManifest& manifest, std::string& error) {
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
    http.addHeader("User-Agent", kUserAgent);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Release-Request fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }

    const std::string raw = http.getString().c_str();
    http.end();
    return parseLatestRelease(raw, manifest, error);
}

// Erwartet pro Komponente ein signiertes Release-Asset: "...-app.bin" (App) bzw. "...-web-ui.bin" (Web-UI).
bool UpdateService::parseLatestRelease(const std::string& rawJson, ReleaseManifest& manifest, std::string& error) {
    manifest = {};
    JsonDocument doc;
    if (deserializeJson(doc, rawJson) != DeserializationError::Ok) {
        error = "Release-Antwort ist kein gültiges JSON";
        return false;
    }

    manifest.version = Version::normalizeTag(doc["tag_name"] | "");
    manifest.releaseUrl = doc["html_url"] | kLatestReleaseUrl;

    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        const std::string name = asset["name"] | "";
        const std::string lowerName = strutil::toLower(name);

        Asset* target = nullptr;
        if (hasSuffix(lowerName, "-app.bin")) {
            target = &manifest.app;
        } else if (hasSuffix(lowerName, "-web-ui.bin")) {
            target = &manifest.webui;
        }
        if (target != nullptr) {
            target->name = name;
            target->url = asset["browser_download_url"] | "";
            target->size = asset["size"] | 0;
        }
    }

    manifest.valid = !manifest.version.empty() && (!manifest.app.url.empty() || !manifest.webui.url.empty());
    if (!manifest.valid) {
        error = "Release enthält keine signierten OTA-Assets";
        return false;
    }
    return true;
}

void UpdateService::startBackgroundManifestFetch() {
    if (manifestFetchInProgress_) {
        return;
    }
    manifestFetchInProgress_ = true;

    const BaseType_t created = xTaskCreate([](void *param) {
        auto *service = static_cast<UpdateService*>(param);
        ReleaseManifest manifest;
        std::string error;
        if (service->fetchLatestManifest(manifest, error)) {
            service->manifestCache_.storeSuccess(manifest);
        } else {
            service->manifestCache_.storeFailure(error);
        }
        service->manifestFetchInProgress_ = false;
        vTaskDelete(nullptr);
    }, "manifest-fetch", 8192, this, 1, nullptr);

    if (created != pdPASS) {
        manifestFetchInProgress_ = false;
        manifestCache_.storeFailure("Manifest-Task konnte nicht gestartet werden");
        DebugLogger::getInstance().log(LogLevel::ERROR, "Manifest-Task konnte nicht gestartet werden");
    }
}

std::string UpdateService::resolveTarget(const ReleaseManifest& manifest, const std::string& requestedTarget, std::string& error) const {
    const bool hasApp = !manifest.app.url.empty();
    const bool hasWebUi = !manifest.webui.url.empty();

    if (requestedTarget == "auto" || requestedTarget.empty()) {
        if (Version::compare(manifest.version, installedVersion()) <= 0) {
            error = "Keine neuere Version verfügbar";
            return "";
        }
        if (hasApp && hasWebUi) return "full";
        if (hasApp) return "app";
        if (hasWebUi) return "webui";
        error = "Manifest enthält keine verfügbaren Update-Assets";
        return "";
    }
    if (requestedTarget == "app") {
        if (hasApp) return "app";
        error = "Im Manifest ist kein App-Update vorhanden";
        return "";
    }
    if (requestedTarget == "webui") {
        if (hasWebUi) return "webui";
        error = "Im Manifest ist kein Web-UI-Update vorhanden";
        return "";
    }
    if (requestedTarget == "full") {
        if (hasApp && hasWebUi) return "full";
        error = "Für ein Komplett-Update fehlen Assets im Manifest";
        return "";
    }
    error = "Ungültiges Update-Ziel";
    return "";
}

// ------------------------------------------------------------------ Repo-Update

void UpdateService::startRemoteUpdateTask(const std::string& target) {
    resetState("repo", target);
    state_.inProgress = true;
    setPhase("queued", "Repo-Update wird vorbereitet");

    struct Context {
        UpdateService* service;
        std::string target;
    };
    auto *context = new Context{ this, target };
    const BaseType_t created = xTaskCreate([](void *param) {
        auto *ctx = static_cast<Context*>(param);
        ctx->service->runRemoteUpdateTask(ctx->target);
        delete ctx;
        vTaskDelete(nullptr);
    }, "level-control-ota", 16384, context, 1, nullptr);

    if (created != pdPASS) {
        delete context;
        markFailed("OTA-Task konnte nicht gestartet werden");
    }
}

void UpdateService::runRemoteUpdateTask(const std::string& requestedTarget) {
    ReleaseManifest manifest;
    std::string error;

    setPhase("release", "Release-Informationen werden geladen");
    if (!fetchLatestManifest(manifest, error)) {
        manifestCache_.storeFailure(error);
        markFailed(error);
        return;
    }
    manifestCache_.storeSuccess(manifest);
    state_.availableVersion = manifest.version;

    const std::string target = resolveTarget(manifest, requestedTarget, error);
    if (target.empty()) {
        markFailed(error);
        return;
    }
    state_.target = target;

    if ((target == "webui" || target == "full")
        && !downloadAndFlash(manifest.webui, FlashSession::Target::WebUi, "webui", error)) {
        markFailed(error);
        return;
    }
    if ((target == "app" || target == "full")
        && !downloadAndFlash(manifest.app, FlashSession::Target::App, "app", error)) {
        markFailed(error);
        return;
    }

    markSucceeded(target == "full" ? "App und Web-UI wurden erfolgreich aktualisiert"
        : (target == "app" ? "App wurde erfolgreich aktualisiert" : "Web-UI wurde erfolgreich aktualisiert"));
    web::scheduleRestart(kRestartDelayMs);
}

bool UpdateService::downloadAndFlash(const Asset& asset, FlashSession::Target target, const std::string& phase, std::string& error) {
    if (!isHttpsUrl(asset.url)) {
        error = "Asset-URL muss HTTPS verwenden";
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);
    HTTPClient http;
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, asset.url.c_str())) {
        error = "Asset konnte nicht geöffnet werden";
        return false;
    }
    http.addHeader("User-Agent", kUserAgent);
    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Asset-Download fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }
    const int contentLength = http.getSize();
    if (contentLength <= static_cast<int>(FlashSession::kTrailerSize)) {
        error = "Asset ist zu klein oder enthält keinen Signatur-Trailer";
        http.end();
        return false;
    }
    const size_t totalSize = static_cast<size_t>(contentLength);

    FlashSession session;  // bricht bei jedem vorzeitigen return automatisch ab
    if (!session.begin(target, totalSize - FlashSession::kTrailerSize, error)) {
        http.end();
        return false;
    }

    setPhase(phase, "Download läuft", 0, totalSize);
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[kDownloadBufferSize];
    size_t received = 0;
    while (received < totalSize) {
        const size_t available = stream->available();
        if (available == 0) {
            if (!http.connected()) {
                break;
            }
            delay(1);
            continue;
        }

        const size_t chunk = stream->readBytes(buffer, std::min<size_t>(available, sizeof(buffer)));
        if (chunk == 0) {
            continue;
        }
        if (received + chunk > totalSize) {
            error = "Asset ist größer als angekündigt";
            http.end();
            return false;
        }
        if (!session.write(buffer, chunk, error)) {
            http.end();
            return false;
        }
        received += chunk;
        setPhase(phase, "Download läuft", received, totalSize);
    }
    http.end();

    if (received != totalSize) {
        error = "Asset-Download wurde vorzeitig beendet";
        return false;
    }

    setPhase(phase, "Signatur wird geprüft", received, totalSize);
    return session.finish(error);
}

// ------------------------------------------------------------------ lokaler Upload

void UpdateService::handleUpload(AsyncWebServerRequest *request, FlashSession::Target target, const String& filename,
                                 size_t index, uint8_t *data, size_t len, bool final) {
    recoverStuckUploadIfNeeded();
    const bool isApp = target == FlashSession::Target::App;
    const char* targetName = isApp ? "app" : "webui";

    if (index == 0) {
        if (state_.inProgress || upload_) {
            sendError(request, 409, "Update läuft bereits");
            return;
        }

        std::string error;
        if (!validateUploadFilename(filename, error)) {
            sendError(request, 400, error);
            return;
        }
        std::unique_ptr<FlashSession> session(new FlashSession());
        resetState("upload", targetName);
        state_.inProgress = true;
        setPhase("validating", "Upload wird geprüft", 0, 0);
        if (!session->begin(target, 0, error)) {
            markFailed(error);
            clearUpload();
            sendError(request, 500, error);
            return;
        }

        upload_ = std::move(session);
        uploadRequest_ = request;
        uploadLastChunkMs_ = millis();
        DebugLogger::getInstance().log(LogLevel::INFO,
            std::string("Manual upload started: target=") + targetName + ", file=" + filename.c_str());
    }

    // Nur der Request, der den Upload gestartet (und damit authentifiziert) hat, darf Daten liefern.
    if (!upload_ || uploadRequest_ != request) {
        return;
    }

    std::string error;
    if (!uploadFailed_ && len > 0) {
        uploadLastChunkMs_ = millis();
        if (upload_->write(data, len, error)) {
            setPhase("uploading", "Upload läuft", upload_->bytesWritten(), 0);
        } else {
            uploadFailed_ = true;
            markFailed(error);
        }
    }

    if (!final) {
        return;
    }

    if (uploadFailed_) {
        const std::string message = state_.message;
        clearUpload();
        sendError(request, 400, message);
        return;
    }

    setPhase("verifying", "Signatur wird geprüft", upload_->bytesWritten(), 0);
    if (!upload_->finish(error)) {
        markFailed(error);
        clearUpload();
        sendError(request, 400, error);
        return;
    }
    clearUpload();

    const std::string successMessage = isApp ? "App wurde erfolgreich hochgeladen" : "Web-UI wurde erfolgreich hochgeladen";
    const bool deferReboot = request->hasParam("deferReboot")
        && (request->getParam("deferReboot")->value() == "1"
            || request->getParam("deferReboot")->value().equalsIgnoreCase("true"));

    JsonDocument doc;
    doc["status"] = "ok";
    doc["message"] = successMessage;
    if (deferReboot) {
        markSucceeded(successMessage + " (Neustart ausstehend)");
        state_.rebootPending = false;
        doc["deferredReboot"] = true;
    } else {
        markSucceeded(successMessage);
        web::scheduleRestart(kRestartDelayMs);
    }
    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}
