# Implementation Plan: Wi-Fi USB Drive on ESP32-S3 (T-Dongle S3)

Step-by-step plan to implement the project per [spec.md](spec.md).
The plan is incremental: each phase must produce a working, verifiable artifact.
The `firmware/` directory is currently empty — we start from scratch.

---

## Phase overview

| Phase | Goal | Verification artifact |
|---|---|---|
| 0 | ESP-IDF project skeleton | `idf.py build` succeeds, "boot" log line |
| 1 | BSP: LED + LCD + pins | Welcome screen on LCD, LED controllable |
| 2 | `sd_raw` + `sd_fatfs` wrappers | Card size in log, `wifi.cfg` readable |
| 3 | `sd_owner` arbiter | Manual FATFS↔NONE switching test |
| 4 | USB MSC (TinyUSB) | Card visible as flash drive on PC |
| 5 | Wi-Fi (cfg + mgr) | STA connect, IP in log |
| 6 | Full startup flow in `main.c` | Full §4 boot sequence |
| 7 | LittleFS (`webfs`) | Static assets served over HTTP |
| 8 | HTTP API: status + mode | `/api/status` + mode switching |
| 9 | HTTP API: file operations | upload/download/list/delete/mkdir |
| 10 | UI state machine (LCD/LED) | All §12.2 screens |
| 11 | Web UI (Preact) integration | Full end-user scenario |
| 12 | Run acceptance tests §15 | All §15 tables green |

---

## Phase 0. ESP-IDF project skeleton

**Goal.** A minimally buildable ESP-IDF v5.3 project in `firmware/`.

**Steps.**
1. Create `firmware/CMakeLists.txt` (root), `firmware/sdkconfig.defaults`, `firmware/partitions.csv`.
2. Create `firmware/main/{main.c,CMakeLists.txt,idf_component.yml}`.
3. In `partitions.csv` put the table from spec §10.5.2 (including `webfs` 1 MB littlefs).
4. In `sdkconfig.defaults` — FreeRTOS / TinyUSB / Wi-Fi / HTTPD / FATFS / partition table settings from §13.1.
5. In `main/idf_component.yml` — dependencies `espressif/esp_tinyusb ^1.4.0`, `joltwallet/littlefs ^1.14.0`.
6. `main.c` — empty `app_main` with `ESP_LOGI("main", "boot")`.

**Definition of Done.**
- `idf.py set-target esp32s3 && idf.py build` with no errors.
- `idf.py flash monitor` shows the `boot` log line.

---

## Phase 1. BSP — pins, LED (APA102), LCD (ST7735)

**Goal.** Minimal board support package: show a Welcome screen and blink the LED.

**Layout.**
```
components/bsp/
  ├── CMakeLists.txt
  ├── include/bsp_pins.h     # all BSP_* macros from spec §2
  ├── include/bsp_led.h
  ├── include/bsp_lcd.h
  ├── bsp_led.c              # APA102 via SPI (or bit-bang)
  └── bsp_lcd.c              # ST7735 80x160, minimal framebuffer
```

**Steps.**
1. `bsp_pins.h` — copy macros from §2.
2. `bsp_led`: APA102 driver via SPI2_HOST (DI=GPIO40, CI=GPIO39). API:
   - `bsp_led_init()`, `bsp_led_set_rgb(r,g,b)`, `bsp_led_off()`.
3. `bsp_lcd`: ST7735 80x160 portrait, MOSI=3, SCLK=5, CS=4, DC=2, RST=1, LEDA=38.
   Use the ESP-IDF SPI driver + manual ST7735 command protocol.
   API:
   - `bsp_lcd_init()`, `bsp_lcd_clear(color)`, `bsp_lcd_draw_text(x,y,str,color,bg)`,
   - `bsp_lcd_set_backlight(bool on)`.
   A single 8x16 built-in font (static array) is enough.
4. In `app_main` — init, Welcome screen, LED solid green for 2 s.

**Definition of Done.**
- LCD shows `Welcome / v0.0.3`.
- On-board LED is solid green.

