#ifndef LEVEL_CORE_CONFIG_H
#define LEVEL_CORE_CONFIG_H

#include <string>

struct WifiConfig {
    std::string ssid;
    std::string password;
    std::string deviceName = "Level-Control";
    std::string ntpServerPrimary = "pool.ntp.org";
    std::string ntpServerSecondary = "time.cloudflare.com";
};

struct StaticIpConfig {
    std::string ip;
    std::string gateway;
    std::string subnet;
    std::string dns;
};

struct MqttConfig {
    std::string server;
    int port = 1883;
    std::string user;
    std::string password;
    bool discovery = true;
};

struct PushConfig {
    bool enabled = false;
    std::string smtpServer;
    int smtpPort = 587;
    bool useSsl = false;
    bool startTls = false;
    bool smtpSkipCertVerify = false;
    std::string authUser;
    std::string authPassword;
    std::string senderName = "Level-Control";
    std::string senderEmail;
    std::string recipientEmail;
    float triggerPercent = 20.0f;
    int sendHour = 8;
    int sendMinute = 0;
    std::string reminderCycle = "day";
    int reminderWeekday = 1;
    std::string subjectTemplate = "Level-Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";
    std::string bodyTemplate = "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Level-Control";
};

struct Config {
    int version = 1;
    WifiConfig wifi;
    StaticIpConfig staticIp;
    MqttConfig mqtt;
    PushConfig push;
    float behaelterhoehe = 95.0;
    float offset = 0.0;
    unsigned long sampleIntervalSeconds = 5;
    std::string sensorType = "rcwl1670";
    int tofRoiSize = 16;  // VL53L1X Sichtfeld (SPADs je Kante): 16 = voll, 8 = mittel, 4 = eng
    std::string adminPasswordHash;
};

// Gueltigkeitsbereiche der Sensor-Einstellungen. Einzige Quelle fuer Web-UI, MQTT, Import und NVS.
namespace ConfigLimits {
constexpr float kMinTankHeightCm = 10.0f;
constexpr float kMaxTankHeightCm = 500.0f;
constexpr float kMaxOffsetCm = 200.0f;
constexpr int kTofRoiFull = 16;
constexpr int kTofRoiMedium = 8;
constexpr int kTofRoiNarrow = 4;
constexpr unsigned long kMinSampleIntervalSeconds = 5UL;
constexpr unsigned long kMaxSampleIntervalSeconds = 2592000UL;  // 30 Tage
}  // namespace ConfigLimits

#endif // LEVEL_CORE_CONFIG_H
