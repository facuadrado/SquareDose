#include "network/WebServer.h"

// Static instance pointer for WebSocket callback
static WebServer* serverInstance = nullptr;

WebServer::WebServer(uint16_t port)
    : server(nullptr), ws(nullptr), dosingHeads(nullptr), numHeads(0),
      motorDriver(nullptr), wifiManager(nullptr), scheduleManager(nullptr), running(false) {
    server = new AsyncWebServer(port);
    ws = new AsyncWebSocket("/ws");
    serverInstance = this;
}

bool WebServer::begin(DosingHead** heads, uint8_t num, MotorDriver* motor, WiFiManager* wifiMgr, ScheduleManager* schedMgr) {
    if (running) {
        return true;
    }

    if (heads == nullptr || num != 4 || motor == nullptr || wifiMgr == nullptr) {
        return false;
    }

    dosingHeads = heads;
    numHeads = num;
    motorDriver = motor;
    wifiManager = wifiMgr;
    scheduleManager = schedMgr;

    // Setup WebSocket
    ws->onEvent(onWebSocketEventStatic);
    server->addHandler(ws);

    // Setup REST API routes
    setupRoutes();

    // Start server
    server->begin();
    running = true;

    return true;
}

void WebServer::stop() {
    if (server && running) {
        server->end();
        running = false;
    }
}

void WebServer::broadcastWebSocket(const String& message) {
    if (ws) {
        ws->textAll(message);
    }
}

bool WebServer::isRunning() const {
    return running;
}

void WebServer::setupRoutes() {
    // System status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleGetStatus(request);
    });

    // Dosing endpoints
    server->on("/api/dose", HTTP_POST, [](AsyncWebServerRequest* request) {},
              nullptr,
              [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  this->handlePostDose(request, data, len, index, total);
              });

    // Calibration endpoints
    server->on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest* request) {},
              nullptr,
              [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  this->handlePostCalibrate(request, data, len, index, total);
              });

    server->on("/api/calibration", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleGetCalibration(request);
    });

    // Emergency stop
    server->on("/api/emergency-stop", HTTP_POST, [this](AsyncWebServerRequest* request) {
        this->handlePostEmergencyStop(request);
    });

    // WiFi management
    server->on("/api/wifi/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleGetWifiStatus(request);
    });

    server->on("/api/wifi/configure", HTTP_POST, [](AsyncWebServerRequest* request) {},
              nullptr,
              [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  this->handlePostWifiConfigure(request, data, len, index, total);
              });

    server->on("/api/wifi/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        this->handlePostWifiReset(request);
    });

    // Schedule management endpoints
    server->on("/api/schedules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleGetAllSchedules(request);
    });

    server->on("/api/schedules", HTTP_POST, [](AsyncWebServerRequest* request) {},
              nullptr,
              [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  this->handlePostSchedule(request, data, len, index, total);
              });

    // Schedule by head index (GET /api/schedules/0, DELETE /api/schedules/0)
    server->on("^\\/api\\/schedules\\/([0-3])$", HTTP_GET, [this](AsyncWebServerRequest* request) {
        this->handleGetSchedule(request);
    });

    server->on("^\\/api\\/schedules\\/([0-3])$", HTTP_DELETE, [this](AsyncWebServerRequest* request) {
        this->handleDeleteSchedule(request);
    });

    // 404 handler
    server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "application/json", "{\"error\":\"Endpoint not found\"}");
    });
}

void WebServer::handleGetStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["uptime"] = millis();
    doc["wifiMode"] = (wifiManager->getCurrentMode() == WIFIMANAGER_MODE_AP) ? "AP" : "STA";
    doc["wifiConnected"] = wifiManager->isConnected();
    doc["ipAddress"] = wifiManager->getLocalIP();
    doc["apSSID"] = wifiManager->getAPSSID();

    // Add dosing head status
    JsonArray heads = doc["dosingHeads"].to<JsonArray>();
    for (uint8_t i = 0; i < numHeads; i++) {
        JsonObject head = heads.add<JsonObject>();
        head["index"] = i;
        head["isDispensing"] = dosingHeads[i]->isDispensing();
        head["isCalibrated"] = dosingHeads[i]->isCalibrated();

        CalibrationData cal = dosingHeads[i]->getCalibrationData();
        head["mlPerSecond"] = cal.mlPerSecond;
    }

    sendJsonResponse(request, 200, doc);
}

