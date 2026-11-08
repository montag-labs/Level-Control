// Native Tests (pio test -e native) fuer die reine Logik in lib/LevelCore – ohne ESP32-Hardware.
#include <unity.h>

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "Config.h"
#include "ConfigRules.h"
#include "MailEncoding.h"
#include "Reminder.h"
#include "SignatureTrailer.h"
#include "StringUtils.h"
#include "Version.h"

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------ StringUtils

void test_strutil_trim_lower_replace() {
    TEST_ASSERT_EQUAL_STRING("a b", strutil::trim("  \t a b \r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("", strutil::trim(" \r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("abc-ÄÖ", strutil::toLower("ABC-ÄÖ").c_str());  // nur ASCII wird umgesetzt

    std::string text = "a.b.c";
    strutil::replaceAll(text, ".", "--");
    TEST_ASSERT_EQUAL_STRING("a--b--c", text.c_str());
    strutil::replaceAll(text, "", "x");  // leeres Suchmuster darf nicht endlos laufen
    TEST_ASSERT_EQUAL_STRING("a--b--c", text.c_str());
}

// ------------------------------------------------------------------ Version

void test_version_compare() {
    TEST_ASSERT_TRUE(Version::compare("1.5.1", "1.5.0") > 0);
    TEST_ASSERT_TRUE(Version::compare("1.5.0", "1.5.1") < 0);
    TEST_ASSERT_EQUAL_INT(0, Version::compare("1.5.1", "1.5.1"));
    TEST_ASSERT_TRUE(Version::compare("1.10.0", "1.9.9") > 0);  // numerisch, nicht als Text
    TEST_ASSERT_TRUE(Version::compare("2.0.0", "1.99.99") > 0);
}

void test_version_dev_label_is_older_than_any_release() {
    TEST_ASSERT_TRUE(Version::compare("1.5.A", "1.5.0") < 0);
    TEST_ASSERT_TRUE(Version::compare("1.5.A", "1.5.5") < 0);
    TEST_ASSERT_TRUE(Version::compare("1.5.1", "1.5.A") > 0);
    TEST_ASSERT_TRUE(Version::compare("1.4.9", "1.5.A") < 0);
}

void test_version_invalid_falls_back_to_text() {
    int major = 0, minor = 0, patch = 0;
    TEST_ASSERT_FALSE(Version::parse("", major, minor, patch));
    TEST_ASSERT_FALSE(Version::parse("1.5", major, minor, patch));
    TEST_ASSERT_FALSE(Version::parse("1.5.x1", major, minor, patch));
    TEST_ASSERT_FALSE(Version::parse("1.5.1.2", major, minor, patch));
    TEST_ASSERT_FALSE(Version::parse("99999999999.1.1", major, minor, patch));  // Ueberlauf
    TEST_ASSERT_TRUE(Version::compare("abc", "abd") < 0);
}

void test_version_tag_normalization() {
    TEST_ASSERT_EQUAL_STRING("1.5.1", Version::normalizeTag("v1.5.1").c_str());
    TEST_ASSERT_EQUAL_STRING("1.5.1", Version::normalizeTag("V1.5.1").c_str());
    TEST_ASSERT_EQUAL_STRING("1.5.1", Version::normalizeTag("1.5.1").c_str());
}

// ------------------------------------------------------------------ ConfigRules

void test_sanitize_keeps_valid_defaults() {
    Config config;
    std::vector<std::string> reasons;
    ConfigRules::sanitize(config, &reasons);
    TEST_ASSERT_TRUE(reasons.empty());
    TEST_ASSERT_EQUAL_FLOAT(95.0f, config.behaelterhoehe);
    TEST_ASSERT_EQUAL_STRING("rcwl1670", config.sensorType.c_str());
}

void test_sanitize_sensor_limits() {
    Config config;
    config.behaelterhoehe = 5.0f;     // < 10
    config.offset = 250.0f;           // > 200
    config.sampleIntervalSeconds = 1;  // < 5
    config.sensorType = "unknown";
    std::vector<std::string> reasons;
    ConfigRules::sanitize(config, &reasons);

    TEST_ASSERT_EQUAL_FLOAT(95.0f, config.behaelterhoehe);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.offset);
    TEST_ASSERT_EQUAL_UINT32(5, config.sampleIntervalSeconds);
    TEST_ASSERT_EQUAL_STRING("rcwl1670", config.sensorType.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, reasons.size());

    config.sampleIntervalSeconds = ConfigLimits::kMaxSampleIntervalSeconds + 1;
    config.behaelterhoehe = NAN;
    ConfigRules::sanitize(config);
    TEST_ASSERT_EQUAL_UINT32(ConfigLimits::kMaxSampleIntervalSeconds, config.sampleIntervalSeconds);
    TEST_ASSERT_EQUAL_FLOAT(95.0f, config.behaelterhoehe);
}

void test_sanitize_boundaries_are_inclusive() {
    Config config;
    config.behaelterhoehe = ConfigLimits::kMinTankHeightCm;
    config.offset = -ConfigLimits::kMaxOffsetCm;
    config.sampleIntervalSeconds = ConfigLimits::kMinSampleIntervalSeconds;
    config.sensorType = "vl53l1x";
    std::vector<std::string> reasons;
    ConfigRules::sanitize(config, &reasons);
    TEST_ASSERT_TRUE(reasons.empty());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, config.behaelterhoehe);
    TEST_ASSERT_EQUAL_FLOAT(-200.0f, config.offset);
    TEST_ASSERT_EQUAL_STRING("vl53l1x", config.sensorType.c_str());
}

