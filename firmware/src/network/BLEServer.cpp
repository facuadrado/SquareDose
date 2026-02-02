#include "network/BLEServer.h"
#include "logs/DosingLogManager.h"
#include <time.h>
#include <sys/time.h>

// Global instance
SquareDoseBLEServer squareDoseBLE;

// Queue size for pending commands
static const uint8_t COMMAND_QUEUE_SIZE = 4;

// Mutex timeout in milliseconds
static const uint32_t MUTEX_TIMEOUT_MS = 5000;

SquareDoseBLEServer::SquareDoseBLEServer()
    : bleServer(nullptr), bleService(nullptr),
      txCharacteristic(nullptr), rxCharacteristic(nullptr),
      advertising(nullptr), dosingHeads(nullptr), numHeads(0),
      motorDriver(nullptr), wifiManager(nullptr),
      scheduleManager(nullptr), logManager(nullptr),
      stateMutex(nullptr), txMutex(nullptr),
      commandQueue(nullptr), workerTaskHandle(nullptr),
      deviceConnected(false), running(false) {
}

SquareDoseBLEServer::~SquareDoseBLEServer() {
    stop();

    if (workerTaskHandle != nullptr) {
        vTaskDelete(workerTaskHandle);
        workerTaskHandle = nullptr;
    }
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
    if (txMutex != nullptr) {
        vSemaphoreDelete(txMutex);
        txMutex = nullptr;
    }
    if (stateMutex != nullptr) {
        vSemaphoreDelete(stateMutex);
        stateMutex = nullptr;
    }
}

bool SquareDoseBLEServer::begin(DosingHead** heads, uint8_t num,
                                 MotorDriver* motor, WiFiManager* wifiMgr,
                                 ScheduleManager* schedMgr, DosingLogManager* logMgr) {
    if (running) {
        return true;
    }

    if (heads == nullptr || num != 4 || motor == nullptr || wifiMgr == nullptr) {
        Serial.println("[BLE] Invalid parameters");
        return false;
    }

    dosingHeads = heads;
    numHeads = num;
    motorDriver = motor;
    wifiManager = wifiMgr;
    scheduleManager = schedMgr;
    logManager = logMgr;

    // Create synchronization primitives
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == nullptr) {
        Serial.println("[BLE] Failed to create state mutex");
        return false;
    }

    txMutex = xSemaphoreCreateMutex();
    if (txMutex == nullptr) {
        Serial.println("[BLE] Failed to create TX mutex");
        vSemaphoreDelete(stateMutex);
        stateMutex = nullptr;
        return false;
    }

    // Create command queue
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(BLECommand));
    if (commandQueue == nullptr) {
        Serial.println("[BLE] Failed to create command queue");
        vSemaphoreDelete(txMutex);
        vSemaphoreDelete(stateMutex);
        txMutex = nullptr;
        stateMutex = nullptr;
        return false;
    }

    // Create worker task with large stack for NVS operations
    BaseType_t result = xTaskCreate(
        workerTask,
        "BLEWorker",
        BLE_WORKER_STACK_SIZE,
        this,
        2,  // Higher priority than idle
        &workerTaskHandle
    );

    if (result != pdPASS) {
        Serial.println("[BLE] Failed to create worker task");
        vQueueDelete(commandQueue);
        vSemaphoreDelete(txMutex);
        vSemaphoreDelete(stateMutex);
        commandQueue = nullptr;
        txMutex = nullptr;
        stateMutex = nullptr;
        return false;
    }

    // Initialize BLE
    String deviceName = getDeviceName();
    BLEDevice::init(deviceName.c_str());

    // Set MTU size
    BLEDevice::setMTU(BLE_MTU_SIZE);

    // Create BLE Server
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(this);

    // Create NUS Service
    bleService = bleServer->createService(BLE_SERVICE_UUID);

    // Create TX Characteristic (Notify - device to app)
    txCharacteristic = bleService->createCharacteristic(
        BLE_TX_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    txCharacteristic->addDescriptor(new BLE2902());

    // Create RX Characteristic (Write - app to device)
    rxCharacteristic = bleService->createCharacteristic(
        BLE_RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    rxCharacteristic->setCallbacks(this);

    // Start service
    bleService->start();

    // Start advertising
    advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);  // For iPhone compatibility
    advertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    running = true;
    Serial.println("[BLE] Server started - Name: " + deviceName);
    return true;
}

