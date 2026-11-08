# Level-Control – ESP32-C3 Ultraschall-Füllstandssensor

Dieses Projekt implementiert einen WLAN-fähigen Füllstandssensor für Salzbehälter auf Basis eines ESP32-C3 mit HC-SR04-Ultraschallsensor. Die Messwerte werden über eine Svelte-Web-UI dargestellt, per WebSocket in Echtzeit übertragen und über MQTT (inkl. Home Assistant Auto-Discovery) bereitgestellt.

## About

Level-Control ist ein ESP32-C3-Projekt zur kontinuierlichen Füllstandsmessung von Salzbehältern mit Ultraschall. Es kombiniert Firmware, Web-Dashboard (Svelte), MQTT-Telemetrie und Home-Assistant-Discovery in einem System.

**Highlights:**
- Live-Dashboard mit Tag/Nacht-Theme und Modulnavigation
- MQTT mit Sensor-, System-, Update- und Konfigurations-Topics
- Home Assistant Auto-Discovery inklusive OTA-Update-Entität
- Persistente Konfiguration in NVS für WiFi, MQTT, Sensor, Push und Update-Workflow

**Tech-Stack:** ESP32-C3, Arduino, PlatformIO, Svelte, PubSubClient, ESPAsyncWebServer, ArduinoJson

---

## Hardware

| Komponente | Details |
|---|---|
| Mikrocontroller | ESP32-C3 DevKitM-1, 160 MHz, 320 KB RAM, 4 MB Flash |
| Sensor (Standard) | RCWL-1670 Ultraschall-Abstandssensor |
| Trigger-Pin | GPIO 4 |
| Echo-Pin | GPIO 5 |
| Sensor (alternativ) | VL53L1X Time-of-Flight Laser-Distanzsensor |
| VL53L1X SDA-Pin | GPIO 6 |
| VL53L1X SCL-Pin | GPIO 7 |
| VL53L1X XSHUT-Pin | GPIO 2 |

---

## Features

### Sensor-Messung
- Eigene `ping()`-Implementierung über `pulseIn()` (keine externe Bibliothek)
- Median aus 10 aufeinanderfolgenden Messungen zur Rauschunterdrückung
- Gültigkeitsbereich: 0–25 000 µs Ping-Zeit
- Alle Messwerte werden in jedem `loop()`-Durchlauf aktualisiert
- 5 Testmessungen beim Start mit Serial-Ausgabe

### Messwerte
| Wert | Berechnung |
|---|---|
| Rohdistanz (m) | Direkt aus `ping_us / 2 / 29.1 / 100` |
| Level-Control (cm) | `(behaelterhoehe + offset) - gemessene_cm` |
| Füllstand (%) | `level_control_cm / behaelterhoehe * 100` |
| Ping-Zeit (µs) | Roher `pulseIn`-Wert |
| Messung gültig | `true` wenn Ping-Zeit im Gültigkeitsbereich |

---

## Persistenz

Die persistente Ablage ist auf drei Flash-Bereiche aufgeteilt:

| Bereich | Partition | Inhalt |
|---|---|---|
| Konfiguration | `nvs` | WiFi-, MQTT-, Sensor- und Push-Einstellungen |
| Messverlauf | `histnvs` | Historienpunkte im Namespace `history` |
| Web-UI | `littlefs` | Gebaute Svelte-Dateien aus `data/` |

Alle Einstellungen werden persistent im NVS-Flash gespeichert (Namespace `config`, JSON-Format).

