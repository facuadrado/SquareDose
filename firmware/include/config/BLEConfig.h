#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

// Nordic UART Service (NUS) UUIDs
// Using standard NUS UUIDs for compatibility with BLE serial apps
#define BLE_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_CHAR_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (app -> device)
#define BLE_TX_CHAR_UUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify (device -> app)

// BLE Configuration
#define BLE_MTU_SIZE            512
#define BLE_TX_BUFFER_SIZE      512
#define BLE_RX_BUFFER_SIZE      512

// Advertising interval in milliseconds
#define BLE_ADVERTISING_INTERVAL_MIN  100
#define BLE_ADVERTISING_INTERVAL_MAX  200

// Connection parameters
#define BLE_CONN_INTERVAL_MIN   6    // 7.5ms (6 * 1.25ms)
#define BLE_CONN_INTERVAL_MAX   12   // 15ms (12 * 1.25ms)
#define BLE_CONN_LATENCY        0
#define BLE_CONN_TIMEOUT        200  // 2000ms (200 * 10ms)

#endif // BLE_CONFIG_H
