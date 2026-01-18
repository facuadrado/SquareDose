#include "logs/DosingLogManager.h"

DosingLogManager::DosingLogManager() : mutex(nullptr), initialized(false) {
}

DosingLogManager::~DosingLogManager() {
    if (mutex != nullptr) {
        vSemaphoreDelete(mutex);
    }
}

void DosingLogManager::initMutex() {
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr) {
        Serial.println("[DosingLogManager] CRITICAL: Failed to create mutex!");
    }
}

bool DosingLogManager::begin() {
    if (initialized) {
        return true;
    }

    if (mutex == nullptr) {
        Serial.println("[DosingLogManager] Error: Mutex not initialized. Call initMutex() first.");
        return false;
    }

    // Initialize the storage layer
    if (!store.begin()) {
        Serial.println("[DosingLogManager] Failed to initialize DosingLogStore");
        return false;
    }

    initialized = true;
    Serial.println("[DosingLogManager] Initialized successfully");
    return true;
}

uint32_t DosingLogManager::roundToHour(uint32_t timestamp) {
    // Round down to nearest hour by removing the "seconds within the hour" portion
    // Example: 1768621234 (3:27:14 PM) → 1768618800 (3:00:00 PM)
    return timestamp - (timestamp % 3600);
}

uint32_t DosingLogManager::getStartOfDay(uint32_t timestamp) {
    // Round down to start of day (midnight) by removing "seconds within the day" portion
    // Example: 1768621234 (3:27:14 PM) → 1768608000 (12:00:00 AM same day)
    return timestamp - (timestamp % 86400);
}

bool DosingLogManager::logDoseInternal(uint8_t head, float scheduledVolume, float adhocVolume, uint32_t timestamp) {
    // Internal method - caller must hold mutex

    if (head >= NUM_DOSING_HEADS) {
        Serial.printf("[DosingLogManager] Invalid head index: %d\n", head);
        return false;
    }

    // Skip logging if time is invalid (before year 2020)
    // This allows schedules to work without NTP, but logs only when time is valid
    if (timestamp < 1577836800) {  // Jan 1, 2020
        Serial.println("[DosingLogManager] Skipping log - time not synced (NTP or manual sync required)");
        return false;  // Not an error, just skipping
    }

    // Round timestamp to hour
    uint32_t hourTimestamp = roundToHour(timestamp);

    // Create log entry
    HourlyDoseLog log;
    log.hourTimestamp = hourTimestamp;
    log.head = head;
    log.scheduledVolume = scheduledVolume;
    log.adhocVolume = adhocVolume;

    // Save to store (store will merge with existing entry if it exists)
    bool success = store.saveLog(log);

    if (success) {
        Serial.printf("[DosingLogManager] Logged dose: head=%d, scheduled=%.2f mL, adhoc=%.2f mL, hour=%lu\n",
                     head, scheduledVolume, adhocVolume, hourTimestamp);
    } else {
        Serial.printf("[DosingLogManager] Failed to log dose for head %d\n", head);
    }

    return success;
}

bool DosingLogManager::logScheduledDose(uint8_t head, float volume, uint32_t timestamp) {
    if (!initialized) {
        Serial.println("[DosingLogManager] Not initialized");
        return false;
    }

    // Thread-safe: Lock before logging
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        bool success = logDoseInternal(head, volume, 0.0f, timestamp);
        xSemaphoreGive(mutex);
        return success;
    }

    Serial.println("[DosingLogManager] Failed to acquire mutex");
    return false;
}

bool DosingLogManager::logAdhocDose(uint8_t head, float volume, uint32_t timestamp) {
    if (!initialized) {
        Serial.println("[DosingLogManager] Not initialized");
        return false;
    }

    // Thread-safe: Lock before logging
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        bool success = logDoseInternal(head, 0.0f, volume, timestamp);
        xSemaphoreGive(mutex);
        return success;
    }

    Serial.println("[DosingLogManager] Failed to acquire mutex");
    return false;
}

