export const deleteEntry = async (currentPath, { type, name }) => {
  const remotePath =
    "/" + [currentPath, name].filter((part) => part.length > 0).join("/");

  const res = await fetch(`/edit?path=${encodeURIComponent(remotePath)}`, {
    method: "DELETE",
  });

  if (!res.ok) {
    const message =
      (await res.text()) || `${window.tr("ERROR_API_DELETE")}: ${res.status}`;
    throw new Error(message);
  }
};

export const downloadEntry = async ({ type, name }) => {
  const res = await fetch(`/download?file=${encodeURIComponent(name)}`, {
    method: "GET",
  });

  if (!res.ok) {
    const message =
      (await res.text()) || `${window.tr("ERROR_API_DOWNLOAD")}: ${res.status}`;
    throw new Error(message);
  }

  return await res.blob();
};

export const getDir = async (path) => {
  const res = await fetch(`list?dir=/${encodeURI(path)}`);
  if (!res.ok) {
    console.error(`Failed to fetch directory listing: ${res.status}`);
    const error = await res.text();
    throw Error(error);
  }
  const data = await res.json();
  return data.sort((d) => d.type !== "dir");
};

export const getStatus = async () => {
  const res = await fetch("/status");

  if (!res.ok) {
    const message =
      (await res.text()) || `${window.tr("ERROR_API_STATUS")}: ${res.status}`;
    throw new Error(message);
  }

  return await res.json();
};

export const reconnectUSB = async () => {
  const res = await fetch("/usb/reconnect", {
    method: "POST",
  });

  if (!res.ok) {
    const message =
      (await res.text()) ||
      `${window.tr("ERROR_API_USB_RECONNECT")}: ${res.status}`;
    throw new Error(message);
  }
};

const USB_CONTROL_PATHS = {
  "soft-disconnect": "/usb/soft-disconnect",
  "soft-connect": "/usb/soft-connect",
  "hard-disconnect": "/usb/hard-disconnect",
  "hard-connect": "/usb/hard-connect",
};

export const usbControl = async (action) => {
  const path = USB_CONTROL_PATHS[action];
  if (!path) {
    throw new Error(window.tr("ERROR_API_USB_CONTROL"));
  }

  const res = await fetch(path, {
    method: "POST",
  });

  if (!res.ok) {
    const message =
      (await res.text()) ||
      `${window.tr("ERROR_API_USB_CONTROL")}: ${res.status}`;
    throw new Error(message);
  }
};

const HASH_CHUNK_SIZE = 1024 * 1024;

const sha256Hex = async (file) => {
  if (!globalThis.sha256?.create) {
    throw new Error(window.tr("ERROR_API_SHA256_NOT_LOADED"));
  }

  const hash = globalThis.sha256.create();

  for (let offset = 0; offset < file.size; offset += HASH_CHUNK_SIZE) {
    const chunk = file.slice(offset, offset + HASH_CHUNK_SIZE);
    const chunkBytes = new Uint8Array(await chunk.arrayBuffer());
    hash.update(chunkBytes);
  }

  return hash.hex();
};

export const uploadFile = (currentPath, file, onProgress, onStatusChange) =>
  new Promise((resolve, reject) => {
    try {
      const remotePath =
        "/" +
        [currentPath, file.name].filter((part) => part.length > 0).join("/");
      const tempRemotePath = `${remotePath}.part`;

      const browserEpochSeconds = Math.floor(Date.now() / 1000);
      onStatusChange?.("UPLOAD_STATUS_HASHING");
      sha256Hex(file)
        .then((checksum) => {
          onStatusChange?.("UPLOAD_STATUS_UPLOADING");
          const formData = new FormData();
          formData.append("data", file, tempRemotePath);

          const xhr = new XMLHttpRequest();
          xhr.open(
            "POST",
            `/edit?mtime=${encodeURIComponent(browserEpochSeconds)}`,
            true,
          );

          xhr.upload.addEventListener("progress", (event) => {
            if (!event.lengthComputable) return;
            onProgress({
              total: event.total,
              loaded: event.loaded,
              percent: (event.loaded / event.total) * 100,
            });
          });

          xhr.addEventListener("load", async () => {
            onStatusChange?.("UPLOAD_STATUS_UPLOADED");
            onProgress({
              total: file.size,
              loaded: file.size,
              percent: 100,
            });

            if (xhr.status < 200 || xhr.status >= 300) {
              onStatusChange?.("UPLOAD_STATUS_FAILED");
              reject(
                new Error(
                  xhr.responseText ||
                    `${window.tr("ERROR_API_UPLOAD")}: ${xhr.status}`,
                ),
              );
              return;
            }

            try {
              const commitUrl =
                `/edit/commit?path=${encodeURIComponent(tempRemotePath)}` +
                `&sha256=${encodeURIComponent(checksum)}` +
                `&mtime=${encodeURIComponent(browserEpochSeconds)}`;
              onStatusChange?.("UPLOAD_STATUS_CHECKING");
              const commitRes = await fetch(commitUrl, { method: "POST" });

              if (!commitRes.ok) {
                onStatusChange?.("UPLOAD_STATUS_CHECK_FAILED");
                const message =
                  (await commitRes.text()) ||
                  `${window.tr("ERROR_API_UPLOAD_COMMIT")}: ${commitRes.status}`;
                reject(new Error(message));
                return;
              }
              onStatusChange?.("UPLOAD_STATUS_SUCCESS");
              resolve();
            } catch (error) {
              onStatusChange?.("UPLOAD_STATUS_CHECK_FAILED");
              console.error(error);
              reject(new Error(window.tr("ERROR_API_UPLOAD_COMMIT")));
            }
          });

          xhr.addEventListener("error", (error) => {
            onStatusChange?.("UPLOAD_STATUS_ERROR");
            console.error(error);
            reject(new Error(window.tr("ERROR_API_UPLOAD")));
          });

          xhr.addEventListener("abort", () => {
            onStatusChange?.("UPLOAD_STATUS_ABORTED");
            reject(new Error(window.tr("ERROR_API_UPLOAD_ABORTED")));
          });

          xhr.send(formData);
        })
        .catch((error) => {
          console.error(error);
          reject(
            error instanceof Error
              ? error
              : new Error(window.tr("ERROR_API_HASH_FAILED")),
          );
        });
    } catch (error) {
      console.error(error);
      reject(new Error(window.tr("ERROR_API_UPLOAD")));
    }
  });
