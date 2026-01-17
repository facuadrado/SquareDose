# SquareDose ESP32-S3 Smart Doser

Smart doser system with 4 DC pump heads, dual WiFi modes (AP/STA), MQTT integration for AWS IoT, and FreeRTOS-based architecture.

## Hardware Configuration

- **MCU**: ESP32-S3-WROOM-1
- **Motor Drivers**: 2x TB6612 dual H-bridge (4 DC pumps total)
- **Framework**: Arduino with FreeRTOS
- **Storage**: NVS for persistent data

## Architecture Highlights

### FreeRTOS Task Design (8 tasks)

1. **WatchdogTask** (Priority 24, Core 0) - System health monitoring
2. **DosingExecutionTask** (Priority 20, Core 1) - Execute dosing commands
3. **MotorControlTask** (Priority 15, Core 1) - Low-level motor PWM
4. **SchedulerTask** (Priority 12, Core 0) - Check/trigger schedules
5. **NetworkTask** (Priority 10, Core 0) - MQTT management
6. **WebServerTask** (Priority 10, Core 0) - HTTP/WebSocket API
7. **LoggingTask** (Priority 8, Core 0) - Async log writing
8. **StatusReportTask** (Priority 5, Core 0) - Periodic status updates

### Layered Architecture

```
Application Layer (DosingController, ScheduleManager, CommandHandler)
    ↕
Service Layer (WiFiManager, MQTTClient, WebServer, DosingEngine)
    ↕
Hardware Abstraction Layer (MotorDriver, DosingHead, GPIOManager)
    ↕
Storage Layer (NVSManager, ScheduleStore, CalibrationStore, Logs)
```

### WiFi Mode Strategy with Automatic Fallback

The device uses a smart WiFi mode switching strategy that prioritizes connectivity and ease of setup:

- **Initial Boot (AP Mode)**:
  - Device starts in AP mode: `SquareDose-XXXXXX`
  - IP: 192.168.4.1
  - WebServer with REST API + WebSocket for setup and configuration
  - Used for initial WiFi credential configuration via iPhone app
  - No internet required - fully functional standalone

- **After WiFi Configuration (STA Mode)**:
  - Device switches to STA mode once credentials are provided
  - Connects to home WiFi network
  - Enables AWS IoT MQTT for cloud control and monitoring
  - NTP time synchronization for accurate scheduling
  - WebServer accessible via local network IP
  - AP mode is disabled to save power and reduce radio interference

- **Automatic Fallback to AP**:
  - If STA connection fails or is lost, device automatically switches back to AP mode
  - Retry logic: Attempts to reconnect to saved WiFi every 60 seconds
  - If reconnection succeeds, switches back to STA mode
  - If user wants to reconfigure WiFi, AP mode provides access
  - Ensures device is always accessible even if home WiFi is down

- **Benefits**:
  - Simple, predictable behavior (one mode at a time)
  - Lower power consumption (only one WiFi interface active)
  - Reduced radio interference
  - Always accessible: AP mode for setup/fallback, STA for normal operation
  - Automatic recovery from WiFi outages

## Project Structure

```
SquareDose/
├── platformio.ini                      # Build configuration
├── include/
│   ├── config/
│   │   ├── Config.h                    # System-wide configuration constants
│   │   ├── HardwareConfig.h            # Pin definitions, hardware specs
│   │   └── NetworkConfig.h             # WiFi/MQTT default configs
│   ├── core/
│   │   ├── SystemState.h               # Global system state management
│   │   ├── TaskManager.h               # FreeRTOS task orchestration
│   │   └── ErrorHandler.h              # Centralized error handling
│   ├── hal/                            # Hardware Abstraction Layer
│   │   ├── MotorDriver.h               # TB6612 driver abstraction
│   │   ├── DosingHead.h                # Individual doser control
│   │   └── GPIOManager.h               # GPIO initialization & control
│   ├── storage/
│   │   ├── NVSManager.h                # Non-Volatile Storage wrapper
│   │   ├── ScheduleStore.h             # Schedule persistence
│   │   ├── CalibrationStore.h          # Calibration data
│   │   ├── CredentialStore.h           # Encrypted credentials
│   │   └── LogStore.h                  # Circular log buffer
│   ├── network/
│   │   ├── WiFiManager.h               # WiFi mode management
│   │   ├── MQTTClient.h                # AWS IoT MQTT client
│   │   ├── WebServer.h                 # Local REST/WebSocket server
│   │   └── OTAManager.h                # Over-the-air updates
│   ├── dosing/
│   │   ├── DosingController.h          # High-level dosing orchestration
│   │   ├── Schedule.h                  # Schedule data structures
│   │   ├── DosingEngine.h              # Execution engine for schedules
│   │   └── Calibration.h               # Calibration algorithms
│   ├── api/
│   │   ├── CommandHandler.h            # Command processing (MQTT/REST)
│   │   ├── MessageBuilder.h            # Response/status message builder
│   │   └── RequestValidator.h          # Input validation & sanitization
│   └── utils/
│       ├── Logger.h                    # Multi-level logging system
│       ├── TimeManager.h               # NTP/RTC time synchronization
│       └── CryptoUtils.h               # Encryption helpers
├── src/                                # Implementation files (mirrors include/)
├── lib/                                # Custom libraries
├── test/                               # Unit and integration tests
└── data/                               # SPIFFS/LittleFS data
    ├── web/                            # Web UI files
    └── certs/                          # AWS IoT certificates
```

