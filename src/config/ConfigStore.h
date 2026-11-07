#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <mutex>
#include <string>

#include "Config.h"
#include "ConfigRules.h"

class ConfigStore {
public:
    static ConfigStore& getInstance();

    // Liefert die (zwischengespeicherte) Konfiguration. false = noch nichts gespeichert.
    bool load(Config& config);
    // Normalisiert die Konfiguration (ConfigRules::sanitize) und schreibt sie in den NVS.
    bool save(const Config& config);
    // Loescht die gespeicherte Konfiguration (Werkseinstellungen).
    void reset();
    bool wasFallbackApplied() const;
    std::string getFallbackMessage() const;

private:
    ConfigStore() = default;
    const char* nvsNamespace_ = "config";
    std::mutex mutex_;
    Config cache_;
    bool cacheValid_ = false;
    bool loadFallbackApplied_ = false;
    std::string loadFallbackMessage_;
};

#endif // CONFIG_STORE_H
