#include <Arduino.h>
#include "network/wifi_manager.h"

void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println("[Main] Starting SquareDose Smart Doser...");
  Serial.println("[Main] Initializing WiFi Manager...");

  // Initialize mutex before using WiFiManager
  wifiManager.initMutex();
  wifiManager.begin();

  Serial.println("[Main] WiFi mode: " + String(wifiManager.getCurrentMode() == WIFIMANAGER_MODE_AP ? "AP" : "STA"));
  Serial.println("[Main] IP Address: " + wifiManager.getLocalIP());

  if (wifiManager.getCurrentMode() == WIFIMANAGER_MODE_AP) {
    Serial.println("[Main] AP SSID: " + wifiManager.getAPSSID());
    Serial.println("[Main] AP Password: " + String(AP_PASSWORD));
    Serial.println("[Main] Connect to AP and configure WiFi via /api/wifi/configure");
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

  Serial.println("[Main] Setup complete");
}

void loop() {
  // put your main code here, to run repeatedly:
}