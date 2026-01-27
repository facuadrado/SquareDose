/**
 * HTTP Transport Implementation
 *
 * This implements the Transport interface for direct HTTP communication
 * with SquareDose devices on the local network.
 *
 * FEATURES:
 * - Automatic retry with exponential backoff
 * - Request timeout handling
 * - Proper error classification
 *
 * PRODUCTION CONSIDERATIONS:
 * - Uses AbortController for timeout (modern, cancellable)
 * - Implements exponential backoff to avoid overwhelming device
 * - All errors are typed for consistent handling
 */

import type {
  Transport,
  HttpMethod,
  TransportRequestOptions,
  TransportResponse,
  TransportErrorCode,
} from './transport.interface';
import { TransportError } from './transport.interface';
import type { TransportType } from '../types';

/**
 * Configuration for HTTP transport
 */
export interface HttpTransportConfig {
  /** Base URL (e.g., "http://192.168.4.1") */
  baseUrl: string;

  /** Default timeout in ms (default: 5000) */
  timeout?: number;

  /** Number of retry attempts (default: 3) */
  retries?: number;

  /** Base delay for exponential backoff in ms (default: 1000) */
  retryDelay?: number;
}

/**
 * Default configuration values
 */
const DEFAULT_CONFIG: Required<Omit<HttpTransportConfig, 'baseUrl'>> = {
  timeout: 5000,
  retries: 3,
  retryDelay: 1000,
};

/**
 * HTTP Transport for local network communication
 *
 * Usage:
 * ```typescript
 * const transport = new HttpTransport({ baseUrl: 'http://192.168.4.1' });
 *
 * // Make a request
 * const response = await transport.request('GET', '/api/status');
 * console.log(response.data);
 *
 * // POST with body
 * const doseResponse = await transport.request('POST', '/api/dose', {
 *   head: 0,
 *   volume: 5.0
 * });
 * ```
 */
export class HttpTransport implements Transport {
  readonly type: TransportType = 'http';
  readonly baseUrl: string;

  private readonly config: Required<Omit<HttpTransportConfig, 'baseUrl'>>;

  constructor(config: HttpTransportConfig) {
    // Normalize base URL (remove trailing slash)
    this.baseUrl = config.baseUrl.replace(/\/$/, '');
    this.config = { ...DEFAULT_CONFIG, ...config };
  }

  /**
   * Execute an HTTP request with retry logic
   */
  async request<TResponse = unknown, TBody = unknown>(
    method: HttpMethod,
    path: string,
    body?: TBody,
    options?: TransportRequestOptions
  ): Promise<TransportResponse<TResponse>> {
    const url = this.buildUrl(path, options?.params);
    const timeout = options?.timeout ?? this.config.timeout;
    const maxRetries = options?.retries ?? this.config.retries;

    let lastError: TransportError | null = null;
    let attempt = 0;

    while (attempt <= maxRetries) {
      const startTime = Date.now();

      try {
        const response = await this.executeRequest<TResponse, TBody>(
          method,
          url,
          body,
          timeout,
          options
        );

        return {
          ...response,
          duration: Date.now() - startTime,
        };
      } catch (error) {
        lastError = this.normalizeError(error);

        // Don't retry if not retryable or cancelled
        if (!lastError.isRetryable || options?.signal?.aborted) {
          throw lastError;
        }

        attempt++;

        if (attempt <= maxRetries) {
          // Exponential backoff: 1s, 2s, 4s, etc.
          const delay = this.config.retryDelay * Math.pow(2, attempt - 1);
          await this.sleep(delay);
        }
      }
    }

    // All retries exhausted
    throw lastError ?? new TransportError('Unknown error', 'UNKNOWN');
  }

  /**
   * Check if device is reachable by hitting /api/status
   */
  async isReachable(timeout = 2000): Promise<boolean> {
    try {
      await this.request('GET', '/api/status', undefined, {
        timeout,
        retries: 0, // No retries for reachability check
      });
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Close transport (no-op for HTTP, but required by interface)
   */
  close(): void {
    // HTTP is stateless, nothing to close
  }

  // ============ Private Methods ============

  /**
   * Execute a single HTTP request (no retry logic)
   */
  private async executeRequest<TResponse, TBody>(
    method: HttpMethod,
    url: string,
    body?: TBody,
    timeout?: number,
    options?: TransportRequestOptions
  ): Promise<Omit<TransportResponse<TResponse>, 'duration'>> {
    // Create abort controller for timeout
    const controller = new AbortController();
    const timeoutId = timeout
      ? setTimeout(() => controller.abort(), timeout)
      : null;

    // Chain with external signal if provided
    if (options?.signal) {
      options.signal.addEventListener('abort', () => controller.abort());
    }

    try {
      const response = await fetch(url, {
        method,
        headers: {
          'Content-Type': 'application/json',
          Accept: 'application/json',
          ...options?.headers,
        },
        body: body ? JSON.stringify(body) : undefined,
        signal: controller.signal,
      });

      // Parse response
      let data: TResponse;
      const contentType = response.headers.get('content-type');

      if (contentType?.includes('application/json')) {
        data = await response.json();
      } else {
        // Handle non-JSON responses
        const text = await response.text();
        data = text as unknown as TResponse;
      }

      // Check for HTTP errors
      if (!response.ok) {
        throw new TransportError(
          `HTTP ${response.status}: ${response.statusText}`,
          'HTTP_ERROR',
          response.status
        );
      }

      return {
        data,
        status: response.status,
        headers: this.parseHeaders(response.headers),
      };
    } catch (error) {
      // Re-throw TransportErrors as-is
      if (error instanceof TransportError) {
        throw error;
      }

      // Handle abort/timeout
      if (error instanceof Error && error.name === 'AbortError') {
        if (options?.signal?.aborted) {
          throw new TransportError('Request cancelled', 'CANCELLED');
        }
        throw new TransportError('Request timed out', 'TIMEOUT');
      }

      // Handle network errors
      if (error instanceof TypeError) {
        throw new TransportError(
          'Network error - device may be unreachable',
          'NETWORK_ERROR',
          undefined,
          error
        );
      }

      throw error;
    } finally {
      if (timeoutId) {
        clearTimeout(timeoutId);
      }
    }
  }

  /**
   * Build URL with query parameters
   */
  private buildUrl(
    path: string,
    params?: Record<string, string | number | boolean>
  ): string {
    const url = new URL(path, this.baseUrl);

    if (params) {
      Object.entries(params).forEach(([key, value]) => {
        url.searchParams.append(key, String(value));
      });
    }

    return url.toString();
  }

  /**
   * Parse response headers to plain object
   */
  private parseHeaders(headers: Headers): Record<string, string> {
    const result: Record<string, string> = {};
    headers.forEach((value, key) => {
      result[key] = value;
    });
    return result;
  }

  /**
   * Normalize any error to TransportError
   */
  private normalizeError(error: unknown): TransportError {
    if (error instanceof TransportError) {
      return error;
    }

    if (error instanceof Error) {
      // Classify error type
      let code: TransportErrorCode = 'UNKNOWN';

      if (error.name === 'AbortError') {
        code = 'CANCELLED';
      } else if (error.message.includes('network') || error instanceof TypeError) {
        code = 'NETWORK_ERROR';
      } else if (error.message.includes('timeout')) {
        code = 'TIMEOUT';
      } else if (error.message.includes('JSON')) {
        code = 'PARSE_ERROR';
      }

      return new TransportError(error.message, code, undefined, error);
    }

    return new TransportError(String(error), 'UNKNOWN');
  }

  /**
   * Sleep utility for retry delays
   */
  private sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }
}
