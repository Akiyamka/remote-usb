import { useMemo, useRef, useState } from 'preact/hooks';
import { fileManager } from '../../../appState.js';
import { formatFileSize } from '../../../utils/format.js';
import { CloseIcon, UploadIcon } from '../icons.jsx';
import styles from './index.module.css';
import sharedStyles from '../shared.module.css';

export function UploadFileButton() {
  const inputRef = useRef<HTMLInputElement>(null);
  const [isDragging, setIsDragging] = useState(false);
  const tasks = fileManager.$uploadTasks.value;
  const isOpen = fileManager.$isUploadModalOpen.value;
  const overallProgress = useMemo(() => getOverallProgress(tasks), [tasks]);

  const startUpload = (files: File[]) => {
    if (files.length === 0) {
      return;
    }

    void fileManager.uploadFilesToDevice(files, fileManager.$currentPath.value);
  };

  return (
    <>
      <button
        aria-label="Upload files"
        class={`${styles['button']} ${overallProgress.isActive ? styles['active'] : ''}`}
        style={`--upload-progress: ${overallProgress.percent}%`}
        title="Upload files"
        type="button"
        onClick={() => {
          fileManager.$isUploadModalOpen.value = true;
        }}
      >
        <UploadIcon />
      </button>

      {isOpen && (
        <div class={styles['modalBackdrop']} role="presentation">
          <section class={styles['modal']} role="dialog" aria-modal="true" aria-label="Upload files">
            <div class={styles['modalHeader']}>
              <h2>Upload files</h2>
              <button
                aria-label="Close upload dialog"
                class={sharedStyles['iconButton']}
                type="button"
                onClick={() => {
                  fileManager.$isUploadModalOpen.value = false;
                }}
              >
                <CloseIcon />
              </button>
            </div>

            <div
              class={`${styles['dropZone']} ${isDragging ? styles['dragging'] : ''}`}
              onDragEnter={(event) => {
                event.preventDefault();
                setIsDragging(true);
              }}
              onDragOver={(event) => {
                event.preventDefault();
              }}
              onDragLeave={(event) => {
                event.preventDefault();
                setIsDragging(false);
              }}
              onDrop={(event) => {
                event.preventDefault();
                setIsDragging(false);
                startUpload(Array.from(event.dataTransfer?.files ?? []));
              }}
            >
              <UploadIcon size={28} />
              <span>Drop files here</span>
              <button
                class={sharedStyles['textAction']}
                type="button"
                onClick={() => inputRef.current?.click()}
              >
                Select files
              </button>
              <input
                ref={inputRef}
                multiple
                type="file"
                onChange={(event) => {
                  startUpload(Array.from(event.currentTarget.files ?? []));
                  event.currentTarget.value = '';
                }}
              />
            </div>

            <div class={styles['taskList']}>
              {tasks.length === 0 ? (
                <div class={styles['taskEmpty']}>No uploads yet.</div>
              ) : (
                tasks.map((task) => (
                  <div
                    class={`${styles['task']} ${uploadTaskStatusClass(task.status)}`}
                    key={task.id}
                    style={`--file-progress: ${Math.max(0, Math.min(100, task.percent ?? 0))}%`}
                  >
                    <div class={styles['taskMain']}>
                      <span class={styles['taskName']} title={task.name}>
                        {task.name}
                      </span>
                      <span class={styles['taskStatus']}>{taskLabel(task.status, task.percent)}</span>
                    </div>
                    <div class={styles['taskMeta']}>
                      <span>
                        {formatFileSize(task.loaded)} / {formatFileSize(task.total ?? task.size)}
                      </span>
                      {task.errorMessage !== null && <span>{task.errorMessage}</span>}
                    </div>
                    <button
                      class={`${sharedStyles['iconButton']} ${styles['taskCancel']}`}
                      disabled={task.status !== 'queued' && task.status !== 'uploading'}
                      title="Cancel upload"
                      type="button"
                      onClick={() => fileManager.cancelUpload(task.id)}
                    >
                      <CloseIcon size={16} />
                    </button>
                  </div>
                ))
              )}
            </div>

            {tasks.some(({ status }) => status === 'done' || status === 'failed' || status === 'cancelled') && (
              <div class={styles['modalFooter']}>
                <button class={sharedStyles['textAction']} type="button" onClick={() => fileManager.clearSettledUploads()}>
                  Clear finished
                </button>
              </div>
            )}
          </section>
        </div>
      )}
    </>
  );
}

type UploadProgressSummary = {
  isActive: boolean;
  percent: number;
};

function getOverallProgress(tasks: typeof fileManager.$uploadTasks.value): UploadProgressSummary {
  const activeTasks = tasks.filter(({ status }) => status === 'queued' || status === 'uploading');

  if (activeTasks.length === 0) {
    return { isActive: false, percent: 0 };
  }

  const total = activeTasks.reduce((sum, task) => sum + (task.total ?? task.size), 0);
  const loaded = activeTasks.reduce((sum, task) => sum + task.loaded, 0);

  return {
    isActive: true,
    percent: total > 0 ? Math.max(0, Math.min(100, (loaded / total) * 100)) : 0,
  };
}

function taskLabel(status: string, percent: number | null): string {
  if (status === 'uploading') {
    return percent === null ? 'uploading' : `${Math.round(percent)}%`;
  }

  return status;
}

function uploadTaskStatusClass(status: string): string {
  switch (status) {
    case 'done':
      return styles['taskDone'];
    case 'failed':
      return styles['taskFailed'];
    case 'cancelled':
      return styles['taskCancelled'];
    default:
      return '';
  }
}
