import { encodePath, joinPath, type PathDescriptor } from './utils/path.js';

export type DeviceMode = 'usb' | 'http' | 'switching';

export type DeviceInfo = {
  fw_version: string;
  api_version: number;
  mode: DeviceMode;
  sd:
    | {
        present: true;
        total_mb: number | null;
        free_mb: number | null;
      }
    | {
        present: false;
      };
  wifi:
    | {
        connected: true;
        ssid: string;
        ip: string;
        rssi: number;
      }
    | {
        connected: false;
      };
};

export type DeviceLiveMessage = {
  deviceInfo: DeviceInfo;
};

export type ActiveDeviceMode = Exclude<DeviceMode, 'switching'>;

export type ModeSwitchResult = {
  ok: true;
  mode: ActiveDeviceMode;
  no_change?: true;
};

export type FileSystemEntry =
  | {
      name: string;
      type: 'dir';
    }
  | {
      name: string;
      type: 'file';
      sizeKb: number;
    };

export type FileListResult = {
  ok: true;
  path: string;
  entries: FileSystemEntry[];
};

export type FileUploadResult = {
  ok: true;
  path: string;
  size: number;
};

export type UploadProgress = {
  loaded: number;
  total: number | null;
  percent: number | null;
};

export type UploadFileOptions = {
  onProgress?: (progress: UploadProgress) => void;
  signal?: AbortSignal;
  contentType?: string;
};

export type RPCOptions = {
  baseUrl?: string;
  statusPollIntervalMs?: number;
  fetch?: FetchLike;
};

type FetchLike = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>;

type DeviceSubscriber = (message: DeviceLiveMessage) => void;

export type DeviceErrorCode = 'ConnectionLost';

export class DeviceError extends Error {
  readonly code: DeviceErrorCode;
  readonly cause: unknown;

  constructor(code: DeviceErrorCode, cause?: unknown) {
    super(code);
    this.name = 'DeviceError';
    this.code = code;
    this.cause = cause;
  }
}

export class RPCError extends Error {
  readonly status: number;
  readonly statusText: string;
  readonly body: unknown;
  readonly code?: string;

  constructor(status: number, statusText: string, body?: unknown) {
    const code = RPCError.extractErrorCode(body);
    const reason = RPCError.extractErrorReason(body);
    const details = [code, reason].filter((value): value is string => value !== undefined);

    super(
      details.length > 0
        ? `RPC ${status}: ${details.join(' - ')}`
        : `RPC ${status}: ${statusText || 'request failed'}`,
    );
    this.name = 'RPCError';
    this.status = status;
    this.statusText = statusText;
    this.body = body;
    this.code = code;
  }

  private static extractErrorCode(body: unknown): string | undefined {
    if (typeof body !== 'object' || body === null || !('error' in body)) {
      return undefined;
    }

    const error = (body as { error?: unknown }).error;
    return typeof error === 'string' ? error : undefined;
  }

  private static extractErrorReason(body: unknown): string | undefined {
    if (typeof body !== 'object' || body === null || !('reason' in body)) {
      return undefined;
    }

    const reason = (body as { reason?: unknown }).reason;
    return typeof reason === 'string' ? reason : undefined;
  }
}

export interface RPCAPI {
  subscribeToDevice: (cb: (message: DeviceLiveMessage) => void) => Promise<void>;

  getDeviceInfo: () => Promise<DeviceInfo>;
  switchToUsbMode: () => Promise<ModeSwitchResult>;
  switchToHttpMode: () => Promise<ModeSwitchResult>;
  listFiles: (pathDescriptor?: PathDescriptor) => Promise<FileListResult>;
  downloadFile: (pathDescriptor: PathDescriptor) => Promise<Blob>;
  uploadFile: (
    pathDescriptor: PathDescriptor,
    file: Blob,
    options?: UploadFileOptions,
  ) => Promise<FileUploadResult>;
  deletePath: (pathDescriptor: PathDescriptor) => Promise<void>;
  createDirectory: (pathDescriptor: PathDescriptor) => Promise<void>;
}

