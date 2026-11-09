#ifndef NTP_TEST_H
#define NTP_TEST_H

#include <string>

/**
 * NtpTest – prueft einen NTP-Server mit einer einzelnen SNTP-Anfrage.
 * Laeuft in einem eigenen Task, damit der Webserver nicht blockiert.
 * Es gibt immer nur einen Test gleichzeitig; das Ergebnis wird abgefragt.
 */
namespace NtpTest {

struct Result {
    bool running = false;
    bool finished = false;
    bool ok = false;
    std::string host;
    std::string error;        ///< leer bei Erfolg
    long long serverEpoch = 0; ///< Unix-Zeit laut Server
    long long deviceEpoch = 0; ///< Unix-Zeit des Geraets (0 = noch nicht synchron)
    uint32_t roundTripMs = 0;
};

/** Startet den Test. Liefert false, wenn bereits einer laeuft oder der Host ungueltig ist. */
bool start(const std::string& host);

/** Kopie des aktuellen Zustands. */
Result get();

} // namespace NtpTest

#endif // NTP_TEST_H
