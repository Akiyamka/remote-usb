# Specification: Wi-Fi USB Drive on ESP32-S3 (T-Dongle S3)

**Version:** 0.0.3
**Platform:** LilyGO T-Dongle S3 (ESP32-S3, 16 MB Flash, no PSRAM)
**SDK:** ESP-IDF v5.3.x

---

## 1. Context and goals

### 1.1. Purpose

The device is a "wireless flash drive" with two SD card operating modes:

- **MODE_USB** — SD card is handed over to the USB host via MSC (USB Mass Storage Class). The device appears as a regular USB flash drive to a PC, 3D printer, or other embedded host.
- **MODE_HTTP** — SD card is mounted to the ESP via FATFS. Accessible via HTTP API (upload/download/delete files).

In both modes, **Wi-Fi STA and the HTTP server are always running** (after a successful connection). In `MODE_USB`, the HTTP file operations API returns 503, but the status and mode-switching API remain available.

### 1.2. Target platform

| Parameter | Value |
|---|---|
| Chip | ESP32-S3 (Xtensa LX7, dual core) |
| Board | LilyGO T-Dongle S3 (not the Dual version) |
| Flash | 16 MB QD |
| PSRAM | Not present |
| SDK | ESP-IDF v5.3.x (release branch) |
| TinyUSB | via managed component `espressif/esp_tinyusb` v1.4+ |
| FreeRTOS | tick rate 1000 Hz |

Details in [hardware.md](hardware.md)

### 1.3. Issues from the previous Arduino implementation (excluded)

1. `SD_MMC.begin()` forcibly mounts FATFS in parallel with MSC → "Volume was not properly unmounted"
2. `setCapacity()` uses `totalBytes()/512` instead of `numSectors()` → "p1 size extends beyond EOD, truncated"
3. No TinyUSB and SDMMC timing logs
4. ~30 second recognition delay on Windows, 3D printer does not recognize the device
5. SCSI Read(10) commands get `DID_TIME_OUT` after 30 seconds

---

## 2. T-Dongle S3 pin mapping

```c
// SDMMC interface (4-bit mode)
#define BSP_SD_CMD      GPIO_NUM_16
#define BSP_SD_CLK      GPIO_NUM_12
#define BSP_SD_D0       GPIO_NUM_14
#define BSP_SD_D1       GPIO_NUM_17
#define BSP_SD_D2       GPIO_NUM_21
#define BSP_SD_D3       GPIO_NUM_18

// USB OTG (native on S3, hardwired)
// D+ = GPIO20, D- = GPIO19

// LCD (ST7735, 160x80 landscape)
// Native panel resolution is 80x160 portrait; we rotate to landscape via
// MADCTL/swap_xy so the long axis matches the dongle's physical orientation
// in a USB port. Backlight (LEDA) is wired through a P-channel high-side
// MOSFET — active level is LOW.
#define BSP_LCD_RST     GPIO_NUM_1
#define BSP_LCD_RS      GPIO_NUM_2  // DC
#define BSP_LCD_SDA     GPIO_NUM_3  // MOSI
#define BSP_LCD_SCL     GPIO_NUM_5  // SCLK
#define BSP_LCD_CS      GPIO_NUM_4
#define BSP_LCD_LEDA    GPIO_NUM_38 // backlight via MOSFET Q1

// APA102 LED (built-in)
#define BSP_LED_DI      GPIO_NUM_40
#define BSP_LED_CI      GPIO_NUM_39
```

---

## 3. Device state machine

### 3.1. States

```
STATE_BOOT          → executing the startup flow (welcome, SD check, wifi)
STATE_ERROR         → terminal error (no SD, invalid wifi.cfg, etc.) — LED blink, awaiting reboot
STATE_USB_MODE      → SD handed over to the host via MSC
STATE_HTTP_MODE     → SD mounted via FATFS, accessible over HTTP
STATE_SWITCHING    → intermediate state during USB↔HTTP transitions
```

### 3.2. Initial mode decision

After the startup flow completes:
- **If Wi-Fi connected successfully** → enter `STATE_HTTP_MODE`
- **If Wi-Fi did NOT connect** (no credentials, no network, wrong password) → enter `STATE_USB_MODE`, leaving the device useful as a plain flash drive for editing `wifi.cfg`

This yields user-friendly behavior: if something is wrong with Wi-Fi, the user plugs the dongle into a PC, edits `wifi.cfg`, reboots — and it works.

---

## 4. Startup flow (BOOT phase)

### 4.1. Full process

```
1. Display "Welcome / v0.0.2"
2. Init APA102 LED, LCD (ST7735)
3. Try sd_fatfs_init()
   ├── Failed → "SD Card required", LED blink red, STATE_ERROR
   └── OK → Display "Memory / total: XX / free: YY"
4. Check /sdcard/wifi.cfg
   ├── Exists + valid → use credentials from file (in_memory_creds_source = FILE)
   ├── Exists + invalid → "wifi.cfg invalid", LED blink red, STATE_ERROR
   └── Not exists
       ├── NVS has credentials → use NVS creds (source = NVS)
       └── NVS empty → create default wifi.cfg, "Please fill", LED blink green, STATE_ERROR
5. Display "Connecting to <SSID>"
6. wifi_sta_connect(ssid, password), timeout 15 sec
   ├── Failed → Display "Can't connect to <SSID>", LED blink red briefly,
   │             →→ transition to STATE_USB_MODE (degraded mode)
   └── Success → Display "<SSID> / <IP>", LED solid green (for 2 sec)
7. Credentials persistence (only on success):
   ├── source = NVS → do nothing
   └── source = FILE:
       ├── Save to NVS
       ├── Success → delete wifi.cfg
       └── Fail → LED solid yellow, keep wifi.cfg
8. If Wi-Fi OK → init HTTP server, transition to STATE_HTTP_MODE
9. If Wi-Fi failed → transition to STATE_USB_MODE without HTTP server
```

### 4.2. Important startup details

**During startup the SD card is owned by FATFS** (`sd_fatfs_init`); this is required to read `wifi.cfg`. After startup, when entering the initial mode, ownership is transferred:
- `STATE_HTTP_MODE` → FATFS stays
- `STATE_USB_MODE` → `sd_fatfs_deinit()` + `sd_raw_init()`

**wifi.cfg format** (simple INI-style):
```ini
ssid=MyHomeWiFi
password=secret123
```

**Validation:**
- File exists, size < 256 bytes
- Contains exactly two lines with keys `ssid=` and `password=`
- SSID 1-32 characters long
- Password is either empty (open network) or 8-63 characters

**Default wifi.cfg on creation:**
```ini
# Wi-Fi credentials for USB Drive
# Edit and reboot device
ssid=YOUR_WIFI_NETWORK
password=YOUR_PASSWORD
```

---

## 5. Component architecture

