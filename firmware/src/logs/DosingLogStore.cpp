#include "logs/DosingLogStore.h"

DosingLogStore::DosingLogStore() : initialized(false) {
}

DosingLogStore::~DosingLogStore() {
    if (initialized) {
        preferences.end();
    }
}

bool DosingLogStore::begin() {
    if (initialized) {
        return true;
    }

    initialized = true;
    Serial.println("[DosingLogStore] Initialized");
    return true;
}

uint32_t DosingLogStore::roundToHour(uint32_t timestamp) {
    // Round down to nearest hour (3600 seconds)
    return (timestamp / 3600) * 3600;
}

String DosingLogStore::getLogKey(uint32_t hourTimestamp, uint8_t head) {
    // NVS key limit: 15 characters
    // Use base offset from Jan 1, 2025 to shorten keys
    // Base: 1735689600 (Jan 1, 2025 00:00:00 UTC)
    // Format: "h<offset>_<head>" where offset is in hours
    // Example: "h12345_0" for head 0

    const uint32_t BASE_TIME = 1735689600;  // Jan 1, 2025
    uint32_t hourOffset = (hourTimestamp - BASE_TIME) / 3600;

    return "h" + String(hourOffset) + "_" + String(head);
}

bool DosingLogStore::saveLog(const HourlyDoseLog& log) {
    if (!initialized) {
        Serial.println("[DosingLogStore] Not initialized");
        return false;
    }

    if (!log.isValid()) {
        Serial.println("[DosingLogStore] Invalid log entry");
        return false;
    }

    // Open NVS for writing
    if (!preferences.begin(LOG_NVS_NAMESPACE, false)) {
        Serial.println("[DosingLogStore] Failed to open NVS for writing");
        return false;
    }

    String key = getLogKey(log.hourTimestamp, log.head);

    // Check if log already exists for this hour+head
    HourlyDoseLog existingLog;
    size_t read = preferences.getBytes(key.c_str(), &existingLog, sizeof(HourlyDoseLog));

    HourlyDoseLog updatedLog = log;

    if (read == sizeof(HourlyDoseLog)) {
        // Log exists, add to existing volumes
        updatedLog.scheduledVolume += existingLog.scheduledVolume;
        updatedLog.adhocVolume += existingLog.adhocVolume;
        Serial.printf("[DosingLogStore] Updating existing log for hour %lu, head %d\n",
                     log.hourTimestamp, log.head);
    } else {
        // New log entry
        Serial.printf("[DosingLogStore] Creating new log for hour %lu, head %d\n",
                     log.hourTimestamp, log.head);
    }

    // Write log as blob
    size_t written = preferences.putBytes(key.c_str(), &updatedLog, sizeof(HourlyDoseLog));

    // Update log count if this is a new entry
    if (read != sizeof(HourlyDoseLog)) {
        uint16_t count = preferences.getUShort(LOG_COUNT_KEY, 0);
        preferences.putUShort(LOG_COUNT_KEY, count + 1);
    }

    preferences.end();

    if (written != sizeof(HourlyDoseLog)) {
        Serial.printf("[DosingLogStore] Failed to write log\n");
        return false;
    }

    Serial.printf("[DosingLogStore] Saved log: %s\n", updatedLog.toString().c_str());
    return true;
}

bool DosingLogStore::loadLog(uint32_t hourTimestamp, uint8_t head, HourlyDoseLog& log) {
    if (!initialized) {
        Serial.println("[DosingLogStore] Not initialized");
        return false;
    }

    if (head >= NUM_DOSING_HEADS) {
        Serial.printf("[DosingLogStore] Invalid head index: %d\n", head);
        return false;
    }

    // Round timestamp to hour
    uint32_t roundedTime = roundToHour(hourTimestamp);

    // Open NVS for reading
    if (!preferences.begin(LOG_NVS_NAMESPACE, true)) {
        Serial.println("[DosingLogStore] Failed to open NVS for reading");
        return false;
    }

    String key = getLogKey(roundedTime, head);

    // Check if key exists first (avoids error logs)
    if (!preferences.isKey(key.c_str())) {
        preferences.end();
        return false;
    }

    // Read log as blob
    size_t read = preferences.getBytes(key.c_str(), &log, sizeof(HourlyDoseLog));

    preferences.end();

    if (read != sizeof(HourlyDoseLog)) {
        // Read failed
        return false;
    }

    return true;
}

