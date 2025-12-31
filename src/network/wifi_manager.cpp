#include "network/wifi_manager.h"

WiFiManager wifiManager;

WiFiManager::WiFiManager() : stateMutex(nullptr), credentialsLoaded(false),
                              currentMode(WIFIMANAGER_MODE_AP),
                              staFailedTime(0), lastSTAAttemptTime(0) {
}

WiFiManager::~WiFiManager() {
    preferences.end();
    if (stateMutex != nullptr) {
        vSemaphoreDelete(stateMutex);
    }
}

void WiFiManager::initMutex() {
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == nullptr) {
        Serial.println("[WiFiManager] CRITICAL: Failed to create mutex!");
    }
}

bool WiFiManager::begin() {
    generateAPSSID();

    loadCredentialsFromNVS();

    if (credentialsLoaded) {
        Serial.println("[WiFiManager] Credentials found in NVS, attempting STA mode...");
        if (switchToSTAMode()) {
            Serial.println("[WiFiManager] Started in STA mode");
            return true;
        }
        Serial.println("[WiFiManager] STA mode failed, falling back to AP mode");
    } else {
        Serial.println("[WiFiManager] No credentials found, starting in AP mode");
    }

    switchToAPMode();
    return true;
}

bool WiFiManager::setCredentials(const char* ssid, const char* password) {
    if (ssid == nullptr || password == nullptr) {
        Serial.println("[WiFiManager] Invalid credentials");
        return false;
    }

    // Thread-safe: Lock before accessing shared state
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        if (!saveCredentialsToNVS(ssid, password)) {
            xSemaphoreGive(stateMutex);
            Serial.println("[WiFiManager] Failed to save credentials to NVS");
            return false;
        }

        currentSSID = String(ssid);
        currentPassword = String(password);
        credentialsLoaded = true;

        xSemaphoreGive(stateMutex);
        Serial.println("[WiFiManager] Credentials updated successfully");
        return true;
    }

    Serial.println("[WiFiManager] Failed to acquire mutex");
    return false;
}

bool WiFiManager::getCredentials(String& ssid, String& password) {
    // Thread-safe: Lock before reading shared state
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        if (!credentialsLoaded) {
            xSemaphoreGive(stateMutex);
            return false;
        }

        ssid = currentSSID;
        password = currentPassword;

        xSemaphoreGive(stateMutex);
        return true;
    }

    return false;
}

bool WiFiManager::switchToSTAMode() {
    // Thread-safe: Lock before mode switch
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        if (!credentialsLoaded) {
            xSemaphoreGive(stateMutex);
            Serial.println("[WiFiManager] Cannot switch to STA mode: no credentials");
            return false;
        }

        Serial.println("[WiFiManager] Switching to STA mode...");
        stopCurrentMode();

        currentMode = WIFIMANAGER_MODE_TRANSITIONING;

        if (connectToSTA()) {
            currentMode = WIFIMANAGER_MODE_STA;
            staFailedTime = 0;
            xSemaphoreGive(stateMutex);
            Serial.println("[WiFiManager] STA mode active - IP: " + WiFi.localIP().toString());
            return true;
        }

        Serial.println("[WiFiManager] Failed to connect to STA");
        staFailedTime = millis();
        xSemaphoreGive(stateMutex);
        return false;
    }

    return false;
}

bool WiFiManager::switchToAPMode() {
    // Thread-safe: Lock before mode switch
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        Serial.println("[WiFiManager] Switching to AP mode...");
        stopCurrentMode();

        currentMode = WIFIMANAGER_MODE_TRANSITIONING;

        if (startAPMode()) {
            currentMode = WIFIMANAGER_MODE_AP;
            xSemaphoreGive(stateMutex);
            Serial.println("[WiFiManager] AP mode active - SSID: " + apSSID + " - IP: " + WiFi.softAPIP().toString());
            return true;
        }

        Serial.println("[WiFiManager] Failed to start AP mode");
        xSemaphoreGive(stateMutex);
        return false;
    }

    return false;
}