void WebServer::handlePostDose(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process if we have the complete body
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
        return;
    }

    uint8_t head;
    float volume;
    String validationError;

    if (!validateDosingRequest(doc, head, volume, validationError)) {
        sendErrorResponse(request, 400, validationError);
        return;
    }

    // Send immediate response acknowledging the dose request
    JsonDocument responseDoc;
    responseDoc["success"] = true;
    responseDoc["head"] = head;
    responseDoc["targetVolume"] = volume;
    responseDoc["message"] = "Dose started";
    responseDoc["note"] = "Dosing operation running in background. Use WebSocket or poll /api/status for completion.";

    sendJsonResponse(request, 202, responseDoc);  // 202 Accepted

    // Execute dose in background task to avoid blocking HTTP response
    struct DoseTaskParams {
        DosingHead** heads;
        uint8_t head;
        float volume;
        AsyncWebSocket* ws;
    };

    DoseTaskParams* params = new DoseTaskParams{dosingHeads, head, volume, ws};

    xTaskCreate([](void* param) {
        DoseTaskParams* p = (DoseTaskParams*)param;

        // Execute dose (blocking within this task)
        DosingResult result = p->heads[p->head]->dispense(p->volume);

        // Broadcast result to WebSocket clients
        if (result.success) {
            JsonDocument wsDoc;
            wsDoc["event"] = "dose_complete";
            wsDoc["head"] = p->head;
            wsDoc["targetVolume"] = result.targetVolume;
            wsDoc["estimatedVolume"] = result.estimatedVolume;
            wsDoc["runtime"] = result.actualRuntime;

            String wsMessage;
            serializeJson(wsDoc, wsMessage);
            p->ws->textAll(wsMessage);

            Serial.printf("[WebServer] Dose complete: Head %d, Volume %.2f mL, Runtime %lu ms\n",
                         p->head, result.estimatedVolume, result.actualRuntime);
        } else {
            JsonDocument wsDoc;
            wsDoc["event"] = "dose_error";
            wsDoc["head"] = p->head;
            wsDoc["error"] = result.errorMessage;

            String wsMessage;
            serializeJson(wsDoc, wsMessage);
            p->ws->textAll(wsMessage);

            Serial.printf("[WebServer] Dose failed: Head %d, Error: %s\n",
                         p->head, result.errorMessage.c_str());
        }

        delete p;
        vTaskDelete(NULL);
    }, "DoseTask", 4096, params, 1, NULL);
}

void WebServer::handlePostCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process if we have the complete body
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
        return;
    }

    uint8_t head;
    float actualVolume;
    String validationError;

    if (!validateCalibrationRequest(doc, head, actualVolume, validationError)) {
        sendErrorResponse(request, 400, validationError);
        return;
    }

    // Perform calibration
    bool success = dosingHeads[head]->calibrate(actualVolume);

    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["head"] = head;

    if (success) {
        CalibrationData cal = dosingHeads[head]->getCalibrationData();
        responseDoc["mlPerSecond"] = cal.mlPerSecond;
        responseDoc["isCalibrated"] = cal.isCalibrated;
    } else {
        responseDoc["error"] = "Calibration failed";
    }

    sendJsonResponse(request, success ? 200 : 500, responseDoc);
}

void WebServer::handleGetCalibration(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray heads = doc["calibrations"].to<JsonArray>();

    for (uint8_t i = 0; i < numHeads; i++) {
        JsonObject head = heads.add<JsonObject>();
        CalibrationData cal = dosingHeads[i]->getCalibrationData();

        head["head"] = i;
        head["isCalibrated"] = cal.isCalibrated;
        head["mlPerSecond"] = cal.mlPerSecond;
        head["lastCalibrationTime"] = cal.lastCalibrationTime;
    }

    sendJsonResponse(request, 200, doc);
}

