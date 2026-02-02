#ifndef SQUAREDOSE_BLE_SERVER_H
#define SQUAREDOSE_BLE_SERVER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "config/BLEConfig.h"
#include "hal/DosingHead.h"
#include "hal/MotorDriver.h"
#include "network/wifi_manager.h"
#include "scheduling/ScheduleManager.h"

// Forward declaration
class DosingLogManager;

// Command queue message structure
struct BLECommand {
    char data[512];  // Command JSON string (max 512 bytes)
    uint16_t length;
};

// Stack sizes for tasks
static const uint32_t BLE_WORKER_STACK_SIZE = 16384;  // 16KB for command processing (NVS needs lots of stack)
static const uint32_t BLE_SEND_STACK_SIZE = 8192;     // 8KB for BLE notify operations

/**
 * @brief BLE Server for SquareDose device
 *
 * Implements Nordic UART Service (NUS) pattern for bidirectional JSON commands.
 * Commands mirror REST API structure for consistency.
 *
 * THREADING MODEL:
 * - BLE callbacks (onWrite, onConnect, onDisconnect) run on BLE stack task (limited stack)
 * - Commands are queued and processed by dedicated worker task (16KB stack)
 * - Responses are sent by dedicated send task (8KB stack)
 * - Shared state protected by mutex
 *
 * Command Format:
 *   Request:  {"cmd":"status"}
 *   Response: {"cmd":"status","success":true,"data":{...}}
 *   Event:    {"event":"dose_complete","head":0,"volume":5.0}
 */
class SquareDoseBLEServer : public BLEServerCallbacks,
                             public BLECharacteristicCallbacks {
public:
    SquareDoseBLEServer();
    ~SquareDoseBLEServer();

    /**
     * @brief Initialize BLE server with device dependencies
     * @return true if initialization successful
     */
    bool begin(DosingHead** dosingHeads, uint8_t numHeads,
               MotorDriver* motorDriver, WiFiManager* wifiMgr,
               ScheduleManager* schedMgr = nullptr,
               DosingLogManager* logMgr = nullptr);

    /**
     * @brief Stop BLE server and advertising
     */
    void stop();

    /**
     * @brief Check if a client is connected (thread-safe)
     */
    bool isConnected();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const;

    /**
     * @brief Send response/event to connected client (thread-safe)
     * @param message JSON string to send
     */
    void sendResponse(const String& message);

    /**
     * @brief Send structured event notification
     * @param eventType Event name (e.g., "dose_complete")
     * @param data Event data
     */
    void sendEvent(const String& eventType, const JsonDocument& data);

    // BLEServerCallbacks
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;

    // BLECharacteristicCallbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;

private:
    BLEServer* bleServer;
    BLEService* bleService;
    BLECharacteristic* txCharacteristic;
    BLECharacteristic* rxCharacteristic;
    BLEAdvertising* advertising;

    // Device dependencies
    DosingHead** dosingHeads;
    uint8_t numHeads;
    MotorDriver* motorDriver;
    WiFiManager* wifiManager;
    ScheduleManager* scheduleManager;
    DosingLogManager* logManager;

    // Synchronization primitives
    SemaphoreHandle_t stateMutex;    // Protects deviceConnected, rxBuffer
    SemaphoreHandle_t txMutex;       // Protects TX operations
    QueueHandle_t commandQueue;      // Queue for incoming commands
    TaskHandle_t workerTaskHandle;   // Handle to worker task

    // State (protected by stateMutex)
    volatile bool deviceConnected;
    bool running;
    String rxBuffer;

    // Worker task function (static for FreeRTOS)
    static void workerTask(void* param);
    void processCommandQueue();

    // Command processing (runs in worker task context)
    void processCommand(const String& command);
    void handleCommand(const JsonDocument& doc);

    // Command handlers (mirror WebServer API)
    void handleStatus(JsonDocument& response);
    void handleDose(const JsonDocument& request, JsonDocument& response);
    void handleCalibrate(const JsonDocument& request, JsonDocument& response);
    void handleCalibrationGet(JsonDocument& response);
    void handleEmergencyStop(JsonDocument& response);
    void handleWifiStatus(JsonDocument& response);
    void handleWifiConfigure(const JsonDocument& request, JsonDocument& response);
    void handleWifiReset(JsonDocument& response);
    void handleSchedulesGet(JsonDocument& response);
    void handleScheduleGet(const JsonDocument& request, JsonDocument& response);
    void handleScheduleSet(const JsonDocument& request, JsonDocument& response);
    void handleScheduleDelete(const JsonDocument& request, JsonDocument& response);
    void handleTimeGet(JsonDocument& response);
    void handleTimeSet(const JsonDocument& request, JsonDocument& response);
    void handleLogsDashboard(JsonDocument& response);
    void handleLogsHourly(const JsonDocument& request, JsonDocument& response);
    void handleLogsClear(JsonDocument& response);

    // Helper methods
    void sendError(const String& cmd, const String& message);
    void sendSuccess(const String& cmd, const JsonDocument& data);
    String getDeviceName();

    // Thread-safe state access
    bool getDeviceConnected();
    void setDeviceConnected(bool connected);
};

// Global instance
extern SquareDoseBLEServer squareDoseBLE;

#endif // SQUAREDOSE_BLE_SERVER_H