| Feld | Typ | Beschreibung |
|---|---|---|
| `wifi.ssid` | String | WLAN-Netzwerkname |
| `wifi.password` | String | WLAN-Passwort |
| `wifi.deviceName` | String | Gerätename (wird als DHCP-Hostname gesetzt) |
| `staticIp.ip` | String | Statische IP (leer = DHCP) |
| `staticIp.gateway` | String | Gateway |
| `staticIp.subnet` | String | Subnetzmaske |
| `staticIp.dns` | String | DNS-Server |
| `mqtt.server` | String | MQTT-Broker-Adresse |
| `mqtt.port` | uint16 | MQTT-Port (Standard: 1883) |
| `mqtt.user` | String | MQTT-Benutzername |
| `mqtt.password` | String | MQTT-Passwort |
| `mqtt.discovery` | bool | Home Assistant Auto-Discovery aktivieren |
| `push.*` | Objekt | SMTP-, Trigger- und Vorlagen-Konfiguration für E-Mail-Benachrichtigungen |
| `behaelterhoehe` | float | Innenhöhe des Behälters in cm |
| `offset` | float | Korrekturwert in cm (für Sensorposition) |
| `sampleIntervalSeconds` | uint32 | Abtastrate des Sensors in Sekunden |

Der Messverlauf wird getrennt davon in der dedizierten Partition `histnvs` gespeichert. Dadurch bleiben Konfiguration und Historie auch bei `upload`, `uploadfs` und OTA-Updates erhalten, ohne die kleine Default-NVS für Konfigurationsdaten zu überladen.

---

## Boot-Verhalten

```
Erster Start / keine SSID konfiguriert  ODER  WLAN-Verbindung schlägt fehl
  → SETUP_MODE: Access Point "Level-Control-Setup" (kein Passwort)
  → Webseite auf 192.168.4.1: WiFi-Konfigurationsformular

Normaler Betrieb
  → NORMAL_MODE: Verbindet mit WLAN, startet Dashboard + MQTT
```

---

## Web-UI (Svelte)

Die UI ist als Svelte-Frontend mit Vite gebaut, wird aus LittleFS ausgeliefert und kommuniziert über WebSocket (Echtzeit) sowie REST-API.

### Tab: Dashboard

**Anzeigefelder:**

| Karte | Inhalt |
|---|---|
| Aktuelle Distanz | Rohdistanz in m (Ultraschall-Messwert) |
| Level-Control | Füllstand in cm und Prozent |
| WiFi-Signal | RSSI in dBm |
| Uptime | Betriebszeit in Stunden und Minuten |
| Netzwerk | IP-Adresse, SSID, BSSID |

**Schnelleinstellungen:**
- Behälterhöhe (cm) und Offset (cm) direkt anpassbar
- Speichern via `POST /api/config`

**Weitere Aktionen:**
- `Neustart`-Button → `POST /api/restart`
- `Ctrl+D` → öffnet/schließt das Debug-Overlay

### Module

**Sensor:**
- Behälterhöhe (cm) einstellen
- Offset (cm) einstellen
- Abtastrate des Ultraschallsensors konfigurieren
- Speichern via `POST /api/config`

**WiFi:**
- SSID und Passwort
- Gerätename als DHCP-Hostname (Erreichbarkeit per Name hängt vom DNS des Routers ab; es läuft kein mDNS)
- Optionale statische IP-Konfiguration (IP, Gateway, Subnetz, DNS)
- Speichern via `POST /api/wifi` (Neustart erforderlich)

**MQTT & HA:**
- Server-Adresse, Port, Benutzer, Passwort
- Home Assistant Discovery aktivieren/deaktivieren
- Speichern via `POST /api/mqtt` (löst sofortigen Reconnect aus)
- Manueller Reconnect und Topic-Übersicht für Home Assistant

**Push Nachricht:**
- SMTP-Provider, Verschlüsselung und Ports konfigurieren
- Test-Mail und SMTP-Diagnose auslösen
- Erinnerungszyklus, Uhrzeit und Vorlage definieren

**Update:**
- Repo-OTA aus dem neuesten GitHub Release starten
- Lokale BIN-Dateien für App oder Web-UI hochladen
- Update-Status, Größenlimits und Fortschritt anzeigen

Die in der UI angezeigte installierte Version stammt bewusst aus der laufenden Firmware (`LEVEL_CONTROL_VERSION`). Bei App-OTA oder App-Upload wechselt diese Anzeige deshalb erst nach dem Neustart in die neue OTA-Partition. Ein reines Web-UI-Update ändert die Firmware-Version nicht.

