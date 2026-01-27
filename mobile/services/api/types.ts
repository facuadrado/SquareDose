/**
 * API Types
 *
 * TypeScript interfaces for all SquareDose firmware API endpoints.
 * These types are derived directly from WebServer.cpp to ensure type safety.
 *
 * ORGANIZATION:
 * - Each API group has its own section
 * - Request types end with "Request"
 * - Response types end with "Response"
 * - Shared types are at the top
 */

// ============================================================
// SHARED TYPES
// ============================================================

/** Dosing head index (0-3) */
export type HeadIndex = 0 | 1 | 2 | 3;

/** Generic success response */
export interface SuccessResponse {
  success: boolean;
  message?: string;
  error?: string;
}

/** Generic error response from firmware */
export interface ErrorResponse {
  error: string;
}

// ============================================================
// SYSTEM API TYPES
// GET /api/status
// GET /api/time
// POST /api/time
// ============================================================

/** Single dosing head status in system response */
export interface DosingHeadStatus {
  index: HeadIndex;
  isDispensing: boolean;
  isCalibrated: boolean;
  mlPerSecond: number;
}

/** GET /api/status response */
export interface SystemStatusResponse {
  uptime: number;
  wifiMode: 'AP' | 'STA';
  wifiConnected: boolean;
  ipAddress: string;
  apSSID: string;
  dosingHeads: DosingHeadStatus[];
}

/** GET /api/time response */
export interface TimeResponse {
  timestamp: number;
  synced: boolean;
  source: string;
}

/** POST /api/time request */
export interface SetTimeRequest {
  timestamp: number;
}

/** POST /api/time response */
export interface SetTimeResponse extends SuccessResponse {
  timestamp: number;
}

// ============================================================
// DOSING API TYPES
// POST /api/dose
// POST /api/emergency-stop
// ============================================================

/** POST /api/dose request */
export interface DoseRequest {
  head: HeadIndex;
  /** Volume in mL (0.1 - 1000) */
  volume: number;
}

/** POST /api/dose response (202 Accepted) */
export interface DoseResponse extends SuccessResponse {
  head: HeadIndex;
  targetVolume: number;
  note?: string;
}

/** POST /api/emergency-stop response */
export interface EmergencyStopResponse extends SuccessResponse {
  // Just success and message
}

// ============================================================
// CALIBRATION API TYPES
// GET /api/calibration
// POST /api/calibrate
// ============================================================

/** Single head calibration data */
export interface CalibrationData {
  head: HeadIndex;
  isCalibrated: boolean;
  mlPerSecond: number;
  lastCalibrationTime: number;
}

/** GET /api/calibration response */
export interface CalibrationResponse {
  calibrations: CalibrationData[];
}

/** POST /api/calibrate request */
export interface CalibrateRequest {
  head: HeadIndex;
  /** Actual volume dispensed in mL (measured by user) */
  actualVolume: number;
}

/** POST /api/calibrate response */
export interface CalibrateResponse extends SuccessResponse {
  head: HeadIndex;
  mlPerSecond?: number;
  isCalibrated?: boolean;
}

// ============================================================
// WIFI API TYPES
// GET /api/wifi/status
// POST /api/wifi/configure
// POST /api/wifi/reset
// ============================================================

/** GET /api/wifi/status response */
export interface WifiStatusResponse {
  mode: 'AP' | 'STA';
  connected: boolean;
  ipAddress: string;
  apSSID: string;
}

/** POST /api/wifi/configure request */
export interface WifiConfigureRequest {
  ssid: string;
  password: string;
}

/** POST /api/wifi/configure response */
export interface WifiConfigureResponse extends SuccessResponse {
  note?: string;
}

/** POST /api/wifi/reset response */
export interface WifiResetResponse extends SuccessResponse {
  note?: string;
  apSSID: string;
}

// ============================================================
// SCHEDULE API TYPES
// GET /api/schedules
// GET /api/schedules/:head
// POST /api/schedules
// DELETE /api/schedules/:head
// ============================================================

/** Schedule data structure */
export interface Schedule {
  head: HeadIndex;
  name: string;
  enabled: boolean;
  /** Total mL to dispense per day */
  dailyTargetVolume: number;
  /** Number of doses per day */
  dosesPerDay: number;
  /** Calculated: mL per dose */
  volume: number;
  /** Calculated: seconds between doses */
  intervalSeconds: number;
  /** Unix timestamp of last execution */
  lastExecutionTime: number;
  /** Total executions since creation */
  executionCount: number;
  /** Unix timestamp when created */
  createdAt: number;
  /** Unix timestamp when last updated */
  updatedAt: number;
}

/** GET /api/schedules response */
export interface SchedulesResponse {
  schedules: Schedule[];
  count: number;
}

/** GET /api/schedules/:head response - same as Schedule */
export type ScheduleResponse = Schedule;

/** POST /api/schedules request */
export interface CreateScheduleRequest {
  head: HeadIndex;
  /** Total mL to dispense per day */
  dailyTargetVolume: number;
  /** Number of doses per day (1-1440) */
  dosesPerDay: number;
  /** Optional: enable/disable (default: true) */
  enabled?: boolean;
  /** Optional: friendly name */
  name?: string;
}

/** POST /api/schedules response */
export interface CreateScheduleResponse extends SuccessResponse {
  head: HeadIndex;
}

/** DELETE /api/schedules/:head response */
export interface DeleteScheduleResponse extends SuccessResponse {
  head: HeadIndex;
}

// ============================================================
// LOG API TYPES
// GET /api/logs/dashboard
// GET /api/logs/hourly
// DELETE /api/logs
// ============================================================

/** Dashboard summary for a single head */
export interface DashboardHeadSummary {
  head: HeadIndex;
  /** Target mL for today from schedule */
  dailyTarget: number;
  /** Scheduled doses dispensed today (mL) */
  scheduledActual: number;
  /** Ad-hoc (manual) doses today (mL) */
  adhocTotal: number;
  /** Doses per day from schedule */
  dosesPerDay: number;
  /** Volume per dose (mL) */
  perDoseVolume: number;
  /** Total dispensed today (scheduled + adhoc) */
  totalToday: number;
  /** Percentage of daily target complete */
  percentComplete: number;
}

/** GET /api/logs/dashboard response */
export interface DashboardResponse {
  heads: DashboardHeadSummary[];
  timestamp: number;
  count: number;
}

/** Single hourly log entry */
export interface HourlyLog {
  /** Start of hour (Unix timestamp) */
  hourTimestamp: number;
  head: HeadIndex;
  /** Scheduled volume dispensed this hour (mL) */
  scheduledVolume: number;
  /** Ad-hoc volume dispensed this hour (mL) */
  adhocVolume: number;
  /** Total volume this hour (mL) */
  totalVolume: number;
}

/** GET /api/logs/hourly query parameters */
export interface HourlyLogsParams {
  /** Number of hours to look back (default: 24, max: 336) */
  hours?: number;
  /** Start timestamp (overrides hours) */
  start?: number;
  /** End timestamp (overrides hours) */
  end?: number;
}

/** GET /api/logs/hourly response */
export interface HourlyLogsResponse {
  logs: HourlyLog[];
  count: number;
  startTime: number;
  endTime: number;
}

/** DELETE /api/logs response */
export interface DeleteLogsResponse extends SuccessResponse {
  // Just success and message
}
