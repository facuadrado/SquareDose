#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "hal/DosingHead.h"
#include "hal/MotorDriver.h"
#include "network/wifi_manager.h"
#include "scheduling/ScheduleManager.h"

// Forward declaration to avoid circular dependency
class DosingLogManager;

/**
 * @brief AsyncWebServer wrapper for SquareDose REST API and WebSocket
 *
 * Provides REST endpoints for:
 * - System status
 * - Dosing control (ad-hoc doses)
 * - Calibration management
 * - Emergency stop
 * - WiFi management
 * - Schedule management (CRUD operations)
 *
 * WebSocket for real-time updates of dosing progress
 *
 * Thread-safety: AsyncWebServer is thread-safe, but shared resources (DosingHead, MotorDriver)
 * should be accessed with appropriate synchronization if used from multiple tasks
 */
class WebServer {
public:
    /**
     * @brief Construct a new Web Server object
     * @param port HTTP server port (default: 80)
     */
    WebServer(uint16_t port = 80);

    /**
     * @brief Initialize the web server with dosing heads and motor driver
     * @param dosingHeads Array of pointers to DosingHead instances
     * @param numHeads Number of dosing heads (must be 4)
     * @param motorDriver Pointer to MotorDriver instance
     * @param wifiMgr Pointer to WiFiManager instance
     * @param schedMgr Pointer to ScheduleManager instance (optional)
     * @param logMgr Pointer to DosingLogManager instance (optional)
     * @return true if initialization successful
     */
    bool begin(DosingHead** dosingHeads, uint8_t numHeads, MotorDriver* motorDriver, WiFiManager* wifiMgr, ScheduleManager* schedMgr = nullptr, DosingLogManager* logMgr = nullptr);

    /**
     * @brief Stop the web server
     */
    void stop();

    /**
     * @brief Send a message to all connected WebSocket clients
     * @param message JSON message to send
     */
    void broadcastWebSocket(const String& message);

    /**
     * @brief Check if server is running
     * @return true if server is running
     */
    bool isRunning() const;

private:
    AsyncWebServer* server;
    AsyncWebSocket* ws;
    DosingHead** dosingHeads;
    uint8_t numHeads;
    MotorDriver* motorDriver;
    WiFiManager* wifiManager;
    ScheduleManager* scheduleManager;
    DosingLogManager* logManager;
    bool running;

    // REST API Handlers
    void handleGetStatus(AsyncWebServerRequest* request);
    void handlePostDose(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
    void handlePostCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
    void handleGetCalibration(AsyncWebServerRequest* request);
    void handlePostEmergencyStop(AsyncWebServerRequest* request);
    void handleGetWifiStatus(AsyncWebServerRequest* request);
    void handlePostWifiConfigure(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
    void handlePostWifiReset(AsyncWebServerRequest* request);

    // Schedule API Handlers
    void handleGetAllSchedules(AsyncWebServerRequest* request);
    void handleGetSchedule(AsyncWebServerRequest* request);
    void handlePostSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
    void handleDeleteSchedule(AsyncWebServerRequest* request);

    // Dosing Log API Handlers
    void handleGetDashboard(AsyncWebServerRequest* request);
    void handleGetHourlyLogs(AsyncWebServerRequest* request);
    void handleDeleteLogs(AsyncWebServerRequest* request);

    // Time Sync API Handlers
    void handleGetTime(AsyncWebServerRequest* request);
    void handlePostTime(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

    // WebSocket Handlers
    void handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                             AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Helper methods
    void setupRoutes();
    void sendJsonResponse(AsyncWebServerRequest* request, int code, const JsonDocument& doc);
    void sendErrorResponse(AsyncWebServerRequest* request, int code, const String& message);
    bool validateDosingRequest(const JsonDocument& doc, uint8_t& head, float& volume, String& error);
    bool validateCalibrationRequest(const JsonDocument& doc, uint8_t& head, float& actualVolume, String& error);
    bool validateScheduleRequest(const JsonDocument& doc, Schedule& sched, String& error);

    // WebSocket event handler wrapper (for C-style callback)
    static void onWebSocketEventStatic(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                      AwsEventType type, void* arg, uint8_t* data, size_t len);
};

#endif // WEB_SERVER_H
