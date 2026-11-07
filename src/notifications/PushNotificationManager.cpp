#include "PushNotificationManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <ctime>

#include "ConfigStore.h"
#include "DebugLogger.h"
#include "MqttManager.h"
#include "SensorManager.h"
#include "SmtpTrustedRoots.h"
#include "StringUtils.h"

namespace {
constexpr unsigned long kProcessIntervalMs = 10000UL;
constexpr time_t kRetryAfterFailureSeconds = 300;
constexpr time_t kMinValidEpoch = 1700000000;
constexpr uint32_t kSmtpTimeoutMs = 15000;

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string formatFloat(float value, int decimals) {
    char buffer[32] = {0};
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string formatTimestamp(time_t epoch) {
    if (epoch < kMinValidEpoch) {
        return "n/a";
    }

    struct tm timeInfo;
    localtime_r(&epoch, &timeInfo);
    char buffer[32] = {0};
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeInfo);
    return buffer;
}

std::string toBase64(const std::string& input) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3) {
        const size_t remaining = input.size() - i;
        const uint32_t octetA = static_cast<unsigned char>(input[i]);
        const uint32_t octetB = remaining > 1 ? static_cast<unsigned char>(input[i + 1]) : 0;
        const uint32_t octetC = remaining > 2 ? static_cast<unsigned char>(input[i + 2]) : 0;
        const uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        output.push_back(alphabet[(triple >> 18) & 0x3F]);
        output.push_back(alphabet[(triple >> 12) & 0x3F]);
        output.push_back(remaining > 1 ? alphabet[(triple >> 6) & 0x3F] : '=');
        output.push_back(remaining > 2 ? alphabet[triple & 0x3F] : '=');
    }

    return output;
}

// Waehlt Klartext- oder TLS-Client gemaess Konfiguration und setzt Zertifikatspruefung und Timeout.
Client& prepareSmtpClient(const PushConfig& push, WiFiClient& plain, WiFiClientSecure& secure) {
    if (push.useSsl) {
        if (push.smtpSkipCertVerify) {
            secure.setInsecure();
        } else {
            secure.setCACert(kSmtpTrustedRootCAs);
        }
        secure.setTimeout(kSmtpTimeoutMs / 1000);
        return secure;
    }
    plain.setTimeout(kSmtpTimeoutMs / 1000);
    return plain;
}

bool readSmtpResponse(Client& client, int expectedCode, std::string& error) {
    unsigned long start = millis();
    std::string lastLine;

    while (millis() - start < kSmtpTimeoutMs) {
        while (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            lastLine = line.c_str();

            if (lastLine.size() < 4) {
                continue;
            }

            if (!isdigit(static_cast<unsigned char>(lastLine[0]))
                || !isdigit(static_cast<unsigned char>(lastLine[1]))
                || !isdigit(static_cast<unsigned char>(lastLine[2]))) {
                continue;
            }

            const int code = atoi(lastLine.substr(0, 3).c_str());
            const char separator = lastLine[3];
            if (separator == '-') {
                continue;
            }

            if (code == expectedCode) {
                return true;
            }

            error = "SMTP " + std::to_string(code) + ": " + lastLine;
            return false;
        }

        delay(10);
    }

    error = lastLine.empty() ? "SMTP Antwort Timeout" : ("SMTP Timeout: " + lastLine);
    return false;
}

bool sendSmtpCommand(Client& client, const std::string& command, int expectedCode, std::string& error) {
    client.print(command.c_str());
    return readSmtpResponse(client, expectedCode, error);
}

std::string normalizeBody(const std::string& body) {
    std::string result = body;
    replaceAll(result, "\r\n", "\n");
    replaceAll(result, "\r", "\n");
    replaceAll(result, "\n", "\r\n");
    return result;
}

std::string readTlsLastError(WiFiClientSecure& client) {
    char buffer[160] = {0};
    const int code = client.lastError(buffer, sizeof(buffer));
    if (code == 0 && buffer[0] == '\0') {
        return "";
    }

    std::string detail = "TLS Fehler";
    if (code != 0) {
        detail += " (" + std::to_string(code) + ")";
    }
    if (buffer[0] != '\0') {
        detail += ": ";
        detail += buffer;
    }
    return detail;
}