Wenn ein manueller Upload mitten im Transfer abbricht, setzt die Firmware einen hängenden Upload-Status nach 15 Sekunden Inaktivität automatisch auf `failed` zurück. Zusätzlich steht über die UI/API ein Reset des Upload-Status zur Verfügung, damit ein Retry ohne Geräteneustart möglich ist.

---

## REST-API

Alle Endpunkte laufen auf Port 80.

| Methode | Endpoint | Beschreibung |
|---|---|---|
| `GET` | `/api/config` | Sensor-Konfiguration lesen (`behaelterhoehe`, `offset`) |
| `POST` | `/api/config` | Sensor-Konfiguration speichern |
| `GET` | `/api/wifi` | WiFi-Konfiguration lesen (nur Admin; Passwort wird nicht zurückgegeben) |
| `POST` | `/api/wifi` | WiFi-Konfiguration speichern |
| `GET` | `/api/mqtt` | MQTT-Konfiguration lesen (nur Admin; Passwort maskiert) |
| `POST` | `/api/mqtt` | MQTT-Konfiguration speichern + sofortiger Reconnect |
| `GET` | `/api/mqtt/status` | MQTT-Verbindungsstatus und Geräte-ID lesen |
| `POST` | `/api/mqtt/reconnect` | MQTT-Verbindung manuell neu aufbauen |
| `GET` | `/api/push` | Push-/SMTP-Konfiguration lesen (nur Admin) |
| `POST` | `/api/push` | Push-/SMTP-Konfiguration speichern |
| `POST` | `/api/push/test` | Test-E-Mail als Auftrag starten (202) |
| `POST` | `/api/push/smtp-check` | SMTP-Diagnose als Auftrag starten (202) |
| `GET` | `/api/push/job` | Stand/Ergebnis des laufenden E-Mail-Auftrags (nur Admin) |
| `GET` | `/api/update/status` | OTA-Status und aktive Versionsdaten lesen |
| `GET` | `/api/update/manifest` | OTA-Manifest aus GitHub Releases lesen |
| `POST` | `/api/update/repo` | Repo-OTA aus GitHub Release starten |
| `POST` | `/api/update/upload/app` | App-Datei (`...-app.bin`, signiert) lokal hochladen |
| `POST` | `/api/update/upload/webui` | Web-UI-Datei (`...-web-ui.bin`, signiert) lokal hochladen |
| `GET` | `/api/history` | Gespeicherte Historienpunkte lesen |
| `DELETE` | `/api/history` | Gespeicherten Messverlauf löschen |
| `GET` | `/api/export` | Konfiguration und Historie als Backup exportieren |
| `POST` | `/api/import` | Konfiguration und Historie aus Backup importieren |
| `POST` | `/api/factory-reset` | Konfiguration, Historie und WiFi-Credentials löschen; Neustart auslösen |
| `POST` | `/api/restart` | ESP32 neu starten |
| `GET` | `/*` | Statische Dateien aus LittleFS (Svelte-UI) |

---

## WebSocket (`/ws`)

Broadcast-Intervalle aus dem `loop()`:

| Typ | Intervall | Felder |
|---|---|---|
| `sensor` | Direkt nach jeder Messung | `ping_us`, `valid`, `rohdistanz`, `salzstandCm`, `salzstandPercent` |
| `wifi` | 5 s | `signal` (dBm), `ip`, `ssid`, `bssid` |
| `uptime` | 5 s | `uptime` (Sekunden) |
| `log` | bei jedem Log-Eintrag | `level`, `timestamp`, `message` |

Sensor-Daten werden unmittelbar nach jeder neuen Messung übertragen – unabhängig vom 5-s-Broadcast-Takt für WiFi/Uptime/MQTT.  
Die UI zeigt Verbindungsprobleme und veraltete Werte (kein Update länger als `2 × Abtastintervall`) mit einem Warnhinweis an.