void SquareDoseBLEServer::stop() {
    if (!running) return;

    if (advertising) {
        BLEDevice::stopAdvertising();
    }

    running = false;
    setDeviceConnected(false);
    Serial.println("[BLE] Server stopped");
}

// Thread-safe accessors for deviceConnected
bool SquareDoseBLEServer::getDeviceConnected() {
    bool connected = false;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        connected = deviceConnected;
        xSemaphoreGive(stateMutex);
    }
    return connected;
}

void SquareDoseBLEServer::setDeviceConnected(bool connected) {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        deviceConnected = connected;
        if (!connected) {
            rxBuffer = "";  // Clear buffer on disconnect
        }
        xSemaphoreGive(stateMutex);
    }
}

bool SquareDoseBLEServer::isConnected() {
    return getDeviceConnected();
}

bool SquareDoseBLEServer::isRunning() const {
    return running;
}

void SquareDoseBLEServer::sendResponse(const String& message) {
    if (!getDeviceConnected() || txCharacteristic == nullptr) return;

    // Copy message for the task (will be deleted by task)
    String* msgCopy = new String(message);
    if (msgCopy == nullptr) return;

    // Create task parameters
    struct SendParams {
        SquareDoseBLEServer* server;
        String* message;
    };

    SendParams* params = new SendParams{this, msgCopy};
    if (params == nullptr) {
        delete msgCopy;
        return;
    }

    // Create send task with adequate stack
    BaseType_t result = xTaskCreate([](void* param) {
        SendParams* p = (SendParams*)param;
        SquareDoseBLEServer* server = p->server;
        String* msg = p->message;

        if (xSemaphoreTake(server->txMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
            // Send in chunks - use 20 bytes (default MTU payload size)
            size_t offset = 0;
            const size_t chunkSize = 20;

            while (offset < msg->length() && server->getDeviceConnected()) {
                size_t len = min(chunkSize, msg->length() - offset);
                server->txCharacteristic->setValue((uint8_t*)(msg->c_str() + offset), len);
                server->txCharacteristic->notify();
                offset += len;

                // Delay between chunks to allow BLE stack to process
                vTaskDelay(pdMS_TO_TICKS(30));
            }

            xSemaphoreGive(server->txMutex);
        }

        delete msg;
        delete p;
        vTaskDelete(NULL);
    }, "BLESend", BLE_SEND_STACK_SIZE, params, 1, NULL);

    // Clean up if task creation failed
    if (result != pdPASS) {
        delete msgCopy;
        delete params;
        Serial.println("[BLE] Failed to create send task");
    }
}

void SquareDoseBLEServer::sendEvent(const String& eventType, const JsonDocument& data) {
    JsonDocument eventDoc;
    eventDoc["event"] = eventType;

    // Copy data fields
    for (JsonPairConst kv : data.as<JsonObjectConst>()) {
        eventDoc[kv.key()] = kv.value();
    }

    String eventStr;
    serializeJson(eventDoc, eventStr);
    sendResponse(eventStr);
}

void SquareDoseBLEServer::onConnect(BLEServer* pServer) {
    setDeviceConnected(true);
    Serial.println("[BLE] Client connected");

    // Send connected event (spawns send task, so safe in callback)
    JsonDocument eventDoc;
    eventDoc["event"] = "connected";
    eventDoc["device"] = getDeviceName();

    String eventStr;
    serializeJson(eventDoc, eventStr);
    sendResponse(eventStr);
}

void SquareDoseBLEServer::onDisconnect(BLEServer* pServer) {
    setDeviceConnected(false);
    Serial.println("[BLE] Client disconnected");

    // Restart advertising in background task to avoid blocking
    xTaskCreate([](void* param) {
        vTaskDelay(pdMS_TO_TICKS(500));
        BLEDevice::startAdvertising();
        Serial.println("[BLE] Advertising restarted");
        vTaskDelete(NULL);
    }, "BLEAdv", 2048, nullptr, 1, NULL);
}

void SquareDoseBLEServer::onWrite(BLECharacteristic* pCharacteristic) {
    // This runs on BLE stack task - keep it minimal!
    if (pCharacteristic != rxCharacteristic) return;

    String value = pCharacteristic->getValue().c_str();
    if (value.length() == 0) return;

    // Protect rxBuffer access
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        // Timeout - drop the data rather than blocking BLE task
        Serial.println("[BLE] Warning: mutex timeout in onWrite");
        return;
    }

    rxBuffer += value;

    // Check if we have complete JSON (look for closing brace at same nesting level)
    // Simple check: try to find balanced braces
    int braceCount = 0;
    bool inString = false;
    bool hasContent = false;
    int completeEnd = -1;

    for (size_t i = 0; i < rxBuffer.length(); i++) {
        char c = rxBuffer[i];
        if (c == '"' && (i == 0 || rxBuffer[i-1] != '\\')) {
            inString = !inString;
        }
        if (!inString) {
            if (c == '{') {
                braceCount++;
                hasContent = true;
            } else if (c == '}') {
                braceCount--;
                if (braceCount == 0 && hasContent) {
                    completeEnd = i;
                    break;
                }
            }
        }
    }

    if (completeEnd >= 0) {
        // Extract complete JSON command
        String command = rxBuffer.substring(0, completeEnd + 1);
        rxBuffer = rxBuffer.substring(completeEnd + 1);

        xSemaphoreGive(stateMutex);

        // Queue command for worker task (non-blocking)
        BLECommand cmd;
        size_t len = min(command.length(), sizeof(cmd.data) - 1);
        memcpy(cmd.data, command.c_str(), len);
        cmd.data[len] = '\0';
        cmd.length = len;

        if (xQueueSend(commandQueue, &cmd, 0) != pdTRUE) {
            // Queue full - send error response
            Serial.println("[BLE] Command queue full");
            sendError("unknown", "Server busy");
        }
    } else {
        xSemaphoreGive(stateMutex);

        // Check for buffer overflow
        if (rxBuffer.length() > 1024) {
            if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                rxBuffer = "";
                xSemaphoreGive(stateMutex);
            }
            sendError("unknown", "Message too large");
        }
    }
}

