import { signal } from '@preact/signals';
import type { FileSystemEntry, RPCAPI } from '../../RPCAPI';

export class FileManager {
  // TODO: persist path in url hash
  $currentPath = signal<string[]>([]);
  $currentList = signal<FileSystemEntry[]>([]);

  constructor(private rpc: RPCAPI) {}

  // Open dir stored in url hash or ~
  async openLastDir() {
    await this.openDir(this.$currentPath.value);
  }

  async openDir(pathDescriptor: string[]) {
    const result = await this.rpc.listFiles(pathDescriptor);
    this.$currentPath.value = [...pathDescriptor];
    this.$currentList.value = result.entries;
  }

  // after file is uploaded then re-fetch current directory files if user in target directory right now
  // upload only one file at time
  // use XMLHttpRequest to be able track progress
  // send files as raw body (File/Blob, not multipart)
  async uploadFilesToDevice(files: File[], pathDescriptor: string[]) {
    for (const file of files) {
      await this.rpc.uploadFile([...pathDescriptor, file.name], file);
    }

    if (this.isSamePath(this.$currentPath.value, pathDescriptor)) {
      await this.openDir(pathDescriptor);
    }
  }

  async downloadFileFromDevice(pathDescriptor: string[]) {
    return await this.rpc.downloadFile(pathDescriptor);
  }

  // after file is deleted then re-fetch current directory
  async deleteFile(pathDescriptor: string[]) {
    await this.rpc.deletePath(pathDescriptor);

    const parentPath = pathDescriptor.slice(0, -1);
    if (this.isSamePath(this.$currentPath.value, parentPath)) {
      await this.openDir(parentPath);
    }
  }

  // after dir is deleted then exit from dir
  async deleteDir(pathDescriptor: string[]) {
    await this.rpc.deletePath(pathDescriptor);

    const parentPath = pathDescriptor.slice(0, -1);
    if (this.isSamePath(this.$currentPath.value, pathDescriptor)) {
      await this.openDir(parentPath);
      return;
    }

    if (this.isSamePath(this.$currentPath.value, parentPath)) {
      await this.openDir(parentPath);
    }
  }

  private isSamePath(left: readonly string[], right: readonly string[]): boolean {
    return left.length === right.length && left.every((part, index) => part === right[index]);
  }
}
