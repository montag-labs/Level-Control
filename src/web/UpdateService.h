#ifndef UPDATE_SERVICE_H
#define UPDATE_SERVICE_H

#include <ESPAsyncWebServer.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "FlashSession.h"

// Firmware-/Web-UI-Updates: Repo-Update (GitHub-Release) und lokaler Upload. Alle Updates sind signiert;
// die Signatur liegt als getrennte .sig-Datei neben dem Image.
class UpdateService {
public:
    using AuthCheck = std::function<bool(AsyncWebServerRequest*)>;

    static UpdateService& getInstance();

    // Registriert alle /api/update/*-Routen. `requireAdmin` sendet bei Misserfolg selbst die 401-Antwort.
    void registerRoutes(AsyncWebServer& server, AuthCheck requireAdmin);

    std::string installedVersion() const;
    // Beide Abfragen blockieren nicht: sie liefern den Cache und aktualisieren ihn bei Bedarf im Hintergrund.
    std::string availableVersion();
    std::string latestReleaseUrl();
    // Startet das Repo-Update ("auto", "app", "webui" oder "full"); Fehlertext in `error`.
    bool requestRepoUpdate(const std::string& target, std::string& error);
    bool isInProgress() const { return state_.inProgress; }
    int progressPercent() const;

private:
    struct Asset {
        std::string name;
        std::string url;
        std::string signatureUrl;
        size_t size = 0;
    };

    struct ReleaseManifest {
        std::string version;
        std::string releaseUrl;
        Asset app;
        Asset webui;
        bool valid = false;
    };

    // Vom Hintergrund-Task geschrieben, von HTTP-/MQTT-Handlern gelesen.
    class ManifestCache {
    public:
        ReleaseManifest manifest() const;
        std::string error() const;
        bool isStale(unsigned long ttlMs) const;
        void storeSuccess(const ReleaseManifest& manifest);
        // Behaelt ein zuvor gueltiges Manifest, damit kurze Netzfehler den Update-Button nicht sperren.
        void storeFailure(const std::string& error);

    private:
        mutable std::mutex mutex_;
        ReleaseManifest manifest_;
        std::string error_;
        unsigned long lastCheckMs_ = 0;
    };

    struct State {
        bool inProgress = false;
        bool success = false;
        bool rebootPending = false;
        std::string source;  // "repo" oder "upload"
        std::string target;
        std::string phase;
        std::string message;
        std::string availableVersion;
        size_t received = 0;
        size_t total = 0;
        unsigned long lastActivityMs = 0;
    };

    UpdateService() = default;

    // Zustand
    void resetState(const std::string& source, const std::string& target);
    void setPhase(const std::string& phase, const std::string& message, size_t received = 0, size_t total = 0);
    void markFailed(const std::string& message);
    void markSucceeded(const std::string& message);

    // HTTP
    void sendStatus(AsyncWebServerRequest *request);
    void sendManifest(AsyncWebServerRequest *request);

    // Release-Manifest
    bool fetchLatestManifest(ReleaseManifest& manifest, std::string& error);
    static bool parseLatestRelease(const std::string& rawJson, ReleaseManifest& manifest, std::string& error);
    void refreshManifestInBackgroundIfStale();
    void startBackgroundManifestFetch();
    std::string resolveTarget(const ReleaseManifest& manifest, const std::string& requestedTarget, std::string& error) const;

    // Repo-Update
    void startRemoteUpdateTask(const std::string& target);
    void runRemoteUpdateTask(const std::string& requestedTarget);
    bool downloadAndFlash(const Asset& asset, FlashSession::Target target, const std::string& phase, std::string& error);

    // Lokaler Upload
    void handleUpload(AsyncWebServerRequest *request, FlashSession::Target target, const String& filename,
                      size_t index, uint8_t *data, size_t len, bool final);
    void clearUpload();
    void recoverStuckUploadIfNeeded();

    State state_;
    ManifestCache manifestCache_;
    volatile bool manifestFetchInProgress_ = false;

    std::unique_ptr<FlashSession> upload_;
    const AsyncWebServerRequest* uploadRequest_ = nullptr;  // Request, der den laufenden Upload gestartet hat
    bool uploadFailed_ = false;
    unsigned long uploadLastChunkMs_ = 0;
    std::vector<uint8_t> appSignature_;
    std::vector<uint8_t> webUiSignature_;
};

#endif  // UPDATE_SERVICE_H
