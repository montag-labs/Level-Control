#include "NtpTest.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <time.h>
#include <mutex>

namespace NtpTest {
namespace {

constexpr uint32_t kNtpUnixOffset = 2208988800UL;  // Sekunden zwischen 1900 und 1970
constexpr uint32_t kReplyTimeoutMs = 3000;
constexpr uint32_t kMinPlausibleEpoch = 1000000000UL;  // nach Jahr 2001

Result g_result;
std::mutex g_lock;
std::string g_host;

bool validHost(const std::string& host) {
    if (host.empty() || host.size() > 100) return false;
    for (char c : host) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

void finish(bool ok, const char* error, long long serverEpoch, uint32_t rtt) {
    g_lock.lock();
    g_result.running = false;
    g_result.finished = true;
    g_result.ok = ok;
    g_result.error = error ? error : "";
    g_result.serverEpoch = serverEpoch;
    g_result.roundTripMs = rtt;
    const time_t now = time(nullptr);
    g_result.deviceEpoch = now > static_cast<time_t>(kMinPlausibleEpoch) ? static_cast<long long>(now) : 0;
    g_lock.unlock();
}

void worker(void*) {
    IPAddress ip;
    if (WiFi.status() != WL_CONNECTED) {
        finish(false, "no_wifi", 0, 0);
        vTaskDelete(nullptr);
        return;
    }
    if (!WiFi.hostByName(g_host.c_str(), ip)) {
        finish(false, "dns", 0, 0);
        vTaskDelete(nullptr);
        return;
    }

    WiFiUDP udp;
    if (!udp.begin(0)) {
        finish(false, "socket", 0, 0);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t packet[48] = {0};
    packet[0] = 0x1B;  // LI=0, Version 3, Modus 3 (Client)
    const uint32_t startMs = millis();
    if (!udp.beginPacket(ip, 123) || udp.write(packet, sizeof(packet)) != sizeof(packet) || !udp.endPacket()) {
        udp.stop();
        finish(false, "send", 0, 0);
        vTaskDelete(nullptr);
        return;
    }

    while (millis() - startMs < kReplyTimeoutMs) {
        if (udp.parsePacket() >= 48) {
            udp.read(packet, sizeof(packet));
            udp.stop();
            const uint32_t rtt = millis() - startMs;
            const uint32_t secs = (static_cast<uint32_t>(packet[40]) << 24) | (static_cast<uint32_t>(packet[41]) << 16) |
                                  (static_cast<uint32_t>(packet[42]) << 8) | static_cast<uint32_t>(packet[43]);
            const uint8_t stratum = packet[1];
            if (stratum == 0 || secs < kNtpUnixOffset) {
                finish(false, "invalid_reply", 0, rtt);
            } else {
                const long long epoch = static_cast<long long>(secs) - static_cast<long long>(kNtpUnixOffset);
                if (epoch < static_cast<long long>(kMinPlausibleEpoch)) {
                    finish(false, "invalid_reply", epoch, rtt);
                } else {
                    finish(true, nullptr, epoch, rtt);
                }
            }
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    udp.stop();
    finish(false, "timeout", 0, 0);
    vTaskDelete(nullptr);
}

} // namespace

bool start(const std::string& host) {
    if (!validHost(host)) return false;
    g_lock.lock();
    if (g_result.running) {
        g_lock.unlock();
        return false;
    }
    g_result = Result();
    g_result.running = true;
    g_result.host = host;
    g_host = host;
    g_lock.unlock();

    if (xTaskCreate(worker, "ntp_test", 4096, nullptr, 1, nullptr) != pdPASS) {
        finish(false, "task", 0, 0);
        return false;
    }
    return true;
}

Result get() {
    g_lock.lock();
    Result copy = g_result;
    g_lock.unlock();
    return copy;
}

} // namespace NtpTest
