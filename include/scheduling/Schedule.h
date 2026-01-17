#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>

/**
 * @brief Schedule type enumeration
 */
enum class ScheduleType : uint8_t {
    ONCE = 0,      // One-time schedule at specific date/time
    DAILY = 1,     // Repeats daily at specific time
    INTERVAL = 2   // Repeats at fixed interval
};

/**
 * @brief Schedule data structure
 *
 * One schedule per dosing head (4 total)
 * Head index (0-3) serves as the schedule identifier
 */
struct Schedule {
    uint8_t head;                   // Dosing head index (0-3) - also serves as schedule ID
    ScheduleType type;              // ONCE, DAILY, or INTERVAL
    bool enabled;                   // Whether schedule is active

    float volume;                   // Volume to dispense in mL

    // Time configuration (interpretation depends on type)
    // ONCE: timestamp is Unix epoch time for when to run
    // DAILY: timestamp is time of day (seconds since midnight, repeated daily)
    // INTERVAL: timestamp is interval in seconds between doses
    uint32_t timestamp;

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
