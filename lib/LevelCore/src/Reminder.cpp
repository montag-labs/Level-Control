#include "Reminder.h"

#include "ConfigRules.h"

namespace {
int toMondayBasedWeekday(const tm& timeInfo) {
    return timeInfo.tm_wday == 0 ? 7 : timeInfo.tm_wday;
}

time_t buildLocalTime(int year, int month, int day, int hour, int minute) {
    struct tm candidate = {};
    candidate.tm_year = year - 1900;
    candidate.tm_mon = month - 1;
    candidate.tm_mday = day;
    candidate.tm_hour = hour;
    candidate.tm_min = minute;
    candidate.tm_sec = 0;
    candidate.tm_isdst = -1;
    return mktime(&candidate);
}

int firstWeekdayOfMonth(int year, int month, int reminderWeekday) {
    struct tm firstDay = {};
    firstDay.tm_year = year - 1900;
    firstDay.tm_mon = month - 1;
    firstDay.tm_mday = 1;
    firstDay.tm_isdst = -1;
    mktime(&firstDay);

    const int firstWeekday = toMondayBasedWeekday(firstDay);
    const int offset = (reminderWeekday - firstWeekday + 7) % 7;
    return 1 + offset;
}

time_t buildMonthlyReminderTime(int year, int month, const PushConfig& pushConfig) {
    const int reminderWeekday = ConfigRules::normalizeReminderWeekday(pushConfig.reminderWeekday);
    const int reminderDay = firstWeekdayOfMonth(year, month, reminderWeekday);
    return buildLocalTime(year, month, reminderDay, pushConfig.sendHour, pushConfig.sendMinute);
}

}  // namespace

namespace Reminder {

time_t latestOccurrence(const PushConfig& pushConfig, time_t nowEpoch) {
    if (nowEpoch < kMinValidEpoch) {
        return 0;
    }

    struct tm localNow;
    localtime_r(&nowEpoch, &localNow);

    const std::string reminderCycle = ConfigRules::normalizeReminderCycle(pushConfig.reminderCycle);

    if (reminderCycle == "day") {
        struct tm candidate = localNow;
        candidate.tm_hour = pushConfig.sendHour;
        candidate.tm_min = pushConfig.sendMinute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        time_t candidateEpoch = mktime(&candidate);
        if (candidateEpoch > nowEpoch) {
            candidate.tm_mday -= 1;
            candidateEpoch = mktime(&candidate);
        }
        return candidateEpoch;
    }

    if (reminderCycle == "week") {
        struct tm candidate = localNow;
        candidate.tm_hour = pushConfig.sendHour;
        candidate.tm_min = pushConfig.sendMinute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        candidate.tm_mday -= (toMondayBasedWeekday(localNow) - ConfigRules::normalizeReminderWeekday(pushConfig.reminderWeekday));
        time_t candidateEpoch = mktime(&candidate);
        if (candidateEpoch > nowEpoch) {
            candidate.tm_mday -= 7;
            candidateEpoch = mktime(&candidate);
        }
        return candidateEpoch;
    }

    int year = localNow.tm_year + 1900;
    int month = localNow.tm_mon + 1;
    time_t candidateEpoch = buildMonthlyReminderTime(year, month, pushConfig);
    if (candidateEpoch > nowEpoch) {
        month -= 1;
        if (month == 0) {
            month = 12;
            year -= 1;
        }
        candidateEpoch = buildMonthlyReminderTime(year, month, pushConfig);
    }
    return candidateEpoch;
}

}  // namespace Reminder