## Implementation Phases

Each phase is a self-contained feature module with complete vertical slice including: architecture decisions, dependencies, core classes, storage, API endpoints, tests, and documentation.

### Module 1: Hardware Control - Motor Driver & Dosing Heads

**Description**: Complete motor control system for 4 DC pump heads using TB6612 drivers

**Deliverables**:
- TB6612 driver abstraction (GPIO, PWM, direction control)
- DosingHead class (calibration, dose calculations, volume tracking)
- Calibration storage in NVS
- Calibration API endpoints for REST
- Hardware configuration files
- Unit tests for calibration math
- Hardware test suite

**Integration points**:
- Exposes `DosingHead` API for other modules to trigger doses
- Stores calibration data in NVS (your storage schema)

**Success criteria**: Can trigger precise doses via API, calibration persists across reboots, ±2% accuracy

---

### Module 2: WiFi Management - AP/STA Mode with Auto-Fallback

**Description**: WiFi state machine that handles AP mode for setup and STA mode for normal operation with automatic fallback

**Deliverables**:
- WiFiManager state machine (AP ↔ STA switching logic)
- Credential storage in NVS (encrypted with AES-256)
- WiFi status monitoring and auto-retry logic
- REST API endpoints: `/api/wifi/configure`, `/api/wifi/status`, `/api/wifi/reset`
- NTP time sync when in STA mode
- Configuration persistence
- Unit tests for state transitions

**Integration points**:
- Exposes WiFi status to other modules (IP address, mode, connectivity)
- Triggers events when mode changes (for MQTT module to start/stop)

**Success criteria**: Boots in AP mode, switches to STA after configuration, auto-falls back to AP if WiFi lost, reconnects automatically

---

### Module 3: Local Control - REST API & WebSocket Server

**Description**: Local HTTP server with REST API and WebSocket for real-time updates, accessible in both AP and STA modes

**Deliverables**:
- AsyncWebServer setup and routing
- REST endpoints: `/api/status`, `/api/schedules/*`, `/api/dose`, `/api/emergency-stop`, `/api/logs`
- WebSocket handler for real-time updates (dosing progress, errors, status)
- Request validation and input sanitization
- JSON request/response builders
- Optional: Web UI static files (HTML/CSS/JS)
- Integration tests for all endpoints

**Integration points**:
- Calls DosingHead module to trigger doses
- Reads WiFi status from WiFi module
- Publishes events to WebSocket clients
- Reads schedule data from Schedule module

**Success criteria**: API accessible at 192.168.4.1 in AP mode and local IP in STA mode, WebSocket provides real-time updates, all CRUD operations work

---

### Module 4: Cloud Integration - MQTT Client for AWS IoT

**Description**: MQTT client with TLS that connects to AWS IoT Core when device is in STA mode

**Deliverables**:
- MQTTClient with mutual TLS authentication
- Certificate storage in NVS
- Subscribe handlers for command topics (`commands/dose`, `schedules/sync`, `commands/config`, `commands/ota`)
- Publish logic for status and events (`status`, `events/dose`, `events/error`, `logs`)
- WiFi mode lifecycle management (start on STA, stop on AP)
- Message validation and JSON parsing
- Integration tests with mock MQTT broker

