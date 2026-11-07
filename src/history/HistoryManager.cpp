#include "HistoryManager.h"
#include "DebugLogger.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <memory>

namespace {
bool mountLittleFsForLegacyMigration() {
    if (LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        return true;
    }
    return LittleFS.begin(false, "/littlefs", 10, "spiffs");
}
}

HistoryManager& HistoryManager::getInstance() {
    static HistoryManager instance;
    return instance;
}

bool HistoryManager::isTimeValid() {
    return time(nullptr) > 1000000000L; // nach Jahr 2001 => NTP synchronisiert
}

void HistoryManager::init() {
    if (initialized_) return;
    initialized_ = true;
    head_  = 0;
    count_ = 0;

    if (loadFromNvs()) {
        return;
    }

    if (loadLegacyFile()) {
        saveToNvs();
        LittleFS.remove(kLegacyPath);
        DebugLogger::getInstance().log(LogLevel::INFO,
            "HistoryManager: Legacy-Historie aus LittleFS nach NVS migriert");
    }
}

bool HistoryManager::loadFromNvs() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true, kNvsPartitionLabel)) {
        DebugLogger::getInstance().log(LogLevel::WARN,
            "HistoryManager: NVS konnte nicht gelesen werden");
        return false;
    }

    const size_t len = prefs.getBytesLength(kNvsKey);
    if (len == 0) {
        prefs.end();
        DebugLogger::getInstance().log(LogLevel::INFO,
            "HistoryManager: keine NVS-Historie vorhanden, pruefe Legacy-Datei");
        return false;
    }

    if (len != sizeof(PersistedData)) {
        prefs.end();
        DebugLogger::getInstance().log(LogLevel::WARN,
            "HistoryManager: NVS-Format ungueltig, ignoriere Historie");
        return false;
    }

    // Heap statt Stack: PersistedData ist 11 KB – zu gross fuer den Stack
    std::unique_ptr<PersistedData> data(new PersistedData());
    const size_t read = prefs.getBytes(kNvsKey, data.get(), sizeof(PersistedData));
    prefs.end();

    if (read != sizeof(PersistedData) || data->magic != kMagic || data->count > kCapacity || data->head >= kCapacity) {
        DebugLogger::getInstance().log(LogLevel::WARN,
            "HistoryManager: NVS-Inhalt ungueltig, ignoriere Historie");
        return false;
    }

    head_ = data->head;
    count_ = data->count;
    std::memcpy(records_, data->records, sizeof(records_));

    DebugLogger::getInstance().log(LogLevel::INFO,
        "HistoryManager: " + std::to_string(count_) + " Eintraege aus NVS geladen");
    return true;
}

bool HistoryManager::loadLegacyFile() {
    if (!mountLittleFsForLegacyMigration()) {
        DebugLogger::getInstance().log(LogLevel::INFO,
            "HistoryManager: LittleFS fuer Legacy-Migration nicht verfuegbar, starte leer");
        return false;
    }

    File f = LittleFS.open(kLegacyPath, "r");
    if (!f) {
        DebugLogger::getInstance().log(LogLevel::INFO,
            "HistoryManager: keine Legacy-Datei vorhanden, starte leer");
        return false;
    }

    FileHeader hdr;
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        DebugLogger::getInstance().log(LogLevel::WARN,
            "HistoryManager: Legacy-Header unlesbar, starte leer");
        return false;
    }

    if (hdr.magic != kMagic || hdr.count > kCapacity || hdr.head >= kCapacity) {
        f.close();
        DebugLogger::getInstance().log(LogLevel::WARN,
            "HistoryManager: ungueltige Legacy-Datei, starte leer");
        return false;
    }

    const size_t dataBytes = hdr.count * sizeof(Record);
    if (f.read(reinterpret_cast<uint8_t*>(records_), dataBytes) != dataBytes) {
        f.close();
        DebugLogger::getInstance().log(LogLevel::WARN,
            "HistoryManager: Legacy-Daten unlesbar, starte leer");
        return false;
    }

    head_  = hdr.head;
    count_ = hdr.count;
    f.close();
    DebugLogger::getInstance().log(LogLevel::INFO,
        "HistoryManager: " + std::to_string(count_) + " Legacy-Eintraege geladen");
    return true;
}

