import { signal } from '@preact/signals';
import type { RPCAPI } from '../../RPCAPI';

type File = {
  name: string;
  sizeKb: number;
}

type Dir = {
  name: string;
}

export class FileManager {
  // TODO: persist path in url hash
  $currentPath = signal<string[]>([]);
  $currentList = signal<(File | Dir)[]>([]);

  constructor(private rpc: RPCAPI) { }

  // Open dir stored in url hash or ~
  async openLastDir() {

  }

  async openDir(pathDescriptor: string[]) {

  }

  // after file is uploaded then re-fetch current directory files if user in target directory right now
  async uploadFileToDevice(file: File, pathDescriptor: string[]) {

  }

  async downloadFileFromDevice(pathDescriptor: string[]) {

  }

  // after file is deleted then re-fetch current directory
  async deleteFile(pathDescriptor: string[]) {

  }

  // after dir is deleted then exit from dir
  async deleteDir(pathDescriptor: string[]) {

  }
}