**Pitfalls.**
- LCD backlight goes through a MOSFET on GPIO38 — verify the active level empirically.
- APA102 needs no strict timing but requires start/stop frames (4×0x00 / 4×0xFF).

---

## Phase 2. Storage: `sd_raw` and `sd_fatfs`

**Goal.** Two independent SDMMC drivers — raw sector access and FATFS — per spec §6–§7.

**Layout.**
```
components/storage/
  ├── CMakeLists.txt
  ├── include/sd_raw.h
  ├── include/sd_fatfs.h
  ├── sd_raw.c
  └── sd_fatfs.c
```

**Steps.**
1. `sd_raw.c` — exactly per §6.3:
   - `sdmmc_host_init`, `sdmmc_host_init_slot`, `sdmmc_card_init`.
   - 4-bit, 40 MHz, `SDMMC_SLOT_FLAG_INTERNAL_PULLUP`.
   - Sector count = `s_card->csd.capacity` (critical, see §16).
   - Mutex around every read/write.
   - `sd_raw_sync()` = `vTaskDelay(300ms)`.
2. `sd_fatfs.c` — `esp_vfs_fat_sdmmc_mount` on `/sdcard`, clean `esp_vfs_fat_sdcard_unmount` in deinit.
3. Temporary smoke test in `app_main`: init FATFS → print total/free → unmount → init raw → print sector count → deinit.

**Definition of Done.**
- Log shows "Card in 4-bit, freq=40MHz, capacity X sectors".
- FATFS reports correct total/free in MB.

**Pitfalls.**
- Parallel raw + FATFS init is forbidden (that is exactly what `sd_owner` solves in phase 3).
- `host.slot = SDMMC_HOST_SLOT_1` is mandatory.

---

## Phase 3. `sd_owner` — card-ownership arbiter

**Goal.** Single transition point for NONE↔FATFS↔MSC per spec §8.

**Layout.**
```
components/storage/include/sd_owner.h
components/storage/sd_owner.c
```

**Steps.**
1. Declare `sd_owner_t` enum, owner mutex, current state.
2. Implement `sd_owner_switch_to_fatfs/_to_msc/_release` exactly as in §8.2.
3. **Important nuance:** `sd_owner_switch_to_msc` calls `usb_msc_set_media_present(true)`.
   To avoid a circular dependency between components:
   - `components/storage` declares an external weak symbol, or takes a callback in `sd_owner_init(usb_msc_cb_t cb)`,
   - or uses an event-based approach via `esp_event` (recommended).
   Document the chosen approach in a comment.
4. Smoke test: FATFS → switch MSC → switch FATFS, verify `/sdcard/test.txt` mtime is preserved.

**Definition of Done.**
- 10 consecutive switches without crashes and without "Volume not properly unmounted".

---

## Phase 4. USB MSC via TinyUSB

**Goal.** Card visible as a USB flash drive on Linux / Windows.

**Layout.**
```
components/usb_msc/
  ├── CMakeLists.txt
  ├── include/usb_msc.h
  ├── usb_msc.c            # init + media_present + tud_msc_* callbacks
  ├── usb_descriptors.c    # tusb_desc_device_t, configuration desc, strings
  └── tusb_config.h
```

**Steps.**
1. `tusb_config.h` — from spec §9.1.
2. `usb_descriptors.c` — VID/PID, device descriptor with `bcdUSB=0x0200`, MSC-only configuration descriptor.
   Serial number generated from MAC in `usb_msc_init`.
3. `usb_msc.c`:
   - `usb_msc_init` — `tinyusb_driver_install(&cfg)`.
   - All `tud_msc_*` callbacks exactly as in spec §9.4.
   - `s_media_present` (volatile), `s_last_io_us`.
4. Wire up the call from `sd_owner.c` (callback / event).
5. **Standalone test (without sd_owner):** temporary code in `app_main` — `sd_raw_init() → usb_msc_init() → usb_msc_set_media_present(true)`. The card shows up as `/dev/sdX` on Linux.

