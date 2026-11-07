#ifndef WEB_SUPPORT_H
#define WEB_SUPPORT_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <string>

// Kleine, zustandslose Helfer fuer die Web-Schicht (Dashboard und Update-Service).
namespace web {

constexpr char kPasswordMask[] = "*****";

bool isMaskedPassword(const std::string& value);
std::string sha256Hex(const std::string& value);

// Sammelt einen Request-Body ueber mehrere Chunks in einem malloc-Puffer in request->_tempObject
// (die Library gibt ihn mit dem Request frei). Liefert den vollstaendigen, NUL-terminierten Body,
// sobald der letzte Chunk eingetroffen ist, sonst nullptr.
//
// WICHTIG: ESPAsyncWebServer ruft den Body-Handler fuer JEDEN Chunk auf - auch nachdem der erste Chunk
// abgelehnt wurde (Auth, Groesse). Der Aufrufer prueft Auth deshalb nur bei index == 0 und ruft diese
// Funktion danach auf; wurde Chunk 0 abgelehnt, existiert kein Puffer und alle Folge-Chunks werden
// verworfen. Es gibt bewusst keinen "Fallback", der einen Puffer erst bei Folge-Chunks anlegt.
const char* collectBody(AsyncWebServerRequest *request, const uint8_t *data, size_t len, size_t index,
                        size_t total, size_t maxBytes, const char *tooLargeJson);

// Request-Handler fuer Routen mit Body-/Upload-Handler: hat keiner geantwortet (z. B. leerer oder falsch
// kodierter Body, so dass der Body-Handler nie lief), bekommt der Client eine Antwort statt eines Timeouts.
void answerIfPending(AsyncWebServerRequest *request);

// Vergleich ohne vorzeitigen Abbruch (verhindert Timing-Angriffe auf Token/Hash).
bool constantTimeEquals(const char *left, size_t leftLen, const char *right, size_t rightLen);

// Liest einen String; fehlt der Schluessel (oder ist er kein String), bleibt `fallback`.
std::string jsonString(JsonVariantConst node, const char *key, const std::string& fallback);

// Uebernimmt ein Passwort nur, wenn es mitgesendet und nicht die Maske ist.
void applySecret(std::string& target, JsonVariantConst node, const char *key, bool allowEmpty);

// LittleFS (Web-UI-Dateien): Mount mit bekannten Partitionsnamen; vor einem Web-UI-Flash aushaengen.
bool ensureLittleFsMounted();
void unmountLittleFs();

// Neustart nach Verzoegerung in eigenem Task (damit die HTTP-Antwort noch ausgeliefert wird).
void scheduleRestart(uint32_t delayMs);

}  // namespace web

#endif  // WEB_SUPPORT_H
