import { useEffect, useState } from 'preact/hooks';
import { CurrentDirLocation } from './CurrentDirLocation/index.jsx';
import { DirectoryContent } from './DirectoryContent/index.jsx';
import { DoneButton } from './DoneButton/index.js';
import { LoadingIndicator } from './LoadingIndicator/index.js';
import { UploadFileButton } from './UploadFileButton/index.js';
import { fileManager } from '../../appState.js';
import { CloseIcon } from './icons.jsx';
import styles from './FileManagerPage.module.css';
import sharedStyles from './shared.module.css';

export function FileManagerPage() {
  const [isDraggingFiles, setIsDraggingFiles] = useState(false);

  useEffect(() => {
    void fileManager.openLastDir();
  }, []);

  return (
    <div
      class={`${styles['root']} ${isDraggingFiles ? styles['dragging'] : ''}`}
      onDragEnter={(event) => {
        if (dragEventHasFiles(event)) {
          event.preventDefault();
          setIsDraggingFiles(true);
        }
      }}
      onDragOver={(event) => {
        if (dragEventHasFiles(event)) {
          event.preventDefault();
        }
      }}
      onDragLeave={(event) => {
        if (event.currentTarget === event.target) {
          setIsDraggingFiles(false);
        }
      }}
      onDrop={(event) => {
        if (!dragEventHasFiles(event)) {
          return;
        }

        event.preventDefault();
        setIsDraggingFiles(false);
        const files = Array.from(event.dataTransfer?.files ?? []);
        void fileManager.uploadFilesToDevice(files, fileManager.$currentPath.value);
      }}
    >
      {isDraggingFiles && <div class={styles['dropOverlay']}>Drop files to upload here</div>}
      <div class={styles['header']}>
        <CurrentDirLocation />
        <UploadFileButton />
      </div>
      <LoadingIndicator />
      <DirectoryContent />
      {fileManager.$errors.value.length > 0 && (
        <div class={styles['errors']}>
          {fileManager.$errors.value.map((message, index) => (
            <div class={styles['error']} key={`${index}-${message}`}>
              <span>{message}</span>
              <button
                aria-label="Dismiss error"
                class={sharedStyles['iconButton']}
                type="button"
                onClick={() => fileManager.dismissError(index)}
              >
                <CloseIcon size={16} />
              </button>
            </div>
          ))}
        </div>
      )}
      <div class={styles['footer']}>
        <DoneButton />
      </div>
    </div>
  );
}

function dragEventHasFiles(event: { dataTransfer: DataTransfer | null }): boolean {
  return Array.from(event.dataTransfer?.types ?? []).includes('Files');
}
