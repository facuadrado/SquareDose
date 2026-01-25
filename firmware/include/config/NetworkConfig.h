#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include <Arduino.h>
#include <WiFi.h>

// WiFi STA Mode Configuration
#define WIFI_STA_TIMEOUT_MS 20000
#define WIFI_STA_RETRY_INTERVAL_MS 60000
#define WIFI_STA_FAIL_THRESHOLD_MS 60000
#define WIFI_CHECK_INTERVAL_MS 10000

// WiFi AP Mode Configuration
#define AP_SSID_PREFIX "SquareDose-"
#define AP_PASSWORD "squaredose123"
#define AP_IP_ADDRESS IPAddress(192, 168, 4, 1)
#define AP_GATEWAY IPAddress(192, 168, 4, 1)
#define AP_SUBNET IPAddress(255, 255, 255, 0)

// NVS Storage Keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_SSID_KEY "ssid"
#define NVS_PASSWORD_KEY "password"

// FreeRTOS Task Configuration
#define WIFI_TASK_STACK_SIZE 5000
#define WIFI_TASK_PRIORITY 1
#define WIFI_TASK_CORE CONFIG_ARDUINO_RUNNING_CORE

#endif