```
firmware/
├── main/
│   ├── main.c                       // app_main, startup flow, state machine
│   ├── CMakeLists.txt
│   └── idf_component.yml
├── components/
│   ├── bsp/                         // Board support package
│   │   ├── bsp_pins.h
│   │   ├── bsp_led.h / bsp_led.c    // APA102 over SPI3 (BGR frame order)
│   │   ├── bsp_lcd.h / bsp_lcd.c    // ST7735 over SPI2 via esp_lcd HAL
│   │   └── esp_lcd_st7735.h/.c      // Vendor-specific ST7735 init sequence
│   ├── fonts/                       // Maple Mono 12 / 14 / 16 px + flat lookup
│   │   ├── include/fonts.h          // Public API: fonts_glyph, metrics, pixel_a4
│   │   ├── priv_include/lvgl.h      // Minimal LVGL ABI shim (no LVGL dep)
│   │   ├── fonts.c / lv_font_stubs.c
│   │   └── // ../../fonts/lv_font_maplemomo_{12,14,16}.c (generated)
│   ├── storage/
│   │   ├── sd_raw.h / sd_raw.c      // Raw SDMMC access (for MSC)
│   │   ├── sd_fatfs.h / sd_fatfs.c  // FATFS access (for HTTP)
│   │   └── sd_owner.h / sd_owner.c  // Card ownership state machine
│   ├── usb_msc/
│   │   ├── usb_msc.h / usb_msc.c
│   │   ├── usb_descriptors.c
│   │   └── tusb_config.h
│   ├── wifi_mgr/
│   │   ├── wifi_mgr.h / wifi_mgr.c  // STA connection
│   │   └── wifi_cfg.h / wifi_cfg.c  // wifi.cfg parsing + NVS
│   ├── http_server/
│   │   ├── http_server.h / http_server.c
│   │   ├── http_api_status.c        // /api/status, /api/mode
│   │   ├── http_api_files.c        // /api/files/*
│   │   └── http_static.c           // serves the web UI from LittleFS
│   ├── webfs/
│   │   └── webfs.h / webfs.c        // mounts LittleFS partition with UI
│   └── ui/
│       ├── ui_state.h / ui_state.c  // LCD rendering for the current state
│       └── ui_led.h / ui_led.c      // LED patterns
└── sdkconfig.defaults
```

---

## 6. Low-level SD driver (raw access)

### 6.1. Requirements

- Initialize the SDMMC host in **4-bit mode at 40 MHz**
- Slot 1 (SDMMC_HOST_SLOT_1)
- **NO** FATFS mount (`esp_vfs_fat_sdmmc_mount` is not called)
- Direct sector access via `sdmmc_read_sectors` / `sdmmc_write_sectors`
- Correctly report the card's physical capacity from the CSD register
- Concurrent-access protection via a mutex

### 6.2. API

```c
// sd_raw.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t sd_raw_init(void);
esp_err_t sd_raw_deinit(void);
uint32_t  sd_raw_get_sector_count(void);
uint16_t  sd_raw_get_sector_size(void);
esp_err_t sd_raw_read_sectors(void *buf, uint32_t lba, uint32_t count);
esp_err_t sd_raw_write_sectors(const void *buf, uint32_t lba, uint32_t count);
bool      sd_raw_is_ready(void);
esp_err_t sd_raw_sync(void);
```

### 6.3. Implementation (key init section)

```c
esp_err_t sd_raw_init(void)
{
    if (s_initialized) return ESP_OK;

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz
    host.flags = SDMMC_HOST_FLAG_4BIT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = BSP_SD_CLK;
    slot_config.cmd = BSP_SD_CMD;
    slot_config.d0 = BSP_SD_D0;
    slot_config.d1 = BSP_SD_D1;
    slot_config.d2 = BSP_SD_D2;
    slot_config.d3 = BSP_SD_D3;
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_ERROR_CHECK(sdmmc_host_init());
    ESP_ERROR_CHECK(sdmmc_host_init_slot(host.slot, &slot_config));

    s_card = heap_caps_calloc(1, sizeof(sdmmc_card_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_card) {
        sdmmc_host_deinit();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = sdmmc_card_init(&host, s_card);
    if (ret != ESP_OK) {
        free(s_card); s_card = NULL;
        sdmmc_host_deinit();
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);

    if (!(s_card->host.flags & SDMMC_HOST_FLAG_4BIT)) {
        ESP_LOGW(TAG, "WARNING: card in 1-bit mode, performance limited");
    }

    s_initialized = true;
    return ESP_OK;
}

uint32_t sd_raw_get_sector_count(void)
{
    if (!s_initialized) return 0;
    return s_card->csd.capacity;  // CRITICAL: physical capacity from CSD
}

esp_err_t sd_raw_read_sectors(void *buf, uint32_t lba, uint32_t count)
{
    if (!s_initialized || !buf || count == 0) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = sdmmc_read_sectors(s_card, buf, lba, count);
    xSemaphoreGive(s_mutex);
    return ret;
}
// write_sectors is analogous

esp_err_t sd_raw_sync(void)
{
    if (!s_initialized) return ESP_OK;
    // CMD13 SEND_STATUS is implicitly issued by the driver before the next command.
    // It is enough to give the card time to program (typical 250ms max for SDHC).
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
}
```

### 6.4. Critical nuances

1. **DMA-capable buffers**: `buf` must live in internal SRAM. In MSC callbacks TinyUSB already passes DMA-capable buffers.
2. **Alignment**: at least 4 bytes, preferably 16.
3. **No vTaskDelay in the hot path** of read/write callbacks.
4. **Mutex everywhere**, even for read — the sdmmc driver is not reentrant.
5. **Do NOT call `esp_vfs_fat_sdmmc_mount`** in this module.

---

## 7. FATFS wrapper

```c
// sd_fatfs.h
esp_err_t sd_fatfs_init(void);
esp_err_t sd_fatfs_deinit(void);
const char* sd_fatfs_mount_point(void);  // returns "/sdcard"
uint64_t sd_fatfs_get_total_bytes(void);
uint64_t sd_fatfs_get_free_bytes(void);
```

```c
// sd_fatfs.c — init
esp_err_t sd_fatfs_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    // ... same pins as in sd_raw

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    return esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                   &mount_config, &s_card);
}

esp_err_t sd_fatfs_deinit(void)
{
    if (!s_card) return ESP_OK;
    // unmount automatically flushes FATFS buffers
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    if (ret != ESP_OK) return ret;
    // IDF 5.3 leaves the SDMMC peripheral initialized after FATFS unmount.
    // Release it explicitly so the next sd_raw_init() starts from a clean host.
    return sdmmc_host_deinit();
}
```

**Critical**: `sd_fatfs_deinit()` must be called:
- On `STATE_HTTP_MODE → STATE_USB_MODE` transitions
- Before `esp_restart()` (if it is ever needed)
- Before any transition into `STATE_ERROR` (if the card was mounted)

Otherwise FATFS buffers in RAM will not be flushed → "Volume was not properly unmounted" on the next mount.

After a successful `esp_vfs_fat_sdcard_unmount()`, `sd_fatfs_deinit()` must also
call `sdmmc_host_deinit()`. This was verified on IDF 5.3: without the explicit
host deinit, a subsequent `sd_raw_init()` logs `SDMMC host already initialized`,
meaning the FATFS release did not fully return the peripheral to the raw/MSC
owner.

---

## 8. SD Owner (card ownership state machine)

An arbiter module that guarantees that at every moment in time the card is owned by exactly one component. All transitions go through it.

### 8.1. API

```c
// sd_owner.h
typedef enum {
    SD_OWNER_NONE,    // card not initialized (boot, error)
    SD_OWNER_FATFS,   // card mounted as /sdcard (HTTP mode)
    SD_OWNER_MSC,     // card handed over via USB MSC
} sd_owner_t;

esp_err_t sd_owner_init(void);
sd_owner_t sd_owner_current(void);

// Atomic transition. Returns ESP_OK or ESP_ERR_INVALID_STATE if busy.
esp_err_t sd_owner_switch_to_fatfs(void);
esp_err_t sd_owner_switch_to_msc(void);
esp_err_t sd_owner_release(void);  // to SD_OWNER_NONE
```

