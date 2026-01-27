/**
 * Transport Interface
 *
 * This is the ABSTRACT CONTRACT that all transport implementations must follow.
 *
 * WHY THIS PATTERN?
 * - Decouples "what you want to do" (call an API) from "how it's done" (HTTP/MQTT)
 * - Makes adding new transports trivial (just implement this interface)
 * - Enables testing with mock transports
 * - Your API services don't need to know or care about the underlying protocol
 *
 * DESIGN PRINCIPLE: "Program to an interface, not an implementation"
 */

import type { TransportType } from '../types';

/**
 * HTTP methods supported by transports
 * Note: MQTT transport will map these to topic patterns
 */
export type HttpMethod = 'GET' | 'POST' | 'PUT' | 'DELETE' | 'PATCH';

/**
 * Generic request options
 */
export interface TransportRequestOptions {
  /** Request timeout in ms (overrides default) */
  timeout?: number;

  /** Custom headers (HTTP) or metadata (MQTT) */
  headers?: Record<string, string>;

  /** Query parameters for GET requests */
  params?: Record<string, string | number | boolean>;

  /** Number of retry attempts (overrides default) */
  retries?: number;

  /** Signal for request cancellation */
  signal?: AbortSignal;
}

/**
 * Generic response from transport
 */
export interface TransportResponse<T = unknown> {
  /** Response data (parsed JSON) */
  data: T;

  /** HTTP status code (or equivalent for MQTT) */
  status: number;

  /** Response headers (HTTP) or metadata (MQTT) */
  headers?: Record<string, string>;

  /** Time taken for request in ms */
  duration: number;
}

/**
 * Error thrown by transports
 */
export class TransportError extends Error {
  constructor(
    message: string,
    public readonly code: TransportErrorCode,
    public readonly status?: number,
    public readonly cause?: Error
  ) {
    super(message);
    this.name = 'TransportError';

    // Maintains proper stack trace in V8 environments
    if (Error.captureStackTrace) {
      Error.captureStackTrace(this, TransportError);
    }
  }

  /** Helper to check if error is retryable */
  get isRetryable(): boolean {
    return RETRYABLE_ERROR_CODES.includes(this.code);
  }
}

/**
 * Transport error codes for consistent error handling
 */
export type TransportErrorCode =
  | 'NETWORK_ERROR'      // Device unreachable
  | 'TIMEOUT'            // Request timed out
  | 'PARSE_ERROR'        // Failed to parse response
  | 'HTTP_ERROR'         // HTTP error (4xx, 5xx)
  | 'CONNECTION_CLOSED'  // Connection was closed
  | 'CANCELLED'          // Request was cancelled
  | 'UNKNOWN';           // Unknown error

/** Error codes that should trigger a retry */
const RETRYABLE_ERROR_CODES: TransportErrorCode[] = [
  'NETWORK_ERROR',
  'TIMEOUT',
  'CONNECTION_CLOSED',
];

/**
 * Transport Interface
 *
 * All transport implementations (HTTP, MQTT, etc.) must implement this interface.
 * This ensures that the API client can work with any transport seamlessly.
 */
export interface Transport {
  /** Transport type identifier */
  readonly type: TransportType;

  /** Base URL or connection string */
  readonly baseUrl: string;

  /**
   * Execute a request
   *
   * @param method - HTTP method (GET, POST, etc.)
   * @param path - API path (e.g., "/api/status")
   * @param body - Request body for POST/PUT/PATCH
   * @param options - Additional request options
   * @returns Promise resolving to response data
   * @throws TransportError on failure
   */
  request<TResponse = unknown, TBody = unknown>(
    method: HttpMethod,
    path: string,
    body?: TBody,
    options?: TransportRequestOptions
  ): Promise<TransportResponse<TResponse>>;

  /**
   * Check if transport can reach the device
   *
   * @param timeout - Optional timeout in ms
   * @returns Promise resolving to true if reachable
   */
  isReachable(timeout?: number): Promise<boolean>;

  /**
   * Close any open connections
   * Called when switching transports or disconnecting
   */
  close(): void;
}