int toMondayBasedWeekday(const tm& timeInfo) {
    return timeInfo.tm_wday == 0 ? 7 : timeInfo.tm_wday;
}

time_t buildLocalTime(int year, int month, int day, int hour, int minute) {
    struct tm candidate = {};
    candidate.tm_year = year - 1900;
    candidate.tm_mon = month - 1;
    candidate.tm_mday = day;
    candidate.tm_hour = hour;
    candidate.tm_min = minute;
    candidate.tm_sec = 0;
    candidate.tm_isdst = -1;
    return mktime(&candidate);
}

int firstWeekdayOfMonth(int year, int month, int reminderWeekday) {
    struct tm firstDay = {};
    firstDay.tm_year = year - 1900;
    firstDay.tm_mon = month - 1;
    firstDay.tm_mday = 1;
    firstDay.tm_isdst = -1;
    mktime(&firstDay);

    const int firstWeekday = toMondayBasedWeekday(firstDay);
    const int offset = (reminderWeekday - firstWeekday + 7) % 7;
    return 1 + offset;
}

time_t buildMonthlyReminderTime(int year, int month, const PushConfig& pushConfig) {
    const int reminderWeekday = ConfigStore::normalizeReminderWeekday(pushConfig.reminderWeekday);
    const int reminderDay = firstWeekdayOfMonth(year, month, reminderWeekday);
    return buildLocalTime(year, month, reminderDay, pushConfig.sendHour, pushConfig.sendMinute);
}

time_t getLatestReminderOccurrence(const PushConfig& pushConfig, time_t nowEpoch) {
    if (nowEpoch < kMinValidEpoch) {
        return 0;
    }

    struct tm localNow;
    localtime_r(&nowEpoch, &localNow);

    const std::string reminderCycle = ConfigStore::normalizeReminderCycle(pushConfig.reminderCycle);

    if (reminderCycle == "day") {
        struct tm candidate = localNow;
        candidate.tm_hour = pushConfig.sendHour;
        candidate.tm_min = pushConfig.sendMinute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        time_t candidateEpoch = mktime(&candidate);
        if (candidateEpoch > nowEpoch) {
            candidate.tm_mday -= 1;
            candidateEpoch = mktime(&candidate);
        }
        return candidateEpoch;
    }

    if (reminderCycle == "week") {
        struct tm candidate = localNow;
        candidate.tm_hour = pushConfig.sendHour;
        candidate.tm_min = pushConfig.sendMinute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        candidate.tm_mday -= (toMondayBasedWeekday(localNow) - ConfigStore::normalizeReminderWeekday(pushConfig.reminderWeekday));
        time_t candidateEpoch = mktime(&candidate);
        if (candidateEpoch > nowEpoch) {
            candidate.tm_mday -= 7;
            candidateEpoch = mktime(&candidate);
        }
        return candidateEpoch;
    }

    int year = localNow.tm_year + 1900;
    int month = localNow.tm_mon + 1;
    time_t candidateEpoch = buildMonthlyReminderTime(year, month, pushConfig);
    if (candidateEpoch > nowEpoch) {
        month -= 1;
        if (month == 0) {
            month = 12;
            year -= 1;
        }
        candidateEpoch = buildMonthlyReminderTime(year, month, pushConfig);
    }
    return candidateEpoch;
}
}

PushNotificationManager& PushNotificationManager::getInstance() {
    static PushNotificationManager instance;
    return instance;
}

bool PushNotificationManager::isConfigured(const PushConfig& push) {
    return !push.smtpServer.empty()
        && !push.senderEmail.empty()
        && !push.recipientEmail.empty()
        && !push.authUser.empty()
        && !push.authPassword.empty();
}

