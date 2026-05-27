export type DeviceInfo = {
  firmware: string;
  totalStorageKb: number;
  availableStorageKb: number;
  /** From 1 to 5  */
  signalStrength: number;
};

type DeviceLiveMessage = {
  deviceInfo: DeviceInfo;
};

export interface RPCAPI {
  subscribeToDevice: (cb: (message: DeviceLiveMessage) => void) => Promise<void>;
}


export class RPC implements RPCAPI {

}