### 8.2. Switching implementation

```c
static sd_owner_t s_owner = SD_OWNER_NONE;
static SemaphoreHandle_t s_owner_mutex = NULL;

esp_err_t sd_owner_switch_to_msc(void)
{
    xSemaphoreTake(s_owner_mutex, portMAX_DELAY);

    if (s_owner == SD_OWNER_MSC) {
        xSemaphoreGive(s_owner_mutex);
        return ESP_OK;
    }

    if (s_owner == SD_OWNER_FATFS) {
        // First, release the card from FATFS
        esp_err_t ret = sd_fatfs_deinit();
        if (ret != ESP_OK) {
            xSemaphoreGive(s_owner_mutex);
            return ret;
        }
    }

    // Initialize raw access
    esp_err_t ret = sd_raw_init();
    if (ret != ESP_OK) {
        s_owner = SD_OWNER_NONE;
        xSemaphoreGive(s_owner_mutex);
        return ret;
    }

    s_owner = SD_OWNER_MSC;

    // Signal the USB MSC module that the media is now available
    usb_msc_set_media_present(true);

    xSemaphoreGive(s_owner_mutex);
    return ESP_OK;
}

esp_err_t sd_owner_switch_to_fatfs(void)
{
    xSemaphoreTake(s_owner_mutex, portMAX_DELAY);

    if (s_owner == SD_OWNER_FATFS) {
        xSemaphoreGive(s_owner_mutex);
        return ESP_OK;
    }

    if (s_owner == SD_OWNER_MSC) {
        // First, "eject" the media for the host
        usb_msc_set_media_present(false);
        // Let TinyUSB process any remaining SCSI commands
        vTaskDelay(pdMS_TO_TICKS(200));
        // Wait for pending writes to complete
        sd_raw_sync();
        sd_raw_deinit();
    }

    esp_err_t ret = sd_fatfs_init();
    if (ret != ESP_OK) {
        s_owner = SD_OWNER_NONE;
        xSemaphoreGive(s_owner_mutex);
        return ret;
    }

    s_owner = SD_OWNER_FATFS;
    xSemaphoreGive(s_owner_mutex);
    return ESP_OK;
}
```

---

## 9. USB MSC via TinyUSB

### 9.1. TinyUSB configuration

`tusb_config.h`:
```c
#pragma once

#define CFG_TUSB_RHPORT0_MODE      OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE     64

#define CFG_TUD_MSC                1
#define CFG_TUD_CDC                0
#define CFG_TUD_HID                0

#define CFG_TUD_MSC_EP_BUFSIZE     8192   // 16 KB for double buffering
#define CFG_TUSB_DEBUG             2      // warning level
```

In `sdkconfig.defaults`:
```
CONFIG_TINYUSB_TASK_PRIORITY=5
CONFIG_TINYUSB_TASK_AFFINITY_CPU0=y     # USB on core 0
CONFIG_TINYUSB_MSC_BUFSIZE=8192
CONFIG_TINYUSB_DEBUG_LEVEL=2
```

### 9.2. Descriptors

```c
#define USB_VID  0xCAFE  // replace with your own
#define USB_PID  0x4001

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // NOT 0x0210 — embedded hosts dislike BOS
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

char const *string_desc_arr[] = {
    (const char[]) {0x09, 0x04},   // English
    "MyDevice",                     // Manufacturer
    "Wireless Drive",               // Product
    "000000000001",                 // Serial — generated from MAC
};

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define EPNUM_MSC_OUT  0x01
#define EPNUM_MSC_IN   0x81

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};
```

**Critical nuances**:
1. `bcdUSB = 0x0200` (not 0x0210)
2. Unique serial from the MAC address
3. ASCII strings, no Cyrillic
4. EP MaxPacketSize = 64 (Full Speed)
5. Inquiry strings — neutral, without "Espressif"

### 9.3. MSC API with "media present" flag support

```c
// usb_msc.h
esp_err_t usb_msc_init(void);

// Controls media presence as seen by the host.
// When false: host sees "card not inserted" (Sense: NOT_READY / MEDIUM_NOT_PRESENT)
// When true: SD is accessible via sd_raw_*
void usb_msc_set_media_present(bool present);

// Is a transfer (read/write) currently active? Used to block switching.
bool usb_msc_is_busy(void);
```

### 9.4. MSC callbacks

```c
// usb_msc.c
#include "tusb.h"
#include "sd_raw.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/atomic.h"

static volatile bool s_media_present = false;
static volatile int64_t s_last_io_us = 0;  // timestamp of the last read/write

void usb_msc_set_media_present(bool present)
{
    s_media_present = present;
    ESP_LOGI(TAG, "Media presence: %s", present ? "INSERTED" : "REMOVED");
}

bool usb_msc_is_busy(void)
{
    // Considered busy if there was read/write activity in the last 500 ms
    int64_t now = esp_timer_get_time();
    return (now - s_last_io_us) < 500000;
}

// ===== Inquiry =====
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4])
{
    const char vid[] = "MyDevice";
    const char pid[] = "Wireless Drive  ";
    const char rev[] = "1.00";
    memcpy(vendor_id, vid, 8);
    memcpy(product_id, pid, 16);
    memcpy(product_rev, rev, 4);
}

// ===== Test Unit Ready =====
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (!s_media_present) {
        // Tell the host "media not present" (sense key 0x02, ASC 0x3A)
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return sd_raw_is_ready();
}

// ===== Capacity =====
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    if (!s_media_present || !sd_raw_is_ready()) {
        *block_count = 0;
        *block_size = 0;
        return;
    }
    *block_count = sd_raw_get_sector_count();  // physical capacity from CSD!
    *block_size  = sd_raw_get_sector_size();
}

// ===== Start/Stop (host eject) =====
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t pc, bool start, bool load_eject)
{
    if (load_eject && !start) {
        ESP_LOGI(TAG, "Host requested eject");
        if (s_media_present) sd_raw_sync();
        // Do not set s_media_present=false here — that is done by sd_owner
    }
    return true;
}

// ===== Read =====
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize)
{
    if (!s_media_present) return -1;
    if (offset != 0) {
        ESP_LOGW(TAG, "Unaligned read offset=%u", offset);
        return -1;
    }

    s_last_io_us = esp_timer_get_time();
    uint32_t sectors = bufsize / 512;

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = sd_raw_read_sectors(buffer, lba, sectors);
    int64_t dt = esp_timer_get_time() - t0;

    if (dt > 50000) {
        ESP_LOGW(TAG, "Slow read lba=%u cnt=%u dt=%lldus", lba, sectors, dt);
    }
    return (err == ESP_OK) ? bufsize : -1;
}

// ===== Write =====
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize)
{
    if (!s_media_present) return -1;
    if (offset != 0) return -1;

    s_last_io_us = esp_timer_get_time();
    uint32_t sectors = bufsize / 512;

    esp_err_t err = sd_raw_write_sectors(buffer, lba, sectors);
    return (err == ESP_OK) ? bufsize : -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize)
{
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    return s_media_present;  // in HTTP mode, everything is read-only from the host's perspective
}
```

### 9.5. Initialization (called once at startup)

```c
esp_err_t usb_msc_init(void)
{
    // Generate serial from MAC
    static char serial_str[13];
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    string_desc_arr[3] = serial_str;

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &desc_device,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };
    return tinyusb_driver_install(&tusb_cfg);
}
```

