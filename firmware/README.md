# Firmware

ESP-IDF v5.3 project for the T-Dongle S3 Wi-Fi USB drive.
See [`../docs/spec.md`](../docs/spec.md) and [`../docs/plan.md`](../docs/plan.md).

## Build environment

This project deliberately does **not** rely on a system-wide ESP-IDF install.
Instead it uses the official `espressif/idf` container via Podman or Docker
through a thin wrapper at [`tools/idf`](tools/idf). The IDF version is pinned in
that script (`release-v5.3`).

Requirements:

- Linux + [Podman](https://podman.io/) or Docker (rootless Podman is fine and
  recommended)
- About 2 GB free disk for the container image on first pull
- About 1–2 GB free in `~/.cache/esp-idf-podman/` for the shared toolchain cache

## Usage

```sh
# First-time only: pull the image (~1.5 GB). The wrapper will also pull on demand.
podman pull docker.io/espressif/idf:release-v5.3
# Or, on Docker-only systems:
docker pull docker.io/espressif/idf:release-v5.3

# From the firmware/ directory:
./tools/idf set-target esp32s3
./tools/idf build

# Flashing (replace the device path as needed):
./tools/idf -p /dev/ttyACM1 flash

# Then unplug/replug the dongle, or press its reset button, and attach without
# another DTR/RTS reset. On ESP32-S3 USB-Serial-JTAG, monitor's default reset
# can leave the chip in ROM download mode (boot:0x22).
./tools/idf -p /dev/ttyACM1 monitor --no-reset

# Menuconfig (interactive ncurses UI — works thanks to -it in the wrapper)
./tools/idf menuconfig
```

The wrapper:

- selects `podman` when available, otherwise `docker`; override with
  `IDF_CONTAINER_RUNTIME=docker` or `IDF_CONTAINER_RUNTIME=podman`,
- mounts `firmware/` at `/project` inside the container,
- forwards any `/dev/tty*` argument as a `--device=` passthrough,
- persists ccache and the component-manager cache under
  `~/.cache/esp-idf-podman/` so subsequent builds are fast,
- keeps files written by the container owned by your host user (Podman uses
  `--userns=keep-id`; Docker runs as your host UID/GID).

## Web UI LittleFS partition

The firmware has a separate `webfs` LittleFS partition for static web UI
assets. It is mounted by the `webfs` component at `/web` on the ESP32-S3.
The partition is defined in [`partitions.csv`](partitions.csv):

```csv
webfs,      data, littlefs,,         0x100000,
```

Static files are packed from [`web_dist/`](web_dist/) into `build/webfs.bin`
during `./tools/idf build`:

```cmake
littlefs_create_partition_image(webfs web_dist FLASH_IN_PROJECT)
```

### Adding files

Put files that should exist on the device under `firmware/web_dist/`. The
directory layout is preserved in LittleFS. For example:

```text
firmware/web_dist/
├── index.html.gz
├── app.js.gz
└── style.css.gz
```

The web server expects static assets to be pre-compressed with gzip. Use
deterministic gzip output (`-n`) so rebuilds do not change files just because
of timestamps:

```sh
# From the firmware/ directory:
gzip -n -c ../web_ui/dist/index.html > web_dist/index.html.gz
gzip -n -c ../web_ui/dist/app.js > web_dist/app.js.gz
gzip -n -c ../web_ui/dist/style.css > web_dist/style.css.gz
```

For nested paths, create the same directories under `web_dist/`; for example
`web_dist/assets/logo.png.gz` becomes `/web/assets/logo.png.gz` on the device.

### Building and flashing

A normal build regenerates the LittleFS image:

```sh
./tools/idf build
```

Because `FLASH_IN_PROJECT` is enabled, a normal firmware flash also writes the
`webfs` image:

```sh
./tools/idf -p /dev/ttyACM1 flash
```

To update only the web UI partition after changing `web_dist/`, use the target
generated from the partition name:

```sh
./tools/idf build
./tools/idf -p /dev/ttyACM1 webfs-flash
```

This writes only `build/webfs.bin` to the `webfs` partition offset. If
`partitions.csv` changed, do a full flash instead so the partition table and
the image agree.

### Managing size

The `webfs` partition is currently `0x100000` bytes (1 MiB). The generated
`build/webfs.bin` is always exactly that size, so use `du` on `web_dist/` to
estimate actual content size:

```sh
du -h web_dist
```

LittleFS also needs metadata space, so keep some headroom below 1 MiB. If the
image creation step fails because the content no longer fits, either reduce
the gzipped assets or increase the `webfs` size in `partitions.csv`.

When changing the size:

1. Edit the `webfs` row in `partitions.csv`.
2. Make sure the app partition and `webfs` still fit in the board flash.
3. Run `./tools/idf build` so `build/webfs.bin` is recreated with the new size.
4. Run `./tools/idf -p /dev/ttyACM1 flash` to write the updated partition
   table, firmware, and LittleFS image together.

## Layout

```
firmware/
├── CMakeLists.txt          # ESP-IDF root project
├── partitions.csv          # custom partition table (spec §10.5.2)
├── sdkconfig.defaults      # baseline Kconfig (spec §13.1)
├── web_dist/               # gzipped files packed into the webfs partition
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml   # esp_tinyusb, littlefs
│   └── main.c              # app_main
└── tools/
    └── idf                 # container wrapper around idf.py
```

## Switching IDF version

Edit the `IMAGE=` line in `tools/idf`, or override per-invocation:

```sh
ESP_IDF_IMAGE=docker.io/espressif/idf:v5.3.2 ./tools/idf build
```
