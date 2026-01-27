/**
 * SquareDose API Client
 *
 * This is the main interface for calling firmware APIs.
 * It uses the ConnectionManager to handle the underlying transport.
 *
 * DESIGN:
 * - Organized by API domain (system, dosing, calibration, wifi, schedules, logs)
 * - Each method is fully typed with request/response interfaces
 * - Handles connection state automatically
 * - Clean, promise-based API
 *
 * USAGE:
 * ```typescript
 * import { api } from '@/services/api';
 *
 * // Get system status
 * const status = await api.system.getStatus();
 *
 * // Trigger a dose
 * await api.dosing.dose({ head: 0, volume: 5.0 });
 *
 * // Get schedules
 * const schedules = await api.schedules.getAll();
 * ```
 */

import { connectionManager } from '../device';
import type { Transport } from '../device';
import type {
  // System
  SystemStatusResponse,
  TimeResponse,
  SetTimeRequest,
  SetTimeResponse,
  // Dosing
  DoseRequest,
  DoseResponse,
  EmergencyStopResponse,
  // Calibration
  CalibrationResponse,
  CalibrateRequest,
  CalibrateResponse,
  // WiFi
  WifiStatusResponse,
  WifiConfigureRequest,
  WifiConfigureResponse,
  WifiResetResponse,
  // Schedules
  SchedulesResponse,
  Schedule,
  CreateScheduleRequest,
  CreateScheduleResponse,
  DeleteScheduleResponse,
  // Logs
  DashboardResponse,
  HourlyLogsParams,
  HourlyLogsResponse,
  DeleteLogsResponse,
  // Shared
  HeadIndex,
} from './types';

/**
 * Get the active transport or throw if not connected
 */
function getTransport(): Transport {
  return connectionManager.getTransport();
}

// ============================================================
// SYSTEM API
// ============================================================

export const systemApi = {
  /**
   * Get system status including WiFi state and dosing head info
   *
   * @example
   * const status = await api.system.getStatus();
   * console.log('Uptime:', status.uptime);
   * console.log('Heads:', status.dosingHeads);
   */
  async getStatus(): Promise<SystemStatusResponse> {
    const response = await getTransport().request<SystemStatusResponse>(
      'GET',
      '/api/status'
    );
    return response.data;
  },

  /**
   * Get current device time
   *
   * @example
   * const time = await api.system.getTime();
   * console.log('Synced:', time.synced);
   */
  async getTime(): Promise<TimeResponse> {
    const response = await getTransport().request<TimeResponse>(
      'GET',
      '/api/time'
    );
    return response.data;
  },

  /**
   * Set device time (for manual sync when NTP unavailable)
   *
   * @example
   * await api.system.setTime({ timestamp: Math.floor(Date.now() / 1000) });
   */
  async setTime(request: SetTimeRequest): Promise<SetTimeResponse> {
    const response = await getTransport().request<SetTimeResponse, SetTimeRequest>(
      'POST',
      '/api/time',
      request
    );
    return response.data;
  },

  /**
   * Sync device time to current phone time
   * Convenience method that wraps setTime
   */
  async syncTime(): Promise<SetTimeResponse> {
    return this.setTime({
      timestamp: Math.floor(Date.now() / 1000),
    });
  },
};

// ============================================================
// DOSING API
// ============================================================

export const dosingApi = {
  /**
   * Trigger a dose on a specific head
   *
   * Note: This returns immediately (202 Accepted).
   * The actual dosing happens in the background on the device.
   *
   * @example
   * await api.dosing.dose({ head: 0, volume: 5.0 });
   */
  async dose(request: DoseRequest): Promise<DoseResponse> {
    const response = await getTransport().request<DoseResponse, DoseRequest>(
      'POST',
      '/api/dose',
      request
    );
    return response.data;
  },

  /**
   * Emergency stop all dosing operations
   *
   * @example
   * await api.dosing.emergencyStop();
   */
  async emergencyStop(): Promise<EmergencyStopResponse> {
    const response = await getTransport().request<EmergencyStopResponse>(
      'POST',
      '/api/emergency-stop'
    );
    return response.data;
  },
};

// ============================================================
// CALIBRATION API
// ============================================================

export const calibrationApi = {
  /**
   * Get calibration data for all heads
   *
   * @example
   * const calibration = await api.calibration.getAll();
   * calibration.calibrations.forEach(c => {
   *   console.log(`Head ${c.head}: ${c.mlPerSecond} mL/s`);
   * });
   */
  async getAll(): Promise<CalibrationResponse> {
    const response = await getTransport().request<CalibrationResponse>(
      'GET',
      '/api/calibration'
    );
    return response.data;
  },

  /**
   * Calibrate a dosing head
   *
   * Workflow:
   * 1. User triggers a test dose (e.g., run pump for 10 seconds)
   * 2. User measures actual volume dispensed
   * 3. Call this with the measured volume
   * 4. Firmware calculates mL/second rate
   *
   * @example
   * await api.calibration.calibrate({ head: 0, actualVolume: 8.5 });
   */
  async calibrate(request: CalibrateRequest): Promise<CalibrateResponse> {
    const response = await getTransport().request<CalibrateResponse, CalibrateRequest>(
      'POST',
      '/api/calibrate',
      request
    );
    return response.data;
  },
};

