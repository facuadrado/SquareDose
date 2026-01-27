/**
 * Services Module
 *
 * Main entry point for all SquareDose services.
 *
 * USAGE:
 * ```typescript
 * import { api, connectionManager } from '@/services';
 *
 * // First, connect to a device
 * await connectionManager.scanNetwork();
 * await connectionManager.connect(device);
 *
 * // Then use the API
 * const status = await api.system.getStatus();
 * await api.dosing.dose({ head: 0, volume: 5.0 });
 * ```
 */

// API Client
export { api } from './api';
export type * from './api';

// Device Connection
export {
  connectionManager,
  ConnectionManager,
  HttpTransport,
  TransportError,
  DEFAULT_CONNECTION_CONFIG,
} from './device';

export type {
  DeviceInfo,
  ConnectionState,
  ConnectionStatus,
  ConnectionConfig,
  Transport,
  ScanResult,
} from './device';
