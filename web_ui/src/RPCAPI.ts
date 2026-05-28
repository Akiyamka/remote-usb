export type DeviceInfo = {
  fw_version: string;
  api_version: number;
  mode: DeviceMode;
  sd: {
    present: true;
    total_mb: number;
    free_mb: number;
  };
  wifi: {
    connected: true;
    ssid: string;
    ip: string;
    rssi: number;
  };
};

type DeviceLiveMessage = {
  deviceInfo: DeviceInfo;
};


export interface RPCAPI {
  subscribeToDevice: (cb: (message: DeviceLiveMessage) => void) => Promise<void>;

  // TODO : implement all from ### 11.2. Endpoints of docs/spec.md
}

export class RPC implements RPCAPI {}
