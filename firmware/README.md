# Firmware

ESP-IDF v5.3 project for the T-Dongle S3 Wi-Fi USB drive.
See [`../docs/spec.md`](../docs/spec.md) and [`../docs/plan.md`](../docs/plan.md).

## Build environment

This project deliberately does **not** rely on a system-wide ESP-IDF install.
Instead it uses the official `espressif/idf` container via Podman through a
thin wrapper at [`tools/idf`](tools/idf). The IDF version is pinned in that
script (`release-v5.3`).

Requirements:

- Linux + [Podman](https://podman.io/) (rootless is fine and recommended)
- About 2 GB free disk for the container image on first pull
- About 1–2 GB free in `~/.cache/esp-idf-podman/` for the shared toolchain cache

## Usage

```sh
# First-time only: pull the image (~1.5 GB)
podman pull docker.io/espressif/idf:release-v5.3

# From the firmware/ directory:
./tools/idf set-target esp32s3
./tools/idf build

# Flashing & monitoring (replace the device path as needed):
./tools/idf -p /dev/ttyACM1 flash monitor

# Menuconfig (interactive ncurses UI — works thanks to -it in the wrapper)
./tools/idf menuconfig
```

The wrapper:

- mounts `firmware/` at `/project` inside the container,
- forwards any `/dev/tty*` argument as a `--device=` passthrough,
- persists ccache and the component-manager cache under
  `~/.cache/esp-idf-podman/` so subsequent builds are fast,
- uses `--userns=keep-id` so files written by the container are owned by your
  host user (no `chown` dance).

## Layout

```
firmware/
├── CMakeLists.txt          # ESP-IDF root project
├── partitions.csv          # custom partition table (spec §10.5.2)
├── sdkconfig.defaults      # baseline Kconfig (spec §13.1)
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml   # esp_tinyusb, littlefs
│   └── main.c              # app_main
└── tools/
    └── idf                 # Podman wrapper around idf.py
```

## Switching IDF version

Edit the `IMAGE=` line in `tools/idf`, or override per-invocation:

```sh
ESP_IDF_IMAGE=docker.io/espressif/idf:v5.3.2 ./tools/idf build
```
