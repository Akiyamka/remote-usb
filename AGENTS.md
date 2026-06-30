# Repository Instructions

This repository contains an ESP-IDF firmware project under `firmware/`.

Do not run `idf.py` directly from the host. Use the project wrapper:

```sh
cd firmware
./tools/idf build
./tools/idf -p /dev/ttyACM1 flash
./tools/idf -p /dev/ttyACM1 monitor --no-reset
```

The wrapper runs the pinned Espressif ESP-IDF container via Podman or Docker.
The currently pinned image is defined in `firmware/tools/idf`.

For firmware changes, validate with:

```sh
cd firmware
./tools/idf build
```

If the build fails because Podman or Docker cannot access `/run`, device nodes,
cache directories, or the network from a sandbox, ask for permission to rerun the
same command outside the sandbox.
