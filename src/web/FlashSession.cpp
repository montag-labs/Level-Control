#include "FlashSession.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/version.h>

#include "ReleaseSigningPublicKey.h"
#include "WebSupport.h"

#if MBEDTLS_VERSION_MAJOR >= 3
// Arduino-ESP32 3.x (mbedTLS 3) hat die *_ret-Varianten entfernt; die Basisfunktionen liefern jetzt int.
#define mbedtls_sha256_starts_ret mbedtls_sha256_starts
#define mbedtls_sha256_update_ret mbedtls_sha256_update
#define mbedtls_sha256_finish_ret mbedtls_sha256_finish
#endif

namespace {
constexpr size_t kMinPlausibleAppBytes = 65536;
constexpr size_t kMinPlausibleWebUiBytes = 4096;
constexpr uint8_t kEspImageMagic = 0xE9;

bool verifySignature(const unsigned char* hash, size_t hashLen, const uint8_t* signature, size_t signatureLen,
                     std::string& error) {
    mbedtls_pk_context publicKey;
    mbedtls_pk_init(&publicKey);

    const int parseResult = mbedtls_pk_parse_public_key(
        &publicKey,
        reinterpret_cast<const unsigned char*>(kReleaseSigningPublicKeyPem),
        strlen(kReleaseSigningPublicKeyPem) + 1);
    if (parseResult != 0) {
        mbedtls_pk_free(&publicKey);
        error = "Öffentlicher Release-Schlüssel konnte nicht geladen werden";
        return false;
    }

    const int verifyResult = mbedtls_pk_verify(&publicKey, MBEDTLS_MD_SHA256, hash, hashLen, signature, signatureLen);
    mbedtls_pk_free(&publicKey);

    if (verifyResult != 0) {
        error = "Dateisignatur ungültig";
        return false;
    }
    return true;
}
}  // namespace

FlashSession::~FlashSession() {
    abort();
}

size_t FlashSession::partitionSize(Target target) {
    const esp_partition_t* partition = target == Target::App
        ? esp_ota_get_next_update_partition(nullptr)
        : esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    return partition ? partition->size : 0;
}

bool FlashSession::fail(std::string& error, const std::string& message) {
    error = message;
    abort();
    return false;
}

bool FlashSession::begin(Target target, size_t expectedPayloadSize, std::string& error) {
    abort();
    target_ = target;
    written_ = 0;
    trailer_.reset();
    limit_ = partitionSize(target);
    bootPartition_ = target == Target::App ? esp_ota_get_next_update_partition(nullptr) : nullptr;

    if (target == Target::App && bootPartition_ == nullptr) {
        error = "Keine OTA-Zielpartition gefunden";
        return false;
    }

    // Das Dateisystem muss fuer den Flash-Vorgang ausgehaengt sein.
    if (target == Target::WebUi) {
        web::unmountLittleFs();
    }

    const size_t beginSize = expectedPayloadSize > 0 ? expectedPayloadSize : (limit_ > 0 ? limit_ : UPDATE_SIZE_UNKNOWN);
    active_ = true;  // ab hier raeumt abort() auf
    if (!Update.begin(beginSize, target == Target::App ? U_FLASH : U_SPIFFS)) {
        return fail(error, Update.errorString());
    }

    mbedtls_sha256_init(&sha_);
    shaActive_ = true;
    if (mbedtls_sha256_starts_ret(&sha_, 0) != 0) {
        return fail(error, "SHA256-Kontext konnte nicht initialisiert werden");
    }
    return true;
}

bool FlashSession::write(const uint8_t* data, size_t len, std::string& error) {
    if (!active_) {
        error = "Keine aktive Update-Sitzung";
        return false;
    }
    if (len == 0) {
        return true;
    }

    // Alles bis auf die letzten 81 Byte ist sicher Image; der Rest koennte der Trailer sein.
    const size_t processLen = trailer_.push(data, len);
    if (processLen == 0) {
        return true;
    }
    const uint8_t* chunk = trailer_.pending();

    if (written_ == 0) {
        const bool looksLikeApp = chunk[0] == kEspImageMagic;
        if (target_ == Target::App && !looksLikeApp) {
            return fail(error, "Die Datei ist kein gültiges ESP32-App-Image");
        }
        if (target_ == Target::WebUi && looksLikeApp) {
            return fail(error, "Die Web-UI-Datei sieht wie eine App aus");
        }
    }
    if (limit_ > 0 && written_ + processLen > limit_) {
        return fail(error, target_ == Target::App
            ? "App-Datei überschreitet die verfügbare App-Partition"
            : "Web-UI-Datei überschreitet die LittleFS-Partition");
    }
    if (mbedtls_sha256_update_ret(&sha_, chunk, processLen) != 0) {
        return fail(error, "SHA256-Berechnung fehlgeschlagen");
    }
    // Update.write erwartet einen nicht-konstanten Zeiger, veraendert die Daten aber nicht.
    if (Update.write(const_cast<uint8_t*>(chunk), processLen) != processLen) {
        return fail(error, Update.errorString());
    }

    written_ += processLen;
    trailer_.consume(processLen);
    return true;
}

bool FlashSession::finish(std::string& error) {
    if (!active_) {
        error = "Keine aktive Update-Sitzung";
        return false;
    }

    const size_t minimum = target_ == Target::App ? kMinPlausibleAppBytes : kMinPlausibleWebUiBytes;
    if (written_ < minimum) {
        return fail(error, target_ == Target::App
            ? "App-Datei ist für ein ESP32-Image unplausibel klein"
            : "Web-UI-Datei ist unplausibel klein");
    }
    if (!trailer_.valid()) {
        return fail(error, "Signatur-Trailer fehlt oder ist ungültig");
    }
    const size_t signatureLen = trailer_.signatureLength();
    const uint8_t* signature = trailer_.signature();

    unsigned char hash[32];
    const int shaResult = mbedtls_sha256_finish_ret(&sha_, hash);
    mbedtls_sha256_free(&sha_);
    shaActive_ = false;
    if (shaResult != 0) {
        return fail(error, "SHA256-Abschluss fehlgeschlagen");
    }
    if (!verifySignature(hash, sizeof(hash), signature, signatureLen, error)) {
        abort();
        return false;
    }

    if (!Update.end(true)) {
        error = Update.errorString();
        active_ = false;  // Update.end() hat bereits aufgeraeumt
        if (target_ == Target::WebUi) {
            web::ensureLittleFsMounted();
        }
        return false;
    }
    active_ = false;

    if (target_ == Target::WebUi) {
        web::ensureLittleFsMounted();
        return true;
    }

    // Nach erfolgreichem Flashen die neue Partition als Boot-Partition setzen und verifizieren.
    const esp_err_t setError = esp_ota_set_boot_partition(bootPartition_);
    if (setError != ESP_OK) {
        error = std::string("Boot-Partition konnte nicht gesetzt werden: ") + esp_err_to_name(setError);
        return false;
    }
    const esp_partition_t* configured = esp_ota_get_boot_partition();
    if (configured == nullptr || configured->address != bootPartition_->address) {
        error = "Boot-Partition wurde nach OTA nicht korrekt übernommen";
        return false;
    }
    return true;
}

void FlashSession::abort() {
    if (shaActive_) {
        mbedtls_sha256_free(&sha_);
        shaActive_ = false;
    }
    if (!active_) {
        return;
    }
    active_ = false;
    Update.abort();
    if (target_ == Target::WebUi) {
        web::ensureLittleFsMounted();
    }
}
