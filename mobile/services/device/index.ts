/**
 * Device Module Exports
 *
 * This is the main entry point for the device connection layer.
 *
 * Usage:
 * ```typescript
 * import {
 *   connectionManager,
 *   ConnectionManager,
 *   HttpTransport,
 *   TransportError,
 * } from '@/services/device';
 *
 * // Connect to a device
 * await connectionManager.connect(device);
 *
 * // Make API calls through the transport
 * const transport = connectionManager.getTransport();
 * const response = await transport.request('GET', '/api/status');
 * ```
 */

// Types
export type {
  DeviceInfo,
  ConnectionState,
  ConnectionStatus,
  ConnectionConfig,
  DiscoveryMethod,
  TransportType,
  ScanOptions,
  ScanResult,
} from './types';

export { DEFAULT_CONNECTION_CONFIG } from './types';

// Connection Manager
export { ConnectionManager, connectionManager } from './connection-manager';

// Transport Layer
export {
  HttpTransport,
  TransportError,
  type Transport,
  type HttpMethod,
  type TransportRequestOptions,
  type TransportResponse,
  type TransportErrorCode,
  type HttpTransportConfig,
} from './transports';
