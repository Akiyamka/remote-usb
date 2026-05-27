import { CurrentDirLocation } from './CurrentDirLocation/index.jsx';
import { DirectoryContent } from './DirectoryContent/index.jsx';
import { DoneButton } from './DoneButton/index.js';
import { LoadingIndicator } from './LoadingIndicator/index.js';
import { UploadFileButton } from './UploadFileButton/index.js';
import './style.css';

export function FileManagerPage() {

  return (
    <div class="file-manager">
      <div class ="file-manager-header">
        <CurrentDirLocation />
        <UploadFileButton />
      </div>
      <LoadingIndicator />
      <DirectoryContent />
      <div class="file-manager-footer">
        <DoneButton />
      </div>
    </div>
  );
}
