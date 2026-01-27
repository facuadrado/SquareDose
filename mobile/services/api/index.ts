/**
 * API Module Exports
 *
 * Usage:
 * ```typescript
 * import { api } from '@/services/api';
 *
 * const status = await api.system.getStatus();
 * await api.dosing.dose({ head: 0, volume: 5.0 });
 * ```
 */

// Main API client
export {
  api,
  systemApi,
  dosingApi,
  calibrationApi,
  wifiApi,
  schedulesApi,
  logsApi,
} from './client';

// All types
export type {
  // Shared
  HeadIndex,
  SuccessResponse,
  ErrorResponse,
  // System
  DosingHeadStatus,
  SystemStatusResponse,
  TimeResponse,
  SetTimeRequest,
  SetTimeResponse,
  // Dosing
  DoseRequest,
  DoseResponse,
  EmergencyStopResponse,
  // Calibration
  CalibrationData,
  CalibrationResponse,
  CalibrateRequest,
  CalibrateResponse,
  // WiFi
  WifiStatusResponse,
  WifiConfigureRequest,
  WifiConfigureResponse,
  WifiResetResponse,
  // Schedules
  Schedule,
  SchedulesResponse,
  ScheduleResponse,
  CreateScheduleRequest,
  CreateScheduleResponse,
  DeleteScheduleResponse,
  // Logs
  DashboardHeadSummary,
  DashboardResponse,
  HourlyLog,
  HourlyLogsParams,
  HourlyLogsResponse,
  DeleteLogsResponse,
} from './types';
