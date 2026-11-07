#ifndef CONFIG_RULES_H
#define CONFIG_RULES_H

#include <string>
#include <vector>

#include "Config.h"

// Reine Regeln fuer die Konfiguration (ohne NVS/Arduino): Normalisierung und Grenzwerte.
namespace ConfigRules {

// Setzt ungueltige Werte auf sichere Defaults bzw. klemmt sie in den gueltigen Bereich.
// Gefundene Korrekturen werden (falls gewuenscht) in `reasons` beschrieben.
void sanitize(Config& config, std::vector<std::string>* reasons = nullptr);

std::string normalizeReminderCycle(const std::string& value);
int normalizeReminderWeekday(int value);
std::string reminderCycleFromLegacyMinutes(unsigned long cycleMinutes);
std::string normalizePushEncryptionMode(const std::string& rawMode);
std::string derivePushEncryptionMode(bool useSsl, bool startTls);

}  // namespace ConfigRules

#endif  // CONFIG_RULES_H