void WebServer::handlePostEmergencyStop(AsyncWebServerRequest* request) {
    motorDriver->emergencyStopAll();

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Emergency stop executed";

    sendJsonResponse(request, 200, doc);

    // Broadcast to WebSocket clients
    JsonDocument wsDoc;
    wsDoc["event"] = "emergency_stop";
    wsDoc["timestamp"] = millis();

    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);
}

void WebServer::handleGetWifiStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    WifiManagerMode mode = wifiManager->getCurrentMode();
    doc["mode"] = (mode == WIFIMANAGER_MODE_AP) ? "AP" : "STA";
    doc["connected"] = wifiManager->isConnected();
    doc["ipAddress"] = wifiManager->getLocalIP();
    doc["apSSID"] = wifiManager->getAPSSID();

    sendJsonResponse(request, 200, doc);
}

void WebServer::handlePostWifiConfigure(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process if we have the complete body
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
        return;
    }

    if (!doc["ssid"].is<const char*>() || !doc["password"].is<const char*>()) {
        sendErrorResponse(request, 400, "Missing required fields: ssid, password");
        return;
    }

    const char* ssid = doc["ssid"];
    const char* password = doc["password"];

    // Save credentials to NVS
    bool success = wifiManager->setCredentials(ssid, password);

    JsonDocument responseDoc;
    responseDoc["success"] = success;

    if (success) {
        responseDoc["message"] = "WiFi credentials saved. Switching to STA mode in background...";
        responseDoc["note"] = "Device will attempt to connect. Check /api/wifi/status for current state.";
    } else {
        responseDoc["error"] = "Failed to save WiFi credentials";
    }

    sendJsonResponse(request, success ? 200 : 500, responseDoc);

    // Switch to STA mode in background (non-blocking)
    // The keepAliveTask will detect the new credentials and switch automatically
    if (success) {
        // Trigger an immediate switch attempt by calling switchToSTAMode in a non-blocking way
        // We do this AFTER responding to avoid blocking the HTTP response
        xTaskCreate([](void* param) {
            WiFiManager* mgr = (WiFiManager*)param;
            vTaskDelay(100 / portTICK_PERIOD_MS); // Small delay to ensure HTTP response is sent
            mgr->switchToSTAMode();
            vTaskDelete(NULL);
        }, "WiFiSwitch", 4096, wifiManager, 1, NULL);
    }
}

void WebServer::handlePostWifiReset(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Clearing WiFi credentials and switching to AP mode...";
    doc["note"] = "Device will reset to AP mode and stay there until reconfigured.";
    doc["apSSID"] = wifiManager->getAPSSID();

    sendJsonResponse(request, 200, doc);

    // Clear credentials and switch to AP mode in background (non-blocking)
    // We do this AFTER responding to avoid disconnecting the client mid-response
    xTaskCreate([](void* param) {
        WiFiManager* mgr = (WiFiManager*)param;
        vTaskDelay(500 / portTICK_PERIOD_MS); // Longer delay to ensure HTTP response is fully sent

        // Clear credentials from NVS so device stays in AP mode
        mgr->clearCredentials();

        // Switch to AP mode
        mgr->switchToAPMode();

        vTaskDelete(NULL);
    }, "WiFiReset", 4096, wifiManager, 1, NULL);
}

void WebServer::handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                    AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WebSocket] Client #%u connected from %s\n",
                         client->id(), client->remoteIP().toString().c_str());
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WebSocket] Client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA:
            // Handle incoming WebSocket data if needed
            break;

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServer::sendJsonResponse(AsyncWebServerRequest* request, int code, const JsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    request->send(code, "application/json", response);
}

void WebServer::sendErrorResponse(AsyncWebServerRequest* request, int code, const String& message) {
    JsonDocument doc;
    doc["error"] = message;
    sendJsonResponse(request, code, doc);
}

bool WebServer::validateDosingRequest(const JsonDocument& doc, uint8_t& head, float& volume, String& error) {
    if (!doc["head"].is<uint8_t>()) {
        error = "Missing required field: head";
        return false;
    }

    if (!doc["volume"].is<float>()) {
        error = "Missing required field: volume";
        return false;
    }

    head = doc["head"];
    volume = doc["volume"];

    if (head >= numHeads) {
        error = "Invalid head index: " + String(head) + " (must be 0-" + String(numHeads - 1) + ")";
        return false;
    }

    if (volume <= 0.0f || volume > 1000.0f) {
        error = "Invalid volume: " + String(volume) + " (must be 0.1-1000 mL)";
        return false;
    }

    return true;
}