std::string PushNotificationManager::renderTemplate(const std::string& input, float levelPercent, float levelCm, float rawDistanceM, time_t nowEpoch) const {
    std::string text = input;
    replaceAll(text, "{level_percent}", formatFloat(levelPercent, 1));
    replaceAll(text, "{level_cm}", formatFloat(levelCm, 1));
    replaceAll(text, "{raw_distance_m}", formatFloat(rawDistanceM, 3));
    replaceAll(text, "{timestamp}", formatTimestamp(nowEpoch));
    replaceAll(text, "{device_id}", MqttManager::getInstance().getDeviceId());
    replaceAll(text, "{ip}", WiFi.localIP().toString().c_str());
    replaceAll(text, "{ssid}", WiFi.SSID().c_str());
    return text;
}

bool PushNotificationManager::sendEmailInternal(const std::string& subject, const std::string& body, std::string& error, bool requireEnabled) {
    if (WiFi.status() != WL_CONNECTED) {
        error = "WLAN nicht verbunden";
        return false;
    }

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        error = "Push-Konfiguration konnte nicht geladen werden";
        return false;
    }

    if (requireEnabled && !config.push.enabled) {
        error = "Push-Benachrichtigung ist deaktiviert";
        return false;
    }

    if (!isConfigured(config.push)) {
        error = "SMTP-Konfiguration unvollstaendig";
        return false;
    }

    if (config.push.startTls && !config.push.useSsl) {
        error = "STARTTLS wird aktuell nicht unterstuetzt. Bitte SSL/TLS aktivieren.";
        return false;
    }

    const std::string smtpServer = strutil::trim(config.push.smtpServer);
    if (smtpServer.empty()) {
        error = "SMTP-Server fehlt";
        return false;
    }

    if (config.push.useSsl && config.push.smtpPort == 587) {
        error = "SSL/TLS aktiv, aber Port 587 gesetzt. Bitte Port 465 verwenden.";
        return false;
    }

    if (config.push.startTls && config.push.smtpPort == 465) {
        error = "STARTTLS aktiv, aber Port 465 gesetzt. Bitte Port 587 verwenden.";
        return false;
    }

    if (config.push.useSsl && !config.push.smtpSkipCertVerify && time(nullptr) < kMinValidEpoch) {
        error = "TLS-Zertifikatspruefung nicht moeglich: Uhrzeit noch nicht synchronisiert. Bitte NTP/WLAN pruefen oder testweise 'Zertifikat ueberspringen' aktivieren.";
        return false;
    }

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    Client* client = &prepareSmtpClient(config.push, plainClient, secureClient);

    IPAddress smtpIp;
    if (!WiFi.hostByName(smtpServer.c_str(), smtpIp)) {
        error = "SMTP DNS-Aufloesung fehlgeschlagen: " + smtpServer;
        return false;
    }

    if (!client->connect(smtpServer.c_str(), config.push.smtpPort)) {
        error = "SMTP Verbindung fehlgeschlagen (" + smtpServer + ":" + std::to_string(config.push.smtpPort) + ")";
        if (config.push.useSsl && !config.push.smtpSkipCertVerify) {
            error += " - pruefe Uhrzeit/NTP oder aktiviere testweise 'Zertifikat ueberspringen'.";

            const std::string tlsDetail = readTlsLastError(secureClient);
            if (!tlsDetail.empty()) {
                error += " - ";
                error += tlsDetail;
            }
        }
        return false;
    }

    if (!readSmtpResponse(*client, 220, error)) return false;
    if (!sendSmtpCommand(*client, "EHLO level-control\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "AUTH LOGIN\r\n", 334, error)) return false;
    if (!sendSmtpCommand(*client, toBase64(config.push.authUser) + "\r\n", 334, error)) return false;
    if (!sendSmtpCommand(*client, toBase64(config.push.authPassword) + "\r\n", 235, error)) return false;
    if (!sendSmtpCommand(*client, "MAIL FROM:<" + config.push.senderEmail + ">\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "RCPT TO:<" + config.push.recipientEmail + ">\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "DATA\r\n", 354, error)) return false;

    std::string payload;
    payload.reserve(body.size() + 512);
    payload += "From: ";
    payload += config.push.senderName.empty() ? "Level-Control" : config.push.senderName;
    payload += " <" + config.push.senderEmail + ">\r\n";
    payload += "To: <" + config.push.recipientEmail + ">\r\n";
    payload += "Subject: " + subject + "\r\n";
    payload += "MIME-Version: 1.0\r\n";
    payload += "Content-Type: text/plain; charset=UTF-8\r\n";
    payload += "\r\n";
    payload += normalizeBody(body);
    payload += "\r\n.\r\n";

    if (!sendSmtpCommand(*client, payload, 250, error)) return false;
    sendSmtpCommand(*client, "QUIT\r\n", 221, error);
    client->stop();
    return true;
}

bool PushNotificationManager::sendThresholdEmail(float levelPercent, float levelCm, float rawDistanceM, std::string& error) {
    const time_t nowEpoch = time(nullptr);

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        error = "Push-Konfiguration konnte nicht geladen werden";
        return false;
    }

    const std::string subject = renderTemplate(config.push.subjectTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch);
    const std::string body = renderTemplate(config.push.bodyTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch);
    return sendEmailInternal(subject, body, error, true);
}