// ============================================================
// WIFI API
// ============================================================

export const wifiApi = {
  /**
   * Get WiFi status
   *
   * @example
   * const wifi = await api.wifi.getStatus();
   * console.log('Mode:', wifi.mode); // 'AP' or 'STA'
   */
  async getStatus(): Promise<WifiStatusResponse> {
    const response = await getTransport().request<WifiStatusResponse>(
      'GET',
      '/api/wifi/status'
    );
    return response.data;
  },

  /**
   * Configure WiFi credentials (switches device to STA mode)
   *
   * Note: After calling this, the device will attempt to connect
   * to the specified network. You may lose connection if the phone
   * is connected to the device's AP.
   *
   * @example
   * await api.wifi.configure({ ssid: 'MyNetwork', password: 'secret' });
   */
  async configure(request: WifiConfigureRequest): Promise<WifiConfigureResponse> {
    const response = await getTransport().request<WifiConfigureResponse, WifiConfigureRequest>(
      'POST',
      '/api/wifi/configure',
      request
    );
    return response.data;
  },

  /**
   * Reset WiFi (clears credentials, switches to AP mode)
   *
   * @example
   * await api.wifi.reset();
   */
  async reset(): Promise<WifiResetResponse> {
    const response = await getTransport().request<WifiResetResponse>(
      'POST',
      '/api/wifi/reset'
    );
    return response.data;
  },
};

// ============================================================
// SCHEDULES API
// ============================================================

export const schedulesApi = {
  /**
   * Get all schedules
   *
   * @example
   * const { schedules } = await api.schedules.getAll();
   */
  async getAll(): Promise<SchedulesResponse> {
    const response = await getTransport().request<SchedulesResponse>(
      'GET',
      '/api/schedules'
    );
    return response.data;
  },

  /**
   * Get schedule for a specific head
   *
   * @example
   * const schedule = await api.schedules.get(0);
   */
  async get(head: HeadIndex): Promise<Schedule> {
    const response = await getTransport().request<Schedule>(
      'GET',
      `/api/schedules/${head}`
    );
    return response.data;
  },

  /**
   * Create or update a schedule
   *
   * @example
   * await api.schedules.create({
   *   head: 0,
   *   dailyTargetVolume: 100, // 100 mL per day
   *   dosesPerDay: 4,         // 4 times per day (25 mL each)
   *   name: 'Nutrient A',
   * });
   */
  async create(request: CreateScheduleRequest): Promise<CreateScheduleResponse> {
    const response = await getTransport().request<CreateScheduleResponse, CreateScheduleRequest>(
      'POST',
      '/api/schedules',
      request
    );
    return response.data;
  },

  /**
   * Delete a schedule
   *
   * @example
   * await api.schedules.delete(0);
   */
  async delete(head: HeadIndex): Promise<DeleteScheduleResponse> {
    const response = await getTransport().request<DeleteScheduleResponse>(
      'DELETE',
      `/api/schedules/${head}`
    );
    return response.data;
  },
};

// ============================================================
// LOGS API
// ============================================================

export const logsApi = {
  /**
   * Get dashboard summary (today's dosing stats per head)
   *
   * @example
   * const dashboard = await api.logs.getDashboard();
   * dashboard.heads.forEach(h => {
   *   console.log(`Head ${h.head}: ${h.percentComplete}% complete`);
   * });
   */
  async getDashboard(): Promise<DashboardResponse> {
    const response = await getTransport().request<DashboardResponse>(
      'GET',
      '/api/logs/dashboard'
    );
    return response.data;
  },

  /**
   * Get hourly dosing logs
   *
   * @example
   * // Last 24 hours (default)
   * const logs = await api.logs.getHourly();
   *
   * // Last 48 hours
   * const logs = await api.logs.getHourly({ hours: 48 });
   *
   * // Specific time range
   * const logs = await api.logs.getHourly({
   *   start: 1706140800,
   *   end: 1706227200,
   * });
   */
  async getHourly(params?: HourlyLogsParams): Promise<HourlyLogsResponse> {
    const response = await getTransport().request<HourlyLogsResponse>(
      'GET',
      '/api/logs/hourly',
      undefined,
      { params: params as Record<string, string | number | boolean> }
    );
    return response.data;
  },

  /**
   * Clear all dosing logs
   *
   * @example
   * await api.logs.clear();
   */
  async clear(): Promise<DeleteLogsResponse> {
    const response = await getTransport().request<DeleteLogsResponse>(
      'DELETE',
      '/api/logs'
    );
    return response.data;
  },
};

// ============================================================
// COMBINED API OBJECT
// ============================================================

/**
 * Main API client
 *
 * Provides access to all firmware APIs through a clean interface.
 *
 * @example
 * import { api } from '@/services/api';
 *
 * // System
 * const status = await api.system.getStatus();
 *
 * // Dosing
 * await api.dosing.dose({ head: 0, volume: 5.0 });
 *
 * // Schedules
 * const schedules = await api.schedules.getAll();
 */
export const api = {
  system: systemApi,
  dosing: dosingApi,
  calibration: calibrationApi,
  wifi: wifiApi,
  schedules: schedulesApi,
  logs: logsApi,
};

export default api;