---

## MQTT

Broker-Verbindung über PubSubClient 2.8.  
Reconnect-Versuch alle 5 Sekunden bei Verbindungsverlust.  
Keepalive: 30 Sekunden, Buffer: 1024 Bytes.  
Geräte-ID: `level_control_` + MAC-Adresse ohne Doppelpunkte (z. B. `level_control_A4CF121E3B00`).

### Topics

| Topic | Richtung | Retain | Beschreibung |
|---|---|---|---|
| `level-control/status` | pub | ✓ | `online` / `offline` (LWT) |
| `level-control/sensor/state` | pub | ✓ | Alle Messwerte als JSON (alle 30 s) |
| `level-control/config/behaelterhoehe/state` | pub | ✓ | Aktuelle Behälterhöhe in cm |
| `level-control/config/behaelterhoehe/set` | **sub** | – | Neue Behälterhöhe setzen (1–1000 cm) |
| `level-control/config/offset/state` | pub | ✓ | Aktueller Offset-Wert |
| `level-control/config/offset/set` | **sub** | – | Neuen Offset setzen (–500 bis +500 cm) |
| `level-control/config/sampleinterval/state` | pub | ✓ | Aktuelle Abtastrate in Sekunden |
| `level-control/config/sampleinterval/set` | **sub** | – | Neue Abtastrate setzen (mind. 5 s) |
| `level-control/system/state` | pub | ✓ | WiFi, Uptime & ESP32-Systemdaten (alle 30 s) |
| `level-control/update/state` | pub | ✓ | OTA-Status und Versionsinformationen |
| `level-control/update/install` | **sub** | – | OTA-Installation auslösen |

### Sensor-State-Payload (`level-control/sensor/state`)

```json
{
  "fill_level": 73.5,
  "distance_cm": 69.8,
  "raw_distance_m": 0.2520,
  "ping_us": 1468,
  "valid": true,
  "status": "ok"
}
```

**`status`-Werte:**

| Wert | Bedeutung |
|---|---|
| `ok` | Messung gültig |
| `timeout` | Kein Echo empfangen (Ping-Zeit = 0) |
| `out_of_range` | Echo außerhalb des Gültigkeitsbereichs |

### System-State-Payload (`level-control/system/state`)

```json
{
  "ip": "192.168.1.100",
  "ssid": "MyNetwork",
  "rssi": -65,
  "uptime_s": 3600,
  "free_heap": 245000,
  "min_free_heap": 220000,
  "cpu_freq_mhz": 160,
  "flash_size_kb": 4096,
  "sketch_size_kb": 980,
  "chip_rev": 3
}
```

### Eingehende Befehle

Über `/set`-Topics können Konfigurationswerte live geändert werden:
- Wert wird validiert (Bereich geprüft)
- Im NVS gespeichert
- Im laufenden `SensorManager` sofort übernommen
- Neuer Wert sofort auf dem zugehörigen `/state`-Topic veröffentlicht

---

## Home Assistant Auto-Discovery

Wenn `mqtt.discovery = true`, werden beim Connect 14 Entitäten veröffentlicht:

| HA-Entität | Domain | Object-ID | Einheit | Kategorie |
|---|---|---|---|---|
| Füllstand | `sensor` | `fill_level` | % | – |
| Level-Control | `sensor` | `distance_cm` | cm | – |
| Rohdistanz | `sensor` | `raw_distance` | m | diagnostic |
| Ultraschall Pingzeit | `sensor` | `ping_us` | µs | diagnostic |
| Behälterhöhe | `number` | `behaelterhoehe` | cm | config |
| Sensor Offset | `number` | `offset` | cm | config |
| Abtastrate Ultraschall | `number` | `sample_interval` | s | config |
| WiFi Signal | `sensor` | `rssi` | dBm | diagnostic |
| IP-Adresse | `sensor` | `ip_address` | – | diagnostic |
| SSID | `sensor` | `ssid` | – | diagnostic |
| Betriebszeit | `sensor` | `uptime` | s | diagnostic |
| Freier Heap | `sensor` | `free_heap` | B | diagnostic |
| CPU-Frequenz | `sensor` | `cpu_freq` | MHz | diagnostic |
| OTA Update | `update` | `ota` | – | – |

