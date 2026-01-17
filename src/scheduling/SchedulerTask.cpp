#include "scheduling/SchedulerTask.h"

SchedulerTask::SchedulerTask()
    : scheduleManager(nullptr), dosingHeads(nullptr), numHeads(0),
      taskHandle(nullptr), running(false) {
}

SchedulerTask::~SchedulerTask() {
    stop();
}

bool SchedulerTask::begin(ScheduleManager* manager, DosingHead** heads, uint8_t numHeads) {
    if (manager == nullptr || heads == nullptr || numHeads == 0) {
        Serial.println("[SchedulerTask] Invalid parameters");
        return false;
    }

    scheduleManager = manager;
    dosingHeads = heads;
    this->numHeads = numHeads;

    Serial.println("[SchedulerTask] Initialized");
    return true;
}

bool SchedulerTask::start() {
    if (running) {
        Serial.println("[SchedulerTask] Already running");
        return true;
    }

    if (scheduleManager == nullptr || dosingHeads == nullptr) {
        Serial.println("[SchedulerTask] Not initialized - call begin() first");
        return false;
    }

    // Create FreeRTOS task
    BaseType_t result = xTaskCreate(
        taskFunction,           // Task function
        "SchedulerTask",        // Task name
        4096,                   // Stack size (bytes)
        this,                   // Parameters (this instance)
        2,                      // Priority (medium)
        &taskHandle             // Task handle
    );

    if (result != pdPASS) {
        Serial.println("[SchedulerTask] Failed to create task");
        return false;
    }

    running = true;
    Serial.println("[SchedulerTask] Started");
    return true;
}

void SchedulerTask::stop() {
    if (!running || taskHandle == nullptr) {
        return;
    }

    running = false;
    vTaskDelay(100 / portTICK_PERIOD_MS); // Give task time to exit

    vTaskDelete(taskHandle);
    taskHandle = nullptr;

    Serial.println("[SchedulerTask] Stopped");
}

bool SchedulerTask::isRunning() const {
    return running;
}

void SchedulerTask::taskFunction(void* parameters) {
    SchedulerTask* task = static_cast<SchedulerTask*>(parameters);
    if (task != nullptr) {
        task->run();
    }
}

void SchedulerTask::run() {
    Serial.println("[SchedulerTask] Task loop started");

    while (running) {
        // Get current time
        uint32_t currentTime = getCurrentTime();

        if (currentTime > 0) {
            // Check and execute schedules
            scheduleManager->checkAndExecute(currentTime, dosingHeads);
        } else {
            // Time not available yet (NTP not synced, etc.)
            // This is normal during startup
        }

        // Wait 1 second before next check
        vTaskDelay(SCHEDULER_CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
    }

    Serial.println("[SchedulerTask] Task loop exited");
}

uint32_t SchedulerTask::getCurrentTime() {
    // Try to get time from system time (if NTP is configured)
    time_t now;
    time(&now);

    // If time is not set (before year 2000), return 0
    if (now < 946684800) {  // Jan 1, 2000
        return 0;
    }

    return static_cast<uint32_t>(now);
}
