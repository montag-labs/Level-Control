#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <vector>
#include <string>
#include "ConfigStore.h"

struct WifiNetwork {
    std::string ssid;
    int rssi;
};

class WifiManager {
public:
    static WifiManager& getInstance();

    void init();
    void process();
    bool connect();
    bool beginConnectAsync();
    void scheduleRestart(uint32_t delayMs);
    bool isRestartScheduled() const { return restartScheduled_; }
    unsigned long getRestartRemainingMs() const;
    void startAP(const std::string& ssid, const std::string& password, uint8_t maxConnections = 1);
    std::vector<WifiNetwork> scanNetworks();
    bool setConfig(const WifiConfig& config, const StaticIpConfig& staticConfig = {});
    WifiConfig getConfig() const;
    std::string getDnsHostname() const;
    std::string getLocalUrl() const;
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

private:
    WifiManager();
    void scheduleReconnect();
    void resetReconnectBackoff();
    void applyIpConfig();
    void applyStationIdentity();
    WifiConfig config_;
    StaticIpConfig staticConfig_;
    bool reconnectPending_ = false;
    bool restartScheduled_ = false;
    unsigned long restartAtMs_ = 0;
    unsigned long nextReconnectAttemptMs_ = 0;
    unsigned long reconnectBackoffMs_ = 2000;
    static constexpr unsigned long reconnectBackoffMinMs_ = 2000;
    static constexpr unsigned long reconnectBackoffMaxMs_ = 60000;
};

#endif // WIFI_MANAGER_H