WifiManagerMode WiFiManager::getCurrentMode() {
    // Thread-safe: Lock before reading mode
    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        WifiManagerMode mode = currentMode;
        xSemaphoreGive(stateMutex);
        return mode;
    }
    return WIFIMANAGER_MODE_AP; // Safe default
}

bool WiFiManager::isConnected() {
    if (currentMode == WIFIMANAGER_MODE_STA) {
        return WiFi.status() == WL_CONNECTED;
    } else if (currentMode == WIFIMANAGER_MODE_AP) {
        return WiFi.softAPgetStationNum() > 0;
    }
    return false;
}

String WiFiManager::getLocalIP() {
    if (currentMode == WIFIMANAGER_MODE_STA && WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (currentMode == WIFIMANAGER_MODE_AP) {
        return WiFi.softAPIP().toString();
    }
    return "No IP";
}

String WiFiManager::getAPSSID() {
    return apSSID;
}

void WiFiManager::keepAliveTask(void* parameters) {
    for (;;) {
        // Thread-safe: Read current mode with mutex
        WifiManagerMode mode;
        if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
            mode = wifiManager.currentMode;
            xSemaphoreGive(wifiManager.stateMutex);
        } else {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (mode == WIFIMANAGER_MODE_STA) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[WiFiManager] STA connected - IP: " + WiFi.localIP().toString());

                // Thread-safe: Reset fail time
                if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
                    wifiManager.staFailedTime = 0;
                    xSemaphoreGive(wifiManager.stateMutex);
                }

                vTaskDelay(WIFI_CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
                continue;
            }

            // Connection lost - track failure time
            unsigned long failTime;
            if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
                if (wifiManager.staFailedTime == 0) {
                    wifiManager.staFailedTime = millis();
                    Serial.println("[WiFiManager] STA connection lost");
                }
                failTime = wifiManager.staFailedTime;
                xSemaphoreGive(wifiManager.stateMutex);
            } else {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            // Overflow-safe: Check if failed for too long
            if (wifiManager.hasElapsed(failTime, WIFI_STA_FAIL_THRESHOLD_MS)) {
                Serial.println("[WiFiManager] STA failed for too long, switching to AP mode");
                wifiManager.switchToAPMode();

                // Thread-safe: Update last attempt time
                if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
                    wifiManager.lastSTAAttemptTime = millis();
                    xSemaphoreGive(wifiManager.stateMutex);
                }
                continue;
            }

            // Attempt reconnection
            Serial.println("[WiFiManager] Attempting to reconnect to STA...");
            if (wifiManager.connectToSTA()) {
                // Thread-safe: Update mode and reset fail time
                if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
                    wifiManager.currentMode = WIFIMANAGER_MODE_STA;
                    wifiManager.staFailedTime = 0;
                    xSemaphoreGive(wifiManager.stateMutex);
                }
                Serial.println("[WiFiManager] Reconnected to STA");
            }

            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (mode == WIFIMANAGER_MODE_AP) {
            String ssid;
            bool hasCredentials = false;
            unsigned long lastAttempt = 0;

            // Thread-safe: Read state
            if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
                ssid = wifiManager.apSSID;
                hasCredentials = wifiManager.credentialsLoaded;
                lastAttempt = wifiManager.lastSTAAttemptTime;
                xSemaphoreGive(wifiManager.stateMutex);
            }

            Serial.print("[WiFiManager] AP mode - SSID: " + ssid);
            Serial.println(" - Clients: " + String(WiFi.softAPgetStationNum()));

            // Overflow-safe: Check if time to retry STA
            if (hasCredentials && wifiManager.hasElapsed(lastAttempt, WIFI_STA_RETRY_INTERVAL_MS)) {
                Serial.println("[WiFiManager] Attempting to switch to STA mode...");
                if (wifiManager.switchToSTAMode()) {
                    Serial.println("[WiFiManager] Successfully switched to STA mode");
                } else {
                    // Thread-safe: Update last attempt time
                    if (xSemaphoreTake(wifiManager.stateMutex, portMAX_DELAY) == pdTRUE) {
                        wifiManager.lastSTAAttemptTime = millis();
                        xSemaphoreGive(wifiManager.stateMutex);
                    }
                    Serial.println("[WiFiManager] STA connection failed, staying in AP mode");
                }
            }

            vTaskDelay(WIFI_CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
            continue;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

bool WiFiManager::loadCredentialsFromNVS() {
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        Serial.println("[WiFiManager] Failed to open NVS namespace");
        return false;
    }

    currentSSID = preferences.getString(NVS_SSID_KEY, "");
    currentPassword = preferences.getString(NVS_PASSWORD_KEY, "");

    preferences.end();

    if (currentSSID.length() == 0 || currentPassword.length() == 0) {
        Serial.println("[WiFiManager] No valid credentials in NVS");
        credentialsLoaded = false;
        return false;
    }

    credentialsLoaded = true;
    return true;
}