void test_sanitize_static_ip_gateway_is_optional() {
    Config config;
    config.staticIp = {" 192.168.1.50 ", "", "255.255.255.0", "192.168.1.1"};
    ConfigRules::sanitize(config);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", config.staticIp.ip.c_str());  // getrimmt
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", config.staticIp.subnet.c_str());
}

void test_sanitize_partial_static_ip_is_discarded() {
    Config config;
    config.staticIp = {"192.168.1.50", "192.168.1.1", "", ""};  // Subnetz und DNS fehlen
    std::vector<std::string> reasons;
    ConfigRules::sanitize(config, &reasons);
    TEST_ASSERT_TRUE(config.staticIp.ip.empty());
    TEST_ASSERT_TRUE(config.staticIp.gateway.empty());
    TEST_ASSERT_EQUAL_UINT32(1, reasons.size());
}

void test_sanitize_ports_and_tls() {
    Config config;
    config.mqtt.port = 70000;
    config.push.smtpPort = 0;
    config.push.useSsl = true;
    config.push.startTls = true;
    ConfigRules::sanitize(config);
    TEST_ASSERT_EQUAL_INT(1883, config.mqtt.port);
    TEST_ASSERT_EQUAL_INT(587, config.push.smtpPort);
    TEST_ASSERT_TRUE(config.push.useSsl);
    TEST_ASSERT_FALSE(config.push.startTls);  // SSL hat Vorrang
}

void test_sanitize_push_values() {
    Config config;
    config.push.sendHour = 30;
    config.push.sendMinute = -5;
    config.push.triggerPercent = 250.0f;
    config.push.reminderCycle = "yearly";
    config.push.reminderWeekday = 9;
    config.push.subjectTemplate.clear();
    config.push.senderName.clear();
    ConfigRules::sanitize(config);
    TEST_ASSERT_EQUAL_INT(23, config.push.sendHour);
    TEST_ASSERT_EQUAL_INT(0, config.push.sendMinute);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, config.push.triggerPercent);
    TEST_ASSERT_EQUAL_STRING("day", config.push.reminderCycle.c_str());
    TEST_ASSERT_EQUAL_INT(1, config.push.reminderWeekday);
    TEST_ASSERT_FALSE(config.push.subjectTemplate.empty());
    TEST_ASSERT_EQUAL_STRING("Level-Control", config.push.senderName.c_str());
}