**Important**: `usb_msc_init()` is **always** called at device startup, regardless of which initial mode is selected. The USB stack comes up, but `s_media_present = false` until `sd_owner` switches into MSC.

---

## 10. Wi-Fi and configuration

### 10.1. wifi_cfg module

```c
// wifi_cfg.h
typedef struct {
    char ssid[33];      // max 32 + null
    char password[64];  // max 63 + null
} wifi_creds_t;

typedef enum {
    WIFI_CREDS_NONE,
    WIFI_CREDS_FROM_NVS,
    WIFI_CREDS_FROM_FILE,
} wifi_creds_source_t;

// Read wifi.cfg from SD (requires FATFS). Returns
// ESP_ERR_NOT_FOUND if the file is missing, ESP_ERR_INVALID_ARG if invalid.
esp_err_t wifi_cfg_read_from_sd(wifi_creds_t *out);

// Read from NVS.
esp_err_t wifi_cfg_read_from_nvs(wifi_creds_t *out);

// Save to NVS.
esp_err_t wifi_cfg_save_to_nvs(const wifi_creds_t *creds);

// Create a default wifi.cfg on SD.
esp_err_t wifi_cfg_create_default(void);

// Delete wifi.cfg from SD.
esp_err_t wifi_cfg_delete_from_sd(void);
```

### 10.2. wifi_mgr module

```c
// wifi_mgr.h
typedef struct {
    bool connected;
    char ssid[33];
    char ip_str[16];
    int8_t rssi;
} wifi_status_t;

esp_err_t wifi_mgr_init(void);  // tcpip, netif, wifi driver init
esp_err_t wifi_mgr_connect(const wifi_creds_t *creds, uint32_t timeout_ms);
esp_err_t wifi_mgr_disconnect(void);
void wifi_mgr_get_status(wifi_status_t *out);
```

---

## 10.5. Web UI: LittleFS storage

### 10.5.1. Rationale

The web UI (HTML/JS/CSS) is stored in a **separate LittleFS partition** in flash memory, not embedded in the firmware binary (EMBED_FILES). Reasons:

1. **Independent updates.** During development the web UI is tweaked frequently (layout, style changes), while the firmware changes rarely. A separate partition lets you re-flash only the LittleFS image (`idf.py littlefs-flash` or similar) without rebuilding and re-flashing firmware, without reboots or dropped sessions. In the future this is the basis for OTA updates of just the UI.
2. **SD card independent.** The partition lives in flash and is available in any SD mode (USB/HTTP), which is mandatory: the web UI must be served even while the card is owned by the host, so the user can press "switch mode".

LittleFS is preferred over SPIFFS: faster reads, real directories, robustness against sudden power loss. Integrated as the managed component `joltwallet/littlefs`.

Note: both approaches (a LittleFS partition and EMBED) physically live in the same 16 MB flash — the difference is only in organization (a separate partition vs. part of the app partition). Neither saves space; the choice is driven by independent updates.

The web UI source files are stored in the `web_ui` directory.

### 10.5.2. Partition table

`partitions.csv`:
```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x6000,
phy_init,   data, phy,     0xf000,   0x1000,
factory,    app,  factory, 0x10000,  0x300000,
webfs,      data, littlefs,,         0x100000,
```

- `factory` (app) — 3 MB for the firmware, with room to spare for Wi-Fi+USB+TinyUSB
- `webfs` — 1 MB for the web UI (LittleFS). With gzip-compressed static assets this is plenty for any reasonable UI
- The rest of flash is free (can be reserved for OTA partitions in a future version)

In `sdkconfig.defaults`:
```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

### 10.5.3. Building and flashing the image

The web UI sources live in a project folder (e.g. `web_dist/`), where the web developer places the built/compressed files. During the build they are packed into the LittleFS image.

Store static assets **already gzipped** (`index.html.gz`, `app.js.gz`, `style.css.gz`) and serve them with the `Content-Encoding: gzip` header — this saves both partition space and bandwidth. The browser decompresses transparently. The static-asset compression step happens during the client build.

In the project's `CMakeLists.txt`:
```cmake
littlefs_create_partition_image(webfs ../web_dist FLASH_IN_PROJECT)
```

`FLASH_IN_PROJECT` means the image is flashed together with `idf.py flash`. To flash **only** the web UI without the firmware — a separate build target produces the image and `esptool` writes it at the `webfs` partition offset (the exact command is generated by the component littlefs).

### 10.5.4. webfs module

```c
// webfs.h
esp_err_t webfs_mount(void);    // mounts the LittleFS partition "webfs" at /web
const char* webfs_root(void);   // returns "/web"
```

```c
// webfs.c
#include "esp_littlefs.h"

static const char *MOUNT = "/web";

