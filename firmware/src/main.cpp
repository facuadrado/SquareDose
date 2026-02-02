#include <Arduino.h>
#include "network/wifi_manager.h"
#include "network/BLEServer.h"
#include "hal/MotorDriver.h"
#include "hal/DosingHead.h"
#include "scheduling/ScheduleManager.h"
#include "scheduling/SchedulerTask.h"
#include "logs/DosingLogManager.h"
#include <time.h>

// NTP Configuration for New York (EST/EDT)
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define GMT_OFFSET_SEC -18000      // EST is UTC-5 hours = -18000 seconds
#define DAYLIGHT_OFFSET_SEC 3600   // Daylight saving time is +1 hour = 3600 seconds

// Motor driver instance
MotorDriver motorDriver;

// Dosing heads (all 4)
DosingHead dosingHead1(0, &motorDriver);
DosingHead dosingHead2(1, &motorDriver);
DosingHead dosingHead3(2, &motorDriver);
DosingHead dosingHead4(3, &motorDriver);

// Array of dosing head pointers for BLE server
DosingHead* dosingHeads[4] = {&dosingHead1, &dosingHead2, &dosingHead3, &dosingHead4};

// Schedule manager instance
ScheduleManager scheduleManager;

// Dosing log manager instance
DosingLogManager dosingLogManager;

// Scheduler task instance
SchedulerTask schedulerTask;

void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println("[Main] Starting SquareDose Smart Doser...");

  // Initialize Motor Driver
  Serial.println("[Main] Initializing Motor Driver...");
  if (motorDriver.begin()) {
    Serial.println("[Main] Motor Driver initialized successfully");
  } else {
    Serial.println("[Main] ERROR: Motor Driver initialization failed!");
  }

  // Initialize all Dosing Heads
  Serial.println("[Main] Initializing Dosing Heads...");
  for (uint8_t i = 0; i < 4; i++) {
    if (dosingHeads[i]->begin()) {
      CalibrationData cal = dosingHeads[i]->getCalibrationData();
      Serial.printf("[Main] Dosing Head %d initialized - Calibrated: %s, Rate: %.3f mL/s\n",
                   i, cal.isCalibrated ? "YES" : "NO", cal.mlPerSecond);
    } else {
      Serial.printf("[Main] ERROR: Dosing Head %d initialization failed!\n", i);
    }
  }

  Serial.println("[Main] Initializing WiFi Manager...");

  // Initialize mutex before using WiFiManager
  wifiManager.initMutex();
  wifiManager.begin();

  Serial.println("[Main] WiFi mode: " + String(wifiManager.getCurrentMode() == WIFIMANAGER_MODE_AP ? "AP" : "STA"));
  Serial.println("[Main] IP Address: " + wifiManager.getLocalIP());

  if (wifiManager.getCurrentMode() == WIFIMANAGER_MODE_AP) {
    Serial.println("[Main] AP SSID: " + wifiManager.getAPSSID());
    Serial.println("[Main] AP Password: " + String(AP_PASSWORD));
    Serial.println("[Main] Connect via BLE to configure WiFi");
  }

  xTaskCreatePinnedToCore(
    WiFiManager::keepAliveTask,    // Function that should be called
    "WiFiKeepAliveTask",           // Name of the task (for debugging)
    WIFI_TASK_STACK_SIZE,          // Stack size (bytes)
    NULL,                          // Parameter to pass
    WIFI_TASK_PRIORITY,            // Task priority
    NULL,                          // Task handle
    WIFI_TASK_CORE                 // Core to run the task on (0 or 1)
  );

  // Configure NTP for New York timezone
  Serial.println("[Main] Configuring NTP...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
  Serial.println("[Main] NTP configured (will sync when connected to WiFi)");

  // Initialize Dosing Log Manager
  Serial.println("[Main] Initializing Dosing Log Manager...");
  dosingLogManager.initMutex();
  if (dosingLogManager.begin()) {
    Serial.println("[Main] Dosing Log Manager initialized successfully");
  } else {
    Serial.println("[Main] ERROR: Dosing Log Manager initialization failed!");
  }

  // Initialize Schedule Manager
  Serial.println("[Main] Initializing Schedule Manager...");
  scheduleManager.initMutex();
  if (scheduleManager.begin()) {
    Serial.println("[Main] Schedule Manager initialized successfully");
  } else {
    Serial.println("[Main] ERROR: Schedule Manager initialization failed!");
  }

  // Connect log manager to schedule manager only
  // Note: DosingHead does NOT get log manager - WebServer handles ad-hoc dose logging
  Serial.println("[Main] Connecting Dosing Log Manager...");
  scheduleManager.setLogManager(&dosingLogManager);
  Serial.println("[Main] Dosing Log Manager connected to ScheduleManager");

  // Initialize Scheduler Task
  Serial.println("[Main] Initializing Scheduler Task...");
  if (schedulerTask.begin(&scheduleManager, dosingHeads, 4)) {
    if (schedulerTask.start()) {
      Serial.println("[Main] Scheduler Task started successfully");
    } else {
      Serial.println("[Main] ERROR: Scheduler Task failed to start!");
    }
  } else {
    Serial.println("[Main] ERROR: Scheduler Task initialization failed!");
  }

  // Initialize BLE Server
  Serial.println("[Main] Initializing BLE Server...");
  if (squareDoseBLE.begin(dosingHeads, 4, &motorDriver, &wifiManager, &scheduleManager, &dosingLogManager)) {
    Serial.println("[Main] BLE Server started successfully");
    Serial.println("[Main] BLE Name: " + wifiManager.getAPSSID());
  } else {
    Serial.println("[Main] ERROR: BLE Server initialization failed!");
  }

  Serial.println("[Main] Setup complete");
  Serial.println();
  Serial.println("========================================");
  Serial.println("  BLE Commands (JSON via Nordic UART):");
  Serial.println("  {\"cmd\":\"status\"}");
  Serial.println("  {\"cmd\":\"dose\",\"head\":0,\"volume\":5.0}");
  Serial.println("  {\"cmd\":\"calibrate\",\"head\":0,\"actualVolume\":8.5}");
  Serial.println("  {\"cmd\":\"emergency_stop\"}");
  Serial.println("  {\"cmd\":\"wifi_configure\",\"ssid\":\"...\",\"password\":\"...\"}");
  Serial.println("  {\"cmd\":\"schedules_get\"}");
  Serial.println("  {\"cmd\":\"schedule_set\",\"head\":0,...}");
  Serial.println("========================================");
}


void loop() {
  // Main loop - all work is handled by FreeRTOS tasks
  // Small delay to prevent watchdog trigger
  delay(100);
}