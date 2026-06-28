import { useEffect, useState } from "preact/hooks";
import { CurrentDirLocation } from "./CurrentDirLocation/index.jsx";
import { DirectoryContent } from "./DirectoryContent/index.jsx";
import { SwitchModeButton } from "./DoneButton/index.js";
import { LoadingIndicator } from "./LoadingIndicator/index.js";
import { UploadFileButton } from "./UploadFileButton/index.js";
import { fileManager } from "../../appState.js";
import { t } from "../../i18n.js";
import { CloseIcon } from "./icons.jsx";
import styles from "./FileManagerPage.module.css";
import sharedStyles from "./shared.module.css";
import { Button } from "#components/Button/index.js";

export function FileManagerPage() {
  const [isDraggingFiles, setIsDraggingFiles] = useState(false);

  useEffect(() => {
    void fileManager.openLastDir();
  }, []);

  return (
    <div
      class={`${styles["root"]} ${isDraggingFiles ? styles["dragging"] : ""}`}
      onDragEnter={(event) => {
        if (dragEventHasFiles(event)) {
          event.preventDefault();
          setIsDraggingFiles(true);
        }
      }}
      onDragOver={(event) => {
        if (dragEventHasFiles(event)) {
          event.preventDefault();
          setIsDraggingFiles(true);
        }
      }}
      onDragLeave={(event) => {
        if (dragEventHasFiles(event) && dragLeftElement(event, event.currentTarget)) {
          setIsDraggingFiles(false);
        }
      }}
      onDragEnd={() => {
        setIsDraggingFiles(false);
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
      <div class={styles["header"]}>
        <CurrentDirLocation />
        <UploadFileButton />
      </div>
      <LoadingIndicator />
      <div class={styles["contentArea"]}>
        <DirectoryContent />
        {isDraggingFiles && <div class={styles["dropOverlay"]}>{t("fileManager.dropOverlay")}</div>}
      </div>
      {fileManager.$errors.value.length > 0 && (
        <div class={styles["errors"]}>
          {fileManager.$errors.value.map((message, index) => (
            <div class={styles["error"]} key={`${index}-${message}`}>
              <span>{message}</span>
              <Button ghost={true}
                aria-label={t("fileManager.dismissError")}
                class={sharedStyles["iconButton"]}
                type="button"
                onClick={() => fileManager.dismissError(index)}
              >
                <CloseIcon size={16} />
              </Button>
            </div>
          ))}
        </div>
      )}
      <div class={styles["footer"]}>
        <SwitchModeButton />
      </div>
    </div>
  );
}

function dragEventHasFiles(event: { dataTransfer: DataTransfer | null }): boolean {
  return Array.from(event.dataTransfer?.types ?? []).includes("Files");
}

function dragLeftElement(
  event: { clientX: number; clientY: number; relatedTarget: EventTarget | null },
  element: HTMLElement,
): boolean {
  const relatedTarget = event.relatedTarget;
  if (relatedTarget instanceof Node) {
    return !element.contains(relatedTarget);
  }

  const rect = element.getBoundingClientRect();
  return (
    event.clientX <= rect.left ||
    event.clientX >= rect.right ||
    event.clientY <= rect.top ||
    event.clientY >= rect.bottom
  );
}