esp_err_t webfs_mount(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT,
        .partition_label = "webfs",
        .format_if_mount_failed = false,  // do not format — the image is flashed at provisioning time
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE("webfs", "mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

const char* webfs_root(void) { return MOUNT; }
```

`webfs_mount()` is called once at startup (the partition is independent of the SD card and is mounted early). The LittleFS partition and SDMMC do not conflict — they are on different peripherals.

### 10.5.5. Serving static assets with gzip

```c
// http_static.c — static asset handler
esp_err_t handle_static(httpd_req_t *req)
{
    // URI → file mapping. "/" → "/index.html"
    char path[300];
    const char *uri = (strcmp(req->uri, "/") == 0) ? "/index.html" : req->uri;
    snprintf(path, sizeof(path), "%s%s.gz", webfs_root(), uri);  // look for the .gz version

    FILE *f = fopen(path, "rb");
    bool gzipped = (f != NULL);
    if (!f) {
        // fallback to the uncompressed version
        snprintf(path, sizeof(path), "%s%s", webfs_root(), uri);
        f = fopen(path, "rb");
    }
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, content_type_from_ext(uri));  // text/html, application/javascript, ...
    if (gzipped) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    char buf[2048];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  // end of response
    return ESP_OK;
}
```

---

## 11. HTTP API

### 11.1. API versioning

The HTTP API has a numeric contract version (`api_version`), independent of the firmware version. At startup the web client compares its hardcoded expected version with whatever the device reports, and on a mismatch shows a mismatch banner instead of silently breaking. This covers two sources of desynchronization: a stale LittleFS image containing the web UI, and a cached older client in the browser.

```c
#define HTTP_API_VERSION  1   // bump on incompatible contract changes
```

Versioning rule: `api_version` is incremented only on **incompatible** changes (removing/renaming an endpoint, changing the response format). Adding a new optional field or endpoint does not bump the version.

### 11.2. Endpoints

| Method | Path | Description | Works in mode |
|---|---|---|---|
| GET | `/api/status` | Device status (mode, SD info, Wi-Fi, versions) | Always |
| POST | `/api/mode/usb` | Switch to USB MSC mode | Always |
| POST | `/api/mode/http` | Switch to HTTP mode | Always |
| GET | `/api/files?path={dir}` | List files/folders in a directory | HTTP only |
| GET | `/api/files/{path}` | Download a file | HTTP only |
| POST | `/api/files/{path}` | Upload a file (raw body, see 11.4) | HTTP only |
| DELETE | `/api/files/{path}` | Delete a file or empty folder | HTTP only |
| POST | `/api/mkdir?path={dir}` | Create a directory | HTTP only |
| GET | `/` and static assets | Web UI from LittleFS | Always |

**Nested paths**: `{path}` may contain `/` for nested directories, e.g. `POST /api/files/gcode/benchy/test.gcode`. Intermediate directories are created automatically on upload. All paths are normalized and checked for the absence of `..` (path-traversal protection outside `/sdcard`).

### 11.3. Response formats

```jsonc
// GET /api/status
{
  "fw_version": "0.0.3",         // firmware version
  "api_version": 1,              // HTTP API contract version
  "mode": "http",                // "usb" | "http" | "switching"
  "sd": {
    "present": true,
    "total_mb": 30720,           // null in usb mode
    "free_mb": 28432             // null in usb mode
  },
  "wifi": {
    "connected": true,
    "ssid": "MyHomeWiFi",
    "ip": "192.168.1.42",
    "rssi": -54
  }
}

// GET /api/files?path=gcode
{
  "ok": true,
  "path": "gcode",
  "entries": [
    { "name": "benchy", "type": "dir", "mtime": 1717245660 },
    { "name": "test.gcode", "type": "file", "sizeKb": 1048576, "mtime": 1717249200 },
    { "name": "calibration.gcode", "type": "file", "sizeKb": 204800, "mtime": null }
  ]
}

// POST /api/mode/usb | /api/mode/http
HTTP 200  { "ok": true, "mode": "usb" }
HTTP 200  { "ok": true, "mode": "usb", "no_change": true }
HTTP 409  { "ok": false, "error": "busy", "reason": "active_upload" }
HTTP 409  { "ok": false, "error": "busy", "reason": "host_io_active" }
HTTP 409  { "ok": false, "error": "busy", "reason": "switch_in_progress" }

// POST /api/files/{path} — success
HTTP 201  { "ok": true, "path": "gcode/test.gcode", "size": 1048576 }

// File operations in USB mode
HTTP 503  { "ok": false, "error": "mode_mismatch", "current_mode": "usb" }

// Path traversal / invalid path
HTTP 400  { "ok": false, "error": "invalid_path" }

// Insufficient space on the card
HTTP 507  { "ok": false, "error": "insufficient_storage", "free_mb": 12 }
```

`mtime` is the last modification time as a Unix timestamp in seconds, UTC. The field may be omitted or set to `null` when the filesystem timestamp is unavailable; clients must handle both cases.

### 11.4. File upload: raw body streaming

The file is transmitted as the **raw request body** (`Content-Type: application/octet-stream`), with the name and path taken from the URL. No multipart parsing. On the ESP side the body is read in chunks and written directly to FATFS — the entire file is never buffered in RAM (critical, since PSRAM is absent).

The concurrency model is simplified: there is always one client and it uploads files strictly one at a time (queued on the web-client side). Therefore the guard is a simple boolean flag `s_upload_in_progress`, not a counter.

```c
// http_api_files.c
#define UPLOAD_CHUNK_SIZE  4096   // socket read buffer (DMA-capable not required for FATFS write)

static volatile bool s_upload_in_progress = false;

esp_err_t handle_file_upload(httpd_req_t *req)
{
    // 1. Mode must be HTTP
    if (sd_owner_current() != SD_OWNER_FATFS) {
        return send_503_mode_mismatch(req);
    }

    // 2. Parse and validate the path from the URI
    char rel_path[256];
    if (extract_and_validate_path(req->uri, "/api/files/", rel_path, sizeof(rel_path)) != ESP_OK) {
        return send_400(req, "invalid_path");
    }

    char full_path[300];
    snprintf(full_path, sizeof(full_path), "%s/%s", sd_fatfs_mount_point(), rel_path);

    // 3. Create intermediate directories
    if (mkdir_parents(full_path) != ESP_OK) {
        return send_500(req, "mkdir_failed");
    }

    // 4. Check free space (Content-Length must be set by the browser)
    size_t content_len = req->content_len;
    if (content_len > sd_fatfs_get_free_bytes()) {
        return send_507(req, sd_fatfs_get_free_bytes() / (1024 * 1024));
    }

    // 5. Mark busy (blocks switching to USB)
    s_upload_in_progress = true;

    FILE *f = fopen(full_path, "wb");
    if (!f) {
        s_upload_in_progress = false;
        return send_500(req, "fopen_failed");
    }

    // 6. Stream the body in chunks
    char *buf = malloc(UPLOAD_CHUNK_SIZE);
    if (!buf) {
        fclose(f);
        remove(full_path);
        s_upload_in_progress = false;
        return send_500(req, "no_mem");
    }

    int remaining = content_len;
    esp_err_t result = ESP_OK;

    while (remaining > 0) {
        int to_read = (remaining < UPLOAD_CHUNK_SIZE) ? remaining : UPLOAD_CHUNK_SIZE;
        int received = httpd_req_recv(req, buf, to_read);

        if (received <= 0) {
            // HTTPD_SOCK_ERR_TIMEOUT can be retried, everything else is an error
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            result = ESP_FAIL;
            break;
        }

        size_t written = fwrite(buf, 1, received, f);
        if (written != (size_t)received) {
            result = ESP_FAIL;  // disk full or write error
            break;
        }
        remaining -= received;
    }

    free(buf);
    fclose(f);

    // 7. CRITICAL: always clear the flag (even on error)
    s_upload_in_progress = false;

    if (result != ESP_OK) {
        remove(full_path);  // delete the partially written file
        return send_500(req, "upload_failed");
    }

    return send_201_uploaded(req, rel_path, content_len);
}
```

**Key upload points:**
1. `s_upload_in_progress` is reset in **all** exit paths (through a single point before return). Leaking the flag will permanently block switching to USB.
2. On error the partially written file is deleted (`remove`) so no broken files are left behind.
3. `httpd_req_recv` may return `HTTPD_SOCK_ERR_TIMEOUT` — this is not fatal, retry the read.
4. The 4 KB chunk size is a balance between RAM and the number of FATFS operations. Can be raised to 8 KB if RAM allows.
5. Pre-checking free space before writing saves time on obviously-too-large files but does not replace the `fwrite` check (space may run out due to fragmentation/metadata).

### 11.5. Race protection during switching

```c
// http_api_status.c
static SemaphoreHandle_t s_switch_mutex = NULL;

esp_err_t handle_switch_to_usb(httpd_req_t *req)
{
    if (xSemaphoreTake(s_switch_mutex, 0) != pdTRUE) {
        return send_409(req, "switch_in_progress");
    }

    // An active HTTP upload blocks the move to USB
    if (s_upload_in_progress) {
        xSemaphoreGive(s_switch_mutex);
        return send_409(req, "active_upload");
    }

    if (sd_owner_current() == SD_OWNER_MSC) {
        xSemaphoreGive(s_switch_mutex);
        return send_200_no_change(req, "usb");
    }

    ui_state_set_switching();
    esp_err_t ret = sd_owner_switch_to_msc();
    xSemaphoreGive(s_switch_mutex);

    if (ret != ESP_OK) {
        ui_state_set_error();
        return send_500(req, esp_err_to_name(ret));
    }
    ui_state_set_mode_usb();
    return send_200_ok(req, "usb");
}

esp_err_t handle_switch_to_http(httpd_req_t *req)
{
    if (xSemaphoreTake(s_switch_mutex, 0) != pdTRUE) {
        return send_409(req, "switch_in_progress");
    }

    // The host is actively reading/writing via MSC — do not yank the card
    if (sd_owner_current() == SD_OWNER_MSC && usb_msc_is_busy()) {
        xSemaphoreGive(s_switch_mutex);
        return send_409(req, "host_io_active");
    }

    if (sd_owner_current() == SD_OWNER_FATFS) {
        xSemaphoreGive(s_switch_mutex);
        return send_200_no_change(req, "http");
    }

    ui_state_set_switching();
    esp_err_t ret = sd_owner_switch_to_fatfs();
    xSemaphoreGive(s_switch_mutex);

    if (ret != ESP_OK) {
        ui_state_set_error();
        return send_500(req, esp_err_to_name(ret));
    }
    ui_state_set_mode_http();
    return send_200_ok(req, "http");
}
```

### 11.6. Path traversal protection

Any path coming from the URL must be validated before use:

```c
// Checks that rel_path does not escape the card root.
// Forbids: ".." components, absolute paths, empty components.
static esp_err_t extract_and_validate_path(const char *uri, const char *prefix,
                                           char *out, size_t out_size)
{
    // 1. Strip the /api/files/ prefix
    const char *p = strstr(uri, prefix);
    if (!p) return ESP_ERR_INVALID_ARG;
    p += strlen(prefix);

    // 2. URL-decode (%20 → space, etc.)
    char decoded[256];
    if (url_decode(p, decoded, sizeof(decoded)) != ESP_OK) return ESP_ERR_INVALID_ARG;

    // 3. Cut off the query string if any
    char *q = strchr(decoded, '?');
    if (q) *q = '\0';

    // 4. Forbid ".." and a leading slash
    if (strstr(decoded, "..") != NULL) return ESP_ERR_INVALID_ARG;
    if (decoded[0] == '/') return ESP_ERR_INVALID_ARG;
    if (strlen(decoded) == 0) return ESP_ERR_INVALID_ARG;

    strlcpy(out, decoded, out_size);
    return ESP_OK;
}
```

### 11.7. Client upload code (for the web developer's reference)

Uploading a single file with progress via `XMLHttpRequest` (fetch does not provide upload progress):

```javascript
function uploadFileWithProgress(file, targetPath, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `/api/files/${encodeURIComponent(targetPath)}`);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) onProgress(e.loaded / e.total * 100, e.loaded, e.total);
    };
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) resolve(JSON.parse(xhr.responseText));
      else reject(new Error(`Upload failed: ${xhr.status} ${xhr.responseText}`));
    };
    xhr.onerror = () => reject(new Error('Network error'));
    xhr.send(file);   // File/Blob goes as the raw body
  });
}

