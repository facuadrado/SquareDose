/**
 * Transport Layer Exports
 *
 * This barrel file provides clean imports for the transport layer.
 *
 * Usage:
 * ```typescript
 * import { HttpTransport, TransportError } from '@/services/device/transports';
 * ```
 */

// Interface and types
export type {
  Transport,
  HttpMethod,
  TransportRequestOptions,
  TransportResponse,
  TransportErrorCode,
} from './transport.interface';

export { TransportError } from './transport.interface';

// Implementations
export { HttpTransport } from './http.transport';
export type { HttpTransportConfig } from './http.transport';
