import { signal } from '@preact/signals';
import { DeviceError, type DeviceInfo, type DeviceMode, type RPCAPI } from '../../RPCAPI';


export class Device {
  $currentMode = signal<DeviceMode>('switching');
  $isConnecting = signal<boolean>(false);
  $errorMessage = signal<string | null>(null);
  $deviceInfo = signal<null | DeviceInfo>(null);
  rpc: RPCAPI;

  constructor(rpc: RPCAPI) {
    this.rpc = rpc;
  }

  /**
   * Subscribes to device info changes and errors
   */
  async connect() {
    this.$isConnecting.value = true;
    try {
      await this.rpc.subscribeToDevice(({ deviceInfo }) => {
        this.$deviceInfo.value = deviceInfo;
        this.$currentMode.value = deviceInfo.mode;
      });
    } catch (e) {
      if (e instanceof DeviceError) {
        if (e.message === 'ConnectionLost') {
          // First time try to reconnect instantly, then increase the reconnect interval
          // Update connecting error message with seconds countdown until next attempt
          this.$errorMessage.value = 'Connection lost';
        }
      } else {
        this.$errorMessage.value = e instanceof Error ? e.message : String(e);
      }
    } finally {
      this.$isConnecting.value = false;
    }
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