export class RPC implements RPCAPI {
  private readonly baseUrl: string;
  private readonly statusPollIntervalMs: number;
  private readonly fetchImpl: FetchLike;
  private readonly deviceSubscribers = new Set<DeviceSubscriber>();
  private statusPollTimer: ReturnType<typeof globalThis.setInterval> | undefined;
  private statusPollInFlight = false;

  constructor(options: RPCOptions = {}) {
    this.baseUrl = options.baseUrl?.replace(/\/$/, '') ?? '';
    this.statusPollIntervalMs = options.statusPollIntervalMs ?? 2_000;
    this.fetchImpl = options.fetch ?? globalThis.fetch.bind(globalThis);
  }

  async subscribeToDevice(cb: DeviceSubscriber): Promise<void> {
    this.deviceSubscribers.add(cb);

    try {
      const deviceInfo = await this.getDeviceInfo();
      cb({ deviceInfo });
      this.startDevicePolling();
    } catch (error) {
      this.deviceSubscribers.delete(cb);
      throw error;
    }
  }

  async getDeviceInfo(): Promise<DeviceInfo> {
    return await this.requestJson<DeviceInfo>('/api/status', { method: 'GET' }, 200);
  }

  async switchToUsbMode(): Promise<ModeSwitchResult> {
    return await this.requestJson<ModeSwitchResult>('/api/mode/usb', { method: 'POST' }, 200);
  }

  async switchToHttpMode(): Promise<ModeSwitchResult> {
    return await this.requestJson<ModeSwitchResult>('/api/mode/http', { method: 'POST' }, 200);
  }

  async listFiles(pathDescriptor: PathDescriptor = []): Promise<FileListResult> {
    const path = joinPath(pathDescriptor, true);
    return await this.requestJson<FileListResult>(
      `/api/files?path=${encodeURIComponent(path)}`,
      { method: 'GET' },
      200,
    );
  }

  async downloadFile(pathDescriptor: PathDescriptor): Promise<Blob> {
    const response = await this.requestOk(
      `/api/files/${encodePath(pathDescriptor, false)}`,
      { method: 'GET' },
      200,
    );

    return await response.blob();
  }

  async uploadFile(
    pathDescriptor: PathDescriptor,
    file: Blob,
    options: UploadFileOptions = {},
  ): Promise<FileUploadResult> {
    return await this.uploadFileWithXHR(
      `/api/files/${encodePath(pathDescriptor, false)}`,
      file,
      options,
    );
  }

  async deletePath(pathDescriptor: PathDescriptor): Promise<void> {
    await this.requestOk(`/api/files/${encodePath(pathDescriptor, false)}`, {
      method: 'DELETE',
    });
  }

  async createDirectory(pathDescriptor: PathDescriptor): Promise<void> {
    const path = joinPath(pathDescriptor, false);
    await this.requestOk(`/api/mkdir?path=${encodeURIComponent(path)}`, { method: 'POST' });
  }

  private startDevicePolling(): void {
    if (this.statusPollTimer !== undefined) {
      return;
    }

    this.statusPollTimer = globalThis.setInterval(() => {
      void this.pollDeviceStatus();
    }, this.statusPollIntervalMs);
  }

  private async pollDeviceStatus(): Promise<void> {
    if (this.statusPollInFlight) {
      return;
    }

    this.statusPollInFlight = true;

    try {
      const deviceInfo = await this.getDeviceInfo();
      for (const cb of this.deviceSubscribers) {
        cb({ deviceInfo });
      }
    } catch {
      // Keep polling. A temporary Wi-Fi drop should not permanently disconnect the UI.
    } finally {
      this.statusPollInFlight = false;
    }
  }

  private async requestJson<T>(
    path: string,
    init: RequestInit,
    expectedStatus?: number | readonly number[],
  ): Promise<T> {
    const response = await this.requestOk(path, this.withJsonAccept(init), expectedStatus);
    const payload = await this.readResponsePayload(response);
    return payload as T;
  }

