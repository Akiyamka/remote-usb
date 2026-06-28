import { batch, signal } from '@preact/signals';
import { RPCError, type FileSystemEntry, type RPCAPI } from '../../RPCAPI';

export type UploadStatus = 'queued' | 'uploading' | 'done' | 'failed' | 'cancelled';

export type UploadTask = {
  id: number;
  name: string;
  size: number;
  loaded: number;
  total: number | null;
  percent: number | null;
  status: UploadStatus;
  errorMessage: string | null;
};

export class FileManager {
  $currentPath = signal<string[]>([]);
  $currentList = signal<FileSystemEntry[]>([]);
  $isLoading = signal<boolean>(false);
  $errors = signal<string[]>([]);
  $uploadTasks = signal<UploadTask[]>([]);
  $isUploadModalOpen = signal<boolean>(false);
  $hasTransferredFiles = signal<boolean>(false);

  private nextUploadTaskId = 1;
  private uploadQueue = Promise.resolve();
  private uploadControllers = new Map<number, AbortController>();

  constructor(private rpc: RPCAPI) {}

  // Open dir stored in url hash or ~
  async openLastDir() {
    await this.openDir(this.readPathFromHash());
  }

  async openDir(pathDescriptor: string[]) {
    const nextPath = [...pathDescriptor];
    this.$isLoading.value = true;

    try {
      const result = await this.rpc.listFiles(nextPath);
      batch(() => {
        this.$currentPath.value = [...nextPath];
        this.$currentList.value = result.entries;
      });
      this.writePathToHash(nextPath);
    } catch (error) {
      this.pushError(this.errorToMessage(error));
    } finally {
      this.$isLoading.value = false;
    }
  }

  // after file is uploaded then re-fetch current directory files if user in target directory right now
  // upload only one file at time
  // use XMLHttpRequest to be able track progress
  // send files as raw body (File/Blob, not multipart)
  async uploadFilesToDevice(files: File[], pathDescriptor: string[]) {
    if (files.length === 0) {
      return;
    }

    const targetPath = [...pathDescriptor];
    const queueItems = files.map((file) => {
      const controller = new AbortController();
      const task: UploadTask = {
        id: this.nextUploadTaskId,
        name: file.name,
        size: file.size,
        loaded: 0,
        total: file.size,
        percent: file.size === 0 ? 100 : 0,
        status: 'queued',
        errorMessage: null,
      };

      this.nextUploadTaskId += 1;
      this.uploadControllers.set(task.id, controller);
      return { file, controller, task };
    });

    this.$uploadTasks.value = [
      ...this.$uploadTasks.value,
      ...queueItems.map(({ task }) => task),
    ];

    const runQueue = this.uploadQueue.then(() => this.runUploadQueue(queueItems, targetPath));
    this.uploadQueue = runQueue.catch(() => undefined);
    await runQueue;
  }

  async downloadFileFromDevice(pathDescriptor: string[]) {
    this.$isLoading.value = true;

    try {
      const blob = await this.rpc.downloadFile(pathDescriptor);
      this.$hasTransferredFiles.value = true;
      return blob;
    } catch (error) {
      this.pushError(this.errorToMessage(error));
      throw error;
    } finally {
      this.$isLoading.value = false;
    }
  }

  // after file is deleted then re-fetch current directory
  async deleteFile(pathDescriptor: string[]) {
    this.$isLoading.value = true;

    try {
      await this.rpc.deletePath(pathDescriptor);

      const parentPath = pathDescriptor.slice(0, -1);
      if (this.isSamePath(this.$currentPath.value, parentPath)) {
        await this.openDir(parentPath);
      }
    } catch (error) {
      this.pushError(this.errorToMessage(error));
    } finally {
      this.$isLoading.value = false;
    }
  }

  // after dir is deleted then exit from dir
  async deleteDir(pathDescriptor: string[]) {
    this.$isLoading.value = true;

    try {
      await this.rpc.deletePath(pathDescriptor);

      const parentPath = pathDescriptor.slice(0, -1);
      if (this.isSamePath(this.$currentPath.value, pathDescriptor)) {
        await this.openDir(parentPath);
        return;
      }

      if (this.isSamePath(this.$currentPath.value, parentPath)) {
        await this.openDir(parentPath);
      }
    } catch (error) {
      this.pushError(this.errorToMessage(error));
    } finally {
      this.$isLoading.value = false;
    }
  }

  async createDirectory(pathDescriptor: string[]) {
    if (pathDescriptor.length === 0) {
      this.pushError('Directory name is required.');
      return;
    }

    this.$isLoading.value = true;

    try {
      await this.rpc.createDirectory(pathDescriptor);
      const parentPath = pathDescriptor.slice(0, -1);
      if (this.isSamePath(this.$currentPath.value, parentPath)) {
        await this.openDir(parentPath);
      }
    } catch (error) {
      this.pushError(this.errorToMessage(error));
    } finally {
      this.$isLoading.value = false;
    }
  }

