#ifndef PUSH_NOTIFICATION_MANAGER_H
#define PUSH_NOTIFICATION_MANAGER_H

#include <ctime>
#include <mutex>
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

// Ergebnis eines manuell ausgeloesten Auftrags (Test-Mail oder SMTP-Diagnose). Die Auftraege laufen im
// Netzwerk-Task, weil ein SMTP-Dialog viele Sekunden dauern kann und kein Web-Handler so lange blockieren darf.
struct PushJobStatus {
    enum class State { Idle, Pending, Running, Done };
    State state = State::Idle;
    std::string kind;  // "test" oder "diagnostic"
    bool success = false;
    std::string error;
    SmtpDiagResult diagnostic;
};

class PushNotificationManager {
public:
    static PushNotificationManager& getInstance();

    // Zyklisch vom Netzwerk-Task aufgerufen: fuehrt anstehende Auftraege aus und prueft die Schwellwert-Mail.
    void service();

    // Aus beliebigen Tasks aufrufbar; false, wenn bereits ein Auftrag laeuft.
    bool requestTestEmail();
    bool requestDiagnostic();
    PushJobStatus jobStatus() const;

private:
    PushNotificationManager() = default;

    void process(float levelPercent, float levelCm, float rawDistanceM, bool validReading);
    bool sendTestEmail(std::string& error);
    SmtpDiagResult smtpDiagnostic();
    bool requestJob(const char* kind);
    void runPendingJob();

    bool sendThresholdEmail(float levelPercent, float levelCm, float rawDistanceM, std::string& error);
    bool sendEmailInternal(const std::string& subject, const std::string& body, std::string& error, bool requireEnabled = true);
    static bool isConfigured(const PushConfig& push);
    std::string renderTemplate(const std::string& input, float levelPercent, float levelCm, float rawDistanceM, time_t nowEpoch) const;

    unsigned long lastProcessMs_ = 0;
    time_t lastSentEpoch_ = 0;
    time_t lastFailureEpoch_ = 0;
    bool wasBelowThreshold_ = false;

    mutable std::mutex jobMutex_;
    PushJobStatus job_;
};

#endif // PUSH_NOTIFICATION_MANAGER_H