void PushNotificationManager::process(float levelPercent, float levelCm, float rawDistanceM, bool validReading) {
    const unsigned long nowMs = millis();
    if (nowMs - lastProcessMs_ < kProcessIntervalMs) {
        return;
    }
    lastProcessMs_ = nowMs;

    Config config;
    if (!ConfigStore::getInstance().load(config) || !config.push.enabled) {
        wasBelowThreshold_ = false;
        return;
    }

    if (!validReading) {
        return;
    }

    const bool isBelowThreshold = levelPercent <= config.push.triggerPercent;
    if (!isBelowThreshold) {
        wasBelowThreshold_ = false;
        return;
    }

    const time_t nowEpoch = time(nullptr);

    if (lastFailureEpoch_ > 0 && (nowEpoch - lastFailureEpoch_) < kRetryAfterFailureSeconds) {
        return;
    }

    bool shouldSend = false;
    if (!wasBelowThreshold_) {
        shouldSend = true;
    } else {
        // Erinnerung: faellig, sobald das letzte Erinnerungsfenster begonnen hat und dafuer noch keine Mail ging.
        const time_t latestReminderOccurrence = getLatestReminderOccurrence(config.push, nowEpoch);
        shouldSend = latestReminderOccurrence > 0 && nowEpoch >= latestReminderOccurrence
            && latestReminderOccurrence > lastSentEpoch_;
    }

    if (!shouldSend) {
        return;
    }

    std::string error;
    if (sendThresholdEmail(levelPercent, levelCm, rawDistanceM, error)) {
        lastSentEpoch_ = nowEpoch;
        lastFailureEpoch_ = 0;
        wasBelowThreshold_ = true;
        DebugLogger::getInstance().log(LogLevel::INFO,
            "Push-Mail versendet bei " + formatFloat(levelPercent, 1) + "% Fuellstand");
    } else {
        lastFailureEpoch_ = nowEpoch;
        DebugLogger::getInstance().log(LogLevel::WARN,
            "Push-Mail Versand fehlgeschlagen: " + error);
    }
}

bool PushNotificationManager::sendTestEmail(std::string& error) {
    const float levelPercent = SensorManager::getInstance().getDistancePercent();
    const float levelCm = SensorManager::getInstance().getDistanceCm();
    const float rawDistanceM = SensorManager::getInstance().getRawDistance();

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        error = "Push-Konfiguration konnte nicht geladen werden";
        return false;
    }

    const time_t nowEpoch = time(nullptr);
    const std::string subject = renderTemplate(config.push.subjectTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch) + " [TEST]";
    const std::string body = renderTemplate(config.push.bodyTemplate, levelPercent, levelCm, rawDistanceM, nowEpoch);

    return sendEmailInternal(subject, body, error, false);
}

