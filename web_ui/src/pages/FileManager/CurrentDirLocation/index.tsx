import { useEffect, useRef, useState } from "preact/hooks";
import { fileManager } from "../../../appState.js";
import {
  ArrowUpIcon,
  CheckIcon,
  CloseIcon,
  EditIcon,
  FolderPlusIcon,
  HomeIcon,
} from "../icons.jsx";
import styles from "./index.module.css";
import sharedStyles from "../shared.module.css";
import { Button } from "#components/Button/index.js";

export function CurrentDirLocation() {
  const currentPath = fileManager.$currentPath.value;
  const [isEditing, setIsEditing] = useState(false);
  const [editablePath, setEditablePath] = useState(currentPath.join("/"));
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!isEditing) {
      setEditablePath(currentPath.join("/"));
    }
  }, [currentPath, isEditing]);

  useEffect(() => {
    const element = scrollRef.current;
    if (element !== null) {
      element.scrollLeft = element.scrollWidth;
    }
  }, [currentPath, isEditing]);

  return (
    <div class={styles["root"]}>
      <Button
        ghost={true}
        aria-label="Go to root directory"
        class={sharedStyles["iconButton"]}
        title="Root directory"
        type="button"
        onClick={() => openDirectory([])}
      >
        <HomeIcon size={24} />
      </Button>

      {isEditing ? (
        <form
          class={styles["edit"]}
          onSubmit={(event) => void submitPath(event, editablePath, setIsEditing)}
        >
          <input
            aria-label="Current path"
            autoFocus
            value={editablePath}
            onInput={(event) => setEditablePath(event.currentTarget.value)}
          />
          <Button ghost={true} class={sharedStyles["iconButton"]} title="Open path" type="submit">
            <CheckIcon />
          </Button>
          <Button
            ghost={true}
            class={sharedStyles["iconButton"]}
            title="Cancel editing"
            type="button"
            onClick={() => {
              setEditablePath(currentPath.join("/"));
              setIsEditing(false);
            }}
          >
            <CloseIcon />
          </Button>
        </form>
      ) : (
        <>
          <div class={styles["breadcrumbs"]} ref={scrollRef}>
            <Button
              ghost={true}
              aria-label="Go to parent directory"
              class={`${sharedStyles["iconButton"]} ${styles["up"]}`}
              disabled={currentPath.length === 0}
              title="Parent directory"
              type="button"
              onClick={openParentDirectory}
            >
              <ArrowUpIcon size={24} />
            </Button>
            {currentPath.map((part, index) => (
              <span class={styles["breadcrumbPart"]} key={`${index}-${part}`}>
                <span class={styles["delimiter"]}>/</span>
                <Button
                  ghost={true}
                  title={currentPath.slice(0, index + 1).join("/")}
                  type="button"
                  onClick={() => openDirectory(currentPath.slice(0, index + 1))}
                >
                  {part}
                </Button>
              </span>
            ))}
          </div>
          <Button
            ghost={true}
            aria-label="Edit current path"
            class={sharedStyles["iconButton"]}
            title="Edit path"
            type="button"
            onClick={() => setIsEditing(true)}
          >
            <EditIcon size={24} />
          </Button>
          <Button
            ghost={true}
            aria-label="Create directory"
            class={sharedStyles["iconButton"]}
            title="Create directory"
            type="button"
            onClick={() => createDirectoryPrompt(currentPath)}
          >
            <FolderPlusIcon size={24} />
          </Button>
        </>
      )}
    </div>
  );
}

function openParentDirectory() {
  const currentPath = fileManager.$currentPath.value;
  if (currentPath.length === 0) {
    return;
  }

  openDirectory(currentPath.slice(0, -1));
}

function openDirectory(pathDescriptor: string[]) {
  void fileManager.openDir(pathDescriptor);
}

async function submitPath(
  event: Event,
  editablePath: string,
  setIsEditing: (isEditing: boolean) => void,
) {
  event.preventDefault();
  const nextPath = parseEditablePath(editablePath);
  if (nextPath === null) {
    fileManager.pushError("Invalid path.");
    return;
  }

  await fileManager.openDir(nextPath);
  setIsEditing(false);
}

function parseEditablePath(value: string): string[] | null {
  const nextPath = value
    .split("/")
    .map((part) => part.trim())
    .filter((part) => part.length > 0);

  if (nextPath.some((part) => part === "." || part.includes(".."))) {
    return null;
  }

  return nextPath;
}

function createDirectoryPrompt(currentPath: string[]) {
  const value = window.prompt("New directory path");
  if (value === null) {
    return;
  }

  const parts = parseEditablePath(value);

  if (parts === null || parts.length === 0) {
    fileManager.pushError("Invalid directory path.");
    return;
  }

  void fileManager.createDirectory([...currentPath, ...parts]);
}
