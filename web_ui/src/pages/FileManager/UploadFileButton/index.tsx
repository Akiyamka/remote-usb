import { useEffect, useMemo, useRef } from "preact/hooks";
import { fileManager } from "../../../appState.js";
import { t } from "../../../i18n.js";
import { formatFileSize } from "../../../utils/format.js";
import { CancelIcon, CloseIcon, UploadIcon } from "../icons.jsx";
import styles from "./index.module.css";
import sharedStyles from "../shared.module.css";
import { Button } from "#components/Button/index.js";

export function UploadFileButton() {
  const inputRef = useRef<HTMLInputElement>(null);
  const modalInputRef = useRef<HTMLInputElement>(null);
  const dialogRef = useRef<HTMLDialogElement>(null);
  const tasks = fileManager.$uploadTasks.value;
  const isOpen = fileManager.$isUploadModalOpen.value;
  const overallProgress = useMemo(() => getOverallProgress(tasks), [tasks]);

  useEffect(() => {
    const dialog = dialogRef.current;
    if (dialog === null) {
      return;
    }

    if (isOpen) {
      if (!dialog.open) {
        dialog.showModal();
      }
      return;
    }

    if (dialog.open) {
      dialog.close();
    }
  }, [isOpen]);

  const startUpload = (files: File[]) => {
    if (files.length === 0) {
      return;
    }

    fileManager.$isUploadModalOpen.value = true;
    void fileManager.uploadFilesToDevice(files, fileManager.$currentPath.value);
  };

  return (
    <>
      <input
        class={styles["fileInput"]}
        ref={inputRef}
        multiple
        type="file"
        onChange={(event) => {
          startUpload(Array.from(event.currentTarget.files ?? []));
          event.currentTarget.value = "";
        }}
      />
      <Button ghost={true}
        aria-label={t("upload.uploadFiles")}
        class={`${styles["button"]} ${overallProgress.isActive ? styles["active"] : ""}`}
        style={`--upload-progress: ${overallProgress.percent}%`}
        title={t("upload.uploadFiles")}
        type="button"
        onClick={() => {
          inputRef.current?.click();
        }}
      >
        <UploadIcon size={24} />
      </Button>

      <dialog
        aria-label={t("upload.uploadFiles")}
        class={styles["modal"]}
        ref={dialogRef}
        onClose={() => {
          fileManager.$isUploadModalOpen.value = false;
        }}
      >
        <div class={styles["modalHeader"]}>
          <h2>{t("upload.uploadFiles")}</h2>
          <Button ghost={true}
            aria-label={t("upload.closeDialog")}
            class={sharedStyles["iconButton"]}
            type="button"
            onClick={() => {
              dialogRef.current?.close();
            }}
          >
            <CloseIcon size={24} />
          </Button>
        </div>

        <div class={styles["taskList"]}>
          {tasks.length === 0 ? (
            <div class={styles["taskEmpty"]}>{t("upload.noUploads")}</div>
          ) : (
            tasks.map((task) => (
              <div
                class={`${styles["task"]} ${uploadTaskStatusClass(task.status)}`}
                key={task.id}
                style={`--file-progress: ${Math.max(0, Math.min(100, task.percent ?? 0))}%`}
              >
                <div class={styles["taskMain"]}>
                  <span class={styles["taskName"]} title={task.name}>
                    {task.name}
                  </span>
                </div>
                <div class={styles["taskMeta"]}>
                  <span>
                    {formatFileSize(task.loaded)} / {formatFileSize(task.total ?? task.size)}
                  </span>
                  -<span class={styles["taskStatus"]}>{taskLabel(task.status, task.percent)}</span>
                  {task.errorMessage !== null && <span>{task.errorMessage}</span>}
                </div>
                {task.status === "queued" || task.status === "uploading" ? (
                  <Button ghost={true}
                    class={`${sharedStyles["iconButton"]} ${styles["taskCancel"]}`}
                    title={t("upload.cancelUpload")}
                    type="button"
                    onClick={() => fileManager.cancelUpload(task.id)}
                  >
                    <CancelIcon size={24} />
                  </Button>
                ) : null}
              </div>
            ))
          )}
        </div>

        <div class={styles["modalFooter"]}>
          <input
            class={styles["fileInput"]}
            ref={modalInputRef}
            multiple
            type="file"
            onChange={(event) => {
              startUpload(Array.from(event.currentTarget.files ?? []));
              event.currentTarget.value = "";
            }}
          />
          <Button ghost={true}
            class={sharedStyles["textAction"]}
            type="button"
            onClick={() => modalInputRef.current?.click()}
          >
            {t("upload.selectFiles")}
          </Button>
          {tasks.some(
            ({ status }) => status === "done" || status === "failed" || status === "cancelled",
          ) && (
            <Button ghost={true}
              class={sharedStyles["textAction"]}
              type="button"
              onClick={() => fileManager.clearSettledUploads()}
            >
              {t("upload.clearFinished")}
            </Button>
          )}
        </div>
      </dialog>
    </>
  );
}

type UploadProgressSummary = {
  isActive: boolean;
  percent: number;
};

function getOverallProgress(tasks: typeof fileManager.$uploadTasks.value): UploadProgressSummary {
  const activeTasks = tasks.filter(({ status }) => status === "queued" || status === "uploading");

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
  if (status === "uploading") {
    return percent === null ? t("upload.status.uploading") : `${Math.round(percent)}%`;
  }

  switch (status) {
    case "queued":
      return t("upload.status.queued");
    case "done":
      return t("upload.status.done");
    case "failed":
      return t("upload.status.failed");
    case "cancelled":
      return t("upload.status.cancelled");
    default:
      return status;
  }
}

function uploadTaskStatusClass(status: string): string {
  switch (status) {
    case "done":
      return styles["taskDone"];
    case "failed":
      return styles["taskFailed"];
    case "cancelled":
      return styles["taskCancelled"];
    default:
      return "";
  }
}
