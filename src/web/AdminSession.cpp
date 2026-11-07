#include "AdminSession.h"

#include <esp_system.h>

#include "ConfigStore.h"
#include "WebSupport.h"

bool AdminSession::isAuthenticated(AsyncWebServerRequest *request) const {
    // Billige Pruefungen zuerst; die Konfiguration (Passwort gesetzt?) wird nur bei passendem Token geladen.
    if (token_.empty() || (millis() - issuedMs_) > kSessionTtlMs) {
        return false;
    }
    const AsyncWebHeader* header = request->getHeader("X-Admin-Token");
    if (header == nullptr
        || !web::constantTimeEquals(header->value().c_str(), header->value().length(),
                                    token_.c_str(), token_.size())) {
        return false;
    }

    Config config;
    return ConfigStore::getInstance().load(config) && !config.adminPasswordHash.empty();
}

bool AdminSession::require(AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
        return true;
    }

    request->send(401, "application/json", "{\"error\":\"admin_auth_required\"}");
    return false;
}

void AdminSession::sendToken(AsyncWebServerRequest *request) {
    char token[65];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()));
    token_ = token;
    issuedMs_ = millis();

    const std::string json = "{\"status\":\"ok\",\"token\":\"" + token_ + "\"}";
    request->send(200, "application/json", json.c_str());
}

void AdminSession::logout() {
    token_.clear();
    issuedMs_ = 0;
}

bool AdminSession::rejectIfLoginLocked(AsyncWebServerRequest *request) {
    if (!loginLocked_) {
        return false;
    }
    const long remainingMs = static_cast<long>(loginLockedUntilMs_ - millis());
    if (remainingMs <= 0) {
        loginLocked_ = false;
        loginFailures_ = 0;
        return false;
    }
    const std::string json = "{\"error\":\"admin_login_locked\",\"retryAfterSeconds\":"
        + std::to_string((remainingMs + 999) / 1000) + "}";
    request->send(429, "application/json", json.c_str());
    return true;
}

void AdminSession::recordLoginFailure() {
    if (++loginFailures_ >= kMaxLoginFailures) {
        loginLocked_ = true;
        loginLockedUntilMs_ = millis() + kLoginLockoutMs;
        loginFailures_ = 0;
    }
}
