import { useEffect, useRef, useState } from 'preact/hooks';
import { fileManager } from '../../../appState.js';
import { ArrowUpIcon, CheckIcon, CloseIcon, EditIcon, HomeIcon } from '../icons.jsx';
import styles from './index.module.css';
import sharedStyles from '../shared.module.css';

export function CurrentDirLocation() {
  const currentPath = fileManager.$currentPath.value;
  const [isEditing, setIsEditing] = useState(false);
  const [editablePath, setEditablePath] = useState(currentPath.join('/'));
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!isEditing) {
      setEditablePath(currentPath.join('/'));
    }
  }, [currentPath, isEditing]);

  useEffect(() => {
    const element = scrollRef.current;
    if (element !== null) {
      element.scrollLeft = element.scrollWidth;
    }
  }, [currentPath, isEditing]);

  return (
    <div class={styles['root']}>
      <button
        aria-label="Go to parent directory"
        class={`${sharedStyles['iconButton']} ${styles['up']}`}
        disabled={currentPath.length === 0}
        title="Parent directory"
        type="button"
        onClick={() => void fileManager.openDir(currentPath.slice(0, -1))}
      >
        <ArrowUpIcon />
      </button>

      {isEditing ? (
        <form class={styles['edit']} onSubmit={(event) => submitPath(event, editablePath, setIsEditing)}>
          <input
            aria-label="Current path"
            autoFocus
            value={editablePath}
            onInput={(event) => setEditablePath(event.currentTarget.value)}
          />
          <button class={sharedStyles['iconButton']} title="Open path" type="submit">
            <CheckIcon />
          </button>
          <button
            class={sharedStyles['iconButton']}
            title="Cancel editing"
            type="button"
            onClick={() => {
              setEditablePath(currentPath.join('/'));
              setIsEditing(false);
            }}
          >
            <CloseIcon />
          </button>
        </form>
      ) : (
        <>
          <div class={styles['breadcrumbs']} ref={scrollRef}>
            <button
              aria-label="Go to root directory"
              class={sharedStyles['iconButton']}
              title="Root directory"
              type="button"
              onClick={() => void fileManager.openDir([])}
            >
              <HomeIcon size={18} />
            </button>
            {currentPath.map((part, index) => (
              <span class={styles['breadcrumbPart']} key={`${index}-${part}`}>
                <span class={styles['delimiter']}>/</span>
                <button
                  title={currentPath.slice(0, index + 1).join('/')}
                  type="button"
                  onClick={() => void fileManager.openDir(currentPath.slice(0, index + 1))}
                >
                  {part}
                </button>
              </span>
            ))}
          </div>
          <button
            aria-label="Edit current path"
            class={sharedStyles['iconButton']}
            title="Edit path"
            type="button"
            onClick={() => setIsEditing(true)}
          >
            <EditIcon />
          </button>
        </>
      )}
    </div>
  );
}

function submitPath(
  event: Event,
  editablePath: string,
  setIsEditing: (isEditing: boolean) => void,
) {
  event.preventDefault();
  const nextPath = editablePath
    .split('/')
    .map((part) => part.trim())
    .filter((part) => part.length > 0);

  setIsEditing(false);
  void fileManager.openDir(nextPath);
}
