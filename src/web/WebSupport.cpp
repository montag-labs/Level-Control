#include "WebSupport.h"

#include <LittleFS.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <esp_system.h>

#include "DebugLogger.h"

#if MBEDTLS_VERSION_MAJOR >= 3
// Arduino-ESP32 3.x (mbedTLS 3) hat die *_ret-Varianten entfernt; die Basisfunktionen liefern jetzt int.
#define mbedtls_sha256_starts_ret mbedtls_sha256_starts
#define mbedtls_sha256_update_ret mbedtls_sha256_update
#define mbedtls_sha256_finish_ret mbedtls_sha256_finish
#endif

namespace web {

namespace {
bool gLittleFsMounted = false;
bool gRestartScheduled = false;

bool mountWithKnownLabels() {
    // Custom partitions often use label "littlefs"; Arduino default is "spiffs".
    if (LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        return true;
    }
    return LittleFS.begin(false, "/littlefs", 10, "spiffs");
}

std::string bytesToHex(const unsigned char *data, size_t length) {
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (size_t index = 0; index < length; ++index) {
        result.push_back(hexChars[(data[index] >> 4) & 0x0F]);
        result.push_back(hexChars[data[index] & 0x0F]);
    }
    return result;
}
}  // namespace

bool isMaskedPassword(const std::string& value) {
    return value == "***" || value == kPasswordMask;
}

std::string sha256Hex(const std::string& value) {
    unsigned char digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts_ret(&context, 0) != 0
        || mbedtls_sha256_update_ret(&context,
            reinterpret_cast<const unsigned char*>(value.data()), value.size()) != 0
        || mbedtls_sha256_finish_ret(&context, digest) != 0) {
        mbedtls_sha256_free(&context);
        return "";
    }
    mbedtls_sha256_free(&context);
    return bytesToHex(digest, sizeof(digest));
}

const char* collectBody(AsyncWebServerRequest *request, const uint8_t *data, size_t len, size_t index,
                        size_t total, size_t maxBytes, const char *tooLargeJson) {
    if (index == 0) {
        if (total == 0 || total > maxBytes) {
            request->send(413, "application/json", tooLargeJson);
            return nullptr;
        }
        free(request->_tempObject);
        request->_tempObject = calloc(total + 1, 1);
        if (request->_tempObject == nullptr) {
            request->send(503, "application/json", "{\"error\":\"out_of_memory\"}");
            return nullptr;
        }
    }

    char *buffer = static_cast<char*>(request->_tempObject);
    if (buffer == nullptr || index + len > total) {
        return nullptr;
    }
    memcpy(buffer + index, data, len);
    return (index + len == total) ? buffer : nullptr;
}

void answerIfPending(AsyncWebServerRequest *request) {
    // send() reiht die Antwort nur ein; isSent() wird erst beim tatsaechlichen Versand wahr. Deshalb muss auch
    // eine bereits eingereihte Antwort (getResponse()) als "beantwortet" gelten, sonst ueberschreibt dieser
    // Fallback jede regulaere Antwort der Body-Handler mit einem 400.
    if (!request->isSent() && request->getResponse() == nullptr) {
        request->send(400, "application/json", "{\"error\":\"invalid_or_empty_request_body\"}");
    }
}

bool constantTimeEquals(const char *left, size_t leftLen, const char *right, size_t rightLen) {
    unsigned char diff = leftLen == rightLen ? 0 : 1;
    const size_t common = leftLen < rightLen ? leftLen : rightLen;
    for (size_t index = 0; index < common; ++index) {
        diff |= static_cast<unsigned char>(left[index] ^ right[index]);
    }
    return diff == 0;
}

[[gnu::noinline]] std::string jsonString(JsonVariantConst node, const char *key, const std::string& fallback) {
    const char *value = node[key] | static_cast<const char*>(nullptr);
    return value != nullptr ? std::string(value) : fallback;
}

void applySecret(std::string& target, JsonVariantConst node, const char *key, bool allowEmpty) {
    const char *value = node[key] | static_cast<const char*>(nullptr);
    if (value == nullptr || isMaskedPassword(value) || (!allowEmpty && value[0] == '\0')) {
        return;
    }
    target = value;
}

bool ensureLittleFsMounted() {
    if (gLittleFsMounted) {
        return true;
    }
    gLittleFsMounted = mountWithKnownLabels();
    DebugLogger::getInstance().log(gLittleFsMounted ? LogLevel::INFO : LogLevel::ERROR,
        gLittleFsMounted ? "LittleFS mounted" : "LittleFS mount failed");
    return gLittleFsMounted;
}

void unmountLittleFs() {
    LittleFS.end();
    gLittleFsMounted = false;
}

void scheduleRestart(uint32_t delayMs) {
    if (gRestartScheduled) {
        return;
    }

    gRestartScheduled = true;
    struct RestartContext {
        uint32_t delayMs;
    };

    auto *context = new RestartContext{ delayMs };
    const BaseType_t created = xTaskCreate([](void *param) {
        auto *ctx = static_cast<RestartContext*>(param);
        vTaskDelay(pdMS_TO_TICKS(ctx->delayMs));
        delete ctx;
        ESP.restart();
    }, "level-control-restart", 4096, context, 1, nullptr);

    if (created != pdPASS) {
        delete context;
        gRestartScheduled = false;
        DebugLogger::getInstance().log(LogLevel::ERROR, "Restart-Task konnte nicht erstellt werden, starte sofort neu");
        ESP.restart();
    }
}

}  // namespace web