  cancelUpload(taskId: number) {
    const controller = this.uploadControllers.get(taskId);
    controller?.abort();

    this.updateUploadTask(taskId, (task) =>
      task.status === 'queued' ? { ...task, status: 'cancelled' } : task,
    );
  }

  clearSettledUploads() {
    this.$uploadTasks.value = this.$uploadTasks.value.filter(
      ({ status }) => status === 'queued' || status === 'uploading',
    );
  }

  pushError(message: string) {
    this.$errors.value = [...this.$errors.value, message];
  }

  dismissError(index: number) {
    this.$errors.value = this.$errors.value.filter((_, errorIndex) => errorIndex !== index);
  }

  hasActiveUploads(): boolean {
    return this.$uploadTasks.value.some(
      ({ status }) => status === 'queued' || status === 'uploading',
    );
  }

  private async runUploadQueue(
    queueItems: {
      file: File;
      controller: AbortController;
      task: UploadTask;
    }[],
    pathDescriptor: string[],
  ) {
    for (const { file, controller, task } of queueItems) {
      if (controller.signal.aborted) {
        this.updateUploadTask(task.id, (currentTask) => ({
          ...currentTask,
          status: 'cancelled',
        }));
        this.uploadControllers.delete(task.id);
        continue;
      }

      this.updateUploadTask(task.id, (currentTask) => ({
        ...currentTask,
        status: 'uploading',
      }));

      try {
        await this.rpc.uploadFile([...pathDescriptor, file.name], file, {
          signal: controller.signal,
          contentType: file.type || undefined,
          onProgress: ({ loaded, total, percent }) => {
            const resolvedTotal = total ?? file.size;
            this.updateUploadTask(task.id, (currentTask) => ({
              ...currentTask,
              loaded,
              total: resolvedTotal,
              percent:
                percent ?? (resolvedTotal > 0 ? Math.min(100, (loaded / resolvedTotal) * 100) : null),
            }));
          },
        });

        batch(() => {
          this.$hasTransferredFiles.value = true;
          this.updateUploadTask(task.id, (currentTask) => ({
            ...currentTask,
            loaded: file.size,
            total: file.size,
            percent: 100,
            status: 'done',
          }));
        });
      } catch (error) {
        const wasAborted = error instanceof DOMException && error.name === 'AbortError';
        const errorMessage = wasAborted ? null : this.errorToMessage(error);

        this.updateUploadTask(task.id, (currentTask) => ({
          ...currentTask,
          status: wasAborted ? 'cancelled' : 'failed',
          errorMessage,
        }));

        if (errorMessage !== null) {
          this.pushError(`Upload failed for "${file.name}": ${errorMessage}`);
        }
      } finally {
        this.uploadControllers.delete(task.id);
      }
    }

    if (this.isSamePath(this.$currentPath.value, pathDescriptor)) {
      await this.openDir(pathDescriptor);
    }
  }

  private updateUploadTask(taskId: number, updater: (task: UploadTask) => UploadTask) {
    this.$uploadTasks.value = this.$uploadTasks.value.map((task) =>
      task.id === taskId ? updater(task) : task,
    );
  }

  private isSamePath(left: readonly string[], right: readonly string[]): boolean {
    return left.length === right.length && left.every((part, index) => part === right[index]);
  }

  private readPathFromHash(): string[] {
    if (typeof window === 'undefined') {
      return this.$currentPath.value;
    }

    return window.location.hash
      .replace(/^#\/?/, '')
      .split('/')
      .map((part) => decodeURIComponent(part))
      .filter((part) => part.length > 0);
  }

  private writePathToHash(pathDescriptor: string[]) {
    if (typeof window === 'undefined') {
      return;
    }

    const hashPath = pathDescriptor.map((part) => encodeURIComponent(part)).join('/');
    const nextHash = hashPath === '' ? '' : `#/${hashPath}`;

    if (window.location.hash !== nextHash) {
      window.history.replaceState(null, '', `${window.location.pathname}${window.location.search}${nextHash}`);
    }
  }

  private errorToMessage(error: unknown): string {
    if (error instanceof RPCError) {
      if (error.status === 503 && error.code === 'mode_mismatch') {
        return 'The SD card is in USB mode. Switch to HTTP mode first.';
      }
      if (error.status === 507) {
        return 'The SD card does not have enough free space.';
      }
      if (error.status === 409) {
        if (error.reason === 'active_upload') {
          return 'An upload is already running.';
        }
        if (error.reason === 'host_io_active') {
          return 'The USB host is still using the card.';
        }
        if (error.reason === 'switch_in_progress') {
          return 'A mode switch is already running.';
        }
      }
    }

    return error instanceof Error ? error.message : String(error);
  }
}
