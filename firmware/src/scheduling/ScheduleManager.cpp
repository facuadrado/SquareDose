#include "scheduling/ScheduleManager.h"
#include "logs/DosingLogManager.h"

ScheduleManager::ScheduleManager() : mutex(nullptr), initialized(false), logManager(nullptr) {
    // Initialize cache validity flags to false
    for (uint8_t i = 0; i < NUM_SCHEDULE_HEADS; i++) {
        cacheValid[i] = false;
    }
}

ScheduleManager::~ScheduleManager() {
    if (mutex != nullptr) {
        vSemaphoreDelete(mutex);
    }
}

void ScheduleManager::initMutex() {
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr) {
        Serial.println("[ScheduleManager] CRITICAL: Failed to create mutex!");
    }
}

bool ScheduleManager::begin() {
    if (initialized) {
        return true;
    }

    if (mutex == nullptr) {
        Serial.println("[ScheduleManager] Error: Mutex not initialized. Call initMutex() first.");
        return false;
    }

    // Initialize the storage layer
    if (!store.begin()) {
        Serial.println("[ScheduleManager] Failed to initialize ScheduleStore");
        return false;
    }

    // Load all schedules from NVS into cache
    reloadCache();

    initialized = true;
    Serial.println("[ScheduleManager] Initialized successfully");
    return true;
}

bool ScheduleManager::setSchedule(const Schedule& sched) {
    if (!initialized) {
        Serial.println("[ScheduleManager] Not initialized");
        return false;
    }

    if (sched.head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleManager] Invalid head index: %d\n", sched.head);
        return false;
    }

    // Thread-safe: Lock before modifying schedule
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // Save to NVS
        bool success = store.saveSchedule(sched);

        if (success) {
            // Update cache
            scheduleCache[sched.head] = sched;
            cacheValid[sched.head] = true;
            Serial.printf("[ScheduleManager] Schedule saved for head %d\n", sched.head);
        } else {
            Serial.printf("[ScheduleManager] Failed to save schedule for head %d\n", sched.head);
        }

        xSemaphoreGive(mutex);
        return success;
    }

    Serial.println("[ScheduleManager] Failed to acquire mutex");
    return false;
}

bool ScheduleManager::getSchedule(uint8_t head, Schedule& sched) {
    if (!initialized) {
        Serial.println("[ScheduleManager] Not initialized");
        return false;
    }

    if (head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleManager] Invalid head index: %d\n", head);
        return false;
    }

    // Thread-safe: Lock before reading cache
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        bool success = false;

        if (cacheValid[head]) {
            sched = scheduleCache[head];
            success = true;
        }

        xSemaphoreGive(mutex);
        return success;
    }

    Serial.println("[ScheduleManager] Failed to acquire mutex");
    return false;
}

bool ScheduleManager::deleteSchedule(uint8_t head) {
    if (!initialized) {
        Serial.println("[ScheduleManager] Not initialized");
        return false;
    }

    if (head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleManager] Invalid head index: %d\n", head);
        return false;
    }

    // Thread-safe: Lock before deleting schedule
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // Delete from NVS
        bool success = store.deleteSchedule(head);

        if (success) {
            // Invalidate cache entry
            cacheValid[head] = false;
            Serial.printf("[ScheduleManager] Schedule deleted for head %d\n", head);
        } else {
            Serial.printf("[ScheduleManager] Failed to delete schedule for head %d\n", head);
        }

        xSemaphoreGive(mutex);
        return success;
    }

    Serial.println("[ScheduleManager] Failed to acquire mutex");
    return false;
}

uint8_t ScheduleManager::getAllSchedules(Schedule* schedules) {
    if (!initialized || schedules == nullptr) {
        Serial.println("[ScheduleManager] Not initialized or null schedules array");
        return 0;
    }

    uint8_t count = 0;

    // Thread-safe: Lock before reading cache
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        for (uint8_t head = 0; head < NUM_SCHEDULE_HEADS; head++) {
            if (cacheValid[head] && scheduleCache[head].enabled) {
                schedules[count] = scheduleCache[head];
                count++;
            }
        }

        xSemaphoreGive(mutex);
    } else {
        Serial.println("[ScheduleManager] Failed to acquire mutex");
    }

    return count;
}

