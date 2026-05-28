import { useEffect } from 'preact/hooks';
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
  useEffect(() => {
    void fileManager.openLastDir();
  }, []);

  return (
    <div class={styles['root']}>
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
