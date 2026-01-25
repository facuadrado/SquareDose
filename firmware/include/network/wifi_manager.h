#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config/NetworkConfig.h"

enum WifiManagerMode {
    WIFIMANAGER_MODE_AP,
    WIFIMANAGER_MODE_STA,
    WIFIMANAGER_MODE_TRANSITIONING
};

class WiFiManager {
public:
    WiFiManager();
    ~WiFiManager();

    void initMutex();

    bool begin();

    bool setCredentials(const char* ssid, const char* password);

    bool getCredentials(String& ssid, String& password);

    bool clearCredentials();

    bool switchToSTAMode();

    bool switchToAPMode();

    WifiManagerMode getCurrentMode();

    bool isConnected();

    String getLocalIP();

    String getAPSSID();

    static void keepAliveTask(void* parameters);

private:
    // Thread-safe mutex for shared state protection
    SemaphoreHandle_t stateMutex;

    // Shared state (protected by mutex)
    Preferences preferences;
    String currentSSID;
    String currentPassword;
    bool credentialsLoaded;
    WifiManagerMode currentMode;
    String apSSID;
    unsigned long staFailedTime;
    unsigned long lastSTAAttemptTime;

    // Helper: Check if duration has elapsed (handles millis overflow)
    bool hasElapsed(unsigned long startTime, unsigned long duration);

    bool loadCredentialsFromNVS();

    bool saveCredentialsToNVS(const char* ssid, const char* password);

    bool clearCredentialsFromNVS();

    bool connectToSTA();

    bool startAPMode();

    void stopCurrentMode();

    void generateAPSSID();
};

extern WiFiManager wifiManager;

#endif
