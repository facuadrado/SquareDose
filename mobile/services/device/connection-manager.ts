/**
 * Connection Manager
 *
 * The "brain" that manages device discovery and connection.
 *
 * RESPONSIBILITIES:
 * 1. Discover devices on the network (scan)
 * 2. Connect to a specific device
 * 3. Manage connection state
 * 4. Store/retrieve known devices
 * 5. Handle AP mode vs STA mode scenarios
 *
 * CONNECTION FLOW:
 * 1. User opens app
 * 2. App checks if connected to device's AP (SSID starts with "SquareDose-")
 *    - If yes: connect directly to 192.168.4.1
 * 3. If not in AP mode, try stored devices
 * 4. If no stored devices, scan network
 * 5. User selects device to connect
 */

import AsyncStorage from '@react-native-async-storage/async-storage';
import { HttpTransport } from './transports/http.transport';
import type { Transport } from './transports/transport.interface';
import type {
  DeviceInfo,
  ConnectionState,
  ConnectionConfig,
  ScanOptions,
  ScanResult,
} from './types';
import { DEFAULT_CONNECTION_CONFIG } from './types';

/** Storage key for saved devices */
const STORAGE_KEY = '@squaredose/devices';

/** Storage key for last connected device */
const LAST_DEVICE_KEY = '@squaredose/last_device';

/**
 * Connection Manager
 *
 * Usage:
 * ```typescript
 * const manager = new ConnectionManager();
 *
 * // Scan for devices
 * const result = await manager.scanNetwork();
 * console.log('Found devices:', result.devices);
 *
 * // Connect to a device
 * await manager.connect(result.devices[0]);
 *
 * // Get current transport for API calls
 * const transport = manager.getTransport();
 * const response = await transport.request('GET', '/api/status');
 * ```
 */
export class ConnectionManager {
  private config: ConnectionConfig;
  private transport: Transport | null = null;
  private state: ConnectionState = {
    status: 'disconnected',
    device: null,
    transportType: null,
  };

  private stateListeners: Set<(state: ConnectionState) => void> = new Set();

  constructor(config: Partial<ConnectionConfig> = {}) {
    this.config = { ...DEFAULT_CONNECTION_CONFIG, ...config };
  }

  // ============ Public API ============

  /**
   * Get current connection state
   */
  getState(): ConnectionState {
    return { ...this.state };
  }

  /**
   * Subscribe to connection state changes
   *
   * @returns Unsubscribe function
   */
  onStateChange(listener: (state: ConnectionState) => void): () => void {
    this.stateListeners.add(listener);
    return () => this.stateListeners.delete(listener);
  }

  /**
   * Get the active transport for making API calls
   *
   * @throws Error if not connected
   */
  getTransport(): Transport {
    if (!this.transport || this.state.status !== 'connected') {
      throw new Error('Not connected to any device');
    }
    return this.transport;
  }

  /**
   * Check if connected to a device
   */
  isConnected(): boolean {
    return this.state.status === 'connected' && this.transport !== null;
  }

  /**
   * Connect to a device
   *
   * @param device - Device to connect to
   * @throws Error if connection fails
   */
  async connect(device: DeviceInfo): Promise<void> {
    this.updateState({
      status: 'connecting',
      device,
      transportType: null,
    });

    try {
      // Create HTTP transport
      const transport = new HttpTransport({
        baseUrl: `http://${device.ipAddress}`,
        timeout: this.config.httpTimeout,
        retries: this.config.retryAttempts,
        retryDelay: this.config.retryDelay,
      });

      // Verify device is reachable
      const isReachable = await transport.isReachable();
      if (!isReachable) {
        throw new Error(`Device at ${device.ipAddress} is not reachable`);
      }

      // Store as last connected device
      await this.saveLastDevice(device);

      // Update state
      this.transport = transport;
      this.updateState({
        status: 'connected',
        device,
        transportType: 'http',
        lastCommunication: Date.now(),
      });
    } catch (error) {
      this.updateState({
        status: 'error',
        device: null,
        transportType: null,
        error: error instanceof Error ? error.message : 'Connection failed',
      });
      throw error;
    }
  }

  /**
   * Disconnect from current device
   */
  disconnect(): void {
    if (this.transport) {
      this.transport.close();
      this.transport = null;
    }

    this.updateState({
      status: 'disconnected',
      device: null,
      transportType: null,
    });
  }

