#include "scheduling/Schedule.h"

bool Schedule::isValid() const {
    // Check head index
    if (head >= 4) {
        return false;
    }

    // Check volume range
    if (volume <= 0.0f || volume > 1000.0f) {
        return false;
    }

    // Type-specific validation
    switch (type) {
        case ScheduleType::ONCE:
            // ONCE schedules need a valid future timestamp
            return timestamp > 0;

        case ScheduleType::DAILY:
            // DAILY schedules: timestamp is seconds since midnight (0-86400)
            return timestamp < 86400;

        case ScheduleType::INTERVAL:
            // INTERVAL schedules: timestamp is interval in seconds (min 60s, max 24h)
            return timestamp >= 60 && timestamp <= 86400;

        default:
            return false;
    }
}

bool Schedule::shouldExecute(uint32_t currentTime) const {
    if (!enabled || !isValid()) {
        return false;
    }

    switch (type) {
        case ScheduleType::ONCE: {
            // Execute once at specific timestamp, only if not executed yet
            if (lastExecutionTime == 0 && currentTime >= timestamp) {
                return true;
            }
            return false;
        }

        case ScheduleType::DAILY: {
            // Calculate current time of day (seconds since midnight)
            uint32_t timeOfDay = currentTime % 86400;

            // Check if we're at or past the scheduled time today
            if (timeOfDay >= timestamp) {
                // Check if we already executed today
                // If lastExecution was on a different day, we can execute
                uint32_t lastExecutionDay = lastExecutionTime / 86400;
                uint32_t currentDay = currentTime / 86400;

                if (currentDay > lastExecutionDay) {
                    return true;
                }
            }
            return false;
        }

        case ScheduleType::INTERVAL: {
            // Execute at fixed intervals
            if (lastExecutionTime == 0) {
                // Never executed, execute now
                return true;
            }

            // Check if interval has elapsed
            uint32_t elapsed = currentTime - lastExecutionTime;
            return elapsed >= timestamp;
        }

        default:
            return false;
    }
}

String Schedule::toString() const {
    String result = "Schedule[";
    result += "head=" + String(head);
    result += ", name=" + String(name);
    result += ", type=";

    switch (type) {
        case ScheduleType::ONCE:
            result += "ONCE";
            break;
        case ScheduleType::DAILY:
            result += "DAILY";
            break;
        case ScheduleType::INTERVAL:
            result += "INTERVAL";
            break;
    }

    result += ", volume=" + String(volume, 2) + "mL";
    result += ", enabled=" + String(enabled ? "true" : "false");
    result += ", execCount=" + String(executionCount);
    result += "]";

    return result;
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

    // Validate volume
    if (sched.volume <= 0.0f || sched.volume > 1000.0f) {
        result.valid = false;
        result.errorMessage = "Invalid volume: " + String(sched.volume) + " (must be 0.1-1000 mL)";
        return result;
    }

    // Type-specific validation
    switch (sched.type) {
        case ScheduleType::ONCE:
            if (sched.timestamp == 0) {
                result.valid = false;
                result.errorMessage = "ONCE schedule requires valid timestamp";
            }
            break;

        case ScheduleType::DAILY:
            if (sched.timestamp >= 86400) {
                result.valid = false;
                result.errorMessage = "DAILY schedule time must be 0-86399 seconds (within 24h)";
            }
            break;

        case ScheduleType::INTERVAL:
            if (sched.timestamp < 60) {
                result.valid = false;
                result.errorMessage = "INTERVAL must be at least 60 seconds";
            } else if (sched.timestamp > 86400) {
                result.valid = false;
                result.errorMessage = "INTERVAL cannot exceed 24 hours (86400 seconds)";
            }
            break;

        default:
            result.valid = false;
            result.errorMessage = "Unknown schedule type";
            break;
    }

    return result;
}