// Worker task - runs with large stack, processes commands from queue
void SquareDoseBLEServer::workerTask(void* param) {
    SquareDoseBLEServer* server = (SquareDoseBLEServer*)param;
    server->processCommandQueue();
}

void SquareDoseBLEServer::processCommandQueue() {
    BLECommand cmd;

    while (true) {
        // Wait for command with timeout
        if (xQueueReceive(commandQueue, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Process command in worker task context (large stack available)
            String command(cmd.data);
            processCommand(command);
        }

        // Yield to other tasks
        taskYIELD();
    }
}

void SquareDoseBLEServer::processCommand(const String& command) {
    Serial.println("[BLE] Command: " + command);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, command);

    if (error) {
        sendError("unknown", "JSON parse error: " + String(error.c_str()));
        return;
    }

    handleCommand(doc);
}

void SquareDoseBLEServer::handleCommand(const JsonDocument& doc) {
    if (!doc["cmd"].is<const char*>()) {
        sendError("unknown", "Missing 'cmd' field");
        return;
    }

    String cmd = doc["cmd"].as<String>();
    JsonDocument response;
    response["cmd"] = cmd;

    if (cmd == "status") {
        handleStatus(response);
    } else if (cmd == "dose") {
        handleDose(doc, response);
    } else if (cmd == "calibrate") {
        handleCalibrate(doc, response);
    } else if (cmd == "calibration_get") {
        handleCalibrationGet(response);
    } else if (cmd == "emergency_stop") {
        handleEmergencyStop(response);
    } else if (cmd == "wifi_status") {
        handleWifiStatus(response);
    } else if (cmd == "wifi_configure") {
        handleWifiConfigure(doc, response);
    } else if (cmd == "wifi_reset") {
        handleWifiReset(response);
    } else if (cmd == "schedules_get") {
        handleSchedulesGet(response);
    } else if (cmd == "schedule_get") {
        handleScheduleGet(doc, response);
    } else if (cmd == "schedule_set") {
        handleScheduleSet(doc, response);
    } else if (cmd == "schedule_delete") {
        handleScheduleDelete(doc, response);
    } else if (cmd == "time_get") {
        handleTimeGet(response);
    } else if (cmd == "time_set") {
        handleTimeSet(doc, response);
    } else if (cmd == "logs_dashboard") {
        handleLogsDashboard(response);
    } else if (cmd == "logs_hourly") {
        handleLogsHourly(doc, response);
    } else if (cmd == "logs_clear") {
        handleLogsClear(response);
    } else {
        sendError(cmd, "Unknown command: " + cmd);
        return;
    }

    String responseStr;
    serializeJson(response, responseStr);
    sendResponse(responseStr);
}