**Integration points**:
- Listens to WiFi module for mode changes (STA connected → start MQTT)
- Calls DosingHead module when dose commands received
- Publishes to cloud when doses executed

**Success criteria**: Connects to AWS IoT when in STA mode, receives and executes cloud commands, publishes events, gracefully disconnects when switching to AP

---

### Module 5: Scheduling System - Schedule Management & Execution

**Description**: Schedule storage, CRUD operations, and scheduler task that triggers doses at the right time. **One schedule per dosing head (4 total)**.

**Deliverables**:
- Schedule data structure (ONCE, DAILY, INTERVAL types)
- ScheduleStore for NVS persistence (4 schedules, one per head)
- Schedule CRUD logic (create, read, update, delete per head)
- SchedulerTask (FreeRTOS task, checks schedules every 1s)
- Schedule validation (time ranges, conflicts, etc.)
- REST API integration (endpoints handled by Module 3)
- Unit tests for schedule logic

**Integration points**:
- Calls DosingHead module when schedule triggers
- Integrates with WebServer module for schedule CRUD endpoints
- Uses NTP time from WiFi module

**Success criteria**: Schedules persist across reboots, trigger doses within ±1s of target time, handle all schedule types, can sync from cloud

---

### Module 6: Logging & Monitoring - Centralized Logging System

**Description**: Multi-level logging system with persistent storage and remote access

**Deliverables**:
- Logger class (multi-level: DEBUG, INFO, WARN, ERROR)
- LogStore (circular buffer in NVS, 200 entries)
- LoggingTask (FreeRTOS task for async log writing)
- REST endpoint `/api/logs` with pagination
- MQTT log publishing (when in STA mode)
- Serial output for debugging
- Log rotation logic

**Integration points**:
- All modules use Logger for event tracking
- WebServer module serves logs via REST
- MQTT module publishes logs to cloud

**Success criteria**: All system events logged, logs accessible via REST API, logs persist across reboots, old logs rotated out

---

### Module 7: System Orchestration - FreeRTOS Task Manager & Main Entry

**Description**: FreeRTOS task creation, inter-task communication, and main application entry point

**Deliverables**:
- TaskManager for creating all 8 FreeRTOS tasks
- Inter-task queues (command queue, motor queue, network queue, log queue)
- Synchronization primitives (mutexes for shared resources, event groups)
- Core assignment (Core 0: network/scheduling, Core 1: motors/dosing)
- main.cpp with initialization sequence
- SystemState singleton for global state
- ErrorHandler for centralized error handling
- Watchdog task for health monitoring

**Integration points**:
- Initializes all other modules in correct order
- Creates tasks for Scheduler, Logging, MQTT, WebServer, Motor Control
- Provides shared queues and mutexes to all modules

**Success criteria**: All tasks start successfully, no stack overflows, system stable under load, watchdog detects and recovers from failures

---

### Module 8: Testing & Quality Assurance

**Description**: Comprehensive test suite for all modules

**Deliverables**:
- Unit tests for each module (calibration math, schedule logic, WiFi state machine, etc.)
- Hardware integration tests (motor control, WiFi switching, NVS persistence)
- API integration tests (REST endpoints, WebSocket, MQTT)
- Performance tests (heap usage, task timing, dosing accuracy)
- Calibration procedure documentation
- Test fixtures and mocks

**Integration points**:
- Tests all module interfaces
- Validates integration between modules

**Success criteria**: >80% code coverage, all critical paths tested, hardware tests pass, performance targets met

---

## Module Development Guidelines

**For each module**:

1. **Plan first**: Understand module scope, dependencies, and integration points
2. **Add dependencies**: Update `platformio.ini` with required libraries
3. **Design classes**: Create module's class structure and interfaces
4. **Implement storage**: Define NVS schema if module needs persistence
5. **Write tests**: Unit tests during development, integration tests when complete
6. **Document**: Update this README with API contracts and integration notes

**Module independence**:
- Each module should be buildable and testable independently where possible
- Use interfaces/abstractions for dependencies on other modules
- Mock other modules in unit tests

**Communication between modules**:
- Prefer FreeRTOS queues for async communication
- Use mutexes for shared state access
- Document module's public API clearly

## API Reference

### REST API Endpoints (Local Access)