uint16_t DosingLogStore::loadLogsInRange(uint32_t startTime, uint32_t endTime, HourlyDoseLog* logs, uint16_t maxLogs) {
    if (!initialized || logs == nullptr) {
        Serial.println("[DosingLogStore] Not initialized or null logs array");
        return 0;
    }

    // Round times to hour boundaries
    uint32_t startHour = roundToHour(startTime);
    uint32_t endHour = roundToHour(endTime);

    uint16_t count = 0;

    // Iterate through each hour in range
    for (uint32_t hour = startHour; hour <= endHour && count < maxLogs; hour += 3600) {
        // Check all heads for this hour
        for (uint8_t head = 0; head < NUM_DOSING_HEADS && count < maxLogs; head++) {
            HourlyDoseLog log;
            if (loadLog(hour, head, log)) {
                logs[count] = log;
                count++;
            }
        }
    }

    Serial.printf("[DosingLogStore] Loaded %d logs in range %lu to %lu\n", count, startTime, endTime);
    return count;
}

uint16_t DosingLogStore::loadLogsForHead(uint8_t head, HourlyDoseLog* logs, uint16_t maxLogs) {
    if (!initialized || logs == nullptr) {
        Serial.println("[DosingLogStore] Not initialized or null logs array");
        return 0;
    }

    if (head >= NUM_DOSING_HEADS) {
        Serial.printf("[DosingLogStore] Invalid head index: %d\n", head);
        return 0;
    }

    // Open NVS for reading
    if (!preferences.begin(LOG_NVS_NAMESPACE, true)) {
        Serial.println("[DosingLogStore] Failed to open NVS for reading");
        return 0;
    }

    uint16_t count = 0;

    // Note: This is a simple implementation that iterates through possible keys
    // In production, you might want to maintain an index of log keys for efficiency
    // For now, we'll rely on the loadLogsInRange method for querying

    preferences.end();

    Serial.printf("[DosingLogStore] loadLogsForHead not fully implemented - use loadLogsInRange instead\n");
    return count;
}

uint16_t DosingLogStore::pruneOldLogs(uint32_t currentTime) {
    if (!initialized) {
        Serial.println("[DosingLogStore] Not initialized");
        return 0;
    }

    // Calculate cutoff time (14 days ago)
    uint32_t cutoffTime = currentTime - (LOG_RETENTION_HOURS * 3600);
    uint32_t cutoffHour = roundToHour(cutoffTime);

    // Open NVS for writing
    if (!preferences.begin(LOG_NVS_NAMESPACE, false)) {
        Serial.println("[DosingLogStore] Failed to open NVS for writing");
        return 0;
    }

    uint16_t deletedCount = 0;

    // Iterate through potential old logs
    // We'll check the last 30 days worth of hourly slots to find old entries
    for (uint32_t hour = cutoffHour - (30 * 24 * 3600); hour < cutoffHour; hour += 3600) {
        for (uint8_t head = 0; head < NUM_DOSING_HEADS; head++) {
            String key = getLogKey(hour, head);
            if (preferences.remove(key.c_str())) {
                deletedCount++;
            }
        }
    }

    // Update log count
    if (deletedCount > 0) {
        uint16_t count = preferences.getUShort(LOG_COUNT_KEY, 0);
        if (count >= deletedCount) {
            preferences.putUShort(LOG_COUNT_KEY, count - deletedCount);
        } else {
            preferences.putUShort(LOG_COUNT_KEY, 0);
        }
    }

    preferences.end();

    Serial.printf("[DosingLogStore] Pruned %d old logs (cutoff: %lu)\n", deletedCount, cutoffTime);
    return deletedCount;
}

bool DosingLogStore::clearAll() {
    if (!initialized) {
        Serial.println("[DosingLogStore] Not initialized");
        return false;
    }

    // Open NVS for writing
    if (!preferences.begin(LOG_NVS_NAMESPACE, false)) {
        Serial.println("[DosingLogStore] Failed to open NVS for writing");
        return false;
    }

    // Clear entire namespace
    bool success = preferences.clear();

    preferences.end();

    if (success) {
        Serial.println("[DosingLogStore] Cleared all logs");
    } else {
        Serial.println("[DosingLogStore] Failed to clear logs");
    }

    return success;
}

uint16_t DosingLogStore::getLogCount() {
    if (!initialized) {
        return 0;
    }

    // Open NVS for reading
    if (!preferences.begin(LOG_NVS_NAMESPACE, true)) {
        return 0;
    }

    uint16_t count = preferences.getUShort(LOG_COUNT_KEY, 0);

    preferences.end();

    return count;
}

uint16_t DosingLogStore::incrementLogIndex() {
    if (!initialized) {
        return 0;
    }

    // Open NVS for writing
    if (!preferences.begin(LOG_NVS_NAMESPACE, false)) {
        return 0;
    }

    uint16_t index = preferences.getUShort(LOG_INDEX_KEY, 0);
    index = (index + 1) % MAX_LOG_ENTRIES;  // Circular buffer
    preferences.putUShort(LOG_INDEX_KEY, index);

    preferences.end();

    return index;
}