void HistoryManager::saveToNvs() const {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false, kNvsPartitionLabel)) {
        DebugLogger::getInstance().log(LogLevel::ERROR,
            "HistoryManager: NVS konnte nicht geoeffnet werden");
        return;
    }

    // Heap statt Stack: PersistedData ist 11 KB – zu gross fuer den Stack
    std::unique_ptr<PersistedData> data(new PersistedData());
    data->magic = kMagic;
    data->head = head_;
    data->count = count_;
    std::memcpy(data->records, records_, sizeof(records_));

    const size_t written = prefs.putBytes(kNvsKey, data.get(), sizeof(PersistedData));
    prefs.end();

    if (written != sizeof(PersistedData)) {
        DebugLogger::getInstance().log(LogLevel::ERROR,
            "HistoryManager: NVS-Schreiben unvollstaendig");
    }
}

void HistoryManager::pruneOld(uint32_t now) {
    // Entferne Einträge am Anfang des Ringpuffers, die älter als kRetentionSec sind
    while (count_ > 0) {
        const Record& oldest = records_[head_];
        if (now >= oldest.ts && (now - oldest.ts) > kRetentionSec) {
            head_ = (head_ + 1) % kCapacity;
            count_--;
        } else {
            break;
        }
    }
}

void HistoryManager::addSample(float value) {
    if (!initialized_) return;
    if (!isTimeValid())  return;

    const uint32_t now = static_cast<uint32_t>(time(nullptr));

    // Zu alten Einträge entfernen
    pruneOld(now);

    // Intervall-Check: letzten gespeicherten Punkt prüfen
    if (count_ > 0) {
        const uint16_t lastIdx = static_cast<uint16_t>((head_ + count_ - 1) % kCapacity);
        if (now - records_[lastIdx].ts < kSampleIntervalSec) {
            return; // Noch nicht 6 Stunden her
        }
    }

    // Schreiben: entweder freien Slot nutzen oder ältesten überschreiben
    const uint16_t writeIdx = static_cast<uint16_t>((head_ + count_) % kCapacity);
    records_[writeIdx] = {now, value};

    if (count_ < kCapacity) {
        count_++;
    } else {
        head_ = static_cast<uint16_t>((head_ + 1) % kCapacity);
    }

    saveToNvs();
    DebugLogger::getInstance().log(LogLevel::INFO,
        "HistoryManager: Datenpunkt gespeichert, total=" + std::to_string(count_) +
        " pct=" + std::to_string(static_cast<int>(value)));
}

std::vector<HistoryEntry> HistoryManager::getHistory() const {
    std::vector<HistoryEntry> result;
    result.reserve(count_);
    for (uint16_t i = 0; i < count_; i++) {
        const Record& r = records_[(head_ + i) % kCapacity];
        result.push_back({r.ts, r.value});
    }
    return result;
}

void HistoryManager::clear() {
    head_  = 0;
    count_ = 0;
    Preferences prefs;
    if (prefs.begin(kNvsNamespace, false, kNvsPartitionLabel)) {
        prefs.remove(kNvsKey);
        prefs.end();
    }
    LittleFS.remove(kLegacyPath);
    DebugLogger::getInstance().log(LogLevel::INFO, "HistoryManager: Verlaufsdaten gelöscht");
}

void HistoryManager::restore(const std::vector<HistoryEntry>& entries) {
    head_  = 0;
    count_ = 0;
    // Backups koennen unsortiert sein; der Ringpuffer (Aufraeumen, Intervallpruefung) braucht aufsteigende Zeitstempel.
    std::vector<HistoryEntry> sorted(entries);
    std::sort(sorted.begin(), sorted.end(), [](const HistoryEntry& a, const HistoryEntry& b) {
        return a.timestamp < b.timestamp;
    });
    // Bei zu vielen Eintraegen bleiben die neuesten erhalten.
    const size_t skip = sorted.size() > kCapacity ? sorted.size() - kCapacity : 0;
    const uint16_t n = static_cast<uint16_t>(sorted.size() - skip);
    for (uint16_t i = 0; i < n; i++) {
        records_[i] = {sorted[skip + i].timestamp, sorted[skip + i].value};
    }
    count_ = n;
    saveToNvs();
    DebugLogger::getInstance().log(LogLevel::INFO,
        "HistoryManager: " + std::to_string(count_) + " Eintraege importiert");
}
