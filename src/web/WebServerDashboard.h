#ifndef WEB_SERVER_DASHBOARD_H
#define WEB_SERVER_DASHBOARD_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <string>
#include <vector>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include "SensorManager.h"
#include "DebugLogger.h"

class WebServerDashboard {
public:
    static WebServerDashboard& getInstance();

    void init();
    void start();
    void broadcastSensorData();
    void broadcastWifiData();
    void broadcastUptime();
    void broadcastMqttState();
    std::string getInstalledVersion() const;
    std::string getAvailableVersion();
    std::string getLatestReleaseUrl();
    bool requestRepoUpdate(const std::string& target, std::string& error);
    bool isUpdateInProgress() const;
    int getUpdateProgressPercent() const;

private:
    struct ManifestAsset {
        std::string name;
        std::string url;
        std::string signatureUrl;
        size_t size = 0;
    };

    struct ReleaseManifest {
        std::string version;
        std::string releaseUrl;
        ManifestAsset app;
        ManifestAsset webui;
        bool valid = false;
    };

    struct UpdateState {
        bool inProgress = false;
        bool success = false;
        bool rebootPending = false;
        std::string source;
        std::string target;
        std::string phase;
        std::string message;
        std::string availableVersion;
        size_t received = 0;
        size_t total = 0;
        unsigned long lastActivityMs = 0;
    };

    WebServerDashboard();
    AsyncWebServer server_;
    AsyncWebSocket ws_;
    UpdateState updateState_;
    ReleaseManifest cachedManifest_;
    std::string manifestError_;
    unsigned long lastManifestCheckMs_ = 0;
    volatile bool manifestFetchInProgress_ = false;
    bool uploadActive_ = false;
    bool uploadFailed_ = false;
    bool restartScheduled_ = false;
    bool littleFsMounted_ = false;
    bool uploadShaActive_ = false;
    const AsyncWebServerRequest* uploadRequest_ = nullptr;  // Request, der den laufenden Upload gestartet hat
    bool uploadDetachedMode_ = false;
    std::vector<uint8_t> uploadAppSignature_;
    std::vector<uint8_t> uploadWebUiSignature_;
    String uploadTarget_;
    String uploadFilename_;
    mbedtls_sha256_context uploadShaContext_;
    std::vector<uint8_t> uploadTailBuffer_;
    const esp_partition_t* uploadTargetPartition_ = nullptr;

    void setupRoutes();
    void setupUpdateRoutes();
    bool ensureLittleFsMounted();

    void resetUpdateState(const std::string& source, const std::string& target);
    void touchUpdateActivity();
    void setUpdatePhase(const std::string& phase, const std::string& message, size_t received = 0, size_t total = 0);
    void clearUploadSession(bool abortUpdate = false);
    void recoverStuckUploadIfNeeded();
    void markUpdateFailed(const std::string& message);
    void markUpdateSucceeded(const std::string& message);
    void scheduleRestart(uint32_t delayMs = 1500);
    void sendUpdateStatus(AsyncWebServerRequest *request);
    void sendUpdateManifest(AsyncWebServerRequest *request);
    bool fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error);
    bool parseLatestReleaseInfo(const std::string& rawJson, ReleaseManifest& manifest, std::string& error) const;
    bool refreshManifestCache(bool forceRefresh, std::string& error);
    void startBackgroundManifestFetch();
    void refreshManifestInBackgroundIfStale();
    void runManifestFetchTask();
    bool verifyDetachedFileSignature(const unsigned char* hash, size_t hashLen, const uint8_t* signature, size_t signatureLen, std::string& error) const;
    bool applyDetachedRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error);
    std::string resolveUpdateTarget(const ReleaseManifest& manifest, const std::string& requestedTarget, std::string& error) const;
    void startRemoteUpdateTask(const std::string& target);
    void runRemoteUpdateTask(const std::string& target);
    bool applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error);
    bool validateUploadChunk(const String& target, const uint8_t *data, size_t len, size_t index, std::string& error) const;
    void handleUpload(AsyncWebServerRequest *request, const String& target, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
    size_t getAppPartitionSize() const;
    size_t getFilesystemPartitionSize() const;

    unsigned long uploadLastChunkMs_ = 0;
    std::string adminSessionToken_;
    unsigned long adminSessionIssuedMs_ = 0;

    bool requireAdmin(AsyncWebServerRequest *request);
    bool isAdminAuthenticated(AsyncWebServerRequest *request) const;
    std::string issueAdminSession();
    void sendSessionToken(AsyncWebServerRequest *request);
};

#endif // WEB_SERVER_DASHBOARD_H