void test_sanitize_wifi_names() {
    Config config;
    config.wifi.deviceName = "   ";
    config.wifi.ntpServerPrimary = "";
    config.wifi.ntpServerSecondary = " time.example.org ";
    ConfigRules::sanitize(config);
    TEST_ASSERT_EQUAL_STRING("Level-Control", config.wifi.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", config.wifi.ntpServerPrimary.c_str());
    TEST_ASSERT_EQUAL_STRING("time.example.org", config.wifi.ntpServerSecondary.c_str());
}

void test_rules_helpers() {
    TEST_ASSERT_EQUAL_STRING("month", ConfigRules::reminderCycleFromLegacyMinutes(43200).c_str());
    TEST_ASSERT_EQUAL_STRING("week", ConfigRules::reminderCycleFromLegacyMinutes(10080).c_str());
    TEST_ASSERT_EQUAL_STRING("day", ConfigRules::reminderCycleFromLegacyMinutes(1440).c_str());
    TEST_ASSERT_EQUAL_STRING("ssl", ConfigRules::normalizePushEncryptionMode("TLS").c_str());
    TEST_ASSERT_EQUAL_STRING("starttls", ConfigRules::normalizePushEncryptionMode("start-tls").c_str());
    TEST_ASSERT_EQUAL_STRING("none", ConfigRules::normalizePushEncryptionMode("").c_str());
    TEST_ASSERT_EQUAL_STRING("ssl", ConfigRules::derivePushEncryptionMode(true, true).c_str());
    TEST_ASSERT_EQUAL_STRING("starttls", ConfigRules::derivePushEncryptionMode(false, true).c_str());
}

// ------------------------------------------------------------------ Reminder (Zeitzone des Geraets: UTC)

static time_t utc(int year, int month, int day, int hour, int minute) {
    struct tm parts = {};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    return timegm(&parts);
}

static PushConfig pushAt8(const char* cycle, int weekday) {
    PushConfig push;
    push.sendHour = 8;
    push.sendMinute = 0;
    push.reminderCycle = cycle;
    push.reminderWeekday = weekday;
    return push;
}

void test_reminder_daily() {
    setenv("TZ", "UTC", 1);
    tzset();
    const PushConfig push = pushAt8("day", 1);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 9, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 10, 7, 59)));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 10, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 10, 8, 0)));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 10, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 10, 23, 0)));
}

void test_reminder_weekly() {
    setenv("TZ", "UTC", 1);
    tzset();
    const PushConfig push = pushAt8("week", 1);  // Montag; 9.3.2026 ist ein Montag
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 9, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 10, 9, 0)));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 2, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 9, 7, 0)));
}

void test_reminder_monthly_first_weekday() {
    setenv("TZ", "UTC", 1);
    tzset();
    const PushConfig push = pushAt8("month", 1);  // erster Montag im Monat: 2.3.2026, 2.2.2026
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 2, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 10, 9, 0)));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 2, 2, 8, 0), Reminder::latestOccurrence(push, utc(2026, 3, 1, 12, 0)));
    TEST_ASSERT_EQUAL_INT64(utc(2025, 12, 1, 8, 0), Reminder::latestOccurrence(push, utc(2026, 1, 1, 0, 0)));  // Jahreswechsel
}

void test_reminder_uses_system_timezone_not_browser() {
    // Dokumentiert das aktuelle Verhalten: sendHour gilt in der Systemzeitzone (auf dem Geraet UTC).
    setenv("TZ", "CET-1", 1);
    tzset();
    const PushConfig push = pushAt8("day", 1);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 10, 7, 0), Reminder::latestOccurrence(push, utc(2026, 3, 10, 12, 0)));
    setenv("TZ", "UTC", 1);
    tzset();
}

void test_reminder_invalid_clock() {
    TEST_ASSERT_EQUAL_INT64(0, Reminder::latestOccurrence(PushConfig(), 1000));
}

// ------------------------------------------------------------------ MailEncoding