// ============ Command Handlers ============

void SquareDoseBLEServer::handleStatus(JsonDocument& response) {
    response["success"] = true;

    JsonObject data = response["data"].to<JsonObject>();
    data["uptime"] = millis();
    data["wifiMode"] = (wifiManager->getCurrentMode() == WIFIMANAGER_MODE_AP) ? "AP" : "STA";
    data["wifiConnected"] = wifiManager->isConnected();
    data["ipAddress"] = wifiManager->getLocalIP();
    data["apSSID"] = wifiManager->getAPSSID();
    data["bleConnected"] = getDeviceConnected();

    JsonArray heads = data["dosingHeads"].to<JsonArray>();
    for (uint8_t i = 0; i < numHeads; i++) {
        JsonObject head = heads.add<JsonObject>();
        head["index"] = i;
        head["isDispensing"] = dosingHeads[i]->isDispensing();
        head["isCalibrated"] = dosingHeads[i]->isCalibrated();

        CalibrationData cal = dosingHeads[i]->getCalibrationData();
        head["mlPerSecond"] = cal.mlPerSecond;
    }
}

void SquareDoseBLEServer::handleDose(const JsonDocument& request, JsonDocument& response) {
    if (!request["head"].is<uint8_t>() || !request["volume"].is<float>()) {
        response["success"] = false;
        response["error"] = "Missing head or volume";
        return;
    }

    uint8_t head = request["head"];
    float volume = request["volume"];

    if (head >= numHeads) {
        response["success"] = false;
        response["error"] = "Invalid head index";
        return;
    }

    if (volume <= 0 || volume > 1000) {
        response["success"] = false;
        response["error"] = "Volume must be 0.1-1000 mL";
        return;
    }

    // Send immediate acknowledgment
    response["success"] = true;
    response["head"] = head;
    response["targetVolume"] = volume;
    response["message"] = "Dose started";

    // Spawn task for actual dosing (non-blocking)
    struct DoseParams {
        SquareDoseBLEServer* server;
        DosingHead** heads;
        uint8_t head;
        float volume;
        DosingLogManager* logManager;
    };

    DoseParams* params = new DoseParams{this, dosingHeads, head, volume, logManager};

    BaseType_t result = xTaskCreate([](void* param) {
        DoseParams* p = (DoseParams*)param;

        DosingResult result = p->heads[p->head]->dispense(p->volume);

        // Log if successful
        if (result.success && p->logManager != nullptr) {
            time_t now;
            time(&now);
            if (now >= 946684800) {
                p->logManager->logAdhocDose(p->head, result.estimatedVolume, (uint32_t)now);
            }
        }

        // Send completion event
        JsonDocument eventDoc;
        eventDoc["head"] = p->head;
        eventDoc["success"] = result.success;
        eventDoc["targetVolume"] = result.targetVolume;
        eventDoc["estimatedVolume"] = result.estimatedVolume;
        eventDoc["runtime"] = result.actualRuntime;
        if (!result.success) {
            eventDoc["error"] = result.errorMessage;
        }

        p->server->sendEvent("dose_complete", eventDoc);

        delete p;
        vTaskDelete(NULL);
    }, "BLEDoseTask", 4096, params, 1, NULL);

    if (result != pdPASS) {
        delete params;
        response["success"] = false;
        response["error"] = "Failed to start dose task";
    }
}

