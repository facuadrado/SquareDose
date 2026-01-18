# SquareDose API Documentation for Frontend Developers

**Version**: 1.0
**Base URL**: `http://{device-ip}`
**Device IP**:
- AP Mode: `192.168.4.1`
- STA Mode: Assigned by DHCP (check `/api/wifi/status`)

---

## Table of Contents

1. [Authentication](#authentication)
2. [System & Status](#system--status)
3. [Dosing Operations](#dosing-operations)
4. [Schedule Management](#schedule-management)
5. [Dosing Logs & Analytics](#dosing-logs--analytics)
6. [Time Synchronization](#time-synchronization)
7. [WiFi Management](#wifi-management)
8. [WebSocket](#websocket)
9. [Error Handling](#error-handling)
10. [Data Models](#data-models)

---

## Authentication

Currently, no authentication is required. All endpoints are publicly accessible on the local network.

---

## System & Status

### GET /api/status

Get current system status including motor states, dosing heads, WiFi connectivity, and uptime.

**Response 200 (application/json)**
```json
{
  "uptime": 123456,
  "wifiMode": "AP",
  "wifiConnected": false,
  "ipAddress": "192.168.4.1",
  "apSSID": "SquareDose-A1B2",
  "dosingHeads": [
    {
      "index": 0,
      "isDispensing": false,
      "isCalibrated": true,
      "mlPerSecond": 0.95
    },
    {
      "index": 1,
      "isDispensing": false,
      "isCalibrated": false,
      "mlPerSecond": 1.0
    }
  ],
  "motors": [
    {
      "index": 0,
      "isRunning": false,
      "direction": "STOP"
    }
  ]
}
```

### GET /api/calibration

Get calibration data for all 4 dosing heads.

**Response 200 (application/json)**
```json
{
  "heads": [
    {
      "index": 0,
      "isCalibrated": true,
      "mlPerSecond": 0.95,
      "lastCalibrationTime": 1768702800
    },
    {
      "index": 1,
      "isCalibrated": false,
      "mlPerSecond": 1.0,
      "lastCalibrationTime": 0
    }
  ]
}
```

---

## Dosing Operations

### POST /api/dose

Execute an ad-hoc dose (manual dose not part of schedule).

**Request Body** (application/json)
```json
{
  "head": 0,
  "volume": 2.5
}
```

**Parameters**
- `head` (integer, required): Dosing head index (0-3)
- `volume` (float, required): Volume in milliliters (0.1 - 1000.0)

**Response 200 (application/json) - Success**
```json
{
  "success": true,
  "head": 0,
  "targetVolume": 2.5,
  "estimatedVolume": 2.48,
  "runtime": 2630,
  "message": "Dose completed successfully"
}
```

**Response 400 (application/json) - Validation Error**
```json
{
  "error": "Invalid volume: must be between 0.1 and 1000.0 mL"
}
```

**Response 500 (application/json) - Execution Error**
```json
{
  "success": false,
  "head": 0,
  "targetVolume": 2.5,
  "estimatedVolume": 0,
  "runtime": 0,
  "error": "Dosing head not calibrated"
}
```

### POST /api/calibrate

Calibrate a dosing head after measuring actual dispensed volume.

**Calibration Workflow**:
1. Run a test dose (e.g., 4mL)
2. Measure the actual volume dispensed
3. Call this endpoint with the actual measured volume
4. System calculates and stores the correct mL/s rate

**Request Body** (application/json)
```json
{
  "head": 0,
  "actualVolume": 3.8
}
```

**Parameters**
- `head` (integer, required): Dosing head index (0-3)
- `actualVolume` (float, required): Actual measured volume in mL

**Response 200 (application/json)**
```json
{
  "success": true,
  "head": 0,
  "mlPerSecond": 0.95,
  "message": "Calibration updated successfully"
}
```

### POST /api/emergency-stop

Immediately stop all pumps. Use this for emergency situations.

**Request**: No body required

**Response 200 (application/json)**
```json
{
  "success": true,
  "message": "All motors stopped"
}
```

---

## Schedule Management

Schedules use a simplified model:
- User provides: `dailyTargetVolume` and `dosesPerDay`
- System auto-calculates: `volume` per dose and `intervalSeconds` between doses
- Only **interval-based** schedules are supported

### GET /api/schedules

Get all schedules for all 4 heads.

**Response 200 (application/json)**
```json
{
  "schedules": [
    {
      "head": 0,
      "name": "Nutrient A",
      "enabled": true,
      "dailyTargetVolume": 2880.0,
      "dosesPerDay": 1440,
      "volume": 2.0,
      "intervalSeconds": 60,
      "lastExecutionTime": 1768702440,
      "executionCount": 127,
      "createdAt": 1768615200,
      "updatedAt": 1768702440
    },
    {
      "head": 1,
      "name": "Nutrient B",
      "enabled": false,
      "dailyTargetVolume": 0,
      "dosesPerDay": 0,
      "volume": 0,
      "intervalSeconds": 0,
      "lastExecutionTime": 0,
      "executionCount": 0,
      "createdAt": 0,
      "updatedAt": 0
    }
  ],
  "count": 2
}
```

### GET /api/schedules/{head}

Get schedule for a specific head (0-3).

**URL Parameters**
- `head` (integer, 0-3): Dosing head index

**Response 200 (application/json)**
```json
{
  "head": 0,
  "name": "Nutrient A",
  "enabled": true,
  "dailyTargetVolume": 2880.0,
  "dosesPerDay": 1440,
  "volume": 2.0,
  "intervalSeconds": 60,
  "lastExecutionTime": 1768702440,
  "executionCount": 127
}
```

**Response 404 (application/json)**
```json
{
  "error": "Schedule not found for head 0"
}
```

### POST /api/schedules

Create or update a schedule.

**Request Body** (application/json)
```json
{
  "head": 0,
  "name": "Nutrient A",
  "enabled": true,
  "dailyTargetVolume": 2880,
  "dosesPerDay": 1440
}
```

**Parameters**
- `head` (integer, required): Dosing head index (0-3)
- `name` (string, optional): Descriptive name for the schedule
- `enabled` (boolean, required): Whether schedule is active
- `dailyTargetVolume` (float, required): Total mL to dose per day (0.1 - 10000)
- `dosesPerDay` (integer, required): Number of doses per day (1 - 1440)

**Auto-calculated values** (returned in response):
- `volume` = `dailyTargetVolume / dosesPerDay` (mL per dose)
- `intervalSeconds` = `86400 / dosesPerDay` (seconds between doses)

**Response 200 (application/json)**
```json
{
  "success": true,
  "schedule": {
    "head": 0,
    "name": "Nutrient A",
    "enabled": true,
    "dailyTargetVolume": 2880.0,
    "dosesPerDay": 1440,
    "volume": 2.0,
    "intervalSeconds": 60
  },
  "message": "Schedule created/updated successfully"
}
```

**Response 400 (application/json) - Validation Error**
```json
{
  "error": "Daily target volume must be 0.1-10000 mL"
}
```

### DELETE /api/schedules/{head}

Delete a schedule for a specific head.

**URL Parameters**
- `head` (integer, 0-3): Dosing head index

**Response 200 (application/json)**
```json
{
  "success": true,
  "message": "Schedule deleted for head 0"
}
```

---

## Dosing Logs & Analytics

Logs are stored hourly and aggregated per head. Separate tracking for scheduled vs. ad-hoc doses.

**Important**: Logging only works when device time is synchronized (via NTP or manual sync). Schedules continue to execute even without time sync.

### GET /api/logs/dashboard

Get daily summary for all heads showing progress against targets.

**Response 200 (application/json)**
```json
{
  "summaries": [
    {
      "head": 0,
      "dailyTarget": 2880.0,
      "scheduledActual": 156.5,
      "adhocTotal": 12.3,
      "dosesPerDay": 1440,
      "perDoseVolume": 2.0
    },
    {
      "head": 1,
      "dailyTarget": 1440.0,
      "scheduledActual": 82.0,
      "adhocTotal": 0.0,
      "dosesPerDay": 1440,
      "perDoseVolume": 1.0
    },
    {
      "head": 2,
      "dailyTarget": 0.0,
      "scheduledActual": 0.0,
      "adhocTotal": 5.0,
      "dosesPerDay": 0,
      "perDoseVolume": 0.0
    },
    {
      "head": 3,
      "dailyTarget": 0.0,
      "scheduledActual": 0.0,
      "adhocTotal": 0.0,
      "dosesPerDay": 0,
      "perDoseVolume": 0.0
    }
  ],
  "count": 4
}
```

**Calculated fields (client-side)**:
- `totalToday` = `scheduledActual + adhocTotal`
- `percentComplete` = `(scheduledActual / dailyTarget) * 100`

### GET /api/logs/hourly

Get hourly logs within a time range.

**Query Parameters**
- `start` (integer, optional): Start timestamp (unix epoch)
- `end` (integer, optional): End timestamp (unix epoch)
- `limit` (integer, optional): Max number of entries (default: 100)

**Example**: `/api/logs/hourly?start=1768615200&end=1768701600&limit=50`

**Response 200 (application/json)**
```json
{
  "logs": [
    {
      "hourTimestamp": 1768617600,
      "head": 0,
      "scheduledVolume": 120.0,
      "adhocVolume": 5.0
    },
    {
      "hourTimestamp": 1768621200,
      "head": 0,
      "scheduledVolume": 118.5,
      "adhocVolume": 0.0
    }
  ],
  "count": 2
}
```

**Calculated fields (client-side)**:
- `totalVolume` = `scheduledVolume + adhocVolume`

### DELETE /api/logs

Clear all dosing logs. Useful for testing or resetting the system.

**Request**: No body required

**Response 200 (application/json)**
```json
{
  "success": true,
  "message": "All dosing logs cleared successfully"
}
```

---

## Time Synchronization

Time sync is critical for logging. Schedules work without time sync using `millis()` fallback.

### GET /api/time

Get current device time and synchronization status.

**Response 200 (application/json)**
```json
{
  "timestamp": 1768702800,
  "synced": true,
  "source": "NTP or Manual"
}
```

**Fields**:
- `timestamp` (integer): Current Unix epoch time
- `synced` (boolean): `true` if time is after year 2020
- `source` (string): "NTP or Manual" if synced, "Unsynced" otherwise

### POST /api/time

Manually set the device time. Required in AP mode (no internet for NTP).

**Request Body** (application/json)
```json
{
  "timestamp": 1768702800
}
```

**Parameters**
- `timestamp` (integer, required): Unix epoch timestamp (2020-2100)

**Response 200 (application/json)**
```json
{
  "success": true,
  "timestamp": 1768702800,
  "message": "Time synchronized successfully"
}
```

**Response 400 (application/json)**
```json
{
  "error": "Invalid timestamp: must be between 2020 and 2100"
}
```

**Important Notes**:
- Time resets on power loss (no RTC battery)
- In AP mode, call this endpoint with phone's current time
- In STA mode, NTP auto-syncs (no manual sync needed)

---

## WiFi Management

### GET /api/wifi/status

Get current WiFi mode and connection status.

**Response 200 (application/json) - AP Mode**
```json
{
  "mode": "AP",
  "connected": false,
  "apSSID": "SquareDose-A1B2",
  "apIP": "192.168.4.1",
  "staSSID": "",
  "staIP": ""
}
```

**Response 200 (application/json) - STA Mode**
```json
{
  "mode": "STA",
  "connected": true,
  "apSSID": "",
  "apIP": "",
  "staSSID": "MyHomeWiFi",
  "staIP": "192.168.1.100",
  "rssi": -45
}
```

### POST /api/wifi/configure

Configure WiFi credentials and switch from AP to STA mode.

**Request Body** (application/json)
```json
{
  "ssid": "MyHomeWiFi",
  "password": "MyPassword"
}
```

**Parameters**
- `ssid` (string, required): WiFi network name
- `password` (string, required): WiFi password

**Response 200 (application/json)**
```json
{
  "success": true,
  "ssid": "MyHomeWiFi",
  "message": "WiFi configured, switching to STA mode..."
}
```

**Behavior**:
- Device saves credentials to NVS
- Switches from AP → STA mode
- Connects to specified WiFi network
- Gets new IP from DHCP
- Starts NTP time sync

### POST /api/wifi/reset

Clear WiFi credentials and return to AP mode.

**Request**: No body required

**Response 200 (application/json)**
```json
{
  "success": true,
  "message": "WiFi credentials cleared, returning to AP mode..."
}
```

---

## WebSocket

Real-time updates for dosing events, status changes, and errors.

**Endpoint**: `ws://{device-ip}/ws`

**Example Connection** (JavaScript):
```javascript
const ws = new WebSocket('ws://192.168.4.1/ws');

ws.onopen = () => {
  console.log('WebSocket connected');
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Received:', data);
};

ws.onerror = (error) => {
  console.error('WebSocket error:', error);
};

ws.onclose = () => {
  console.log('WebSocket disconnected');
};
```

**Message Types**:

1. **Dose Start**
```json
{
  "event": "dose_start",
  "head": 0,
  "volume": 2.5,
  "timestamp": 1768702800
}
```

2. **Dose Complete**
```json
{
  "event": "dose_complete",
  "head": 0,
  "volume": 2.48,
  "runtime": 2630,
  "timestamp": 1768702803
}
```

3. **Schedule Executed**
```json
{
  "event": "schedule_executed",
  "head": 0,
  "volume": 2.0,
  "timestamp": 1768702800
}
```

4. **Error**
```json
{
  "event": "error",
  "message": "Motor driver initialization failed",
  "timestamp": 1768702800
}
```

---

## Error Handling

All endpoints follow consistent error response format:

**Error Response Format**
```json
{
  "error": "Descriptive error message"
}
```

**HTTP Status Codes**:
- `200 OK`: Success
- `400 Bad Request`: Validation error, malformed JSON, missing required fields
- `404 Not Found`: Resource not found (e.g., schedule doesn't exist)
- `500 Internal Server Error`: Server-side error (e.g., motor driver failure)
- `503 Service Unavailable`: Service not initialized (e.g., log manager unavailable)

**Common Errors**:

1. **Invalid JSON**
```json
{
  "error": "Invalid JSON: MissingColon"
}
```

2. **Missing Required Field**
```json
{
  "error": "Missing required field: head"
}
```

3. **Out of Range**
```json
{
  "error": "Invalid head index: 5 (must be 0-3)"
}
```

4. **Service Unavailable**
```json
{
  "error": "Dosing log manager not available"
}
```

---

## Data Models

### Schedule
```typescript
interface Schedule {
  head: number;              // 0-3
  name: string;              // Descriptive name
  enabled: boolean;          // Active/inactive
  dailyTargetVolume: number; // User input: total mL per day
  dosesPerDay: number;       // User input: doses per day (1-1440)
  volume: number;            // Auto-calc: mL per dose
  intervalSeconds: number;   // Auto-calc: seconds between doses
  lastExecutionTime: number; // Unix epoch
  executionCount: number;    // Total executions
  createdAt: number;         // Unix epoch
  updatedAt: number;         // Unix epoch
}
```

### DailySummary
```typescript
interface DailySummary {
  head: number;              // 0-3
  dailyTarget: number;       // From schedule
  scheduledActual: number;   // Actual scheduled doses today
  adhocTotal: number;        // Actual ad-hoc doses today
  dosesPerDay: number;       // From schedule
  perDoseVolume: number;     // From schedule

  // Calculated client-side:
  totalToday(): number;      // scheduledActual + adhocTotal
  percentComplete(): number; // (scheduledActual / dailyTarget) * 100
}
```

### HourlyLog
```typescript
interface HourlyLog {
  hourTimestamp: number;     // Unix epoch rounded to hour
  head: number;              // 0-3
  scheduledVolume: number;   // mL from scheduled doses this hour
  adhocVolume: number;       // mL from ad-hoc doses this hour

  // Calculated client-side:
  totalVolume(): number;     // scheduledVolume + adhocVolume
}
```

### CalibrationData
```typescript
interface CalibrationData {
  index: number;             // 0-3
  isCalibrated: boolean;     // Has been calibrated
  mlPerSecond: number;       // Dispensing rate
  lastCalibrationTime: number; // Unix epoch
}
```

---

## Quick Start Guide

### 1. Initial Setup (AP Mode)

```javascript
// Connect to device AP: SquareDose-XXXX
const BASE_URL = 'http://192.168.4.1';

// Check status
const status = await fetch(`${BASE_URL}/api/status`).then(r => r.json());
console.log(status);

// Sync time (required for logging)
await fetch(`${BASE_URL}/api/time`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ timestamp: Math.floor(Date.now() / 1000) })
});
```

### 2. Calibrate Dosing Heads

```javascript
// Run test dose
await fetch(`${BASE_URL}/api/dose`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ head: 0, volume: 4.0 })
});

// User measures actual volume (e.g., 3.8mL)
// Update calibration
await fetch(`${BASE_URL}/api/calibrate`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ head: 0, actualVolume: 3.8 })
});
```

### 3. Create Schedules

```javascript
// Create schedule: 2mL every minute (2880mL/day)
await fetch(`${BASE_URL}/api/schedules`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    head: 0,
    name: 'Nutrient A',
    enabled: true,
    dailyTargetVolume: 2880,
    dosesPerDay: 1440
  })
});
```

### 4. Monitor Progress

```javascript
// Get daily summary
const dashboard = await fetch(`${BASE_URL}/api/logs/dashboard`).then(r => r.json());
console.log(dashboard.summaries);

// Connect WebSocket for real-time updates
const ws = new WebSocket(`ws://${BASE_URL.replace('http://', '')}/ws`);
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  // Update UI with real-time events
};
```

---

## Testing Tips

1. **Use Postman or Insomnia** for API testing during development
2. **Check serial monitor** for detailed logs and error messages
3. **Test time sync first** - logging won't work without it
4. **Emergency stop** is always available: `POST /api/emergency-stop`
5. **WebSocket reconnection** - implement auto-reconnect in your app

---

## Support

For questions or issues:
- Check serial monitor output for debugging
- Verify WiFi connection mode (AP vs STA)
- Ensure time is synced before expecting logs
- Use emergency stop if pumps don't stop properly
