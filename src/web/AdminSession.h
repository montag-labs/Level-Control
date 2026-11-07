#ifndef ADMIN_SESSION_H
#define ADMIN_SESSION_H

#include <ESPAsyncWebServer.h>

#include <string>

// Admin-Anmeldung per Sitzungs-Token (Header X-Admin-Token) mit Sperre nach wiederholten Fehlversuchen.
class AdminSession {
public:
    static constexpr unsigned long kSessionTtlMs = 8UL * 60UL * 60UL * 1000UL;

    bool isAuthenticated(AsyncWebServerRequest *request) const;
    // Antwortet bei fehlender Anmeldung mit 401 und gibt false zurueck.
    bool require(AsyncWebServerRequest *request);
    // Stellt ein neues Token aus und sendet es als {"status":"ok","token":...}.
    void sendToken(AsyncWebServerRequest *request);
    void logout();

    // Bremst Passwort-Raten: nach kMaxLoginFailures Fehlversuchen in Folge ist der Login kLoginLockoutMs gesperrt.
    bool rejectIfLoginLocked(AsyncWebServerRequest *request);
    void recordLoginFailure();
    void resetLoginFailures() { loginFailures_ = 0; }

private:
    static constexpr int kMaxLoginFailures = 5;
    static constexpr unsigned long kLoginLockoutMs = 60UL * 1000UL;

    std::string token_;
    unsigned long issuedMs_ = 0;
    int loginFailures_ = 0;
    bool loginLocked_ = false;
    unsigned long loginLockedUntilMs_ = 0;
};

#endif  // ADMIN_SESSION_H