Jede Entität enthält:
- `unique_id` (Geräte-ID + Feldname) für stabile HA-Identifikation
- `availability_topic` → `level-control/status`
- Device-Block: Name `Level-Control`, Manufacturer `DIY`, Model `ESP32-C3`

Discovery-Topic-Schema:
```
homeassistant/{domain}/{deviceId}/{objectId}/config
```

---

## Architektur

```
main.cpp
 └── App                    → Boot-Ablauf, Hauptschleife (Messung, Verlauf, WebSocket-Broadcast)
      ├── SystemStateManager   → Bestimmt Boot-Modus (SETUP / NORMAL)
      ├── WifiManager          → Verbindungsverwaltung, exponentielle Backoff-Wiederverbindung
      ├── WebServerDashboard   → REST-API (Konfiguration, Backup, Push, MQTT, WLAN), WebSocket, LittleFS
      │    ├── AdminSession    → Anmeldung per Token, Sperre nach Fehlversuchen
      │    └── WebSupport      → Body-Sammler (Auth-sicher), JSON-/Passwort-Helfer, Neustart, LittleFS
      ├── UpdateService        → Repo-Update (GitHub-Release) und lokaler Upload, /api/update/*
      │    └── FlashSession    → Schreiben + SHA-256 + Signaturprüfung, räumt bei Fehlern selbst auf
      ├── WebServerSetup       → WiFi-Erstkonfiguration im AP-Modus
      ├── SensorManager        → Ultraschall-/ToF-Messung, Median-Filter, Füllstandsberechnung
      ├── HistoryManager       → Verlauf im NVS-Ringpuffer
      ├── ConfigStore          → NVS-Persistenz (ArduinoJson), zwischengespeichert
      ├── DebugLogger          → Serielle Logausgabe (nur Debug-Builds)
      └── NetworkWorker        → eigener FreeRTOS-Task: MqttManager (Verbindung, Discovery, Publish)
                                 und PushNotificationManager (SMTP, Test-Mail, Diagnose)

lib/LevelCore                → reine Logik ohne Hardware, am PC getestet:
                               Config-Regeln, Versionsvergleich, Erinnerungszeiten, Mail-Kodierung
```

Nur der Netzwerk-Task bedient MQTT und SMTP; andere Tasks stellen Anfragen
(`MqttManager::requestReconnect`, `PushNotificationManager::requestTestEmail/requestDiagnostic`).

**Singleton-Muster** für alle Manager — Zugriff immer via `XManager::getInstance()`.

---

## Build & Flash