void SquareDoseBLEServer::handleCalibrate(const JsonDocument& request, JsonDocument& response) {
    if (!request["head"].is<uint8_t>() || !request["actualVolume"].is<float>()) {
        response["success"] = false;
        response["error"] = "Missing head or actualVolume";
        return;
    }

    uint8_t head = request["head"];
    float actualVolume = request["actualVolume"];

    if (head >= numHeads) {
        response["success"] = false;
        response["error"] = "Invalid head index";
        return;
    }

    bool success = dosingHeads[head]->calibrate(actualVolume);
    response["success"] = success;
    response["head"] = head;

    if (success) {
        CalibrationData cal = dosingHeads[head]->getCalibrationData();
        response["mlPerSecond"] = cal.mlPerSecond;
        response["isCalibrated"] = cal.isCalibrated;
    } else {
        response["error"] = "Calibration failed";
    }
}

void SquareDoseBLEServer::handleCalibrationGet(JsonDocument& response) {
    response["success"] = true;

    JsonArray calibrations = response["calibrations"].to<JsonArray>();
    for (uint8_t i = 0; i < numHeads; i++) {
        JsonObject cal = calibrations.add<JsonObject>();
        CalibrationData data = dosingHeads[i]->getCalibrationData();

        cal["head"] = i;
        cal["isCalibrated"] = data.isCalibrated;
        cal["mlPerSecond"] = data.mlPerSecond;
        cal["lastCalibrationTime"] = data.lastCalibrationTime;
    }
}

void SquareDoseBLEServer::handleEmergencyStop(JsonDocument& response) {
    motorDriver->emergencyStopAll();
    response["success"] = true;
    response["message"] = "Emergency stop executed";
}

void SquareDoseBLEServer::handleWifiStatus(JsonDocument& response) {
    response["success"] = true;

    WifiManagerMode mode = wifiManager->getCurrentMode();
    response["mode"] = (mode == WIFIMANAGER_MODE_AP) ? "AP" : "STA";
    response["connected"] = wifiManager->isConnected();
    response["ipAddress"] = wifiManager->getLocalIP();
    response["apSSID"] = wifiManager->getAPSSID();
}

void SquareDoseBLEServer::handleWifiConfigure(const JsonDocument& request, JsonDocument& response) {
    if (!request["ssid"].is<const char*>() || !request["password"].is<const char*>()) {
        response["success"] = false;
        response["error"] = "Missing ssid or password";
        return;
    }

    const char* ssid = request["ssid"];
    const char* password = request["password"];

    bool success = wifiManager->setCredentials(ssid, password);
    response["success"] = success;

    if (success) {
        response["message"] = "WiFi credentials saved. Attempting connection...";

        // Switch to STA mode in background
        xTaskCreate([](void* param) {
            WiFiManager* mgr = (WiFiManager*)param;
            vTaskDelay(pdMS_TO_TICKS(100));
            mgr->switchToSTAMode();
            vTaskDelete(NULL);
        }, "WiFiSwitch", 4096, wifiManager, 1, NULL);
    } else {
        response["error"] = "Failed to save WiFi credentials";
    }
}

void SquareDoseBLEServer::handleWifiReset(JsonDocument& response) {
    response["success"] = true;
    response["message"] = "Clearing WiFi credentials...";
    response["apSSID"] = wifiManager->getAPSSID();

    xTaskCreate([](void* param) {
        WiFiManager* mgr = (WiFiManager*)param;
        vTaskDelay(pdMS_TO_TICKS(500));
        mgr->clearCredentials();
        mgr->switchToAPMode();
        vTaskDelete(NULL);
    }, "WiFiReset", 4096, wifiManager, 1, NULL);
}

void SquareDoseBLEServer::handleSchedulesGet(JsonDocument& response) {
    if (scheduleManager == nullptr) {
        response["success"] = false;
        response["error"] = "Schedule manager not available";
        return;
    }

    response["success"] = true;

    Schedule schedules[NUM_SCHEDULE_HEADS];
    uint8_t count = scheduleManager->getAllSchedules(schedules);

    JsonArray schedulesArray = response["schedules"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        JsonObject sched = schedulesArray.add<JsonObject>();
        sched["head"] = schedules[i].head;
        sched["name"] = schedules[i].name;
        sched["enabled"] = schedules[i].enabled;
        sched["dailyTargetVolume"] = schedules[i].dailyTargetVolume;
        sched["dosesPerDay"] = schedules[i].dosesPerDay;
        sched["volume"] = schedules[i].volume;
        sched["intervalSeconds"] = schedules[i].intervalSeconds;
    }

    response["count"] = count;
}

