# Changelog

Für jede Version gibt es einen Abschnitt `## <version>`. `scripts/prepare-release.js` übernimmt ihn in die
GitHub-Release-Notes und bricht ab, wenn der Abschnitt fehlt oder leer ist.

## 1.5.6

### Web-UI
- Neues Erscheinungsbild: Der Behälter wird als Zeichnung gezeigt, mit Messstrahl vom Sensor und Markierung bei 20 %. Daneben stehen die Füllstandsprozente groß, die geschätzte Restdauer und das Nachfülldatum.
- Die Restdauer rechnet die Web-UI aus dem Verbrauch der letzten 21 Tage seit der letzten Nachfüllung. Die Firmware ist unverändert.
- Der Verlauf hat eine feste Skala von 0 bis 100 %, die Nachfüllschwelle als gestrichelte Linie und zeigt beim Überfahren (oder Antippen) Datum und Wert.
- Die Seitenleiste entfällt: Die Reiter stehen oben, auf dem Handy in einer Leiste unten. Das leere „Mehr"-Menü ist entfernt.
- Tag/Nacht, Neustart und Sprache sind kompakte Knöpfe in der Kopfzeile; ein verfügbares Update erscheint dort als Hinweis mit Versionsnummer.
- Neue Farbpalette für Tag und Nacht; Admin-Module übernehmen sie über die gemeinsamen Farbvariablen.

## 1.5.5

### Zuverlässigkeit
- E-Mail (SMTP) und MQTT laufen in einem eigenen Task. Die Messung und der Webserver werden dadurch nicht mehr von langsamen Mailservern oder einem nicht erreichbaren MQTT-Broker aufgehalten.
- Test-E-Mail und SMTP-Diagnose laufen im Hintergrund; die Web-UI zeigt das Ergebnis, sobald es vorliegt.
- OTA-Rollback ist jetzt wirksam: Eine neu installierte Version gilt erst nach 20 Sekunden stabiler Laufzeit als gut. Stürzt sie vorher ab, startet das Gerät wieder die vorherige Version. Bisher wurde jede Version sofort als gut markiert.
- Die Speichern-Leiste im Admin-Bereich wurde nach einer Änderung nie aktiv (nur der Speichern-Button im Abschnitt funktionierte). Das ist behoben.

### Update
- Der Update-Code ist neu strukturiert (`UpdateService`, `FlashSession`): Repo-Update und lokaler Upload nutzen dieselbe Prüfung und räumen bei Fehlern automatisch auf.
- Es bleibt bei zwei signierten Dateien pro Release (App und Web-UI, Signatur steckt in der Datei). Die doppelten Release-Dateien und die Sonderbehandlung nach Dateinamen entfallen; das Release enthält genau die Dateien, die in den Release-Notes stehen (App, Web-UI, Dateien für Recovery per Kabel, Prüfsummen).
- Das Erkennen der Signatur am Dateiende ist am PC getestet (beliebige Stückelung, fehlender oder defekter Trailer).

### Web-UI
- Alle Texte sind jetzt in Deutsch und Englisch verfügbar, auch Update, Sicherung, Anmeldung und Dialoge. Meldungen, die das Gerät selbst liefert, bleiben deutsch.
- Die Konfigurationsmodule (Sensor, WLAN, MQTT, Push) sind eigenständige Komponenten in Svelte 5.
- Der Hinweis zur Push-Uhrzeit nennt jetzt ausdrücklich, dass sie in UTC gilt.

### Entwicklung
- Die reine Logik (Konfigurationsregeln, Versionsvergleich, Erinnerungszeiten, Mail-Kodierung) liegt in `lib/LevelCore` und wird mit 28 Tests am PC geprüft (`pio test -e native`).
- Browser-Smoke-Test der Web-UI gegen ein simuliertes Gerät (`scripts/ui-smoke.mjs`), GitHub-Actions-Workflow für Tests und Build.
- `WebServerDashboard` ist in Dashboard, `UpdateService`, `FlashSession`, `AdminSession` und `WebSupport` aufgeteilt (von rund 2600 auf rund 900 Zeilen im Dashboard).
- Das Release-Skript lädt alle Dateien aus dem Release-Ordner hoch; der Hardware-OTA-Test ist nur noch mit `-WithHardwareTest` aktiv.
- Dev-Stände tragen in `version.json` ein `label` (z. B. `1.5.A`) und lassen sich nicht als Release verpacken.

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