### Voraussetzungen
- [PlatformIO](https://platformio.org/) (VS Code Extension oder CLI)
- [Node.js](https://nodejs.org/) ≥ 18 für die UI

### Abhängigkeiten (platformio.ini)

| Bibliothek | Version |
|---|---|
| ESP32Async/ESPAsyncWebServer | ^3.12.1 |
| ESP32Async/AsyncTCP | ^3.5.0 |
| bblanchon/ArduinoJson | ^7.4.3 |
| knolleary/PubSubClient | ^2.8 |
| pololu/VL53L1X | ^1.3.1 |

Plattform: `espressif32 ^7.1.3` (Arduino-ESP32 2.0.17). Die Plattform 8.x bringt Arduino-ESP32 3.x mit und lässt das Image auf rund 2 MB anwachsen – das passt nicht in die 1,5-MB-OTA-Slots von `partitions.csv`.

### UI bauen

```bash
cd ui
npm install
npm run build
```

Die gebauten Dateien werden per `extra_scripts` automatisch nach `data/` geschrieben.

### Firmware kompilieren und flashen

```bash
# Dateisystem-Image erstellen und flashen
pio run --target uploadfs

# Firmware kompilieren und flashen
pio run --target upload
```

Hinweis zur Speicheraufteilung auf ESP32-C3:
- Das Projekt nutzt eine benutzerdefinierte Partitionstabelle in `partitions.csv`.
- Aktuelle Aufteilung:
  - `nvs`: `0x5000` für Konfiguration
  - `app0` / `app1`: jeweils `0x180000`
  - `histnvs`: `0x050000` für Historie
  - `littlefs`: `0x0A0000` ab Adresse `0x360000`
- Bei Änderungen an der Partitionstabelle ist ein einmaliger Full-Erase vor dem nächsten Flash sinnvoll, damit keine Alt-Daten aus dem vorherigen Layout übrig bleiben.

### Signierte OTA-Releases

Die OTA-Dateien werden mit ECDSA P-256 und SHA-256 signiert. Die Signatur steckt als 81-Byte-Trailer am Ende jeder Datei (`...-app.bin`, `...-web-ui.bin`): Magic `LCSIGV1!`, Signaturlänge, Signatur. Die Firmware hält beim Empfang immer die letzten 81 Byte zurück, schreibt nur das Image, bildet dabei den SHA-256 und prüft die Signatur mit dem fest eingebetteten Public Key, bevor das Image aktiviert wird. Für den Nutzer bleibt es bei zwei Dateien, beim Repo-Update bei gar keiner.
Für ein Recovery per Kabel (`esptool`) liegen unsignierte Images bei (`...-recovery-appimage.bin`, `...-recovery-uiimage.bin`), denn die signierte Web-UI-Datei wäre 81 Byte größer als ihre Partition. Diese Dateien sind kein Update-Weg.
Ein neu installiertes Image wird nach 20 Sekunden stabiler Laufzeit als gültig bestätigt; stürzt es vorher ab, startet der Bootloader wieder das vorherige Image (Rollback).
Manifest- und Asset-Downloads werden per TLS-Zertifikatskette (Root-CA-Pruefung) validiert.

Einmalig Schluessel erzeugen:

```bash
node scripts/generate-release-signing-keys.js
```

Ergebnis:
- Privater Schluessel: `signing/release_private.pem` (bleibt lokal, ist per `.gitignore` ausgeschlossen)
- Oeffentlicher Schluessel: `signing/release_public.pem`
- Eingebetteter Firmware-Key: `include/ReleaseSigningPublicKey.h`

Signiertes Release erzeugen:

```bash
# optional: eigener Pfad zum privaten Schluessel
set RELEASE_SIGNING_PRIVATE_KEY=C:\path\to\release_private.pem

node scripts/prepare-release.js
```

`prepare-release.js` bricht ab, wenn kein privater Signatur-Schluessel vorhanden ist.

Vor dem GitHub-Upload prueft `scripts/run-release.ps1` automatisch beide Release-Assets:

```bash
node scripts/test-ota-release.mjs
```

Der Regressionstest prueft die exakte Build-Nutzlast, die Signatur im Trailer (inkl. Empfang in Stuecken unterschiedlicher Groesse) und `SHA256SUMS.txt`. Ein Release mit abweichenden oder unvollstaendigen Assets wird dadurch vor der Veroeffentlichung abgebrochen.

Die Release-Notes entstehen aus `CHANGELOG.md` (Abschnitt `## <version>`); `prepare-release.js` bricht ab, wenn der Abschnitt fehlt.
Lokale Dev-Staende bekommen in `version.json` das Feld `"label"` (z. B. `"1.5.A"`). Sie gelten als aelter als jedes Release und lassen sich nicht als Release verpacken.

Zusätzlich läuft vor dem Upload `scripts/test-phase6-contracts.mjs`. Der Test prüft die Verträge für Sensorfehler, Persistenz, Backup, Historie, Dirty-State, Admin-Schutz und Request-Grenzen.

### Tests

| Test | Aufruf | Prüft |
|---|---|---|
| Logik-Tests (PC) | `pio test -e native` | Config-Regeln, Versionsvergleich, Erinnerungszeiten, Mail-Kodierung, Signatur-Trailer (braucht gcc; unter Windows z. B. im Docker-Container `python:3.12`) |
| Verträge | `node scripts/test-phase6-contracts.mjs` | Sicherheits- und Verdrahtungsregeln im Quelltext |
| UI-Smoke-Test | `node scripts/ui-smoke.mjs` (`--lang=en`, `--strict`) | Web-UI im Browser gegen ein simuliertes Gerät (Chrome/Edge, Pfad per `BROWSER=...`) |
| Release-Artefakte | `node scripts/test-ota-release.mjs` | Images, Signaturen, Prüfsummen |

Die CI (`.github/workflows/ci.yml`) führt Logik-Tests, UI-Build, Verträge und den Firmware-Build bei jedem Push aus.

Die Admin-Konfiguration liegt in `ui/src/config/` (Sensor, WiFi, MQTT, Push; gemeinsamer Rahmen `ConfigPanel.svelte`). Texte stehen in `ui/src/i18n/` (Deutsch und Englisch).

Optional (`scripts/run-release.ps1 -WithHardwareTest`) fuehrt der Release-Workflow nach dem GitHub-Upload einen echten Repo-OTA-Test gegen ein Geraet aus; dabei wird das Geraet aktualisiert. Der Host wird ueber `OTA_TEST_HOST` gesetzt; standardmaessig wird `http://stand.local` verwendet:

```powershell
$env:OTA_TEST_HOST = 'http://192.168.1.123'
$env:OTA_TEST_ADMIN_PASSWORD = '<Admin-Testpasswort>'
```

Alternativ kann ein bereits gültiger Token über `OTA_TEST_ADMIN_TOKEN` gesetzt werden. Ist kein PlatformIO-USB-Geraet verbunden, wird der Hardware-Test protokolliert uebersprungen. Ist ein Geraet verbunden, aber keine HIL-Authentifizierung gesetzt, wird der Test ebenfalls nachvollziehbar uebersprungen; mit Passwort oder Token sind ein nicht erreichbarer OTA-Host oder ein fehlgeschlagener Versionswechsel Release-Fehler.

Wichtiger Workflow-Hinweis:
- `scripts/run-release.ps1` baut Firmware und Web-UI nicht neu, sondern verpackt die vorhandenen Artefakte aus `.pio/build`.
- Nach `scripts/run-push.ps1` sollte deshalb immer `scripts/run-deploy.ps1` oder mindestens ein frischer Build des gepushten Stands erfolgt sein, bevor das GitHub-Release erstellt wird.

### Seriell-Monitor

```bash
pio device monitor --baud 115200
```

---

## Erstkonfiguration

1. ESP32 einschalten → Access Point **"Level-Control-Setup"** erscheint
2. Mit dem AP verbinden (kein Passwort)
3. Browser öffnen: `http://192.168.4.1`
4. SSID und WLAN-Passwort eintragen → Speichern
5. ESP32 startet neu und verbindet sich mit dem WLAN
6. IP-Adresse aus dem Router-DHCP ermitteln (oder im Serial-Monitor ablesen)
7. Dashboard unter `http://<IP>/` aufrufen
8. Unter **Konfiguration → Sensor**: Behälterhöhe eintragen und ggf. Offset anpassen
9. Unter **MQTT & HA**: Broker, Port und Zugangsdaten eingeben
10. Optional unter **Push Nachricht**: SMTP und Schwellwert konfigurieren

---

## VL53L1X Laser-Kalibrierung

Der VL53L1X ist ein Time-of-Flight (ToF) Laser-Distanzsensor, der deutlich präzisere Messungen als ein Ultraschallsensor liefert. Er wird über I²C angesprochen und in der Firmware über die Pololu-Bibliothek (`VL53L1X`) angesteuert.

### Funktionsweise

- **Distanzmodus:** `Short` (bis ~1,3 m) – für typische Salzbehälter optimal.
- **Timing-Budget:** 50 ms pro Einzelmessung.
- **Sampling:** Median aus 5 aufeinanderfolgenden Messungen; nur Messungen mit `range_status == 0` (gültig) fließen ein.
- **Berechnung:** `Level-Control (cm) = (Behälterhöhe + Offset) – gemessene_Distanz_cm`

### Kalibrierungsparameter

Alle Kalibrierungsparameter werden persistent im NVS gespeichert und sind über **Konfiguration → Sensor** in der Web-UI einstellbar:

| Parameter | Beschreibung | Einheit |
|---|---|---|
| `behaelterhoehe` | Innentiefe des Behälters vom Sensor bis zum Boden | cm |
| `offset` | Korrekturwert für den Abstand zwischen Sensor und Behälteroberseite | cm |

### Schritt-für-Schritt-Kalibrierung

1. **Sensor montieren** – Den VL53L1X möglichst senkrecht über der Behälteröffnung befestigen.
2. **Behälterhöhe eintragen** – Innentiefe des Behälters in cm in der Web-UI unter **Konfiguration → Sensor → Behälterhöhe** eingeben und speichern.
3. **Offset bestimmen** – Befülle den Behälter mit einer bekannten Menge (z. B. vollständig leer oder auf einen bekannten Füllstand). Lies den angezeigten Rohwert (`Aktuelle Distanz`) ab und berechne den Offset:
   ```
   offset = bekannter_Fuellstand_cm - ((behaelterhoehe + offset_aktuell) - gemessene_Distanz_cm)
   ```
   Einfacher: Lass den Behälter leer und stelle sicher, dass `Level-Control = 0 cm`. Falls die Anzeige einen Wert ≠ 0 zeigt, passe den Offset entsprechend an (positiver Offset = Sensor sitzt weiter vom Inhalt entfernt als angenommen).
4. **Verifizieren** – Befülle den Behälter auf einen bekannten Pegelstand und prüfe, ob der angezeigte Wert übereinstimmt. Wiederhole Schritt 3, bis die Abweichung < 1 cm ist.

### Hinweise zur Messgenauigkeit

- **Umgebungslicht:** Der VL53L1X ist empfindlich gegenüber starkem Umgebungslicht (Sonneneinstrahlung direkt auf den Sensor). Im Zweifel Sensor abschirmen.
- **Oberfläche:** Körniges oder weißes Salz reflektiert gut; klare Flüssigkeiten oder dunkle Oberflächen können die Messqualität reduzieren.
- **Abstandsbereich:** Der `Short`-Modus ist bis ca. 1,3 m zuverlässig. Für größere Behälter (> 1,3 m) muss ggf. in der Firmware auf `Long`-Modus gewechselt werden (`setDistanceMode(VL53L1X::Long)` in `SensorManager.cpp`).
- **Temperaturdrift:** Der Sensor hat eine geringe Temperaturdrift. Bei großen Temperaturschwankungen (z. B. Außenaufstellung) kann eine erneute Kalibrierung notwendig sein.

---

## Technische Kenndaten

| Eigenschaft | Wert |
|---|---|
| Flash-Nutzung | ~74,8 % (4 MB) |
| RAM-Nutzung | ~12,3 % (320 KB) |
| Sensor-Messintervall | konfigurierbar, mindestens 5 Sekunden |
| MQTT-Publish-Intervall | 30 Sekunden |
| WebSocket-Broadcast (Sensor) | Sofort nach jeder Messung |
| WebSocket-Broadcast (WiFi/Uptime/MQTT) | 5 Sekunden |
| MQTT-Reconnect-Prüfung | 5 Sekunden |
| Serial-Diagnose | 10 Sekunden |
| MQTT Keepalive | 30 Sekunden |
| MQTT Buffer | 1024 Bytes |

