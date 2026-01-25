#ifndef DOSING_LOG_STORE_H
#define DOSING_LOG_STORE_H

#include <Arduino.h>
#include <Preferences.h>
#include "logs/DosingLog.h"

#define LOG_NVS_NAMESPACE "dosinglogs"
#define LOG_INDEX_KEY "log_index"  // Rolling index for circular buffer
#define LOG_COUNT_KEY "log_count"  // Total number of logs stored

/**
 * @brief NVS storage manager for hourly dosing logs
 *
 * Stores up to 14 days (336 hours) of logs per dosing head = 1344 total entries
 * Uses circular buffer approach with rolling index
 * Each log entry is ~13 bytes (4 + 1 + 4 + 4 = 13 bytes)
 * Total: ~17KB storage
 */
class DosingLogStore {
public:
    DosingLogStore();
    ~DosingLogStore();

    /**
     * @brief Initialize the dosing log store
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Save or update a log entry for a specific hour and head
     * If entry exists for this hour+head, updates it (adds to volumes)
     * Otherwise creates new entry
     * @param log Log entry to save
     * @return true if save successful
     */
    bool saveLog(const HourlyDoseLog& log);

    /**
     * @brief Load a specific log entry by hour timestamp and head
     * @param hourTimestamp Unix epoch rounded to hour
     * @param head Head index (0-3)
     * @param log Output log entry
     * @return true if log found
     */
    bool loadLog(uint32_t hourTimestamp, uint8_t head, HourlyDoseLog& log);

    /**
     * @brief Load all logs for a specific time range
     * @param startTime Start hour timestamp (inclusive)
     * @param endTime End hour timestamp (inclusive)
     * @param logs Output array (must be large enough to hold results)
     * @param maxLogs Maximum number of logs to return
     * @return Number of logs loaded
     */
    uint16_t loadLogsInRange(uint32_t startTime, uint32_t endTime, HourlyDoseLog* logs, uint16_t maxLogs);

    /**
     * @brief Load all logs for a specific head
     * @param head Head index (0-3)
     * @param logs Output array (must be large enough to hold results)
     * @param maxLogs Maximum number of logs to return
     * @return Number of logs loaded
     */
    uint16_t loadLogsForHead(uint8_t head, HourlyDoseLog* logs, uint16_t maxLogs);

    /**
     * @brief Delete old logs beyond retention period
     * @param currentTime Current Unix epoch time
     * @return Number of logs deleted
     */
    uint16_t pruneOldLogs(uint32_t currentTime);

    /**
     * @brief Clear all logs from NVS
     * @return true if clear successful
     */
    bool clearAll();

    /**
     * @brief Get total number of logs stored
     * @return Number of log entries
     */
    uint16_t getLogCount();

private:
    Preferences preferences;
    bool initialized;

    /**
     * @brief Get NVS key for log entry
     * Format: "log_<timestamp>_<head>"
     * @param hourTimestamp Hour timestamp
     * @param head Head index
     * @return NVS key string
     */
    String getLogKey(uint32_t hourTimestamp, uint8_t head);

    /**
     * @brief Round timestamp to hour boundary
     * @param timestamp Unix epoch time
     * @return Timestamp rounded down to nearest hour
     */
    uint32_t roundToHour(uint32_t timestamp);

    /**
     * @brief Update the rolling log index
     * @return New index value
     */
    uint16_t incrementLogIndex();
};

#endif // DOSING_LOG_STORE_H