bool DosingLogManager::getDailySummary(uint8_t head, uint32_t currentTime, float dailyTarget,
                                       uint16_t dosesPerDay, float perDoseVolume, DailySummary& summary) {
    if (!initialized) {
        Serial.println("[DosingLogManager] Not initialized");
        return false;
    }

    if (head >= NUM_DOSING_HEADS) {
        Serial.printf("[DosingLogManager] Invalid head index: %d\n", head);
        return false;
    }

    // Initialize summary
    summary.head = head;
    summary.dailyTarget = dailyTarget;
    summary.dosesPerDay = dosesPerDay;
    summary.perDoseVolume = perDoseVolume;
    summary.scheduledActual = 0.0f;
    summary.adhocTotal = 0.0f;

    // Calculate start of today
    uint32_t startOfDay = getStartOfDay(currentTime);
    uint32_t endOfDay = startOfDay + 86400 - 1;  // 23:59:59 today

    // Thread-safe: Lock before reading logs
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // Load all logs for today
        HourlyDoseLog logs[24];  // Maximum 24 hours in a day
        uint16_t count = store.loadLogsInRange(startOfDay, endOfDay, logs, 24);

        // Sum up volumes for this head
        for (uint16_t i = 0; i < count; i++) {
            if (logs[i].head == head) {
                summary.scheduledActual += logs[i].scheduledVolume;
                summary.adhocTotal += logs[i].adhocVolume;
            }
        }

        xSemaphoreGive(mutex);

        Serial.printf("[DosingLogManager] Daily summary for head %d: scheduled=%.2f/%.2f mL, adhoc=%.2f mL\n",
                     head, summary.scheduledActual, summary.dailyTarget, summary.adhocTotal);
        return true;
    }

    Serial.println("[DosingLogManager] Failed to acquire mutex");
    return false;
}

uint8_t DosingLogManager::getAllDailySummaries(uint32_t currentTime, Schedule* schedules, DailySummary* summaries) {
    if (!initialized || summaries == nullptr) {
        Serial.println("[DosingLogManager] Not initialized or null summaries array");
        return 0;
    }

    uint8_t count = 0;

    // Generate summary for each head
    for (uint8_t head = 0; head < NUM_DOSING_HEADS; head++) {
        DailySummary summary;

        // Get schedule data for this head (if available)
        float dailyTarget = 0.0f;
        uint16_t dosesPerDay = 0;
        float perDoseVolume = 0.0f;

        if (schedules != nullptr && schedules[head].head == head && schedules[head].enabled) {
            dailyTarget = schedules[head].dailyTargetVolume;
            dosesPerDay = schedules[head].dosesPerDay;
            perDoseVolume = schedules[head].volume;
        }

        // Get daily summary
        if (getDailySummary(head, currentTime, dailyTarget, dosesPerDay, perDoseVolume, summary)) {
            summaries[count] = summary;
            count++;
        }
    }

    Serial.printf("[DosingLogManager] Generated %d daily summaries\n", count);
    return count;
}

uint16_t DosingLogManager::getHourlyLogs(uint32_t startTime, uint32_t endTime, HourlyDoseLog* logs, uint16_t maxLogs) {
    if (!initialized || logs == nullptr) {
        Serial.println("[DosingLogManager] Not initialized or null logs array");
        return 0;
    }

    // Thread-safe: Lock before reading logs
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        uint16_t count = store.loadLogsInRange(startTime, endTime, logs, maxLogs);
        xSemaphoreGive(mutex);

        Serial.printf("[DosingLogManager] Retrieved %d hourly logs\n", count);
        return count;
    }

    Serial.println("[DosingLogManager] Failed to acquire mutex");
    return 0;
}

uint16_t DosingLogManager::pruneOldLogs(uint32_t currentTime) {
    if (!initialized) {
        Serial.println("[DosingLogManager] Not initialized");
        return 0;
    }

    // Thread-safe: Lock before pruning
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        uint16_t count = store.pruneOldLogs(currentTime);
        xSemaphoreGive(mutex);

        Serial.printf("[DosingLogManager] Pruned %d old logs\n", count);
        return count;
    }

    Serial.println("[DosingLogManager] Failed to acquire mutex");
    return 0;
}

uint16_t DosingLogManager::getLogCount() {
    if (!initialized) {
        return 0;
    }

    // Thread-safe: Lock before reading count
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        uint16_t count = store.getLogCount();
        xSemaphoreGive(mutex);
        return count;
    }

    return 0;
}

bool DosingLogManager::clearAll() {
    if (!initialized) {
        Serial.println("[DosingLogManager] Not initialized");
        return false;
    }

    // Thread-safe: Lock before clearing
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        bool success = store.clearAll();
        xSemaphoreGive(mutex);
        return success;
    }

    Serial.println("[DosingLogManager] Failed to acquire mutex");
    return false;
}
