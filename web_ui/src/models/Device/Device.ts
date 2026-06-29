import { batch, signal } from '@preact/signals';
import { t } from '../../i18n.js';
import {
  DeviceError,
  type DeviceInfo,
  type DeviceLiveMessage,
  type DeviceMode,
  type RPCAPI,
} from '../../RPCAPI';

export class Device {
  static readonly expectedApiVersion = 1;

  private static readonly reconnectBaseDelayMs = 1_000;
  private static readonly reconnectMaxDelayMs = 30_000;

  $currentMode = signal<DeviceMode>('switching');
  $isConnecting = signal<boolean>(false);
  $errorMessage = signal<string | null>(null);
  $apiVersionMismatch = signal<{ expected: number; actual: number } | null>(null);
  $deviceInfo = signal<null | DeviceInfo>(null);
  rpc: RPCAPI;

  constructor(rpc: RPCAPI) {
    this.rpc = rpc;
  }

  /**
   * Subscribes to device info changes and errors
   */
  async connect() {
    let reconnectDelayMs = 0;

    this.$isConnecting.value = true;
    for (;;) {
      try {
        await this.rpc.subscribeToDevice((message) => this.handleDeviceMessage(message));

        batch(() => {
          this.$errorMessage.value = null;
          this.$isConnecting.value = false;
        });
        return;
      } catch (e) {
        if (e instanceof DeviceError && e.code === 'ConnectionLost') {
          this.clearLiveState();
          await this.waitBeforeReconnect(reconnectDelayMs);
          reconnectDelayMs = this.nextReconnectDelay(reconnectDelayMs);
          continue;
        }

        this.$errorMessage.value = e instanceof Error ? e.message : String(e);
        this.$isConnecting.value = false;
        return;
      }
    }
  }

  private handleDeviceMessage(message: DeviceLiveMessage): void {
    if (message.deviceInfo !== undefined) {
      this.applyDeviceInfo(message.deviceInfo);
      return;
    }

    this.handlePollingError(message.error);
  }

  private applyDeviceInfo(deviceInfo: DeviceInfo): void {
    batch(() => {
      this.$deviceInfo.value = deviceInfo;
      this.$currentMode.value = deviceInfo.mode;
      this.$errorMessage.value = null;
      this.$isConnecting.value = false;
      this.$apiVersionMismatch.value =
        deviceInfo.api_version === Device.expectedApiVersion
          ? null
          : { expected: Device.expectedApiVersion, actual: deviceInfo.api_version };
    });
  }

  private handlePollingError(error: unknown): void {
    this.clearLiveState();

    if (error instanceof DeviceError && error.code === 'ConnectionLost') {
      this.$errorMessage.value = t('app.connectionLostReconnecting');
      return;
    }

    this.$errorMessage.value = error instanceof Error ? error.message : String(error);
  }

  private clearLiveState(): void {
    batch(() => {
      this.$currentMode.value = 'switching';
      this.$deviceInfo.value = null;
      this.$apiVersionMismatch.value = null;
      this.$isConnecting.value = false;
    });
  }

  private async waitBeforeReconnect(delayMs: number): Promise<void> {
    if (delayMs === 0) {
      this.$errorMessage.value = t('app.connectionLostReconnecting');
      return;
    }

    let remainingMs = delayMs;

    while (remainingMs > 0) {
      this.$errorMessage.value = t('app.connectionLostReconnectingIn', {
        seconds: Math.ceil(remainingMs / 1_000),
      });

      const stepMs = Math.min(remainingMs, 1_000);
      await this.delay(stepMs);
      remainingMs -= stepMs;
    }

    this.$errorMessage.value = t('app.connectionLostReconnecting');
  }

  private nextReconnectDelay(currentDelayMs: number): number {
    if (currentDelayMs === 0) {
      return Device.reconnectBaseDelayMs;
    }

    return Math.min(currentDelayMs * 2, Device.reconnectMaxDelayMs);
  }

  private async delay(ms: number): Promise<void> {
    await new Promise<void>((resolve) => {
      globalThis.setTimeout(resolve, ms);
    });
  }

  restart() {}

  // Toggle between HTTP file-management mode and USB MSC mode.
  async toggleState() {
    const result =
      this.$currentMode.value === 'http'
        ? await this.rpc.switchToUsbMode()
        : await this.rpc.switchToHttpMode();

    this.$currentMode.value = result.mode;
  }
}
