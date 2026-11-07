#ifndef FLASH_SESSION_H
#define FLASH_SESSION_H

#include <esp_partition.h>
#include <mbedtls/sha256.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Eine signierte Flash-Aktualisierung (App oder Web-UI): schreibt Daten in die Zielpartition, bildet
// parallel den SHA-256 und prueft am Ende die getrennte Signatur (ECDSA/RSA ueber den SHA-256).
//
// Aufraeumen passiert automatisch: wird die Sitzung nicht mit finish() erfolgreich beendet, bricht der
// Destruktor (oder abort()) das Update ab und haengt LittleFS bei Web-UI-Updates wieder ein.
class FlashSession {
public:
    enum class Target { App, WebUi };

    FlashSession() = default;
    ~FlashSession();
    FlashSession(const FlashSession&) = delete;
    FlashSession& operator=(const FlashSession&) = delete;

    // expectedSize 0 = unbekannt (es gilt die Partitionsgroesse als Obergrenze).
    bool begin(Target target, size_t expectedSize, std::string& error);
    // Schreibt einen Datenblock. Bei einem Fehler ist die Sitzung bereits abgebrochen.
    bool write(const uint8_t* data, size_t len, std::string& error);
    // Schliesst die Sitzung ab: Pruefsumme, Signatur, Update.end(), Boot-Partition bzw. LittleFS-Remount.
    bool finish(const uint8_t* signature, size_t signatureLen, std::string& error);
    void abort();

    bool active() const { return active_; }
    size_t bytesWritten() const { return written_; }

    static size_t partitionSize(Target target);
    static constexpr size_t kMaxSignatureBytes = 72;

private:
    bool fail(std::string& error, const std::string& message);

    Target target_ = Target::App;
    bool active_ = false;
    bool shaActive_ = false;
    size_t limit_ = 0;
    size_t written_ = 0;
    mbedtls_sha256_context sha_;
    const esp_partition_t* bootPartition_ = nullptr;
};

#endif  // FLASH_SESSION_H
