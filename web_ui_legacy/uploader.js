import { html, signal } from "./preactive.js";
import { formatFileSize } from "./utils.js";

export const createProgressSignals = () => ({
  total: signal(0),
  loaded: signal(0),
  percent: signal(0),
});

export const $uploadingFiles = signal([]);

function Status({ status, progress }) {
  return status.value === "UPLOAD_STATUS_UPLOADING"
    ? html`<span class="size">${Math.max(0, Math.min(100, Math.round(progress.percent.value)))} %</span>`
    : html`<span class="size">${window.tr(status.value)}</span>`;
}

export function UploadingView() {
  return html`<div class="uploading-view">
    ${$uploadingFiles.value.map(
    ({ name, progress, status }) =>
      html`<div
          class="uploading-file"
          style="${`--file-progress: ${progress.percent.value}%`}"
        >
          <span class="uploading-file-name">${name}</span>
          ${Status({ status, progress })}
          <span class="size">
            ${formatFileSize(progress.loaded.value)} /
            ${formatFileSize(progress.total.value)}
          </span>
        </div>`,
  )}
  </div>`;
}

export const selectFiles = () =>
  new Promise((resolve) => {
    const input = document.createElement("input");
    input.type = "file";
    input.multiple = true;
    input.style.display = "none";
    let settled = false;

    const finish = (files) => {
      if (settled) return;
      settled = true;
      cleanup();
      resolve(files);
    };

    const cleanup = () => {
      input.removeEventListener("change", onChange);
      input.removeEventListener("cancel", onCancel);
      window.removeEventListener("focus", onWindowFocus);
      document.body.removeChild(input);
    };

    const onChange = () => {
      const files = Array.from(input.files || []);
      finish(files);
    };

    const onCancel = () => {
      finish([]);
    };

    const onWindowFocus = () => {
      setTimeout(() => {
        if (settled) return;
        const files = Array.from(input.files || []);
        finish(files);
      }, 250);
    };

    input.addEventListener("change", onChange, { once: true });
    input.addEventListener("cancel", onCancel, { once: true });
    window.addEventListener("focus", onWindowFocus, { once: true });

    document.body.appendChild(input);
    input.click();
  });
