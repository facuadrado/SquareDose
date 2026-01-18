#include "logs/DosingLog.h"

bool HourlyDoseLog::isValid() const {
    // Check head index
    if (head >= NUM_DOSING_HEADS) {
        return false;
    }

    // Check volumes are non-negative
    if (scheduledVolume < 0.0f || adhocVolume < 0.0f) {
        return false;
    }

    // Check timestamp is reasonable (after year 2000)
    if (hourTimestamp < 946684800) {
        return false;
    }

    // Validate hourTimestamp is actually aligned to hour boundary
    // Hour timestamps should be divisible by 3600
    if (hourTimestamp % 3600 != 0) {
        return false;
    }

    return true;
}

String HourlyDoseLog::toString() const {
    String result = "HourlyDoseLog[";
    result += "time=" + String(hourTimestamp);
    result += ", head=" + String(head);
    result += ", scheduled=" + String(scheduledVolume, 2) + "mL";
    result += ", adhoc=" + String(adhocVolume, 2) + "mL";
    result += ", total=" + String(getTotalVolume(), 2) + "mL";
    result += "]";
    return result;
}