// Queue: upload strictly one file at a time
async function uploadQueue(files, onFileProgress) {
  for (const file of files) {
    await uploadFileWithProgress(file, file.name, (pct) => onFileProgress(file, pct));
  }
}

// API version compatibility check at client startup
const EXPECTED_API_VERSION = 1;
async function checkCompatibility() {
  const status = await fetch('/api/status').then(r => r.json());
  if (status.api_version !== EXPECTED_API_VERSION) {
    showVersionMismatchBanner(EXPECTED_API_VERSION, status.api_version);
    return false;
  }
  return true;
}
```

---

## 12. LCD and LED indication

### 12.1. APA102 LED states

| State | Color / pattern |
|---|---|
| `STATE_BOOT` (welcome) | Off |
| `STATE_BOOT` (connecting wifi) | Blink white |
| `STATE_USB_MODE` | Solid blue |
| `STATE_HTTP_MODE` | Solid green |
| `STATE_SWITCHING` | Solid yellow |
| `STATE_ERROR` | Blink red |
| Saving creds failed (special) | Solid yellow |
| wifi.cfg created (waiting fill) | Blink green |

### 12.2. LCD screens

**Hardware.** ST7735 0.96" IPS panel, native 80x160. The driver configures
the panel in **landscape 160x80** (`esp_lcd_panel_swap_xy=true`,
`mirror(false, true)`, `set_gap(1, 26)`) so the long axis matches how the
dongle sits in a horizontal USB port. The IPS panel needs
`invert_color=true`. SPI link runs at 40 MHz on SPI2_HOST with hardware
RGB565 byte-swap enabled, so tile buffers stay in host-endian uint16_t.

**Fonts.** Three Maple Mono variants generated by `lv_font_conv` cover
ASCII 0x20..0x7E at 4 bpp anti-aliased:

| Handle | Size | line_height | typical advance |
|---|---|---|---|
| `FONT_SMALL`  | 12 px | 13 px | ~7 px |
| `FONT_MEDIUM` | 14 px | 17 px | ~8 px |
| `FONT_LARGE`  | 16 px | 19 px | ~10 px |

Approximate text grid in landscape 160x80:

| Font | Chars / line | Lines / screen |
|---|---|---|
| Small  | ~22 | 6 |
| Medium | ~20 | 4 |
| Large  | ~16 | 4 |

**Rendering pipeline.** LVGL itself is *not* linked into the firmware.
The `fonts` component ships a 100-line shim header that declares only the
LVGL ABI types `lv_font_conv` emits (`lv_font_t`, `lv_font_fmt_txt_dsc_t`,
`lv_font_fmt_txt_glyph_dsc_t`, `lv_font_fmt_txt_cmap_t`), so the
auto-generated `.c` files compile as-is. Two callback-pointer fields
(`get_glyph_dsc`, `get_glyph_bitmap`) point at no-op `abort()` stubs and
are never invoked — the renderer walks `dsc` directly.

For each character `bsp_lcd_draw_text()`:

1. Looks up the glyph descriptor (single FORMAT0_TINY cmap covers 32..126).
2. Allocates a static cell of `adv_w × line_height` pixels in BSS
   (max 16×24 RGB565 = 768 B; reused across all draws).
3. Pre-fills the cell with `bg`.
4. Walks the 4 bpp glyph box and, for each pixel with alpha 0..15:
   - `α = 0`  → keep `bg`
   - `α = 15` → write `fg`
   - otherwise → linear blend per RGB565 channel.
5. Ships the cell via `esp_lcd_panel_draw_bitmap(x, y, x+adv_w, y+line_h)`.

`bsp_lcd_draw_text_centered()` first walks the string to sum advance widths
then offsets the caret to centre the line in the 160 px viewport.

**Welcome screen:**
```
Welcome
v0.0.3
```

**Memory screen:**
```
Memory
total: 32GB
free: 30GB
```

**Connecting:**
```
Connecting to
<SSID>
```

**Connection failed:**
```
Can't connect
to <SSID>
USB drive
mode active
```

**Normal operation (HTTP):**
* Display the IP address in a large font
```
<SSID>
192.168.1.42
Mode: HTTP
```

**Normal operation (USB):**
* Display the IP address in a large font
```
<SSID>
192.168.1.42
Mode: USB
```

**Switching:**
* Display the IP address in a large font
```
<SSID>
192.168.1.42
Switching...
```

**Errors:**
```
SD Card
required
```
```
wifi.cfg invalid
Fix and reboot
```

### 12.3. Module ui_state

```c
// ui_state.h
typedef enum {
    UI_BOOT_WELCOME,
    UI_BOOT_SD_MEMORY,         // shows total/free
    UI_BOOT_CONNECTING,        // shows SSID
    UI_BOOT_CONFIG_INVALID,
    UI_BOOT_CONFIG_CREATED,
    UI_BOOT_CONNECT_FAILED,
    UI_MODE_USB,
    UI_MODE_HTTP,
    UI_SWITCHING,
    UI_ERROR_SD,
    UI_ERROR_GENERIC,
} ui_screen_t;