bool WebServer::validateCalibrationRequest(const JsonDocument& doc, uint8_t& head, float& actualVolume, String& error) {
    if (!doc["head"].is<uint8_t>()) {
        error = "Missing required field: head";
        return false;
    }

    if (!doc["actualVolume"].is<float>()) {
        error = "Missing required field: actualVolume";
        return false;
    }

    head = doc["head"];
    actualVolume = doc["actualVolume"];

    if (head >= numHeads) {
        error = "Invalid head index: " + String(head) + " (must be 0-" + String(numHeads - 1) + ")";
        return false;
    }

    if (actualVolume <= 0.0f) {
        error = "Invalid actual volume: " + String(actualVolume);
        return false;
    }

    return true;
}

void WebServer::handleGetAllSchedules(AsyncWebServerRequest* request) {
    if (scheduleManager == nullptr) {
        sendErrorResponse(request, 503, "Schedule manager not available");
        return;
    }

    Schedule schedules[NUM_SCHEDULE_HEADS];
    uint8_t count = scheduleManager->getAllSchedules(schedules);

    JsonDocument doc;
    JsonArray schedulesArray = doc["schedules"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject schedObj = schedulesArray.add<JsonObject>();
        schedObj["head"] = schedules[i].head;
        schedObj["name"] = schedules[i].name;
        schedObj["type"] = static_cast<uint8_t>(schedules[i].type);
        schedObj["enabled"] = schedules[i].enabled;
        schedObj["volume"] = schedules[i].volume;
        schedObj["timestamp"] = schedules[i].timestamp;
        schedObj["lastExecutionTime"] = schedules[i].lastExecutionTime;
        schedObj["executionCount"] = schedules[i].executionCount;
        schedObj["createdAt"] = schedules[i].createdAt;
        schedObj["updatedAt"] = schedules[i].updatedAt;
    }

    doc["count"] = count;
    sendJsonResponse(request, 200, doc);
}

void WebServer::handleGetSchedule(AsyncWebServerRequest* request) {
    if (scheduleManager == nullptr) {
        sendErrorResponse(request, 503, "Schedule manager not available");
        return;
    }

    // Extract head from URL path parameter
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash == -1) {
        sendErrorResponse(request, 400, "Invalid URL format");
        return;
    }

    String headStr = path.substring(lastSlash + 1);
    uint8_t head = headStr.toInt();

    if (head >= NUM_SCHEDULE_HEADS) {
        sendErrorResponse(request, 400, "Invalid head index: " + String(head));
        return;
    }

    Schedule sched;
    if (!scheduleManager->getSchedule(head, sched)) {
        sendErrorResponse(request, 404, "Schedule not found for head " + String(head));
        return;
    }

    JsonDocument doc;
    doc["head"] = sched.head;
    doc["name"] = sched.name;
    doc["type"] = static_cast<uint8_t>(sched.type);
    doc["enabled"] = sched.enabled;
    doc["volume"] = sched.volume;
    doc["timestamp"] = sched.timestamp;
    doc["lastExecutionTime"] = sched.lastExecutionTime;
    doc["executionCount"] = sched.executionCount;
    doc["createdAt"] = sched.createdAt;
    doc["updatedAt"] = sched.updatedAt;

    sendJsonResponse(request, 200, doc);
}

void WebServer::handlePostSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (scheduleManager == nullptr) {
        sendErrorResponse(request, 503, "Schedule manager not available");
        return;
    }

    // Only process if we have the complete body
    if (index + len != total) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
        return;
    }

    Schedule sched;
    String validationError;

    if (!validateScheduleRequest(doc, sched, validationError)) {
        sendErrorResponse(request, 400, validationError);
        return;
    }

    // Set timestamps
    uint32_t now = millis() / 1000;
    sched.createdAt = now;
    sched.updatedAt = now;

    // Save schedule
    bool success = scheduleManager->setSchedule(sched);

    JsonDocument responseDoc;
    responseDoc["success"] = success;
    responseDoc["head"] = sched.head;

    if (success) {
        responseDoc["message"] = "Schedule created/updated successfully";
    } else {
        responseDoc["error"] = "Failed to save schedule";
    }

    sendJsonResponse(request, success ? 200 : 500, responseDoc);
}

