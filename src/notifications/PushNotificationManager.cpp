#include "PushNotificationManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cctype>
#include <ctime>

#include "ConfigStore.h"
#include "DebugLogger.h"
#include "MqttManager.h"
#include "SensorManager.h"
#include "SmtpTrustedRoots.h"
#include "UpdateService.h"
#include "MailEncoding.h"
#include "Reminder.h"
#include "StringUtils.h"

namespace {
constexpr unsigned long kProcessIntervalMs = 10000UL;
constexpr time_t kRetryAfterFailureSeconds = 300;
constexpr uint32_t kSmtpTimeoutMs = 15000;

std::string formatFloat(float value, int decimals) {
    char buffer[32] = {0};
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string formatTimestamp(time_t epoch) {
    if (epoch < Reminder::kMinValidEpoch) {
        return "n/a";
    }

    struct tm timeInfo;
    localtime_r(&epoch, &timeInfo);
    char buffer[32] = {0};
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeInfo);
    return buffer;
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

}  // namespace

PushNotificationManager& PushNotificationManager::getInstance() {
    static PushNotificationManager instance;
    return instance;
}

void PushNotificationManager::service() {
    runPendingJob();
    if (UpdateService::getInstance().isInProgress()) {
        return;  // waehrend eines Updates keinen TLS-Verbindungsaufbau starten
    }
    SensorManager& sensor = SensorManager::getInstance();
    process(sensor.getDistancePercent(), sensor.getDistanceCm(), sensor.getRawDistance(), sensor.hasValidReading());
}

bool PushNotificationManager::requestJob(const char* kind) {
    std::lock_guard<std::mutex> lock(jobMutex_);
    if (job_.state == PushJobStatus::State::Pending || job_.state == PushJobStatus::State::Running) {
        return false;
    }
    job_ = PushJobStatus();
    job_.state = PushJobStatus::State::Pending;
    job_.kind = kind;
    return true;
}

bool PushNotificationManager::requestTestEmail() {
    return requestJob("test");
}

bool PushNotificationManager::requestDiagnostic() {
    return requestJob("diagnostic");
}

PushJobStatus PushNotificationManager::jobStatus() const {
    std::lock_guard<std::mutex> lock(jobMutex_);
    return job_;
}

void PushNotificationManager::runPendingJob() {
    std::string kind;
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        if (job_.state != PushJobStatus::State::Pending) {
            return;
        }
        job_.state = PushJobStatus::State::Running;
        kind = job_.kind;
    }

    PushJobStatus result;
    result.kind = kind;
    if (kind == "diagnostic") {
        result.diagnostic = smtpDiagnostic();
        result.success = result.diagnostic.success;
    } else {
        result.success = sendTestEmail(result.error);
        if (!result.success && result.error.empty()) {
            result.error = "Unbekannter SMTP-Fehler";
        }
    }
    result.state = PushJobStatus::State::Done;

    std::lock_guard<std::mutex> lock(jobMutex_);
    job_ = result;
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
    strutil::replaceAll(text, "{level_percent}", formatFloat(levelPercent, 1));
    strutil::replaceAll(text, "{level_cm}", formatFloat(levelCm, 1));
    strutil::replaceAll(text, "{raw_distance_m}", formatFloat(rawDistanceM, 3));
    strutil::replaceAll(text, "{timestamp}", formatTimestamp(nowEpoch));
    strutil::replaceAll(text, "{device_id}", MqttManager::getInstance().getDeviceId());
    strutil::replaceAll(text, "{ip}", WiFi.localIP().toString().c_str());
    strutil::replaceAll(text, "{ssid}", WiFi.SSID().c_str());
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

    if (config.push.useSsl && !config.push.smtpSkipCertVerify && time(nullptr) < Reminder::kMinValidEpoch) {
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
    if (!sendSmtpCommand(*client, Mail::toBase64(config.push.authUser) + "\r\n", 334, error)) return false;
    if (!sendSmtpCommand(*client, Mail::toBase64(config.push.authPassword) + "\r\n", 235, error)) return false;
    const std::string senderEmail = Mail::cleanAddress(config.push.senderEmail);
    const std::string recipientEmail = Mail::cleanAddress(config.push.recipientEmail);
    if (!sendSmtpCommand(*client, "MAIL FROM:<" + senderEmail + ">\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "RCPT TO:<" + recipientEmail + ">\r\n", 250, error)) return false;
    if (!sendSmtpCommand(*client, "DATA\r\n", 354, error)) return false;

    std::string payload;
    payload.reserve(body.size() * 4 / 3 + 512);
    payload += "From: " + Mail::encodeDisplayName(config.push.senderName.empty() ? "Level-Control" : config.push.senderName);
    payload += " <" + senderEmail + ">\r\n";
    payload += "To: <" + recipientEmail + ">\r\n";
    payload += "Subject: " + Mail::encodeHeaderText(subject) + "\r\n";
    payload += "MIME-Version: 1.0\r\n";
    payload += "Content-Type: text/plain; charset=UTF-8\r\n";
    payload += "Content-Transfer-Encoding: base64\r\n";
    payload += "\r\n";
    payload += Mail::encodeBodyBase64(body);
    payload += ".\r\n";

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
        const time_t latestReminderOccurrence = Reminder::latestOccurrence(config.push, nowEpoch);
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
    if (config.push.useSsl && !config.push.smtpSkipCertVerify && time(nullptr) < Reminder::kMinValidEpoch) {
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
    if (!sendSmtpCommand(*client, Mail::toBase64(config.push.authUser) + "\r\n", 334, error)) {
        result.steps.push_back({"Auth", false, "Benutzername abgelehnt: " + error});
        client->stop();
        return result;
    }
    if (config.push.authPassword.empty()) {
        result.steps.push_back({"Auth", false, "Kein SMTP-Passwort hinterlegt"});
        client->stop();
        return result;
    }
    if (!sendSmtpCommand(*client, Mail::toBase64(config.push.authPassword) + "\r\n", 235, error)) {
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