**Definition of Done.**
- `dmesg`: device enumerates as `Wireless Drive` < 2 s after plug-in.
- No "extends beyond EOD" message.
- `dd ... bs=1M count=100` reports ≥ 3 MB/s.

**Pitfalls.**
- Do NOT call `vTaskDelay` inside `tud_msc_read10/write10_cb`. Only the sd_raw mutex.
- `CFG_TUD_MSC_EP_BUFSIZE=8192` — double buffering of 4 KB.
- TinyUSB task must run **on core 0** (`CONFIG_TINYUSB_TASK_AFFINITY_CPU0=y`).

---

## Phase 5. Wi-Fi: `wifi_cfg` + `wifi_mgr`

**Goal.** Parse `/sdcard/wifi.cfg`, read/write NVS, connect as STA.

**Layout.**
```
components/wifi_mgr/
  ├── CMakeLists.txt
  ├── include/wifi_cfg.h
  ├── include/wifi_mgr.h
  ├── wifi_cfg.c
  └── wifi_mgr.c
```

**Steps.**
1. `wifi_cfg.c`:
   - INI parser: keys `ssid=`, `password=`, `#` comments, file size < 256 bytes.
   - Validation: SSID 1–32, password "" or 8–63.
   - NVS namespace `"wifi"`, keys `"ssid"`, `"password"`.
   - `wifi_cfg_create_default()` writes the template from spec §4.2.
   - `wifi_cfg_delete_from_sd()` — `remove("/sdcard/wifi.cfg")`.
2. `wifi_mgr.c`:
   - `wifi_mgr_init`: `esp_netif_init`, `esp_event_loop_create_default`, default STA netif, `esp_wifi_init`.
   - `wifi_mgr_connect(creds, timeout_ms)`: configure, `esp_wifi_start`, wait for `IP_EVENT_STA_GOT_IP` via an EventGroup with timeout.
   - `wifi_mgr_get_status` — current SSID, IP string, RSSI.
3. **Standalone test:** temporary `app_main` — fatfs init → read wifi.cfg → connect → print IP.

**Definition of Done.**
- Log shows `Got IP: 192.168.X.Y` within 15 s.
- Wrong password — clean timeout, no hang.

---

## Phase 6. `main.c` — full startup flow

**Goal.** Implement `app_main` per spec §14 — the full boot sequence.

**Steps.**
1. Port the contents of §14 into `main.c`.
2. Resolve all TODO dependencies: ensure every called function exists. Some come from later phases — use stubs:
   - `webfs_mount()` — returns `ESP_OK` for now, real impl in phase 7.
   - `http_server_start()` — empty for now, real impl in phase 8.
   - `ui_state_show/update_*`, `ui_led_set_*` — log-only for now, full impl in phase 10.
3. Implement the credentials persistence matrix from §4.1 (step 7):
   - FROM_FILE + success → save_to_nvs → delete_from_sd.
   - save_to_nvs failed → LED yellow, keep the file.
4. `startup_error_loop` — infinite `vTaskDelay`.

**Definition of Done.**
- All 6 scenarios in §15.2 pass (with UI/HTTP still stubbed).

---

## Phase 7. `webfs` — LittleFS partition for the web UI

**Goal.** Mount the `webfs` partition and serve gzipped files over HTTP.

**Layout.**
```
components/webfs/
  ├── CMakeLists.txt
  ├── include/webfs.h
  └── webfs.c
```
Plus `firmware/web_dist/` — destination for `index.html.gz`, `app.js.gz`, `style.css.gz` copied from `web_ui`.

**Steps.**
1. `webfs.c` — `esp_vfs_littlefs_register` with `partition_label="webfs"`, mount at `/web`.
2. In the root `firmware/CMakeLists.txt` add:
   ```cmake
   littlefs_create_partition_image(webfs ../web_dist FLASH_IN_PROJECT)
   ```
3. Drop in a stub `web_dist/index.html.gz` (generated from a trivial `<h1>OK</h1>`).
4. The `handle_static` from spec §10.5.5 is implemented in phase 8.