  private async requestOk(
    path: string,
    init: RequestInit,
    expectedStatus?: number | readonly number[],
  ): Promise<Response> {
    const response = await this.request(path, this.withJsonAccept(init));

    if (!this.isExpectedStatus(response, expectedStatus)) {
      await this.throwRPCError(response);
    }

    return response;
  }

  private async request(path: string, init: RequestInit): Promise<Response> {
    try {
      return await this.fetchImpl(this.url(path), init);
    } catch (error) {
      throw new DeviceError('ConnectionLost', error);
    }
  }

  private url(path: string): string {
    return `${this.baseUrl}${path}`;
  }

  private withJsonAccept(init: RequestInit): RequestInit {
    const headers = new Headers(init.headers);
    if (!headers.has('Accept')) {
      headers.set('Accept', 'application/json');
    }

    return {
      ...init,
      headers,
    };
  }

  private isExpectedStatus(
    response: Response,
    expectedStatus: number | readonly number[] | undefined,
  ): boolean {
    if (expectedStatus === undefined) {
      return response.ok;
    }

    return Array.isArray(expectedStatus)
      ? expectedStatus.includes(response.status)
      : response.status === expectedStatus;
  }

  private async throwRPCError(response: Response): Promise<never> {
    const payload = await this.readResponsePayload(response);
    throw new RPCError(response.status, response.statusText, payload);
  }

  private async readResponsePayload(response: Response): Promise<unknown> {
    const contentType = response.headers.get('content-type') ?? '';
    const text = await response.text();
    return this.parseTextPayload(text, contentType);
  }

  private parseTextPayload(text: string, contentType: string): unknown {
    const trimmedText = text.trim();

    if (trimmedText === '') {
      return null;
    }

    if (contentType.includes('application/json') || trimmedText.startsWith('{')) {
      return JSON.parse(trimmedText);
    }

    return text;
  }

  private uploadFileWithXHR(
    path: string,
    file: Blob,
    options: UploadFileOptions,
  ): Promise<FileUploadResult> {
    return new Promise<FileUploadResult>((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      const abortUpload = () => xhr.abort();
      const cleanup = () => {
        options.signal?.removeEventListener('abort', abortUpload);
      };
      const resolveUpload = (result: FileUploadResult) => {
        cleanup();
        resolve(result);
      };
      const rejectUpload = (error: unknown) => {
        cleanup();
        reject(error);
      };

      if (options.signal?.aborted === true) {
        rejectUpload(new DOMException('Upload aborted', 'AbortError'));
        return;
      }

      options.signal?.addEventListener('abort', abortUpload, { once: true });

      xhr.open('POST', this.url(path), true);
      xhr.setRequestHeader('Accept', 'application/json');
      xhr.setRequestHeader('Content-Type', options.contentType ?? 'application/octet-stream');

      xhr.upload.onprogress = ({ lengthComputable, loaded, total }) => {
        const totalBytes = lengthComputable ? total : null;
        options.onProgress?.({
          loaded,
          total: totalBytes,
          percent: totalBytes === null || totalBytes === 0 ? null : (loaded / totalBytes) * 100,
        });
      };

      xhr.onload = () => {
        const payload = this.parseTextPayload(
          xhr.responseText,
          xhr.getResponseHeader('content-type') ?? '',
        );

        if (xhr.status === 201) {
          resolveUpload(payload as FileUploadResult);
          return;
        }

        rejectUpload(new RPCError(xhr.status, xhr.statusText, payload));
      };

      xhr.onerror = () => rejectUpload(new DeviceError('ConnectionLost'));
      xhr.ontimeout = () => rejectUpload(new DeviceError('ConnectionLost'));
      xhr.onabort = () => rejectUpload(new DOMException('Upload aborted', 'AbortError'));
      xhr.send(file);
    });
  }
}