void SquareDoseBLEServer::handleScheduleGet(const JsonDocument& request, JsonDocument& response) {
    if (scheduleManager == nullptr) {
        response["success"] = false;
        response["error"] = "Schedule manager not available";
        return;
    }

    if (!request["head"].is<uint8_t>()) {
        response["success"] = false;
        response["error"] = "Missing head";
        return;
    }

    uint8_t head = request["head"];
    Schedule sched;

    if (!scheduleManager->getSchedule(head, sched)) {
        response["success"] = false;
        response["error"] = "Schedule not found";
        return;
    }

    response["success"] = true;
    response["head"] = sched.head;
    response["name"] = sched.name;
    response["enabled"] = sched.enabled;
    response["dailyTargetVolume"] = sched.dailyTargetVolume;
    response["dosesPerDay"] = sched.dosesPerDay;
    response["volume"] = sched.volume;
    response["intervalSeconds"] = sched.intervalSeconds;
}

void SquareDoseBLEServer::handleScheduleSet(const JsonDocument& request, JsonDocument& response) {
    if (scheduleManager == nullptr) {
        response["success"] = false;
        response["error"] = "Schedule manager not available";
        return;
    }

    if (!request["head"].is<uint8_t>() ||
        !request["dailyTargetVolume"].is<float>() ||
        !request["dosesPerDay"].is<uint16_t>()) {
        response["success"] = false;
        response["error"] = "Missing required fields";
        return;
    }

    Schedule sched;
    sched.head = request["head"];
    sched.dailyTargetVolume = request["dailyTargetVolume"];
    sched.dosesPerDay = request["dosesPerDay"].as<uint16_t>();
    sched.enabled = request["enabled"] | true;

    if (request["name"].is<const char*>()) {
        strncpy(sched.name, request["name"], sizeof(sched.name) - 1);
    } else {
        snprintf(sched.name, sizeof(sched.name), "Schedule %d", sched.head);
    }

    if (!sched.calculateFromDailyTarget()) {
        response["success"] = false;
        response["error"] = "Invalid schedule parameters";
        return;
    }

    uint32_t now = millis() / 1000;
    sched.createdAt = now;
    sched.updatedAt = now;
    sched.lastExecutionTime = 0;
    sched.executionCount = 0;

    bool success = scheduleManager->setSchedule(sched);
    response["success"] = success;
    response["head"] = sched.head;

    if (success) {
        response["message"] = "Schedule saved";
        response["volume"] = sched.volume;
        response["intervalSeconds"] = sched.intervalSeconds;
    } else {
        response["error"] = "Failed to save schedule";
    }
}

void SquareDoseBLEServer::handleScheduleDelete(const JsonDocument& request, JsonDocument& response) {
    if (scheduleManager == nullptr) {
        response["success"] = false;
        response["error"] = "Schedule manager not available";
        return;
    }

    if (!request["head"].is<uint8_t>()) {
        response["success"] = false;
        response["error"] = "Missing head";
        return;
    }

    uint8_t head = request["head"];
    bool success = scheduleManager->deleteSchedule(head);

    response["success"] = success;
    response["head"] = head;

    if (success) {
        response["message"] = "Schedule deleted";
    } else {
        response["error"] = "Failed to delete schedule";
    }
}

void SquareDoseBLEServer::handleTimeGet(JsonDocument& response) {
    time_t now;
    time(&now);

    response["success"] = true;
    response["timestamp"] = (uint32_t)now;
    response["synced"] = (now >= 1577836800);  // After Jan 1, 2020
}

void SquareDoseBLEServer::handleTimeSet(const JsonDocument& request, JsonDocument& response) {
    if (!request["timestamp"].is<uint32_t>()) {
        response["success"] = false;
        response["error"] = "Missing timestamp";
        return;
    }

    uint32_t timestamp = request["timestamp"];

    if (timestamp < 1577836800 || timestamp > 4102444800) {
        response["success"] = false;
        response["error"] = "Invalid timestamp";
        return;
    }

    struct timeval tv;
    tv.tv_sec = timestamp;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    response["success"] = true;
    response["timestamp"] = timestamp;
    response["message"] = "Time synchronized";

    Serial.printf("[BLE] Time set to: %lu\n", timestamp);
}

