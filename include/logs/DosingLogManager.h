#ifndef DOSING_LOG_MANAGER_H
#define DOSING_LOG_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "logs/DosingLog.h"
#include "logs/DosingLogStore.h"
#include "scheduling/Schedule.h"

/**
 * @brief Thread-safe manager for dosing logs
 *
 * Provides high-level operations for logging doses:
 * - Log scheduled doses (from ScheduleManager)
 * - Log ad-hoc doses (from DosingHead)
 * - Query logs for dashboard and hourly grid
 * - Automatic pruning of old logs
 *
 * Thread-safety: All public methods use mutex protection for FreeRTOS
 */
class DosingLogManager {
public:
    DosingLogManager();
    ~DosingLogManager();

    /**
     * @brief Initialize the mutex (must be called BEFORE begin())
     * Call this in setup() before FreeRTOS scheduler starts
     */
    void initMutex();

    /**
     * @brief Initialize the log manager
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Log a scheduled dose
     * @param head Head index (0-3)
     * @param volume Volume dosed in mL
     * @param timestamp Unix epoch time when dose occurred
     * @return true if log successful
     */
    bool logScheduledDose(uint8_t head, float volume, uint32_t timestamp);

    /**
     * @brief Log an ad-hoc (manual) dose
     * @param head Head index (0-3)
     * @param volume Volume dosed in mL
     * @param timestamp Unix epoch time when dose occurred
     * @return true if log successful
     */
    bool logAdhocDose(uint8_t head, float volume, uint32_t timestamp);

    /**
     * @brief Get daily summary for a specific head
     * @param head Head index (0-3)
     * @param currentTime Current Unix epoch time
     * @param dailyTarget Daily target volume from schedule (pass 0 if no schedule)
     * @param dosesPerDay Doses per day from schedule (pass 0 if no schedule)
     * @param perDoseVolume Per-dose volume from schedule (pass 0 if no schedule)
     * @param summary Output summary
     * @return true if summary generated
     */
    bool getDailySummary(uint8_t head, uint32_t currentTime, float dailyTarget,
                        uint16_t dosesPerDay, float perDoseVolume, DailySummary& summary);

    /**
     * @brief Get all daily summaries for dashboard (all 4 heads)
     * @param currentTime Current Unix epoch time
     * @param schedules Array of schedules (used to populate target values)
     * @param summaries Output array (must be size 4)
     * @return Number of summaries generated (should be 4)
     */
    uint8_t getAllDailySummaries(uint32_t currentTime, Schedule* schedules, DailySummary* summaries);

    /**
     * @brief Get hourly logs for a specific time range
     * @param startTime Start time (Unix epoch)
     * @param endTime End time (Unix epoch)
     * @param logs Output array
     * @param maxLogs Maximum number of logs to return
     * @return Number of logs returned
     */
    uint16_t getHourlyLogs(uint32_t startTime, uint32_t endTime, HourlyDoseLog* logs, uint16_t maxLogs);

    /**
     * @brief Prune old logs beyond retention period
     * @param currentTime Current Unix epoch time
     * @return Number of logs pruned
     */
    uint16_t pruneOldLogs(uint32_t currentTime);

    /**
     * @brief Get total number of logs stored
     * @return Log count
     */
    uint16_t getLogCount();

    /**
     * @brief Clear all logs (for testing/debugging)
     * @return true if successful
     */
    bool clearAll();

private:
    DosingLogStore store;
    SemaphoreHandle_t mutex;
    bool initialized;

    /**
     * @brief Round timestamp to hour boundary
     * @param timestamp Unix epoch time
     * @return Timestamp rounded down to nearest hour
     */
    uint32_t roundToHour(uint32_t timestamp);

    /**
     * @brief Get start of day timestamp
     * @param timestamp Unix epoch time
     * @return Timestamp at 00:00:00 of the same day
     */
    uint32_t getStartOfDay(uint32_t timestamp);

    /**
     * @brief Log a dose (internal, mutex must be held by caller)
     * @param head Head index
     * @param scheduledVolume Scheduled volume (0 if ad-hoc)
     * @param adhocVolume Ad-hoc volume (0 if scheduled)
     * @param timestamp Unix epoch time
     * @return true if log successful
     */
    bool logDoseInternal(uint8_t head, float scheduledVolume, float adhocVolume, uint32_t timestamp);
};

#endif // DOSING_LOG_MANAGER_H
