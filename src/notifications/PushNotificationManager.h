#ifndef PUSH_NOTIFICATION_MANAGER_H
#define PUSH_NOTIFICATION_MANAGER_H

#include <ctime>
#include <string>
#include <vector>

#include "ConfigStore.h"

struct SmtpDiagStep {
    std::string name;
    bool ok;
    std::string detail;
};

struct SmtpDiagResult {
    bool success = false;
    std::vector<SmtpDiagStep> steps;
};

class PushNotificationManager {
public:
    static PushNotificationManager& getInstance();

    void process(float levelPercent, float levelCm, float rawDistanceM, bool validReading);
    bool sendTestEmail(std::string& error);
    SmtpDiagResult smtpDiagnostic();

private:
    PushNotificationManager() = default;

    bool sendThresholdEmail(float levelPercent, float levelCm, float rawDistanceM, std::string& error);
    bool sendEmailInternal(const std::string& subject, const std::string& body, std::string& error, bool requireEnabled = true);
    static bool isConfigured(const PushConfig& push);
    std::string renderTemplate(const std::string& input, float levelPercent, float levelCm, float rawDistanceM, time_t nowEpoch) const;

    unsigned long lastProcessMs_ = 0;
    time_t lastSentEpoch_ = 0;
    time_t lastFailureEpoch_ = 0;
    bool wasBelowThreshold_ = false;
};

#endif // PUSH_NOTIFICATION_MANAGER_H