SmtpDiagResult PushNotificationManager::smtpDiagnostic() {
    SmtpDiagResult result;

    if (WiFi.status() != WL_CONNECTED) {
        result.steps.push_back({"WiFi", false, "WLAN nicht verbunden"});
        return result;
    }

    Config config;
    if (!ConfigStore::getInstance().load(config)) {
        result.steps.push_back({"Config", false, "Konfiguration konnte nicht geladen werden"});
        return result;
    }

    const std::string smtpServer = strutil::trim(config.push.smtpServer);
    if (smtpServer.empty() || config.push.authUser.empty()) {
        result.steps.push_back({"Config", false, "SMTP-Server oder Benutzername fehlen"});
        return result;
    }
    if (config.push.startTls && !config.push.useSsl) {
        result.steps.push_back({"Config", false, "STARTTLS wird nicht unterstuetzt – bitte SSL/TLS aktivieren"});
        return result;
    }
    if (config.push.useSsl && !config.push.smtpSkipCertVerify && time(nullptr) < kMinValidEpoch) {
        result.steps.push_back({"Zeit", false, "Uhrzeit noch nicht synchronisiert (NTP). TLS-Zertifikatspruefung dadurch nicht moeglich."});
        return result;
    }
    result.steps.push_back({"Config", true, "Konfiguration vorhanden"});

    IPAddress smtpIp;
    if (!WiFi.hostByName(smtpServer.c_str(), smtpIp)) {
        result.steps.push_back({"DNS", false, "DNS-Aufloesung fehlgeschlagen: " + smtpServer});
        return result;
    }
    result.steps.push_back({"DNS", true, smtpServer + " -> " + smtpIp.toString().c_str()});

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    Client* client = &prepareSmtpClient(config.push, plainClient, secureClient);

    if (!client->connect(smtpServer.c_str(), config.push.smtpPort)) {
        std::string detail = "Verbindung fehlgeschlagen (" + smtpServer + ":" + std::to_string(config.push.smtpPort) + ")";
        if (config.push.useSsl && !config.push.smtpSkipCertVerify) {
            detail += " – Tipp: Zertifikatspruefung deaktivieren, falls der Provider kein hinterlegtes Root-CA verwendet.";

            const std::string tlsDetail = readTlsLastError(secureClient);
            if (!tlsDetail.empty()) {
                detail += " - ";
                detail += tlsDetail;
            }
        }
        const std::string label = config.push.useSsl ? "TLS" : "Verbinden";
        result.steps.push_back({label, false, detail});
        return result;
    }

    {
        const std::string label = config.push.useSsl ? "TLS" : "Verbinden";
        const std::string detail = "Verbunden auf Port " + std::to_string(config.push.smtpPort)
            + (config.push.useSsl
                ? (config.push.smtpSkipCertVerify ? " (ohne Zertifikatspruefung)" : " (Zertifikat verifiziert)")
                : " (unverschluesselt)");
        result.steps.push_back({label, true, detail});
    }

    std::string error;
    if (!readSmtpResponse(*client, 220, error)) {
        result.steps.push_back({"SMTP-Greeting", false, error});
        client->stop();
        return result;
    }
    result.steps.push_back({"SMTP-Greeting", true, "Server bereit (220)"});

    if (!sendSmtpCommand(*client, "EHLO level-control\r\n", 250, error)) {
        result.steps.push_back({"EHLO", false, error});
        client->stop();
        return result;
    }
    result.steps.push_back({"EHLO", true, "Server antwortet korrekt (250)"});

    if (!sendSmtpCommand(*client, "AUTH LOGIN\r\n", 334, error)) {
        result.steps.push_back({"Auth", false, "AUTH LOGIN nicht akzeptiert: " + error});
        client->stop();
        return result;
    }
    if (!sendSmtpCommand(*client, toBase64(config.push.authUser) + "\r\n", 334, error)) {
        result.steps.push_back({"Auth", false, "Benutzername abgelehnt: " + error});
        client->stop();
        return result;
    }
    if (config.push.authPassword.empty()) {
        result.steps.push_back({"Auth", false, "Kein SMTP-Passwort hinterlegt"});
        client->stop();
        return result;
    }
    if (!sendSmtpCommand(*client, toBase64(config.push.authPassword) + "\r\n", 235, error)) {
        result.steps.push_back({"Auth", false, "Anmeldung fehlgeschlagen: " + error});
        client->stop();
        return result;
    }
    result.steps.push_back({"Auth", true, "Anmeldung erfolgreich (235)"});

    sendSmtpCommand(*client, "QUIT\r\n", 221, error);
    client->stop();

    result.success = true;
    return result;
}