**Definition of Done.**
- `idf.py flash` flashes the webfs image.
- Log shows `littlefs partition mounted at /web`.
- `curl http://<ip>/` returns the HTML (once phase 8 is done).

**Pitfalls.**
- Partition table must contain `webfs` (already added in phase 0).
- If the partition size changes, the image must be rebuilt (`idf.py littlefs-webfs-flash` or full flash).

---

## Phase 8. HTTP server: status + mode

**Goal.** `GET /api/status`, `POST /api/mode/usb|http`, static assets. Spec §11.1–11.3, §11.5, §11.6.

**Layout.**
```
components/http_server/
  ├── CMakeLists.txt
  ├── include/http_server.h
  ├── http_server.c          # http_server_start/stop, URI registration
  ├── http_api_status.c      # /api/status, /api/mode/*
  ├── http_static.c          # static handler from §10.5.5
  └── http_util.c            # send_4xx/5xx helpers, JSON helpers, path validator
```

**Steps.**
1. Set up `httpd_handle_t`, `httpd_config_t` (`max_open_sockets=4`, `max_uri_handlers=16`).
2. Implement helpers:
   - `send_json(req, code, json_str)`.
   - `send_409/503/500/400/507/201` following §11.3.
   - `extract_and_validate_path` (§11.6) — stub validator, fully used in phase 9.
3. `/api/status`: assemble JSON from `wifi_mgr_get_status`, `sd_fatfs_get_*` (when FATFS owns the card), `sd_owner_current`, `HTTP_API_VERSION=1`, `fw_version="0.0.3"`.
4. `/api/mode/usb` and `/api/mode/http` — exactly per §11.5:
   - `s_switch_mutex`, `try-take 0`.
   - Check `s_upload_in_progress` and `usb_msc_is_busy()`.
   - Call `sd_owner_switch_to_*`, return JSON, update UI.
5. `/` and any GET not under `/api/` → `handle_static` (§10.5.5).
6. Register wildcard for `/api/files/*` as a stub (phase 9).

**Definition of Done.**
- `curl /api/status` returns valid JSON.
- `curl -X POST /api/mode/usb` → 200, host sees the flash drive.
- `curl -X POST /api/mode/http` → 200, files accessible again.
- 50 USB↔HTTP cycles without crashes.

---

## Phase 9. HTTP API: file operations

**Goal.** Full CRUD per spec §11.2, §11.4, §11.6.

**Endpoints.**
- `GET /api/files?path={dir}` — list directory.
- `GET /api/files/{path}` — download (stream via `httpd_resp_send_chunk`).
- `POST /api/files/{path}` — upload (`application/octet-stream`, raw body).
- `DELETE /api/files/{path}` — delete a file or empty directory.
- `POST /api/mkdir?path={dir}` — create a directory (with intermediates).

**Steps.**
1. Implement `mkdir_parents(full_path)` — iterative mkdir per path component.
2. `handle_file_upload` — literally the code from §11.4. Critical points:
   - `s_upload_in_progress = false` through a single exit point before every `return`.
   - Delete the partial file on any error.
3. `handle_file_download` — `fopen` + `fread → httpd_resp_send_chunk` loop, `Content-Type` by extension.
4. `handle_file_list` — `opendir/readdir` + `stat` for size/mtime. JSON shape per §11.3.
5. `handle_file_delete` — `remove()` for files, `rmdir()` for directories.
6. `handle_mkdir` — `mkdir_parents`.
7. All handlers:
   - Check `sd_owner_current() == SD_OWNER_FATFS`, otherwise 503.
   - Validate the path via `extract_and_validate_path`.

**Definition of Done.**
- Every row in table §15.3a passes.
- 100 queued uploads succeed.
- Interrupted upload (Ctrl+C on curl) — partial file removed, `s_upload_in_progress` cleared.

---

## Phase 10. UI state machine: `ui_state` and `ui_led`

**Goal.** All 11 screens of §12.3 and the LED patterns of §12.1.