void SquareDoseBLEServer::handleLogsDashboard(JsonDocument& response) {
    if (logManager == nullptr || scheduleManager == nullptr) {
        response["success"] = false;
        response["error"] = "Log manager not available";
        return;
    }

    time_t now;
    time(&now);
    uint32_t currentTime = (uint32_t)now;

    if (currentTime < 946684800) {
        response["success"] = false;
        response["error"] = "Time not synchronized";
        return;
    }

    Schedule schedules[NUM_DOSING_HEADS];
    for (uint8_t i = 0; i < NUM_DOSING_HEADS; i++) {
        if (!scheduleManager->getSchedule(i, schedules[i])) {
            schedules[i].head = i;
            schedules[i].enabled = false;
            schedules[i].dailyTargetVolume = 0;
        }
    }

    DailySummary summaries[NUM_DOSING_HEADS];
    uint8_t count = logManager->getAllDailySummaries(currentTime, schedules, summaries);

    response["success"] = true;

    JsonArray heads = response["heads"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        JsonObject h = heads.add<JsonObject>();
        h["head"] = summaries[i].head;
        h["dailyTarget"] = summaries[i].dailyTarget;
        h["scheduledActual"] = summaries[i].scheduledActual;
        h["adhocTotal"] = summaries[i].adhocTotal;
        h["totalToday"] = summaries[i].getTotalToday();
        h["percentComplete"] = summaries[i].getPercentComplete();
    }

    response["timestamp"] = currentTime;
}

void SquareDoseBLEServer::handleLogsHourly(const JsonDocument& request, JsonDocument& response) {
    if (logManager == nullptr) {
        response["success"] = false;
        response["error"] = "Log manager not available";
        return;
    }

    time_t now;
    time(&now);
    uint32_t currentTime = (uint32_t)now;

    if (currentTime < 946684800) {
        response["success"] = false;
        response["error"] = "Time not synchronized";
        return;
    }

    uint32_t hours = request["hours"] | 24;
    if (hours > 336) hours = 336;

    uint32_t startTime = currentTime - (hours * 3600);
    uint32_t endTime = currentTime;

    HourlyDoseLog logs[48];  // Limit for BLE response size
    uint16_t count = logManager->getHourlyLogs(startTime, endTime, logs, 48);

    response["success"] = true;

    JsonArray logsArray = response["logs"].to<JsonArray>();
    for (uint16_t i = 0; i < count; i++) {
        JsonObject log = logsArray.add<JsonObject>();
        log["hour"] = logs[i].hourTimestamp;
        log["head"] = logs[i].head;
        log["scheduled"] = logs[i].scheduledVolume;
        log["adhoc"] = logs[i].adhocVolume;
    }

    response["count"] = count;
}

void SquareDoseBLEServer::handleLogsClear(JsonDocument& response) {
    if (logManager == nullptr) {
        response["success"] = false;
        response["error"] = "Log manager not available";
        return;
    }

    bool success = logManager->clearAll();
    response["success"] = success;

    if (success) {
        response["message"] = "Logs cleared";
    } else {
        response["error"] = "Failed to clear logs";
    }
}

// ============ Helper Methods ============

void SquareDoseBLEServer::sendError(const String& cmd, const String& message) {
    JsonDocument doc;
    doc["cmd"] = cmd;
    doc["success"] = false;
    doc["error"] = message;

    String response;
    serializeJson(doc, response);
    sendResponse(response);
}

void SquareDoseBLEServer::sendSuccess(const String& cmd, const JsonDocument& data) {
    JsonDocument doc;
    doc["cmd"] = cmd;
    doc["success"] = true;
    doc["data"] = data;

    String response;
    serializeJson(doc, response);
    sendResponse(response);
}

String SquareDoseBLEServer::getDeviceName() {
    if (wifiManager != nullptr) {
        return wifiManager->getAPSSID();
    }

    // Fallback: generate from MAC
    uint64_t chipid = ESP.getEfuseMac();
    uint16_t chip = (uint16_t)(chipid >> 32);
    char name[20];
    snprintf(name, sizeof(name), "SquareDose-%04X", chip);
    return String(name);
}
