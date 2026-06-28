import { fileManager } from '../../../appState.js';
import type { FileSystemEntry } from '../../../RPCAPI.js';
import { formatFileSize, formatMTime, formatRelativeMTime } from '../../../utils/format.js';
import { encodePath } from '../../../utils/path.js';
import { DownloadIcon, FileIcon, FolderIcon, MoreIcon, TrashIcon } from '../icons.jsx';
import styles from './index.module.css';
import sharedStyles from '../shared.module.css';

export function DirectoryContent() {
  const currentPath = fileManager.$currentPath.value;
  const entries = [...fileManager.$currentList.value].sort(compareEntries);

  if (entries.length === 0 && !fileManager.$isLoading.value) {
    return <div class={styles['empty']}>This directory is empty.</div>;
  }

  return (
    <div class={styles['content']} role="grid" aria-label="Directory content">
      {entries.map((entry, index) => {
        const pathDescriptor = [...currentPath, entry.name];
        const menuKey = `${entry.type}:${pathDescriptor.join('/')}`;
        const menuId = `entry-menu-${index}`;
        const fullMTime = formatMTime(entry.mtime);

        return (
          <div
            class={styles['entry']}
            key={menuKey}
            role="row"
            tabIndex={0}
            onClick={() => void openEntry(entry, pathDescriptor)}
            onKeyDown={(event) => {
              if (event.key === 'Enter') {
                void openEntry(entry, pathDescriptor);
              }
            }}
          >
            <div class={styles['entryIcon']} role="gridcell">
              {entry.type === 'dir' ? <FolderIcon /> : <FileIcon />}
            </div>
            <div class={styles['entryName']} role="gridcell" title={entry.name}>
              {entry.name}
            </div>
            <div class={styles['entrySize']} role="gridcell">
              {entry.type === 'file' ? formatFileSize(entry.sizeKb) : 'Folder'}
            </div>
            <div class={styles['entryMTime']} role="gridcell" title={fullMTime}>
              {formatRelativeMTime(entry.mtime)}
            </div>
            <div
              class={styles['entryActions']}
              role="gridcell"
              onClick={(event) => event.stopPropagation()}
              onKeyDown={(event) => event.stopPropagation()}
            >
              <button
                aria-label={`Actions for ${entry.name}`}
                aria-haspopup="menu"
                aria-controls={menuId}
                class={sharedStyles['iconButton']}
                popovertarget={menuId}
                type="button"
              >
                <MoreIcon size={18} />
              </button>
              <div class={styles['entryMenu']} id={menuId} popover="auto" role="menu">
                {entry.type === 'file' && (
                  <button
                    popovertarget={menuId}
                    popovertargetaction="hide"
                    role="menuitem"
                    type="button"
                    onClick={() => {
                      void downloadEntry(pathDescriptor, entry.name);
                    }}
                  >
                    <DownloadIcon size={16} />
                    <span>Download</span>
                  </button>
                )}
                <button
                  class={styles['danger']}
                  popovertarget={menuId}
                  popovertargetaction="hide"
                  role="menuitem"
                  type="button"
                  onClick={() => {
                    void deleteEntry(entry, pathDescriptor);
                  }}
                >
                  <TrashIcon size={16} />
                  <span>Delete</span>
                </button>
              </div>
            </div>
          </div>
        );
      })}
    </div>
  );
}

function compareEntries(left: FileSystemEntry, right: FileSystemEntry): number {
  if (left.type !== right.type) {
    return left.type === 'dir' ? -1 : 1;
  }

  return left.name.localeCompare(right.name, undefined, {
    numeric: true,
    sensitivity: 'base',
  });
}

async function openEntry(entry: FileSystemEntry, pathDescriptor: string[]) {
  if (entry.type === 'dir') {
    await fileManager.openDir(pathDescriptor);
    return;
  }

  openFileInNewTab(pathDescriptor);
}

function openFileInNewTab(pathDescriptor: string[]) {
  try {
    window.open(`/api/files/${encodePath(pathDescriptor, false)}`, '_blank', 'noopener');
  } catch (error) {
    fileManager.pushError(error instanceof Error ? error.message : String(error));
  }
}

async function downloadEntry(pathDescriptor: string[], fileName: string) {
  try {
    const blob = await fileManager.downloadFileFromDevice(pathDescriptor);
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = fileName;
    link.click();
    URL.revokeObjectURL(url);
  } catch {
    // The model already stores the user-visible error.
  }
}

async function deleteEntry(entry: FileSystemEntry, pathDescriptor: string[]) {
  if (!window.confirm(`Delete "${entry.name}"?`)) {
    return;
  }

  if (entry.type === 'dir') {
    await fileManager.deleteDir(pathDescriptor);
    return;
  }

  await fileManager.deleteFile(pathDescriptor);
}
