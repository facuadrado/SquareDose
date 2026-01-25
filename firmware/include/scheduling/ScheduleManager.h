#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include "scheduling/Schedule.h"
#include "scheduling/ScheduleStore.h"
#include "hal/DosingHead.h"

// Forward declaration to avoid circular dependency
class DosingLogManager;

/**
 * @brief High-level schedule management with thread safety
 *
 * Provides CRUD operations for schedules with FreeRTOS mutex protection
 * Coordinates between REST API handlers and Scheduler task
 */
class ScheduleManager {
public:
    ScheduleManager();
    ~ScheduleManager();

    /**
     * @brief Initialize the schedule manager
     * Must be called before using any other methods
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Initialize mutex (must be called before begin())
     */
    void initMutex();

    /**
     * @brief Set or update a schedule for a head
     * @param sched Schedule to save
     * @return true if save successful
     */
    bool setSchedule(const Schedule& sched);

    /**
     * @brief Get a schedule for a specific head
     * @param head Head index (0-3)
     * @param sched Output schedule
     * @return true if schedule exists
     */
    bool getSchedule(uint8_t head, Schedule& sched);

    /**
     * @brief Delete a schedule for a specific head
     * @param head Head index (0-3)
     * @return true if delete successful
     */
    bool deleteSchedule(uint8_t head);

    /**
     * @brief Get all active schedules
     * @param schedules Output array (must be at least NUM_SCHEDULE_HEADS size)
     * @return Number of active schedules
     */
    uint8_t getAllSchedules(Schedule* schedules);

    /**
     * @brief Check schedules and execute any that are due
     * Called by SchedulerTask every second
     * @param currentTime Current Unix epoch time
     * @param dosingHeads Array of dosing head pointers
     */
    void checkAndExecute(uint32_t currentTime, DosingHead** dosingHeads);

    /**
     * @brief Update last execution time for a schedule
     * @param head Head index
     * @param executionTime Unix epoch time of execution
     */
    void updateLastExecution(uint8_t head, uint32_t executionTime);

    /**
     * @brief Set the dosing log manager for logging scheduled doses
     * @param logManager Pointer to DosingLogManager instance (optional)
     */
    void setLogManager(DosingLogManager* logManager);

private:
    ScheduleStore store;
    SemaphoreHandle_t mutex;
    bool initialized;
    DosingLogManager* logManager;  // Pointer to log manager for scheduled doses

    // In-memory cache of schedules for fast access
    Schedule scheduleCache[NUM_SCHEDULE_HEADS];
    bool cacheValid[NUM_SCHEDULE_HEADS];

    /**
     * @brief Reload schedule cache from NVS
     */
    void reloadCache();

    /**
     * @brief Execute a scheduled dose
     * @param sched Schedule to execute
     * @param dosingHeads Array of dosing head pointers
     * @param currentTime Current time (same as used for shouldExecute check)
     */
    void executeSchedule(Schedule& sched, DosingHead** dosingHeads, uint32_t currentTime);
};

#endif // SCHEDULE_MANAGER_H
