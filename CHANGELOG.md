# Changelog

Für jede Version gibt es einen Abschnitt `## <version>`. `scripts/prepare-release.js` übernimmt ihn in die
GitHub-Release-Notes und bricht ab, wenn der Abschnitt fehlt oder leer ist.

## 1.5.1

### Sicherheit
- Die Konfigurations-Abfragen für WLAN, MQTT und Push (`GET /api/wifi`, `/api/mqtt`, `/api/push`) sind jetzt nur noch mit Admin-Anmeldung abrufbar. Vorher waren SSID, MQTT-Benutzer, SMTP-Benutzer und E-Mail-Adressen für jeden im Netzwerk lesbar.
- Login-Sperre: nach 5 falschen Passwörtern in Folge ist die Anmeldung 60 Sekunden gesperrt.
- E-Mail-Versand gehärtet: Zeilenumbrüche in Absender-, Empfänger- und Betreff-Feldern können keine SMTP-Befehle oder Header mehr einschleusen.

### Verbesserungen
- E-Mails werden standardkonform kodiert: Umlaute im Betreff und Absendernamen (RFC 2047), Text als Base64 (UTF-8). Das verhindert Darstellungsfehler und abgelehnte Mails bei strengen Mailservern.
- Ein Repo-Update blockiert den Webserver nicht mehr: das Release-Manifest wird im Update-Task geladen, nicht mehr im Web-Handler.
- Das Release-Manifest wird thread-sicher gelesen und geschrieben (vorher konnten Hintergrund-Task und Web-Anfragen gleichzeitig darauf zugreifen).
- Anfragen mit leerem oder ungültigem Body bekommen eine Fehlerantwort statt eines Timeouts.
- Anmeldefehler werden in der Web-UI verständlich angezeigt (falsches Passwort, Sperre mit Restzeit).

### Entwicklung
- Release-Notes werden aus dieser Datei erzeugt statt aus Commit-Titeln.

## 1.5.0

### Sicherheit
- Behoben: Mehrteilige POST-Anfragen konnten die Anmeldeprüfung umgehen (`/api/config`, `/api/wifi`, `/api/mqtt`, `/api/import`, `/api/update/repo`, Signatur-Upload). Wer im selben Netzwerk war, konnte so Einstellungen ohne Passwort ändern. Folge-Teile eines abgelehnten Requests werden jetzt verworfen.

### Fehlerbehebungen
- Der Setup-Access-Point („Level-Control-Setup“) blieb nach fehlgeschlagener WLAN-Verbindung nur etwa 2 Sekunden aktiv. Er bleibt jetzt erhalten; sobald das gespeicherte WLAN wieder erreichbar ist, startet das Gerät automatisch neu.
- Die eingestellten NTP-Server wurden beim Start durch feste Werte überschrieben.
- Eine statische IP ohne Gateway ging beim Neustart verloren.
- Das Update-Manifest wurde über MQTT in der Hauptschleife geladen und hielt dabei die Messung an.
- Höhe, Offset und Abtastrate werden überall gleich geprüft (Web-UI, MQTT, Backup-Import): Höhe 10–500 cm, Offset ±200 cm, Abtastrate 5 s bis 30 Tage. Bei ungültigen Werten zeigt die Web-UI eine verständliche Meldung.
- Nach einem Backup-Import mit anderem Sensortyp wird der Sensor neu initialisiert.
- Importierte Verlaufsdaten werden nach Zeit sortiert.
- Werksreset und Neustart blockieren den Webserver nicht mehr.

### Änderungen
- Nicht gesendete Felder in Einstellungs-Anfragen behalten ihren bisherigen Wert statt auf Standardwerte zurückzufallen.
- Plattform und Bibliotheken aktualisiert: espressif32 7.1.3, ArduinoJson 7.4.3, ESPAsyncWebServer 3.12.1, Svelte 5.57.3, Vite 8.3.6.
- Aufgeräumt: der Firmware-Speicherbedarf sank von 89,8 % auf 78,0 % des OTA-Slots. Entfernt wurden u. a. der interne EventBus, die Log-Übertragung per WebSocket und `GET /api/nvs`.