void ScheduleManager::checkAndExecute(uint32_t currentTime, DosingHead** dosingHeads) {
    if (!initialized || dosingHeads == nullptr) {
        return;
    }

    // Thread-safe: Lock before checking schedules
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        for (uint8_t head = 0; head < NUM_SCHEDULE_HEADS; head++) {
            if (cacheValid[head] && scheduleCache[head].enabled) {
                Schedule sched = scheduleCache[head]; // Make a COPY

                if (sched.shouldExecute(currentTime)) {
                    // CRITICAL: Release mutex before blocking operation!
                    xSemaphoreGive(mutex);

                    // Execute dose (this is a BLOCKING operation that takes seconds)
                    executeSchedule(sched, dosingHeads, currentTime);

                    // Reacquire mutex to continue loop
                    xSemaphoreTake(mutex, portMAX_DELAY);
                }
            }
        }

        xSemaphoreGive(mutex);
    }
}

void ScheduleManager::updateLastExecution(uint8_t head, uint32_t executionTime) {
    if (!initialized || head >= NUM_SCHEDULE_HEADS) {
        return;
    }

    // Thread-safe: Lock before updating schedule
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (cacheValid[head]) {
            scheduleCache[head].lastExecutionTime = executionTime;
            scheduleCache[head].executionCount++;
            scheduleCache[head].updatedAt = executionTime;

            // Save updated schedule to NVS
            store.saveSchedule(scheduleCache[head]);

            Serial.printf("[ScheduleManager] Updated last execution for head %d: time=%lu, count=%lu\n",
                         head, executionTime, scheduleCache[head].executionCount);
        }

        xSemaphoreGive(mutex);
    }
}

void ScheduleManager::reloadCache() {
    Serial.println("[ScheduleManager] Reloading schedule cache from NVS...");

    for (uint8_t head = 0; head < NUM_SCHEDULE_HEADS; head++) {
        Schedule sched;
        if (store.loadSchedule(head, sched)) {
            scheduleCache[head] = sched;
            cacheValid[head] = true;
            Serial.printf("[ScheduleManager] Loaded schedule for head %d into cache\n", head);
        } else {
            cacheValid[head] = false;
        }
    }

    Serial.println("[ScheduleManager] Cache reload complete");
}

void ScheduleManager::executeSchedule(Schedule& sched, DosingHead** dosingHeads, uint32_t currentTime) {
    if (sched.head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleManager] Invalid head index in schedule: %d\n", sched.head);
        return;
    }

    DosingHead* head = dosingHeads[sched.head];
    if (head == nullptr) {
        Serial.printf("[ScheduleManager] Null dosing head pointer for head %d\n", sched.head);
        return;
    }

    Serial.printf("[ScheduleManager] Starting scheduled dose: Head %d, Volume %.2f mL\n",
                 sched.head, sched.volume);

    // Execute the dose (blocking operation)
    DosingResult result = head->dispense(sched.volume);

    if (result.success) {
        Serial.printf("[ScheduleManager] Scheduled dose complete: Head %d, Volume %.2f mL, Runtime %lu ms\n",
                     sched.head, result.estimatedVolume, result.actualRuntime);

        // Log scheduled dose if log manager is configured
        if (logManager != nullptr) {
            logManager->logScheduledDose(sched.head, result.estimatedVolume, currentTime);
        }

        // Update last execution time with the SAME time used for checking
        updateLastExecution(sched.head, currentTime);
    } else {
        Serial.printf("[ScheduleManager] Scheduled dose failed: Head %d, Error: %s\n",
                     sched.head, result.errorMessage.c_str());
    }
}

void ScheduleManager::setLogManager(DosingLogManager* logMgr) {
    logManager = logMgr;
    if (logManager != nullptr) {
        Serial.println("[ScheduleManager] Log manager configured");
    }
}
