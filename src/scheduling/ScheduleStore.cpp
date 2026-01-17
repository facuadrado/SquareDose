#include "scheduling/ScheduleStore.h"

ScheduleStore::ScheduleStore() : initialized(false) {
}

ScheduleStore::~ScheduleStore() {
    if (initialized) {
        preferences.end();
    }
}

bool ScheduleStore::begin() {
    if (initialized) {
        return true;
    }

    initialized = true;
    Serial.println("[ScheduleStore] Initialized");
    return true;
}

String ScheduleStore::getScheduleKey(uint8_t head) {
    return "sched" + String(head);
}

bool ScheduleStore::saveSchedule(const Schedule& sched) {
    if (!initialized) {
        Serial.println("[ScheduleStore] Not initialized");
        return false;
    }

    if (sched.head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleStore] Invalid head index: %d\n", sched.head);
        return false;
    }

    // Validate schedule before saving
    ScheduleValidationResult validation = validateSchedule(sched);
    if (!validation.valid) {
        Serial.printf("[ScheduleStore] Validation failed: %s\n", validation.errorMessage.c_str());
        return false;
    }

    // Open NVS for writing
    if (!preferences.begin(SCHEDULE_NVS_NAMESPACE, false)) {
        Serial.println("[ScheduleStore] Failed to open NVS for writing");
        return false;
    }

    String key = getScheduleKey(sched.head);

    // Write schedule as blob
    size_t written = preferences.putBytes(key.c_str(), &sched, sizeof(Schedule));

    preferences.end();

    if (written != sizeof(Schedule)) {
        Serial.printf("[ScheduleStore] Failed to write schedule for head %d\n", sched.head);
        return false;
    }

    Serial.printf("[ScheduleStore] Saved schedule for head %d: %s\n",
                  sched.head, sched.toString().c_str());
    return true;
}

bool ScheduleStore::loadSchedule(uint8_t head, Schedule& sched) {
    if (!initialized) {
        Serial.println("[ScheduleStore] Not initialized");
        return false;
    }

    if (head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleStore] Invalid head index: %d\n", head);
        return false;
    }

    // Open NVS for reading
    if (!preferences.begin(SCHEDULE_NVS_NAMESPACE, true)) {
        Serial.println("[ScheduleStore] Failed to open NVS for reading");
        return false;
    }

    String key = getScheduleKey(head);

    // Read schedule as blob
    size_t read = preferences.getBytes(key.c_str(), &sched, sizeof(Schedule));

    preferences.end();

    if (read != sizeof(Schedule)) {
        // No schedule found for this head
        return false;
    }

    Serial.printf("[ScheduleStore] Loaded schedule for head %d: %s\n",
                  head, sched.toString().c_str());
    return true;
}

bool ScheduleStore::deleteSchedule(uint8_t head) {
    if (!initialized) {
        Serial.println("[ScheduleStore] Not initialized");
        return false;
    }

    if (head >= NUM_SCHEDULE_HEADS) {
        Serial.printf("[ScheduleStore] Invalid head index: %d\n", head);
        return false;
    }

    // Open NVS for writing
    if (!preferences.begin(SCHEDULE_NVS_NAMESPACE, false)) {
        Serial.println("[ScheduleStore] Failed to open NVS for writing");
        return false;
    }

    String key = getScheduleKey(head);

    // Remove the schedule entry
    bool success = preferences.remove(key.c_str());

    preferences.end();

    if (success) {
        Serial.printf("[ScheduleStore] Deleted schedule for head %d\n", head);
    } else {
        Serial.printf("[ScheduleStore] Failed to delete schedule for head %d\n", head);
    }

    return success;
}

uint8_t ScheduleStore::loadAllSchedules(Schedule* schedules) {
    if (!initialized || schedules == nullptr) {
        Serial.println("[ScheduleStore] Not initialized or null schedules array");
        return 0;
    }

    uint8_t count = 0;

    for (uint8_t head = 0; head < NUM_SCHEDULE_HEADS; head++) {
        Schedule sched;
        if (loadSchedule(head, sched) && sched.enabled) {
            schedules[count] = sched;
            count++;
        }
    }

    Serial.printf("[ScheduleStore] Loaded %d active schedules\n", count);
    return count;
}

bool ScheduleStore::clearAll() {
    if (!initialized) {
        Serial.println("[ScheduleStore] Not initialized");
        return false;
    }

    // Open NVS for writing
    if (!preferences.begin(SCHEDULE_NVS_NAMESPACE, false)) {
        Serial.println("[ScheduleStore] Failed to open NVS for writing");
        return false;
    }

    // Clear entire namespace
    bool success = preferences.clear();

    preferences.end();

    if (success) {
        Serial.println("[ScheduleStore] Cleared all schedules");
    } else {
        Serial.println("[ScheduleStore] Failed to clear schedules");
    }

    return success;
}

bool ScheduleStore::hasSchedule(uint8_t head) {
    if (!initialized || head >= NUM_SCHEDULE_HEADS) {
        return false;
    }

    Schedule sched;
    return loadSchedule(head, sched) && sched.enabled;
}