The WebServer is accessible at:
- **AP Mode**: 192.168.4.1 (when in setup/fallback mode)
- **STA Mode**: Local network IP assigned by DHCP (shown in `/api/wifi/status`)

```
GET    /api/status              - System status (mode, connectivity, uptime, etc.)
GET    /api/schedules           - List all schedules
POST   /api/schedules           - Create new schedule
PUT    /api/schedules/:id       - Update schedule
DELETE /api/schedules/:id       - Delete schedule
POST   /api/dose                - Ad-hoc dosing {head, volume_ml}
POST   /api/dose/multi          - Multi-head dosing {heads[], volumes[]}
POST   /api/calibrate           - Start calibration {head, target_volume_ml, duration_ms}
GET    /api/calibration         - Get calibration data
GET    /api/logs                - Retrieve logs (paginated)

WiFi Management Endpoints:
POST   /api/wifi/configure      - Set WiFi credentials and switch to STA {ssid, password}
GET    /api/wifi/status         - Current mode (AP/STA), connection status, IP addresses
POST   /api/wifi/reset          - Clear credentials and return to AP mode

Other:
POST   /api/emergency-stop      - Emergency stop all pumps
```

### MQTT Topics (Cloud Access - STA Mode Only)

MQTT client only operates when device is in STA mode and connected to WiFi. Automatically stops when falling back to AP mode.

**Subscribe**:
- `devices/{deviceId}/schedules/sync` - Schedule updates from cloud
- `devices/{deviceId}/commands/dose` - Ad-hoc dose commands
- `devices/{deviceId}/commands/config` - Configuration updates
- `devices/{deviceId}/commands/ota` - OTA update trigger

**Publish**:
- `devices/{deviceId}/status` - Periodic status (every 30s when in STA mode)
- `devices/{deviceId}/logs` - Log entries
- `devices/{deviceId}/events/dose` - Dosing events (real-time)
- `devices/{deviceId}/events/error` - Error notifications
- `devices/{deviceId}/events/online` - Device connected to WiFi and MQTT
- `devices/{deviceId}/events/offline` - Device disconnected (LWT - Last Will Testament)

## Data Flow Examples

### Ad-hoc Dosing via Local API
```
iPhone App / Browser → REST POST /api/dose
    ↓ WebServer (on 192.168.4.1 or STA IP) validates request
    ↓ CommandHandler enqueues to commandQueue
    ↓ DosingExecutionTask dequeues command
    ↓ DosingEngine calculates runtime from calibration
    ↓ MotorControlTask controls PWM pins
    ↓ Motor runs for calculated duration
    ↓ Log event to LogStore
    ↓ Notify via WebSocket (local clients)
    ↓ If in STA mode: Notify via MQTT to cloud
```

### Ad-hoc Dosing via Cloud (MQTT - STA Mode Only)
```
AWS IoT Core → MQTT command to devices/{id}/commands/dose
    ↓ NetworkTask receives MQTT message (only if in STA mode)
    ↓ CommandHandler validates and enqueues
    ↓ DosingExecutionTask executes dose
    ↓ MotorControlTask runs pumps
    ↓ Log event to LogStore
    ↓ StatusReportTask → MQTT event → AWS IoT
    ↓ Also notify local WebSocket clients (if any connected)
```

### Scheduled Dosing
```
SchedulerTask checks time (every 1s)
    ↓ Schedule is due → enqueue to commandQueue
    ↓ DosingExecutionTask executes dose
    ↓ MotorControlTask runs pumps
    ↓ Log event to LogStore
    ↓ Notify local WebSocket clients (if any)
    ↓ If in STA mode: StatusReportTask → MQTT → AWS IoT
    ↓ If in AP mode: Event only logged locally
```

### WiFi Mode Transitions
```
Initial Boot:
    ↓ Check NVS for saved credentials
    ↓ If credentials exist: Try STA mode
    ↓ If STA fails or no credentials: Start AP mode

WiFi Configuration (via iPhone app):
    ↓ POST /api/wifi/configure {ssid, password}
    ↓ Save credentials to NVS
    ↓ Switch from AP → STA mode
    ↓ Connect to WiFi
    ↓ Start MQTT client
    ↓ Start NTP sync

WiFi Failure (automatic):
    ↓ STA connection lost detected
    ↓ Stop MQTT client gracefully
    ↓ Switch from STA → AP mode
    ↓ Start retry timer (60s)
    ↓ If retry succeeds: Switch back to STA
    ↓ If retry fails: Stay in AP, retry again in 60s
```

