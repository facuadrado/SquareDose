#ifndef SCHEDULER_TASK_H
#define SCHEDULER_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "scheduling/ScheduleManager.h"
#include "hal/DosingHead.h"
#include <time.h>

#define SCHEDULER_CHECK_INTERVAL_MS 1000  // Check schedules every 1 second

/**
 * @brief FreeRTOS task that checks and executes schedules
 *
 * Runs every second, checking if any schedules are due for execution
 * Coordinates with ScheduleManager for thread-safe schedule access
 */
class SchedulerTask {
public:
    SchedulerTask();
    ~SchedulerTask();

    /**
     * @brief Initialize the scheduler task
     * @param manager Pointer to ScheduleManager
     * @param heads Array of dosing head pointers
     * @param numHeads Number of dosing heads (should be 4)
     * @return true if initialization successful
     */
    bool begin(ScheduleManager* manager, DosingHead** heads, uint8_t numHeads);

    /**
     * @brief Start the FreeRTOS scheduler task
     * @return true if task started successfully
     */
    bool start();

    /**
     * @brief Stop the scheduler task
     */
    void stop();

    /**
     * @brief Check if task is running
     * @return true if running
     */
    bool isRunning() const;

    /**
     * @brief FreeRTOS task function (static wrapper)
     */
    static void taskFunction(void* parameters);

private:
    ScheduleManager* scheduleManager;
    DosingHead** dosingHeads;
    uint8_t numHeads;
    TaskHandle_t taskHandle;
    bool running;

    /**
     * @brief Get current Unix epoch time
     * @return Current time in seconds since epoch, or 0 if not available
     */
    uint32_t getCurrentTime();

    /**
     * @brief Main task loop
     */
    void run();
};

#endif // SCHEDULER_TASK_H