static std::string decodeBase64(const std::string& input) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = -8;
    for (char ch : input) {
        const size_t index = alphabet.find(ch);
        if (index == std::string::npos) {
            continue;  // '=', CR, LF
        }
        value = (value << 6) + static_cast<int>(index);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

void test_base64_rfc4648_vectors() {
    TEST_ASSERT_EQUAL_STRING("", Mail::toBase64("").c_str());
    TEST_ASSERT_EQUAL_STRING("Zg==", Mail::toBase64("f").c_str());
    TEST_ASSERT_EQUAL_STRING("Zm8=", Mail::toBase64("fo").c_str());
    TEST_ASSERT_EQUAL_STRING("Zm9v", Mail::toBase64("foo").c_str());
    TEST_ASSERT_EQUAL_STRING("Zm9vYg==", Mail::toBase64("foob").c_str());
    TEST_ASSERT_EQUAL_STRING("Zm9vYmE=", Mail::toBase64("fooba").c_str());
    TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", Mail::toBase64("foobar").c_str());
}

void test_clean_address_blocks_injection() {
    TEST_ASSERT_EQUAL_STRING("a@b.de", Mail::cleanAddress(" a@b.de ").c_str());
    TEST_ASSERT_EQUAL_STRING("a@b.deRCPT TO:x@y.de",
                             Mail::cleanAddress("a@b.de\r\nRCPT TO:<x@y.de>").c_str());
}

void test_header_text_ascii_passthrough() {
    TEST_ASSERT_EQUAL_STRING("Salz fast leer", Mail::encodeHeaderText("Salz fast leer").c_str());
    TEST_ASSERT_EQUAL_STRING("AB", Mail::encodeHeaderText("A\r\nB").c_str());
}

void test_header_text_rfc2047_does_not_split_utf8() {
    // 40 Umlaute = 80 Bytes -> mehrere encoded-words
    std::string subject;
    for (int i = 0; i < 40; ++i) {
        subject += "ä";
    }
    const std::string encoded = Mail::encodeHeaderText(subject);
    TEST_ASSERT_TRUE(encoded.rfind("=?UTF-8?B?", 0) == 0);

    std::string decoded;
    size_t pos = 0;
    while ((pos = encoded.find("=?UTF-8?B?", pos)) != std::string::npos) {
        const size_t start = pos + 10;
        const size_t end = encoded.find("?=", start);
        TEST_ASSERT_TRUE(end != std::string::npos);
        TEST_ASSERT_TRUE(end + 2 - pos <= 75);  // RFC 2047: hoechstens 75 Zeichen je encoded-word
        const std::string part = decodeBase64(encoded.substr(start, end - start));
        TEST_ASSERT_TRUE(!part.empty());
        TEST_ASSERT_TRUE((static_cast<unsigned char>(part[0]) & 0xC0) != 0x80);  // beginnt nicht mitten im Zeichen
        decoded += part;
        pos = end + 2;
    }
    TEST_ASSERT_EQUAL_STRING(subject.c_str(), decoded.c_str());
}

void test_display_name() {
    TEST_ASSERT_EQUAL_STRING("\"Level-Control\"", Mail::encodeDisplayName("Level-Control").c_str());
    TEST_ASSERT_EQUAL_STRING("\"Evil Name\"", Mail::encodeDisplayName("Evil \"Name\\").c_str());
    TEST_ASSERT_TRUE(Mail::encodeDisplayName("Müller").rfind("=?UTF-8?B?", 0) == 0);
}

void test_body_base64_roundtrip_and_line_length() {
    std::string body = "Hallo Welt\n";
    for (int i = 0; i < 20; ++i) {
        body += "Zeile mit Umlauten äöü und Punkt am Ende.\n";
    }
    body += ".\nEnde";
    const std::string encoded = Mail::encodeBodyBase64(body);

    std::string expected = body;
    strutil::replaceAll(expected, "\n", "\r\n");
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), decodeBase64(encoded).c_str());

    size_t lineStart = 0;
    while (lineStart < encoded.size()) {
        const size_t lineEnd = encoded.find("\r\n", lineStart);
        TEST_ASSERT_TRUE(lineEnd != std::string::npos);
        TEST_ASSERT_TRUE(lineEnd - lineStart <= 76);
        TEST_ASSERT_TRUE(encoded[lineStart] != '.');  // kein Dot-Stuffing noetig
        lineStart = lineEnd + 2;
    }
}

// ------------------------------------------------------------------ SignatureTrailer

static std::vector<uint8_t> makeSignedFile(size_t payloadSize, size_t signatureLength, bool withMagic = true) {
    std::vector<uint8_t> file(payloadSize);
    for (size_t i = 0; i < payloadSize; ++i) file[i] = static_cast<uint8_t>(i * 31 + 7);
    std::vector<uint8_t> trailer(SignatureTrailer::kSize, 0);
    const char* magic = withMagic ? "LCSIGV1!" : "XXXXXXXX";
    for (size_t i = 0; i < 8; ++i) trailer[i] = static_cast<uint8_t>(magic[i]);
    trailer[8] = static_cast<uint8_t>(signatureLength);
    for (size_t i = 0; i < signatureLength; ++i) trailer[9 + i] = static_cast<uint8_t>(0xA0 + i);
    file.insert(file.end(), trailer.begin(), trailer.end());
    return file;
}

// Fuettert die Datei in Stuecken (wie die Firmware) und sammelt, was als Image geschrieben wuerde.
static std::vector<uint8_t> feed(SignatureTrailer& trailer, const std::vector<uint8_t>& file, const std::vector<size_t>& pattern) {
    std::vector<uint8_t> written;
    trailer.reset();
    size_t offset = 0;
    size_t index = 0;
    while (offset < file.size()) {
        const size_t size = std::min(pattern[index++ % pattern.size()], file.size() - offset);
        const size_t process = trailer.push(file.data() + offset, size);
        offset += size;
        written.insert(written.end(), trailer.pending(), trailer.pending() + process);
        trailer.consume(process);
    }
    return written;
}

