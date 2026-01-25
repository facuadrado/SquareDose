#include "scheduling/Schedule.h"

bool Schedule::isValid() const {
    // Check head index
    if (head >= 4) {
        return false;
    }

    // Check volume range (auto-calculated from dailyTarget)
    if (volume <= 0.0f || volume > 1000.0f) {
        return false;
    }

    // Check interval (minimum 60 seconds)
    if (intervalSeconds < 60 || intervalSeconds > 86400) {
        return false;
    }

    // Check user inputs
    if (dailyTargetVolume <= 0.0f || dosesPerDay == 0) {
        return false;
    }

    return true;
}

bool Schedule::shouldExecute(uint32_t currentTime) const {
    if (!enabled || !isValid()) {
        return false;
    }

    // Execute at fixed intervals
    if (lastExecutionTime == 0) {
        // Never executed, execute now
        return true;
    }

    // Check if interval has elapsed
    uint32_t elapsed = currentTime - lastExecutionTime;
    return elapsed >= intervalSeconds;
}

String Schedule::toString() const {
    String result = "Schedule[";
    result += "head=" + String(head);
    result += ", name=" + String(name);
    result += ", dailyTarget=" + String(dailyTargetVolume, 2) + "mL";
    result += ", dosesPerDay=" + String(dosesPerDay);
    result += ", volume=" + String(volume, 2) + "mL/dose";
    result += ", interval=" + String(intervalSeconds) + "s";
    result += ", enabled=" + String(enabled ? "true" : "false");
    result += ", execCount=" + String(executionCount);
    result += "]";

    return result;
}

bool Schedule::calculateFromDailyTarget() {
    // Validate inputs
    if (dosesPerDay == 0 || dailyTargetVolume <= 0.0f) {
        Serial.println("[Schedule] Invalid dailyTarget or dosesPerDay");
        return false;
    }

    // Calculate volume per dose
    volume = dailyTargetVolume / static_cast<float>(dosesPerDay);

    // Calculate interval in seconds (24 hours = 86400 seconds)
    intervalSeconds = 86400 / dosesPerDay;

    Serial.printf("[Schedule] Calculated: %.2f mL/day, %d doses → %.2f mL/dose every %lu seconds\n",
                 dailyTargetVolume, dosesPerDay, volume, intervalSeconds);

    return true;
}

ScheduleValidationResult validateSchedule(const Schedule& sched) {
    ScheduleValidationResult result;
    result.valid = true;

    // Validate head index
    if (sched.head >= 4) {
        result.valid = false;
        result.errorMessage = "Invalid head index: " + String(sched.head) + " (must be 0-3)";
        return result;
    }

    // Validate user inputs
    if (sched.dailyTargetVolume <= 0.0f || sched.dailyTargetVolume > 10000.0f) {
        result.valid = false;
        result.errorMessage = "Daily target volume must be 0.1-10000 mL";
        return result;
    }

    if (sched.dosesPerDay == 0 || sched.dosesPerDay > 1440) {
        result.valid = false;
        result.errorMessage = "Doses per day must be 1-1440 (max 1 per minute)";
        return result;
    }

    // Validate calculated fields
    if (sched.volume <= 0.0f || sched.volume > 1000.0f) {
        result.valid = false;
        result.errorMessage = "Invalid calculated volume: " + String(sched.volume) + " mL";
        return result;
    }

    if (sched.intervalSeconds < 60) {
        result.valid = false;
        result.errorMessage = "Interval must be at least 60 seconds";
        return result;
    }

    if (sched.intervalSeconds > 86400) {
        result.valid = false;
        result.errorMessage = "Interval cannot exceed 24 hours";
        return result;
    }

    return result;
}
