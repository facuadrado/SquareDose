#include "network/BLEServer.h"
#include "logs/DosingLogManager.h"
#include <time.h>
#include <sys/time.h>

// Global instance
SquareDoseBLEServer squareDoseBLE;

SquareDoseBLEServer::SquareDoseBLEServer()
    : bleServer(nullptr), bleService(nullptr),
      txCharacteristic(nullptr), rxCharacteristic(nullptr),
      advertising(nullptr), dosingHeads(nullptr), numHeads(0),
      motorDriver(nullptr), wifiManager(nullptr),
      scheduleManager(nullptr), logManager(nullptr),
      txMutex(nullptr), deviceConnected(false), running(false) {
}

SquareDoseBLEServer::~SquareDoseBLEServer() {
    stop();
    if (txMutex != nullptr) {
        vSemaphoreDelete(txMutex);
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

    // Create mutex for TX operations
    txMutex = xSemaphoreCreateMutex();
    if (txMutex == nullptr) {
        Serial.println("[BLE] Failed to create mutex");
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
    deviceConnected = false;
    Serial.println("[BLE] Server stopped");
}

bool SquareDoseBLEServer::isConnected() const {
    return deviceConnected;
}

bool SquareDoseBLEServer::isRunning() const {
    return running;
}

void SquareDoseBLEServer::sendResponse(const String& message) {
    if (!deviceConnected || txCharacteristic == nullptr) return;

    // Copy message for the task (will be deleted by task)
    String* msgCopy = new String(message);

    // Create a task with adequate stack to handle the BLE notify call
    // The BLE library's notify() uses hexDump which needs ~4KB stack
    struct SendParams {
        SquareDoseBLEServer* server;
        String* message;
    };

    SendParams* params = new SendParams{this, msgCopy};

    xTaskCreate([](void* param) {
        SendParams* p = (SendParams*)param;
        SquareDoseBLEServer* server = p->server;
        String* msg = p->message;

        Serial.printf("[BLE] Sending response: %d bytes\n", msg->length());

        if (xSemaphoreTake(server->txMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Send in chunks - use 20 bytes (default MTU payload size)
            // MTU negotiation often doesn't happen, so use safe default
            size_t offset = 0;
            const size_t chunkSize = 20;
            int chunkNum = 0;

            while (offset < msg->length() && server->deviceConnected) {
                size_t len = min(chunkSize, msg->length() - offset);
                server->txCharacteristic->setValue((uint8_t*)(msg->c_str() + offset), len);
                server->txCharacteristic->notify();
                offset += len;
                chunkNum++;

                // Delay between chunks to allow BLE stack to process
                vTaskDelay(pdMS_TO_TICKS(30));
            }

            Serial.printf("[BLE] Sent %d chunks, connected=%d\n", chunkNum, server->deviceConnected);
            xSemaphoreGive(server->txMutex);
        } else {
            Serial.println("[BLE] Failed to acquire TX mutex");
        }

        delete msg;
        delete p;
        vTaskDelete(NULL);
    }, "BLESend", 8192, params, 1, NULL);  // 8KB stack for BLE operations
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
    deviceConnected = true;
    Serial.println("[BLE] Client connected");

    // Send connected event
    JsonDocument eventDoc;
    eventDoc["event"] = "connected";
    eventDoc["device"] = getDeviceName();

    String eventStr;
    serializeJson(eventDoc, eventStr);

    // Delay to allow connection to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
    sendResponse(eventStr);
}

void SquareDoseBLEServer::onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    rxBuffer = "";
    Serial.println("[BLE] Client disconnected");

    // Restart advertising
    vTaskDelay(pdMS_TO_TICKS(500));
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising restarted");
}

void SquareDoseBLEServer::onWrite(BLECharacteristic* pCharacteristic) {
    if (pCharacteristic != rxCharacteristic) return;

    String value = pCharacteristic->getValue().c_str();
    if (value.length() == 0) return;

    rxBuffer += value;

    // Try to parse as complete JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, rxBuffer);

    if (!error) {
        // Valid JSON received
        String command = rxBuffer;
        rxBuffer = "";
        processCommand(command);
    } else if (error != DeserializationError::IncompleteInput) {
        // Invalid JSON - clear buffer and send error
        Serial.println("[BLE] Invalid JSON: " + rxBuffer);
        rxBuffer = "";
        sendError("unknown", "Invalid JSON format");
    }
    // IncompleteInput means we're waiting for more data
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
    data["bleConnected"] = deviceConnected;

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

    xTaskCreate([](void* param) {
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
