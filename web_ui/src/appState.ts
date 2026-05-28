import { Device } from '#models/Device/Device.js';
import { FileManager } from '#models/FileManager/FileManager.js';
import { RPC } from './RPCAPI.js';

export const rpc = new RPC({ statusPollIntervalMs: 3000 });
export const device = new Device(rpc);
export const fileManager = new FileManager(rpc);
