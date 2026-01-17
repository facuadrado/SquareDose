#ifndef SCHEDULE_STORE_H
#define SCHEDULE_STORE_H

#include <Arduino.h>
#include <Preferences.h>
#include "scheduling/Schedule.h"

#define NUM_SCHEDULE_HEADS 4
#define SCHEDULE_NVS_NAMESPACE "schedules"

/**
 * @brief NVS storage manager for schedules
 *
 * Stores exactly 4 schedules (one per dosing head)
 * Head index (0-3) is used as the schedule identifier
 */
class ScheduleStore {
public:
    ScheduleStore();
    ~ScheduleStore();

    /**
     * @brief Initialize the schedule store
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Save a schedule to NVS for a specific head
     * @param sched Schedule to save (sched.head determines which slot)
     * @return true if save successful
     */
    bool saveSchedule(const Schedule& sched);

    /**
     * @brief Load a schedule from NVS for a specific head
     * @param head Head index (0-3)
     * @param sched Output schedule
     * @return true if schedule found and loaded
     */
    bool loadSchedule(uint8_t head, Schedule& sched);

    /**
     * @brief Delete (disable) a schedule for a specific head
     * @param head Head index (0-3)
     * @return true if delete successful
     */
    bool deleteSchedule(uint8_t head);

    /**
     * @brief Load all schedules from NVS
     * @param schedules Output array (must be at least NUM_SCHEDULE_HEADS size)
     * @return Number of active schedules loaded
     */
    uint8_t loadAllSchedules(Schedule* schedules);

    /**
     * @brief Clear all schedules from NVS
     * @return true if clear successful
     */
    bool clearAll();

    /**
     * @brief Check if a schedule exists for a head
     * @param head Head index (0-3)
     * @return true if schedule exists and is enabled
     */
    bool hasSchedule(uint8_t head);

private:
    Preferences preferences;
    bool initialized;

    /**
     * @brief Get NVS key for schedule by head index
     * @param head Head index (0-3)
     * @return NVS key string
     */
    String getScheduleKey(uint8_t head);
};

#endif // SCHEDULE_STORE_H