## Dependencies

Dependencies are managed per-module. Add required libraries to `platformio.ini` during module development.

## Security Considerations

1. **Credential encryption**: WiFi passwords and AWS private keys encrypted with device-unique AES-256 key
2. **Input validation**: All API/MQTT inputs validated for range, type, format
3. **TLS for MQTT**: Mutual TLS authentication with AWS IoT
4. **AP mode password**: Default password should be changed via API
5. **Rate limiting**: Prevent DOS on web endpoints (optional for v1)

## Performance Targets

- **Dosing accuracy**: ±2% after calibration
- **Schedule timing**: ±1 second for scheduled doses
- **MQTT latency**: <500ms from command to execution start
- **WebSocket latency**: <100ms for local commands
- **Heap usage**: <50% during normal operation
- **Task stack**: No overflows (monitor with high water marks)

## Getting Started

### Prerequisites

1. PlatformIO installed
2. ESP32-S3 board definition
3. USB driver for Seeed XIAO ESP32S3

### Build and Upload

```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

### Initial Configuration

1. **First Boot** - Device starts in AP mode: `SquareDose-XXXXXX`

2. **iPhone App Setup** (Recommended):
   - Download the SquareDose iOS app
   - App automatically detects and connects to the device's AP
   - Use the app's guided setup workflow to:
     - Configure WiFi credentials (home router SSID/password)
     - Device switches from AP → STA mode
     - Set up AWS IoT certificates (optional)
     - Calibrate dosing heads
     - Create initial schedules
   - App uses REST API endpoints (`/api/wifi/configure`, etc.) under the hood

3. **Alternative: Manual API Setup** (for development/testing):
   - Connect your computer to the AP (default password in code)
   - Navigate to `http://192.168.4.1/api/status` to verify device is running
   - POST to `/api/wifi/configure` with WiFi credentials
   - Device switches to STA mode automatically
   - POST to `/api/calibrate` for each dosing head

**Access After WiFi Configuration**:
- **Normal Operation (STA Mode)**:
  - Via iPhone app on same WiFi network
  - Via local network at IP shown in `/api/wifi/status`
  - Via cloud through AWS IoT MQTT
- **Fallback (AP Mode if WiFi fails)**:
  - Connect to `SquareDose-XXXXXX` AP
  - Access at `192.168.4.1`
  - Check `/api/wifi/status` to see why STA failed
  - Can reconfigure WiFi or wait for auto-retry

## Calibration Procedure

For each head (0-3):

1. Send POST to `/api/calibrate` with:
   ```json
   {
     "head": 0,
     "target_volume_ml": 4
   }
   ```
2. Measure actual volume dispensed
3. System calculates and stores mL/s rate
4. Repeat for accuracy

## Companion iPhone App

A native iOS app is planned to provide a user-friendly interface for the SquareDose system:

**Features** (Out of scope for firmware - separate project):
- **Automated Setup Workflow**: Detects device AP, guides through WiFi configuration
- **WiFi Provisioning**: Configure home network credentials via intuitive UI (calls `/api/wifi/sta/enable`)
- **Calibration Wizard**: Step-by-step calibration process for all 4 dosing heads
- **Schedule Management**: Create, edit, and manage dosing schedules with schedule UI
- **Real-time Monitoring**: WebSocket connection for live dosing status and notifications
- **Multi-device Support**: Manage multiple SquareDose devices from one app
- **Cloud Integration**: View dosing history and logs from AWS IoT

**How It Works**:
- App connects to device via AP (192.168.4.1) for initial setup
- Once STA configured, app can connect via local network or cloud
- All communication uses the REST API and WebSocket protocols documented above
- No firmware changes needed - app is a client of the existing API

## Future Enhancements

- Flow sensors for closed-loop control
- Display/buttons for local UI
- Multi-device coordination
- Home Assistant MQTT Discovery
- Advanced scheduling (moon phases, conditional logic)
- Voice control integration (Alexa, Google Home)

## License

MIT License - see LICENSE file for details

## Contributing

Contributions should follow best practices for embedded C++ development, including proper resource management, real-time constraints, and hardware safety.

---

**Built with production-grade architecture for reliability, scalability, and maintainability.**