void ui_state_init(void);
void ui_state_show(ui_screen_t screen);
void ui_state_update_wifi(const char *ssid, const char *ip);
void ui_state_update_memory(uint64_t total_bytes, uint64_t free_bytes);
```

The LED is driven by a separate `ui_led` module that synchronizes automatically with the current state:
```c
void ui_led_init(void);
void ui_led_set_mode(ui_screen_t state);
```

---

## 13. FreeRTOS task layout

The T-Dongle S3 is dual core with no PSRAM. Core assignment is critical for performance:

| Task | Priority | Core | Stack |
|---|---|---|---|
| `tusb_device_task` | 5 | 0 | 4096 |
| `tusbd_msc_task` | 4 | 0 | 4096 |
| `wifi_task` (internal) | 23 | 0 | — |
| `httpd_task` | 5 | 1 | 8192 |
| `httpd_session_*` | 5 | 1 | 4096 |
| `ui_task` (LCD/LED updates) | 2 | 1 | 4096 |
| `app_main` | 1 | 0 (default) | 4096 |

**Key idea**: USB and Wi-Fi RX/TX tasks live on core 0, while the HTTP server and UI live on core 1. This separates the data paths and prevents Wi-Fi at top speed from blocking USB callbacks.

### 13.1. sdkconfig.defaults

```
# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=n
CONFIG_FREERTOS_USE_TICKLESS_IDLE=n

# TinyUSB
CONFIG_TINYUSB_TASK_PRIORITY=5
CONFIG_TINYUSB_TASK_AFFINITY_CPU0=y
CONFIG_TINYUSB_MSC_BUFSIZE=8192
CONFIG_TINYUSB_DEBUG_LEVEL=2

# Wi-Fi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=y
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y

# HTTP server
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_HTTPD_MAX_URI_LEN=512

# FATFS
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
CONFIG_FATFS_PER_FILE_CACHE=n  # save RAM

# Partition table (custom, with the webfs LittleFS partition)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Flash size — T-Dongle S3 ships with 16 MB Quad-SPI flash (see hardware.md).
# Required: the partition table from §10.5.2 (3 MB factory + 1 MB webfs +
# bootloader/nvs/phy_init) does not fit into the IDF default of 2 MB.
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"

# Post-flash behaviour: keep the chip in the bootloader instead of doing a
# hard reset. On ESP32-S3 the host talks to the built-in USB-Serial-JTAG over
# USB-CDC; the "RTS pulse" that esptool's default `hard_reset` uses is
# interpreted as GPIO0=LOW by that peripheral, so the chip lands back in
# DOWNLOAD mode (boot:0x22) right after flashing. With NORESET the developer
# simply unplugs/replugs (or presses the on-board reset) and the new app runs.
CONFIG_ESPTOOLPY_AFTER_NORESET=y
CONFIG_ESPTOOLPY_AFTER="no_reset"

# System
CONFIG_ESP_TASK_WDT_INIT=n     # for development; set back to y for production
CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y
```

Dependencies in `main/idf_component.yml`:
```yaml
dependencies:
  espressif/esp_tinyusb: "^1.4.0"
  joltwallet/littlefs: "^1.14.0"
```

---

## 14. main.c — startup flow

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp_pins.h"
#include "ui_state.h"
#include "ui_led.h"
#include "sd_fatfs.h"
#include "sd_owner.h"
#include "wifi_cfg.h"
#include "wifi_mgr.h"
#include "usb_msc.h"
#include "http_server.h"
#include "webfs.h"

static const char *TAG = "main";

static void startup_error_loop(void)
{
    // LED is already blinking red, just hang here
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

void app_main(void)
{
    // ===== 1. Init basics =====
    ESP_ERROR_CHECK(nvs_flash_init());
    ui_state_init();
    ui_led_init();
    ui_state_show(UI_BOOT_WELCOME);

    // ===== 2. USB MSC is always initialized =====
    // (media_present=false, the host will see "no media" until sd_owner allows it)
    ESP_ERROR_CHECK(usb_msc_init());

    // ===== 2.5. Mount the web UI (LittleFS, independent of SD) =====
    if (webfs_mount() != ESP_OK) {
        ESP_LOGW(TAG, "Web UI partition mount failed — HTTP server will 404 on static");
        // Not fatal: the API works, only static assets return errors
    }

    // ===== 3. SD card check =====
    vTaskDelay(pdMS_TO_TICKS(1500));  // show the welcome screen for 1.5 sec

    sd_owner_init();
    esp_err_t ret = sd_owner_switch_to_fatfs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card not detected: %s", esp_err_to_name(ret));
        ui_state_show(UI_ERROR_SD);
        startup_error_loop();
    }

    uint64_t total = sd_fatfs_get_total_bytes();
    uint64_t free  = sd_fatfs_get_free_bytes();
    ui_state_update_memory(total, free);
    ui_state_show(UI_BOOT_SD_MEMORY);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // ===== 4. Wi-Fi credentials lookup =====
    wifi_creds_t creds;
    wifi_creds_source_t source = WIFI_CREDS_NONE;

    if (wifi_cfg_read_from_sd(&creds) == ESP_OK) {
        source = WIFI_CREDS_FROM_FILE;
        ESP_LOGI(TAG, "Using wifi.cfg from SD");
    } else if (wifi_cfg_read_from_nvs(&creds) == ESP_OK) {
        source = WIFI_CREDS_FROM_NVS;
        ESP_LOGI(TAG, "Using credentials from NVS");
    } else {
        // Need to create default wifi.cfg
        if (wifi_cfg_create_default() == ESP_OK) {
            ui_state_show(UI_BOOT_CONFIG_CREATED);  // LED blink green
        } else {
            ui_state_show(UI_BOOT_CONFIG_INVALID);
        }
        startup_error_loop();
    }

    // ===== 5. Connect Wi-Fi =====
    ui_state_update_wifi(creds.ssid, "");
    ui_state_show(UI_BOOT_CONNECTING);

    ESP_ERROR_CHECK(wifi_mgr_init());
    ret = wifi_mgr_connect(&creds, 15000);

    bool wifi_ok = (ret == ESP_OK);

    if (wifi_ok) {
        wifi_status_t status;
        wifi_mgr_get_status(&status);
        ui_state_update_wifi(status.ssid, status.ip_str);

        // ===== 6. Persist credentials =====
        if (source == WIFI_CREDS_FROM_FILE) {
            if (wifi_cfg_save_to_nvs(&creds) == ESP_OK) {
                wifi_cfg_delete_from_sd();
            } else {
                ESP_LOGW(TAG, "Failed to save creds to NVS, keeping wifi.cfg");
                // LED solid yellow (special case from the spec)
                ui_led_set_special_yellow();
            }
        }

        // ===== 7. Start HTTP server =====
        ESP_ERROR_CHECK(http_server_start());

        // Final mode — HTTP (SD is already in SD_OWNER_FATFS)
        ui_state_show(UI_MODE_HTTP);

    } else {
        ESP_LOGW(TAG, "Wi-Fi connect failed, falling back to USB mode");
        ui_state_show(UI_BOOT_CONNECT_FAILED);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Switch the SD into MSC mode — the device stays useful as a flash drive
        ret = sd_owner_switch_to_msc();
        if (ret != ESP_OK) {
            ui_state_show(UI_ERROR_GENERIC);
            startup_error_loop();
        }
        ui_state_show(UI_MODE_USB);
    }

    // app_main returns — the tasks keep running
    ESP_LOGI(TAG, "Startup complete");
}
```

---