bool WiFiManager::saveCredentialsToNVS(const char* ssid, const char* password) {
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        Serial.println("[WiFiManager] Failed to open NVS namespace for writing");
        return false;
    }

    bool success = true;

    if (preferences.putString(NVS_SSID_KEY, ssid) == 0) {
        Serial.println("[WiFiManager] Failed to write SSID to NVS");
        success = false;
    }

    if (preferences.putString(NVS_PASSWORD_KEY, password) == 0) {
        Serial.println("[WiFiManager] Failed to write password to NVS");
        success = false;
    }

    preferences.end();

    return success;
}

bool WiFiManager::connectToSTA() {
    if (!credentialsLoaded) {
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(currentSSID.c_str(), currentPassword.c_str());

    Serial.println("[WiFiManager] Connecting to WiFi: " + currentSSID);

    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttemptTime < WIFI_STA_TIMEOUT_MS) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFiManager] Failed to connect to STA");
        return false;
    }

    Serial.println("[WiFiManager] Connected to STA - IP: " + WiFi.localIP().toString());
    return true;
}

bool WiFiManager::startAPMode() {
    WiFi.mode(WIFI_AP);

    if (!WiFi.softAPConfig(AP_IP_ADDRESS, AP_GATEWAY, AP_SUBNET)) {
        Serial.println("[WiFiManager] Failed to configure AP IP");
        return false;
    }

    if (!WiFi.softAP(apSSID.c_str(), AP_PASSWORD)) {
        Serial.println("[WiFiManager] Failed to start AP");
        return false;
    }

    Serial.println("[WiFiManager] AP started - SSID: " + apSSID);
    Serial.println("[WiFiManager] AP IP: " + WiFi.softAPIP().toString());
    return true;
}

void WiFiManager::stopCurrentMode() {
    if (currentMode == WIFIMANAGER_MODE_STA) {
        WiFi.disconnect(true);
        Serial.println("[WiFiManager] STA mode stopped");
    } else if (currentMode == WIFIMANAGER_MODE_AP) {
        WiFi.softAPdisconnect(true);
        Serial.println("[WiFiManager] AP mode stopped");
    }

    WiFi.mode(WIFI_OFF);
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

void WiFiManager::generateAPSSID() {
    uint64_t chipid = ESP.getEfuseMac();
    uint16_t chip = (uint16_t)(chipid >> 32);
    apSSID = String(AP_SSID_PREFIX) + String(chip, HEX);
    apSSID.toUpperCase();
}

bool WiFiManager::hasElapsed(unsigned long startTime, unsigned long duration) {
    // Handles millis() overflow correctly
    // Works because unsigned arithmetic wraps around
    return (millis() - startTime) >= duration;
}
