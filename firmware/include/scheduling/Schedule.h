#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>

/**
 * @brief Schedule data structure
 *
 * Simplified: Only supports INTERVAL-based schedules
 * User specifies total daily volume and number of doses per day
 * System auto-calculates per-dose volume and interval
 *
 * One schedule per dosing head (4 total)
 * Head index (0-3) serves as the schedule identifier
 */
struct Schedule {
    uint8_t head;                   // Dosing head index (0-3) - also serves as schedule ID
    bool enabled;                   // Whether schedule is active

    // User inputs
    float dailyTargetVolume;        // Total mL per day (e.g., 24.0)
    uint16_t dosesPerDay;           // Number of doses per day (e.g., 12, max 1440)

    // Auto-calculated fields (derived from dailyTargetVolume + dosesPerDay)
    float volume;                   // Volume per dose in mL (dailyTargetVolume / dosesPerDay)
    uint32_t intervalSeconds;       // Interval in seconds between doses (86400 / dosesPerDay)

    // Last execution tracking
    uint32_t lastExecutionTime;     // Unix epoch time of last execution
    uint32_t executionCount;        // Number of times executed

    // Metadata
    char name[32];                  // User-friendly name (optional)
    uint32_t createdAt;             // Unix epoch time when created
    uint32_t updatedAt;             // Unix epoch time when last modified

    // Helper methods
    bool isValid() const;
    bool shouldExecute(uint32_t currentTime) const;
    bool calculateFromDailyTarget();  // Calculate volume & intervalSeconds from dailyTarget + dosesPerDay
    String toString() const;
};

/**
 * @brief Schedule validation result
 */
struct ScheduleValidationResult {
    bool valid;
    String errorMessage;
};

/**
 * @brief Validate schedule parameters
 * @param sched Schedule to validate
 * @return Validation result
 */
ScheduleValidationResult validateSchedule(const Schedule& sched);

#endif // SCHEDULE_H
