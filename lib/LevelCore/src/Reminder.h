#ifndef REMINDER_H
#define REMINDER_H

#include <ctime>

#include "Config.h"

namespace Reminder {

// Ab diesem Zeitstempel gilt die Systemuhr als per NTP synchronisiert.
constexpr time_t kMinValidEpoch = 1700000000;

// Beginn des letzten faelligen Erinnerungsfensters (taeglich/woechentlich/monatlich) vor `nowEpoch`.
// 0 = Uhr noch nicht gueltig. Die Auswertung erfolgt in der lokalen Zeitzone des Systems (auf dem Geraet UTC).
time_t latestOccurrence(const PushConfig& pushConfig, time_t nowEpoch);

}  // namespace Reminder

#endif  // REMINDER_H
