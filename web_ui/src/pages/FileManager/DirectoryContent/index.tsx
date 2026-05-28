import { useState } from 'preact/hooks';
import { fileManager } from '../../../appState.js';
import type { FileSystemEntry } from '../../../RPCAPI.js';
import { formatFileSize, formatMTime } from '../../../utils/format.js';
import { DownloadIcon, FileIcon, FolderIcon, MoreIcon, TrashIcon } from '../icons.jsx';
import styles from './index.module.css';
import sharedStyles from '../shared.module.css';

export function DirectoryContent() {
  const [openMenuKey, setOpenMenuKey] = useState<string | null>(null);
  const currentPath = fileManager.$currentPath.value;
  const entries = [...fileManager.$currentList.value].sort(compareEntries);

  if (entries.length === 0 && !fileManager.$isLoading.value) {
    return <div class={styles['empty']}>This directory is empty.</div>;
  }

  return (
    <div class={styles['content']} role="grid" aria-label="Directory content">
      {entries.map((entry) => {
        const pathDescriptor = [...currentPath, entry.name];
        const menuKey = `${entry.type}:${pathDescriptor.join('/')}`;

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
            <div class={styles['entryMeta']} role="gridcell">
              <span>{entry.type === 'file' ? formatFileSize(entry.sizeKb) : 'Folder'}</span>
              <span>{formatMTime(entry.mtime)}</span>
            </div>
            <div class={styles['entryActions']} role="gridcell">
              <button
                aria-label={`Actions for ${entry.name}`}
                aria-expanded={openMenuKey === menuKey}
                class={sharedStyles['iconButton']}
                type="button"
                onClick={(event) => {
                  event.stopPropagation();
                  setOpenMenuKey(openMenuKey === menuKey ? null : menuKey);
                }}
              >
                <MoreIcon size={18} />
              </button>
              {openMenuKey === menuKey && (
                <div class={styles['entryMenu']} onClick={(event) => event.stopPropagation()}>
                  {entry.type === 'file' && (
                    <button
                      type="button"
                      onClick={() => {
                        setOpenMenuKey(null);
                        void downloadEntry(pathDescriptor, entry.name);
                      }}
                    >
                      <DownloadIcon size={16} />
                      <span>Download</span>
                    </button>
                  )}
                  <button
                    class={styles['danger']}
                    type="button"
                    onClick={() => {
                      setOpenMenuKey(null);
                      void deleteEntry(entry, pathDescriptor);
                    }}
                  >
                    <TrashIcon size={16} />
                    <span>Delete</span>
                  </button>
                </div>
              )}
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

  await openFileInNewTab(pathDescriptor, entry.name);
}

async function openFileInNewTab(pathDescriptor: string[], fileName: string) {
  const openedWindow = window.open('about:blank', '_blank');

  try {
    const blob = await fileManager.downloadFileFromDevice(pathDescriptor);
    const url = URL.createObjectURL(blob);

    if (openedWindow !== null) {
      openedWindow.document.title = fileName;
      openedWindow.location.href = url;
    } else {
      window.open(url, '_blank');
    }

    window.setTimeout(() => URL.revokeObjectURL(url), 60_000);
  } catch {
    openedWindow?.close();
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