void test_trailer_splits_payload_and_trailer_for_any_chunking() {
    const std::vector<std::vector<size_t>> patterns = {
        {1}, {7}, {80}, {81}, {82}, {137, 4096, 8191, 512, 16384}, {100000}};
    for (const auto& pattern : patterns) {
        const std::vector<uint8_t> file = makeSignedFile(20000, 70);
        SignatureTrailer trailer;
        const std::vector<uint8_t> written = feed(trailer, file, pattern);
        TEST_ASSERT_EQUAL_UINT32(20000, written.size());
        TEST_ASSERT_EQUAL_INT(0, std::memcmp(written.data(), file.data(), written.size()));
        TEST_ASSERT_TRUE(trailer.valid());
        TEST_ASSERT_EQUAL_UINT32(70, trailer.signatureLength());
        TEST_ASSERT_EQUAL_UINT8(0xA0, trailer.signature()[0]);
        TEST_ASSERT_EQUAL_UINT8(0xA0 + 69, trailer.signature()[69]);
    }
}

void test_trailer_rejects_missing_or_broken_trailer() {
    SignatureTrailer trailer;
    // zu kurz: nur Image-Bytes, kein Trailer
    std::vector<uint8_t> plain(5000, 0x55);
    feed(trailer, plain, {4096});
    TEST_ASSERT_FALSE(trailer.valid());
    // falsches Magic
    feed(trailer, makeSignedFile(3000, 70, false), {1000});
    TEST_ASSERT_FALSE(trailer.valid());
    // Signaturlaenge 0 und > 72
    feed(trailer, makeSignedFile(3000, 0), {1000});
    TEST_ASSERT_FALSE(trailer.valid());
    feed(trailer, makeSignedFile(3000, 72), {1000});
    TEST_ASSERT_TRUE(trailer.valid());  // 72 ist die Obergrenze und gueltig
    // weniger als 81 Byte insgesamt
    std::vector<uint8_t> tiny(40, 1);
    feed(trailer, tiny, {40});
    TEST_ASSERT_FALSE(trailer.valid());
}

void test_trailer_never_releases_the_last_81_bytes_early() {
    SignatureTrailer trailer;
    trailer.reset();
    std::vector<uint8_t> chunk(81, 9);
    TEST_ASSERT_EQUAL_UINT32(0, trailer.push(chunk.data(), chunk.size()));  // noch alles koennte Trailer sein
    TEST_ASSERT_EQUAL_UINT32(1, trailer.push(chunk.data(), 1));             // 82 Byte -> 1 Byte ist sicher Image
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_strutil_trim_lower_replace);
    RUN_TEST(test_version_compare);
    RUN_TEST(test_version_dev_label_is_older_than_any_release);
    RUN_TEST(test_version_invalid_falls_back_to_text);
    RUN_TEST(test_version_tag_normalization);
    RUN_TEST(test_sanitize_keeps_valid_defaults);
    RUN_TEST(test_sanitize_sensor_limits);
    RUN_TEST(test_sanitize_boundaries_are_inclusive);
    RUN_TEST(test_sanitize_static_ip_gateway_is_optional);
    RUN_TEST(test_sanitize_partial_static_ip_is_discarded);
    RUN_TEST(test_sanitize_ports_and_tls);
    RUN_TEST(test_sanitize_push_values);
    RUN_TEST(test_sanitize_wifi_names);
    RUN_TEST(test_rules_helpers);
    RUN_TEST(test_reminder_daily);
    RUN_TEST(test_reminder_weekly);
    RUN_TEST(test_reminder_monthly_first_weekday);
    RUN_TEST(test_reminder_uses_system_timezone_not_browser);
    RUN_TEST(test_reminder_invalid_clock);
    RUN_TEST(test_trailer_splits_payload_and_trailer_for_any_chunking);
    RUN_TEST(test_trailer_rejects_missing_or_broken_trailer);
    RUN_TEST(test_trailer_never_releases_the_last_81_bytes_early);
    RUN_TEST(test_base64_rfc4648_vectors);
    RUN_TEST(test_clean_address_blocks_injection);
    RUN_TEST(test_header_text_ascii_passthrough);
    RUN_TEST(test_header_text_rfc2047_does_not_split_utf8);
    RUN_TEST(test_display_name);
    RUN_TEST(test_body_base64_roundtrip_and_line_length);
    return UNITY_END();
}