## 15. Testing and acceptance

### 15.1. USB MSC acceptance criteria

| Test | Method | Criterion |
|---|---|---|
| Linux enumeration | `dmesg -w` on connect | From "new full-speed" to "Attached SCSI removable disk" < 2 sec |
| No "extends beyond EOD" | `dmesg` | Message does not appear |
| No "Volume not properly unmounted" | After HTTP→USB switch | Message does not appear |
| Windows recognition | Plug into Win 10/11 | Appears in Explorer < 5 sec |
| 3D printer | Bambu/Prusa/Creality | Printer reads .gcode |
| Read speed | `dd if=/dev/sda of=/dev/null bs=1M count=100` | ≥ 3 MB/s |
| Write speed | `dd if=/dev/zero of=/dev/sda bs=1M count=100` | ≥ 1.5 MB/s |
| OS eject | Eject → `dmesg` | No errors, no "improperly unmounted" |
| Read timing | `tud_msc_read10_cb` logs | < 5 ms average |

### 15.2. Startup flow acceptance criteria

| Test | Criterion |
|---|---|
| Boot with valid wifi.cfg | Connecting → success → wifi.cfg deleted → mode HTTP |
| Boot without wifi.cfg, NVS empty | Default wifi.cfg created, blink green, halt |
| Boot with invalid wifi.cfg | "wifi.cfg invalid", blink red, halt |
| Boot, wifi.cfg valid but network unreachable | "Can't connect", 2 sec later → mode USB |
| Boot from MODE_USB, wifi.cfg edited, reboot | New credentials applied, mode HTTP |
| Boot without SD card | "SD Card required", blink red, halt |

### 15.3. Runtime switching acceptance criteria

| Test | Criterion |
|---|---|
| HTTP→USB: `POST /api/mode/usb` while idle | 200 OK, mode = usb, host sees the flash drive |
| HTTP→USB during an active upload | 409 Conflict, mode does not change |
| USB→HTTP: `POST /api/mode/http` while idle | 200 OK, mode = http, files accessible |
| USB→HTTP while the host is writing | 409 Conflict, mode does not change |
| USB↔HTTP loop 50 times | FAT is not corrupted, files intact |
| Write via HTTP → switch to USB → file visible to host | File identical |
| Write via USB → switch to HTTP → file visible via API | File identical |
| Concurrent `POST /api/mode/*` from two clients | One gets 200, the other 409 |

### 15.3a. HTTP file API acceptance criteria

| Test | Criterion |
|---|---|
| Upload a file to the root | 201, file on the card, size matches |
| Upload to a nested folder `a/b/c/file.bin` | 201, intermediate folders created |
| Upload a file larger than free space | 507 Insufficient Storage, partial file deleted |
| Connection interrupted mid-upload | Partial file deleted, `s_upload_in_progress` cleared |
| Any file endpoint in USB mode | 503 mode_mismatch |
| Path traversal `../../etc` | 400 invalid_path, no access outside the card |
| Download an existing file | 200, bytes identical to what was uploaded |
| List a nested directory | Correct JSON with type=dir/file and size |
| Queue-upload 100 files | All 100 on the card, flag not stuck |
| Web UI loads in both SD modes | Static assets served in both USB and HTTP |
| `api_version` mismatch | Client shows mismatch banner |

### 15.4. UI acceptance criteria

| State | LED | LCD |
|---|---|---|
| Boot welcome | Off | "Welcome / v0.0.2" |
| SD memory shown | Off | "Memory / total: X / free: Y" |
| Connecting | Blink white | "Connecting to <SSID>" |
| Connect success | Solid green (2s) → mode color | "<SSID> / <IP>" |
| Connect failed | Blink red (3s) | "Can't connect / Fallback USB" |
| MODE_HTTP | Solid green | "<SSID> / <IP> / Mode: HTTP" |
| MODE_USB | Solid blue | "<SSID> / <IP> / Mode: USB" or "USB mode (no wifi)" |
| Switching | Solid yellow | "Switching..." |
| Error (SD missing) | Blink red | "SD Card required" |

---

## 16. Checklist of potential problems

| Symptom | Cause | Solution |
|---|---|---|
| "extends beyond EOD" | Capacity ≠ physical capacity | `s_card->csd.capacity`, not `totalBytes()/512` |
| "Volume not properly unmounted" | FATFS not unmounted before switch | `sd_fatfs_deinit()` in `sd_owner_switch_to_msc()` |
| SCSI DID_TIME_OUT | 1-bit SDMMC or stuck USB task | Check `SDMMC_HOST_FLAG_4BIT`, split tasks across cores |
| 3D printer can't see device | bcdUSB 0x0210 or bad inquiry strings | bcdUSB = 0x0200, ASCII strings |
| Windows recognizes slowly | Capacity bug + retries | Fix capacity |
| Crash during concurrent switch | Race condition | Mutex on switch, busy-flag check |
| HTTP file API returns 404 in HTTP mode | Wrong sd_owner | Check `sd_owner_current() == SD_OWNER_FATFS` |
| Host gets I/O error after HTTP switch | Correct (media removed), but poor UX | API returns 409 when `usb_msc_is_busy()` |
| Wi-Fi slow when the host is actively writing | CPU contention | Wi-Fi on core 0, HTTP/UI on core 1 |
| Card not in 4-bit | Bad pull-ups on D1-D3 | Check the flag after init, inspect routing |
| "device descriptor read error -110" | USB stack crashes | Bring `usb_msc_init` up BEFORE `wifi_mgr_init` |
| `s_upload_in_progress` stuck true | Not cleared on error/exception | Clear via a single exit point before each return |
| Web UI does not load (404 everywhere) | webfs not mounted or image not flashed | Check `webfs_mount()` log, re-flash the LittleFS image |
| Old UI after reflash | Browser cache, or webfs image not updated | Versioned `api_version` check, hard refresh, re-flash the image |
| Broken file after an interrupted upload | `remove()` not called on error | Delete the partial file on every upload error path |
| Path traversal (access outside the card) | URL path not validated | `extract_and_validate_path`: forbid `..`, leading `/` |

---

## 17. Locked-in design decisions

Decisions made while elaborating the spec (previously open questions):

1. **Web UI** — a separate LittleFS partition (`webfs`), updated independently of the firmware. Static assets are stored in gzip and served with `Content-Encoding: gzip`. See section 10.5.
2. **HTTP auth** — not required. The device is used only in a trusted local network.
3. **File upload** — raw body (`application/octet-stream`), streamed to FATFS in chunks without buffering the whole file. Progress is implemented on the web client via `XMLHttpRequest.upload.onprogress`. See section 11.4.
4. **Multiple clients** — not supported and not needed. A single client is expected, uploading files strictly one at a time (queued on the web-client side). Guard — boolean flag `s_upload_in_progress`. `httpd` is configured with `max_open_sockets = 4`.
5. **Nested directories** — supported. Paths with `/`, recursive listing via `GET /api/files?path=`, automatic creation of intermediate directories on upload, path-traversal protection. See sections 11.2 and 11.6.
6. **API versioning** — `/api/status` reports `api_version`, the client compares against its hardcoded version and shows a mismatch banner. See section 11.1.

### 17.1. Moved to separate documents / future versions

- **OTA firmware updates** over HTTP — a separate feature for a future version, separate document. The partition table (section 10.5.2) leaves room for OTA partitions.
- **OTA web-UI-only updates** (rewriting the `webfs` partition over HTTP) — a logical evolution, also a future version.