void WebServer::handleDeleteSchedule(AsyncWebServerRequest* request) {
    if (scheduleManager == nullptr) {
        sendErrorResponse(request, 503, "Schedule manager not available");
        return;
    }

    // Extract head from URL path parameter
    String path = request->url();
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash == -1) {
        sendErrorResponse(request, 400, "Invalid URL format");
        return;
    }

    String headStr = path.substring(lastSlash + 1);
    uint8_t head = headStr.toInt();

    if (head >= NUM_SCHEDULE_HEADS) {
        sendErrorResponse(request, 400, "Invalid head index: " + String(head));
        return;
    }

    bool success = scheduleManager->deleteSchedule(head);

    JsonDocument doc;
    doc["success"] = success;
    doc["head"] = head;

    if (success) {
        doc["message"] = "Schedule deleted successfully";
    } else {
        doc["error"] = "Failed to delete schedule";
    }

    sendJsonResponse(request, success ? 200 : 500, doc);
}

bool WebServer::validateScheduleRequest(const JsonDocument& doc, Schedule& sched, String& error) {
    // Validate required fields
    if (!doc["head"].is<uint8_t>()) {
        error = "Missing required field: head";
        return false;
    }

    if (!doc["type"].is<uint8_t>()) {
        error = "Missing required field: type";
        return false;
    }

    if (!doc["volume"].is<float>()) {
        error = "Missing required field: volume";
        return false;
    }

    if (!doc["timestamp"].is<uint32_t>()) {
        error = "Missing required field: timestamp";
        return false;
    }

    // Extract values
    sched.head = doc["head"];
    sched.type = static_cast<ScheduleType>(doc["type"].as<uint8_t>());
    sched.volume = doc["volume"];
    sched.timestamp = doc["timestamp"];

    // Optional fields
    sched.enabled = doc["enabled"].is<bool>() ? doc["enabled"].as<bool>() : true;

    if (doc["name"].is<const char*>()) {
        strncpy(sched.name, doc["name"], sizeof(sched.name) - 1);
        sched.name[sizeof(sched.name) - 1] = '\0';
    } else {
        snprintf(sched.name, sizeof(sched.name), "Schedule %d", sched.head);
    }

    // Initialize execution tracking
    sched.lastExecutionTime = 0;
    sched.executionCount = 0;

    // Validate values
    if (sched.head >= NUM_SCHEDULE_HEADS) {
        error = "Invalid head index: " + String(sched.head) + " (must be 0-3)";
        return false;
    }

    if (sched.volume <= 0.0f || sched.volume > 1000.0f) {
        error = "Invalid volume: " + String(sched.volume) + " (must be 0.1-1000 mL)";
        return false;
    }

    // Validate type-specific constraints
    switch (sched.type) {
        case ScheduleType::ONCE:
            // timestamp should be a future Unix epoch time
            break;
        case ScheduleType::DAILY:
            // timestamp should be seconds since midnight (0-86399)
            if (sched.timestamp >= 86400) {
                error = "Invalid DAILY timestamp: " + String(sched.timestamp) + " (must be 0-86399 seconds since midnight)";
                return false;
            }
            break;
        case ScheduleType::INTERVAL:
            // timestamp should be interval in seconds (minimum 60 seconds)
            if (sched.timestamp < 60) {
                error = "Invalid INTERVAL timestamp: " + String(sched.timestamp) + " (must be >= 60 seconds)";
                return false;
            }
            break;
        default:
            error = "Invalid schedule type: " + String(static_cast<uint8_t>(sched.type));
            return false;
    }

    return true;
}

void WebServer::onWebSocketEventStatic(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (serverInstance) {
        serverInstance->handleWebSocketEvent(server, client, type, arg, data, len);
    }
}
