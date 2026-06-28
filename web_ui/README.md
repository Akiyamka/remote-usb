# Web UI

Preact + Vite client for the ESP32-S3 wireless USB drive.

## Development Mock

`pnpm dev` starts Vite at `http://localhost:5173/` and serves an in-memory mock
of the firmware HTTP API under the same `/api/*` paths used on the device.

The mock includes:

- `/api/status`, `/api/mode/usb`, `/api/mode/http`
- file listing, download, upload, delete, mkdir
- sample folders: `gcode`, `images`, `docs`, `configs`, nested paths with spaces
- sample files: `.gcode`, `.jpg`, `.svg`, `.pdf`, `.txt`, `.json`, `.csv`

```sh
pnpm dev
```

## Build

```sh
pnpm build
```

To update the firmware LittleFS payload from the built UI, run from the repo
root:

```sh
./build_webfs.sh
```
