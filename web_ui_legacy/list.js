import { html, signal } from "./preactive.js";
import { FileIcon, FolderIcon, TrashIcon, DownloadIcon } from "./icons.js";
import { formatFileSize } from "./utils.js";

export const $list = signal([]);

export const List = ({ onDownload, onDelete, onOpen }) => {
  return html`<table class="list">
    ${$list.value.map(
      (entry) => html`
        <tr class="list-entry" onclick=${() => onOpen(entry)}>
          <td class="icon">${entry.type === "file" ? FileIcon : FolderIcon}</td>
          <td class="name"><a>${entry.name}</a></td>
          <td class="size">${typeof entry.size === "number" ? formatFileSize(entry.size) : ""}</td>
          <td class="actions">
            ${entry.type === "file"
              ? html`<button
                  class="action"
                  onclick=${(e) => {
                    e.stopPropagation();
                    onDownload(entry);
                  }}
                >
                  ${DownloadIcon}
                </button>`
              : null}
            ${entry.type === "file"
              ? html`<button
                  class="action"
                  style="color: red"
                  onclick=${(e) => {
                    e.stopPropagation();
                    onDelete(entry);
                  }}
                >
                  ${TrashIcon}
                </button>`
              : null}
          </td>
        </tr>
      `,
    )}
  </table>`;
};
