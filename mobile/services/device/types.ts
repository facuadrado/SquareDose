/**
 * Device Connection Types
 *
 * These types define how we identify and connect to SquareDose devices.
 *
 * KEY CONCEPTS:
 * 1. DeviceInfo - Represents a discovered device (from network scan or stored)
 * 2. ConnectionState - Tracks the current connection status
 * 3. TransportType - Defines HOW we communicate (HTTP now, MQTT later)
 */

/**
 * How the device was discovered or is being connected to
 */
export type DiscoveryMethod = 'ap_mode' | 'network_scan' | 'manual' | 'stored' | 'mdns';

/**
 * Transport protocol for communication
 * - 'http': Direct HTTP calls (local network)
 * - 'mqtt': Cloud relay via MQTT (future)
 */
export type TransportType = 'http' | 'mqtt';

/**
 * Represents a SquareDose device that has been discovered or stored
 */
export interface DeviceInfo {
  /** Unique device ID (from ESP32 MAC address, e.g., "ABC1") */
  id: string;

  /** Full AP SSID when in AP mode (e.g., "SquareDose-ABC1") */
  apSSID: string;

  /** Current IP address (192.168.4.1 for AP, dynamic for STA) */
  ipAddress: string;

  /** Current WiFi mode of the device */
  wifiMode: 'AP' | 'STA';

  /** How this device was discovered */
  discoveryMethod: DiscoveryMethod;

  /** Timestamp when device was last seen/verified */
  lastSeen: number;

  /** Optional friendly name set by user */
  name?: string;
}

/**
 * Connection state machine states
 *
 * State transitions:
 * disconnected -> connecting -> connected
 * connected -> disconnected (on error/timeout)
 * any -> error (on fatal error)
 */
export type ConnectionStatus =
  | 'disconnected'
  | 'connecting'
  | 'connected'
  | 'error';

/**
 * Current connection state with metadata
 */
export interface ConnectionState {
  status: ConnectionStatus;

  /** Currently connected device (if any) */
  device: DeviceInfo | null;

  /** Active transport type */
  transportType: TransportType | null;

  /** Error message if status is 'error' */
  error?: string;

  /** Timestamp of last successful communication */
  lastCommunication?: number;
}

/**
 * Options for network scanning
 */
export interface ScanOptions {
  /** Timeout for each device probe in ms (default: 2000) */
  timeout?: number;

  /** Subnet to scan (default: derived from current IP) */
  subnet?: string;

  /** Port to probe (default: 80) */
  port?: number;
}

/**
 * Result from a network scan
 */
export interface ScanResult {
  /** Devices found during scan */
  devices: DeviceInfo[];

  /** Total IPs scanned */
  scannedCount: number;

  /** Time taken in ms */
  duration: number;

  /** Any errors encountered */
  errors?: string[];
}

/**
 * Configuration for the connection manager
 */
export interface ConnectionConfig {
  /** AP mode IP address (default: 192.168.4.1) */
  apModeIP: string;

  /** HTTP timeout in ms (default: 5000) */
  httpTimeout: number;

  /** Number of retry attempts (default: 3) */
  retryAttempts: number;

  /** Delay between retries in ms (default: 1000) */
  retryDelay: number;

  /** Enable auto-reconnect on disconnect (default: true) */
  autoReconnect: boolean;

  /** Interval for keep-alive pings in ms (default: 30000) */
  keepAliveInterval: number;
}

/**
 * Default connection configuration
 * These values are tuned for ESP32 on local network
 */
export const DEFAULT_CONNECTION_CONFIG: ConnectionConfig = {
  apModeIP: '192.168.4.1',
  httpTimeout: 5000,
  retryAttempts: 3,
  retryDelay: 1000,
  autoReconnect: true,
  keepAliveInterval: 30000,
};