**Layout.**
```
components/ui/
  ├── CMakeLists.txt
  ├── include/ui_state.h
  ├── include/ui_led.h
  ├── ui_state.c   # render each screen via bsp_lcd
  └── ui_led.c     # task that blinks/holds the LED based on the current mode
```

**Steps.**
1. `ui_state`:
   - Stores the current `ui_screen_t` and last known ssid/ip/total/free.
   - On screen change: `bsp_lcd_clear` + multi-line `bsp_lcd_draw_text`.
   - Optionally use a large 16x32 font for the IP address.
2. `ui_led`:
   - Spawn a task (priority 2, core 1, stack 4096) that updates the LED color every ~100 ms.
   - Internal state: solid_blue / solid_green / solid_yellow / blink_red / blink_white / blink_green / off.
   - `ui_led_set_mode(ui_screen_t)` updates the internal state.
3. Hook calls into `main.c` (phase 6) and `http_api_status.c` (phase 8): `ui_state_set_switching`, `ui_state_set_mode_*`.

**Definition of Done.**
- Table §15.4 passes by visual inspection.

---

## Phase 11. Web UI (Preact) integration

**Goal.** Wire the existing `web_ui` (Vite + Preact) up to the real HTTP API.

**Steps.**
1. In `web_ui/src` implement (if not already there):
   - `api_version === 1` check on startup (§11.7).
   - Status polling (`/api/status` every 2 s).
   - Mode-switch buttons handling 409 responses (show the reason).
   - File listing, drag-and-drop upload with progress (`XMLHttpRequest`, §11.7).
   - Download / delete / mkdir.
2. Vite build: `pnpm build` → `dist/`.
3. Gzip the final files (vite-plugin-compression or manual `gzip -9`).
4. `build_webfs.sh` script copies `dist/*.gz` into `firmware/web_dist/`.
5. `idf.py flash` automatically builds the webfs image (`FLASH_IN_PROJECT`).

**Definition of Done.**
- Visiting `http://<ip>/` shows the UI.
- Full user scenario: upload a file → switch to USB → verify on PC.
- On `api_version` mismatch — banner is shown.

---

## Phase 12. Acceptance testing (§15)

**Goal.** Systematically walk through tables §15.1–§15.4.

**Approach.**
1. Create `docs/acceptance_results.md` — "test / result / log" table.
2. Run in order: §15.1 (USB MSC) → §15.2 (boot) → §15.3 (switching) → §15.3a (HTTP API) → §15.4 (UI).
3. For disk throughput — `dd`/`hdparm` under Linux, record MB/s.
4. For Windows / 3D printer — separate manual test, screenshots.
5. Per §16 ("checklist of potential problems") — explicitly confirm that each issue does NOT reproduce.

**Definition of Done.**
- 100% of §15 tables — pass.
- §16 — no regressions.

---

## Component dependency graph

```
                          main
                            │
        ┌───────────┬───────┼────────┬─────────┬──────────┐
        ▼           ▼       ▼        ▼         ▼          ▼
    wifi_mgr   http_server  ui    sd_owner   usb_msc   webfs
        │           │       │        │         │          │
        │           ▼       ▼        ▼         ▼          │
        │         (uses)  bsp_lcd  sd_raw   tinyusb       │
        │                 bsp_led  sd_fatfs                │
        ▼                                                  │
   esp_wifi/nvs                                         littlefs
```

**Initialization order in `app_main` (fixed by §14):**
1. `nvs_flash_init`.
2. `ui_state_init` / `ui_led_init` (bsp + ui).
3. `usb_msc_init` (BEFORE `wifi_mgr_init` — otherwise USB enumeration breaks, §16).
4. `webfs_mount`.
5. `sd_owner_init` → `sd_owner_switch_to_fatfs`.
6. `wifi_cfg_read_*`.
7. `wifi_mgr_init` + `wifi_mgr_connect`.
8. `http_server_start` (only on Wi-Fi success).
9. Final mode (HTTP or USB fallback).

---

## Critical invariants (must not be violated)

These are mirrored into the code as warning comments at the relevant sites:

1. **`sd_raw_get_sector_count` ALWAYS returns `csd.capacity`**, never `total_bytes/512` (§6.3, §16).
2. **`sd_fatfs_deinit` ALWAYS called before switching to MSC or ERROR** (§7, §16).
3. **`bcdUSB = 0x0200`** in the device descriptor — otherwise embedded hosts break (§9.2).
4. **`usb_msc_init` ALWAYS called**, even when the initial mode is USB (§9.5).
5. **`usb_msc_init` BEFORE `wifi_mgr_init`** (§16).
6. **`s_upload_in_progress` cleared through a single exit point** (§11.4).
7. **Every error path in `handle_file_upload` calls `remove(full_path)`** (§11.4).
8. **Every URL-derived path goes through `extract_and_validate_path`** (§11.6).
9. **TinyUSB task on core 0, HTTP/UI on core 1** (§13).
10. **API mode-switch is guarded by `s_switch_mutex` + `s_upload_in_progress` + `usb_msc_is_busy` checks** (§11.5).

---

## Open technical decisions (to be made during implementation)

| Question | Options | Recommendation |
|---|---|---|
| How does `sd_owner` call `usb_msc_set_media_present` | direct call / weak symbol / `esp_event` | `esp_event` — cleanest |
| Where does fw_version live | `#define` in `version.h` / `idf.py`-generated / CMake | `#define FW_VERSION "0.0.3"` in `version.h` |
| LCD fonts | single 8x16 / dual (8x16 + 16x32 for IP) | start with 8x16, add larger if needed |
| Web UI compression | vite plugin / separate shell step | `vite-plugin-compression` — simpler |
| MSC speed logging | always / debug only | keep `ESP_LOGW` for >50 ms — as in §9.4 |
| api_version mismatch on the client | modal / banner | non-blocking banner on top |

---

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| ST7735 driver on 80x160 has offset (col=24, row=0) | Image is shifted | Verify empirically, expose configurable offsets in `bsp_lcd_init` |
| `esp_tinyusb` 1.4.x API changes | Does not compile | Pin minor version, read changelog |
| LittleFS component requires ESP-IDF 5.x | Incompatibility | sdkconfig targets esp32s3, IDF 5.3 — OK |
| httpd + tusb stacks consume too much RAM | OOM | Profile `heap_caps_get_free_size`, shrink buffers as needed |
| Card runs in 1-bit (bad routing) | Speed < 1 MB/s | Warning in `sd_raw_init`, user replaces cable/card |
| Interrupted upload leaves stale files | "Garbage" files on FAT | Unit test: simulate `httpd_req_recv == 0` → verify `remove` was called |

---

## Intermediate milestone demos

1. **M1 (after phases 0–2):** Welcome on the display, SD size in the log.
2. **M2 (after phase 4):** Card works as a USB flash drive on PC.
3. **M3 (after phase 6):** Full boot sequence, USB fallback when Wi-Fi unavailable.
4. **M4 (after phase 9):** curl demo of file upload and mode switching.
5. **M5 (after phase 11):** Browser UI with drag-and-drop and live status.
6. **M6 (phase 12):** All acceptance tests green.

---

## Coding conventions

- Style: ESP-IDF (snake_case, `static` for file-locals, `s_` prefix for static variables).
- Error returns: `esp_err_t`; `ESP_ERROR_CHECK` only where a panic is acceptable (`nvs_flash_init` at startup).
- Every `.c` starts with `static const char *TAG = "module_name";`.
- Logging: `ESP_LOGI` for normal flow, `ESP_LOGW` for the unusual (slow MSC read, httpd_recv retry), `ESP_LOGE` on returned errors.
- Comments in English, matching the spec.
- No `printf` in production code.
- Public API headers in `components/<name>/include/`, private headers next to the `.c`.

---

## Out of scope in this version (per §17.1)

- OTA firmware updates — separate document, separate partition.
- OTA web-UI updates (`POST` partition image) — future feature.
- HTTP authentication — not needed (trusted local network).
- Multi-client upload — unsupported (single client, client-side queue).

These items are explicitly skipped and not treated as gaps.