  /**
   * Try to connect to AP mode device (192.168.4.1)
   *
   * Call this when you detect the phone is connected to a "SquareDose-*" WiFi
   */
  async connectToAPMode(): Promise<boolean> {
    const apDevice: DeviceInfo = {
      id: 'ap-mode',
      apSSID: 'SquareDose-AP',
      ipAddress: this.config.apModeIP,
      wifiMode: 'AP',
      discoveryMethod: 'ap_mode',
      lastSeen: Date.now(),
    };

    try {
      await this.connect(apDevice);
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Try to reconnect to the last connected device
   */
  async reconnectToLastDevice(): Promise<boolean> {
    const lastDevice = await this.getLastDevice();
    if (!lastDevice) {
      return false;
    }

    try {
      await this.connect(lastDevice);
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Scan network for SquareDose devices
   *
   * This probes common IP ranges looking for devices that respond
   * to /api/status with valid SquareDose data.
   */
  async scanNetwork(options: ScanOptions = {}): Promise<ScanResult> {
    const {
      timeout = 2000,
      port = 80,
    } = options;

    const startTime = Date.now();
    const devices: DeviceInfo[] = [];
    const errors: string[] = [];

    // Get IPs to scan (common home network ranges)
    const ipsToScan = this.getIPsToScan();

    // Scan in parallel batches to avoid overwhelming the network
    const BATCH_SIZE = 20;
    let scannedCount = 0;

    for (let i = 0; i < ipsToScan.length; i += BATCH_SIZE) {
      const batch = ipsToScan.slice(i, i + BATCH_SIZE);

      const results = await Promise.allSettled(
        batch.map((ip) => this.probeDevice(ip, port, timeout))
      );

      results.forEach((result) => {
        scannedCount++;
        if (result.status === 'fulfilled' && result.value) {
          devices.push(result.value);
        }
      });
    }

    return {
      devices,
      scannedCount,
      duration: Date.now() - startTime,
      errors: errors.length > 0 ? errors : undefined,
    };
  }

  /**
   * Manually add a device by IP address
   */
  async addDeviceByIP(ipAddress: string): Promise<DeviceInfo | null> {
    const device = await this.probeDevice(ipAddress, 80, 5000);

    if (device) {
      await this.saveDevice(device);
    }

    return device;
  }

  /**
   * Get all saved devices
   */
  async getSavedDevices(): Promise<DeviceInfo[]> {
    try {
      const data = await AsyncStorage.getItem(STORAGE_KEY);
      if (!data) return [];
      return JSON.parse(data);
    } catch {
      return [];
    }
  }

  /**
   * Save a device to storage
   */
  async saveDevice(device: DeviceInfo): Promise<void> {
    const devices = await this.getSavedDevices();

    // Update existing or add new
    const index = devices.findIndex((d) => d.id === device.id);
    if (index >= 0) {
      devices[index] = device;
    } else {
      devices.push(device);
    }

    await AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(devices));
  }

  /**
   * Remove a saved device
   */
  async removeDevice(deviceId: string): Promise<void> {
    const devices = await this.getSavedDevices();
    const filtered = devices.filter((d) => d.id !== deviceId);
    await AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(filtered));
  }

  // ============ Private Methods ============

  /**
   * Update connection state and notify listeners
   */
  private updateState(newState: Partial<ConnectionState>): void {
    this.state = { ...this.state, ...newState };

    this.stateListeners.forEach((listener) => {
      try {
        listener(this.getState());
      } catch (e) {
        console.error('[ConnectionManager] Error in state listener:', e);
      }
    });
  }

  /**
   * Probe a single IP to check if it's a SquareDose device
   */
  private async probeDevice(
    ip: string,
    port: number,
    timeout: number
  ): Promise<DeviceInfo | null> {
    const transport = new HttpTransport({
      baseUrl: `http://${ip}:${port}`,
      timeout,
      retries: 0,
    });

    try {
      const response = await transport.request<{
        apSSID?: string;
        wifiMode?: string;
        ipAddress?: string;
      }>('GET', '/api/status');

      // Verify it's a SquareDose device by checking response structure
      const data = response.data;
      if (data && typeof data.apSSID === 'string' && data.apSSID.startsWith('SquareDose-')) {
        // Extract device ID from SSID (e.g., "SquareDose-ABC1" -> "ABC1")
        const id = data.apSSID.replace('SquareDose-', '');

        return {
          id,
          apSSID: data.apSSID,
          ipAddress: ip,
          wifiMode: data.wifiMode === 'AP' ? 'AP' : 'STA',
          discoveryMethod: 'network_scan',
          lastSeen: Date.now(),
        };
      }

      return null;
    } catch {
      return null;
    } finally {
      transport.close();
    }
  }

  /**
   * Get list of IPs to scan
   *
   * Scans common home network subnets
   */
  private getIPsToScan(): string[] {
    const ips: string[] = [];

    // Common home network subnets
    const subnets = [
      '192.168.1',
      '192.168.0',
      '192.168.4',   // ESP32 AP mode subnet
      '192.168.50',  // Your network
      '10.0.0',
      '10.0.1',
    ];

    // Scan common IP ranges (1-254)
    // Prioritize lower numbers where routers usually assign
    const priorityRanges = [
      [1, 50],
      [51, 100],
      [100, 150],
      [150, 200],
      [200, 254],
    ];

    for (const subnet of subnets) {
      for (const [start, end] of priorityRanges) {
        for (let i = start; i <= end; i++) {
          ips.push(`${subnet}.${i}`);
        }
      }
    }

    return ips;
  }

  /**
   * Save last connected device
   */
  private async saveLastDevice(device: DeviceInfo): Promise<void> {
    try {
      await AsyncStorage.setItem(LAST_DEVICE_KEY, JSON.stringify(device));
    } catch (e) {
      console.error('[ConnectionManager] Failed to save last device:', e);
    }
  }

  /**
   * Get last connected device
   */
  private async getLastDevice(): Promise<DeviceInfo | null> {
    try {
      const data = await AsyncStorage.getItem(LAST_DEVICE_KEY);
      if (!data) return null;
      return JSON.parse(data);
    } catch {
      return null;
    }
  }
}

/** Singleton instance for convenience */
export const connectionManager = new ConnectionManager();
