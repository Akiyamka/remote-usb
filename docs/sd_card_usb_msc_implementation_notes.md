# Specification: SD Card Implementation for USB MSC + Wi-Fi on ESP32-S3 (T-Dongle S3)

## 1. Context and Goals

### 1.1. Document Purpose

This technical specification describes the correct implementation of the storage subsystem (SD card) for a "Wi-Fi USB Drive" device based on the LilyGO T-Dongle S3 (ESP32-S3, 16 MB Flash, no PSRAM). The device must operate in two **mutually exclusive** modes:

- **USB MSC mode**: the host (PC, 3D printer, or other embedded device) sees the device as a standard USB flash drive
- **Wi-Fi mode**: the device starts a Wi-Fi access point and a web server for uploading and downloading files

Mode switching is performed through a reboot (`esp_restart()`), which guarantees that there are no races or FAT corruption.

### 1.2. Target Platform

| Parameter | Value |
|---|---|
| Chip | ESP32-S3 (Xtensa LX7, dual core) |
| Board | LilyGO T-Dongle S3 (not the Dual version) |
| Flash | 16 MB QD |
| PSRAM | Not present |
| SDK | ESP-IDF v5.3.x (release branch) |
| TinyUSB | via managed component `espressif/esp_tinyusb` v1.4+ |
| FreeRTOS | tick rate 1000 Hz |

### 1.3. Problems in the Current Arduino Implementation (Recorded to Avoid Them)

1. `SD_MMC.begin()` forcibly mounts FATFS in parallel with MSC, which causes "Volume was not properly unmounted" on every connection
2. `setCapacity()` uses `totalBytes()/512` instead of the card's physical capacity, which causes "p1 size extends beyond EOD, truncated"
3. There is no way to measure SDMMC operation timing
4. There are no TinyUSB logs for diagnostics
5. Windows recognition delay is about 30 seconds, and the 3D printer does not recognize the device at all
6. SCSI Read(10) commands get `DID_TIME_OUT` after 30 seconds

## 2. T-Dongle S3 Pin Mapping

Hardwired for this device, **not configurable**:

```c
// SDMMC interface (4-bit mode)
#define BSP_SD_CMD      GPIO_NUM_16
#define BSP_SD_CLK      GPIO_NUM_12
#define BSP_SD_D0       GPIO_NUM_14
#define BSP_SD_D1       GPIO_NUM_17
#define BSP_SD_D2       GPIO_NUM_21
#define BSP_SD_D3       GPIO_NUM_18

// USB OTG (native, hardwired on S3)
// D+ = GPIO20, D- = GPIO19 - no configuration required; ESP-IDF picks them up automatically

// Mode selection button (BOOT is used)
#define BSP_BTN_MODE    GPIO_NUM_0

// LCD (ST7735) - on schematic FPC1, not part of this specification, but the pins are occupied:
// LCD_RST=GPIO1, LCD_RS=GPIO2, LCD_SDA=GPIO3, LCD_SCL=GPIO5, LCD_CS=GPIO4
// LCD_LEDA through MOSFET Q1, backlight controlled by a separate GPIO

// APA102 LED (built-in, U5 on the schematic)
// LED_DI=GPIO40, LED_CI=GPIO39
```

## 3. Solution Architecture

### 3.1. Component Structure

```
firmware/
├── main/
│   ├── main.c                  // Entry point, mode selection
│   ├── CMakeLists.txt
│   └── idf_component.yml       // dependencies
├── components/
│   ├── bsp/                    // Board support package
│   │   ├── bsp_pins.h
│   │   └── bsp_button.c
│   ├── screen/
│   │   ├── screen.h
│   │   └── screen.c
│   ├── led/   
│   │   ├── led.h
│   │   └── led.c
│   ├── storage/
│   │   ├── sd_raw.h            // Raw SDMMC access (for MSC)
│   │   ├── sd_raw.c
│   │   ├── sd_fatfs.h          // FATFS access (for Wi-Fi mode)
│   │   └── sd_fatfs.c
│   ├── usb_msc/
│   │   ├── usb_msc.h
│   │   ├── usb_msc.c
│   │   └── tusb_config.h
│   └── wifi_server/
│       ├── wifi_server.h
│       └── wifi_server.c
└── sdkconfig.defaults
```

### 3.2. Device Lifecycle

```
[Power on / reset]
       │
       ▼
[bootloader -> app_main]
       │
       ▼
[Read NVS flag "next_mode" + poll button for 500 ms]
       │
       ├── flag="msc"   OR button NOT pressed -> USB MSC mode
       │       │
       │       ▼
       │   [sd_raw_init() - SDMMC WITHOUT FATFS]
       │       │
       │       ▼
       │   [usb_msc_init() - start TinyUSB]
       │       │
       │       ▼
       │   [Loop: wait for "switch to Wi-Fi" event
       │           via 3 sec long button press]
       │       │
       │       ▼
       │   [nvs_set "next_mode"="wifi" -> esp_restart()]
       │
       └── flag="wifi"  OR button pressed at boot -> Wi-Fi mode
               │
               ▼
           [sd_fatfs_init() - SDMMC + FATFS mount RW]
               │
               ▼
           [wifi_server_init() - AP + HTTP server]
               │
               ▼
           [Loop: wait for "switch to MSC" event
                  via button press or inactivity timeout]
               │
               ▼
           [sd_fatfs_deinit() - mandatory unmount with flush!]
               │
               ▼
           [nvs_set "next_mode"="msc" -> esp_restart()]
```

**Critical rule:** `sd_raw` and `sd_fatfs` **must never exist at the same time**. This guarantees that there is no double ownership of the SDMMC controller.

## 4. Low-Level SD Driver (Raw Access for MSC)

### 4.1. Requirements

- Initialize the SDMMC host in **4-bit mode at 40 MHz**
- Use **high-speed DMA** through slot 1 (`SDMMC_HOST_SLOT_1`)
- **Do not** mount FATFS (no `esp_vfs_fat_sdmmc_mount`)
- Access sectors directly through `sdmmc_read_sectors` / `sdmmc_write_sectors`
- Return the correct physical card capacity from the CSD register
- Provide an atomic flush operation through `sdmmc_io_write_blocks` cancel (for an SD card it is enough to wait until there is no busy state)
- Protect concurrent access with a mutex, even though in MSC mode only the USB task accesses the card

### 4.2. Header File

```c
// sd_raw.h
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the SDMMC controller in 4-bit mode without FATFS.
 *        After this call, sd_raw_read_sectors / sd_raw_write_sectors are available.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code
 */
esp_err_t sd_raw_init(void);

/**
 * @brief Fully deinitializes SDMMC. After this call, sd_raw_init() is required again.
 */
esp_err_t sd_raw_deinit(void);

/**
 * @brief Returns the number of logical card sectors (512 bytes each).
 *        This value CAN and SHOULD be returned in the MSC capacity callback.
 */
uint32_t sd_raw_get_sector_count(void);

/**
 * @brief Returns the sector size (always 512 for SDHC, but read from the card).
 */
uint16_t sd_raw_get_sector_size(void);

/**
 * @brief Reads a block of sectors. Thin wrapper around sdmmc_read_sectors.
 *
 * @param buf       buffer of count * sector_size bytes, aligned to 4 bytes,
 *                  PREFERABLY in DMA-capable memory (internal SRAM)
 * @param lba       starting sector
 * @param count     number of sectors
 */
esp_err_t sd_raw_read_sectors(void *buf, uint32_t lba, uint32_t count);

/**
 * @brief Writes a block of sectors.
 */
esp_err_t sd_raw_write_sectors(const void *buf, uint32_t lba, uint32_t count);

/**
 * @brief Returns true if the card is initialized and ready.
 */
bool sd_raw_is_ready(void);

/**
 * @brief Waits for all pending write operations to finish.
 *        MUST be called before esp_restart() or disconnect.
 */
esp_err_t sd_raw_sync(void);

#ifdef __cplusplus
}
#endif
```

### 4.3. Implementation

```c
// sd_raw.c
#include "sd_raw.h"
#include "bsp_pins.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "sd_raw";

static sdmmc_card_t *s_card = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

esp_err_t sd_raw_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // Create mutex
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // SDMMC host configuration
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz
    host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DDR;

    // Slot configuration (pins)
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = BSP_SD_CLK;
    slot_config.cmd = BSP_SD_CMD;
    slot_config.d0  = BSP_SD_D0;
    slot_config.d1  = BSP_SD_D1;
    slot_config.d2  = BSP_SD_D2;
    slot_config.d3  = BSP_SD_D3;
    slot_config.width = 4;
    // CRITICAL: T-Dongle already has 10K pull-ups, so internal pull-ups are not required,
    // but enabling them for reliability is acceptable and will not hurt.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // Host initialization
    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot failed: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return ret;
    }

    // Allocate card structure
    s_card = (sdmmc_card_t *)heap_caps_calloc(1, sizeof(sdmmc_card_t),
                                              MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_card == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sdmmc_card_t");
        sdmmc_host_deinit();
        return ESP_ERR_NO_MEM;
    }

    // Card initialization (probe + identify + select bus width + freq)
    ret = sdmmc_card_init(&host, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed: %s", esp_err_to_name(ret));
        free(s_card);
        s_card = NULL;
        sdmmc_host_deinit();
        return ret;
    }

    // Log card characteristics
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "Card initialized: %llu MB, sector=%u, sectors=%u, bus_width=%d, freq=%d kHz",
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024),
             s_card->csd.sector_size,
             s_card->csd.capacity,
             s_card->host.flags & SDMMC_HOST_FLAG_4BIT ? 4 : 1,
             s_card->max_freq_khz);

    // Check that the bus is actually 4-bit
    if (!(s_card->host.flags & SDMMC_HOST_FLAG_4BIT)) {
        ESP_LOGW(TAG, "WARNING: card initialized in 1-bit mode, performance will be poor");
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t sd_raw_deinit(void)
{
    if (!s_initialized) return ESP_OK;

    // Wait for all pending operations
    sd_raw_sync();

    if (s_card != NULL) {
        free(s_card);
        s_card = NULL;
    }

    sdmmc_host_deinit();
    s_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

uint32_t sd_raw_get_sector_count(void)
{
    if (!s_initialized || s_card == NULL) return 0;
    // IMPORTANT: return the physical capacity from CSD
    return s_card->csd.capacity;
}

uint16_t sd_raw_get_sector_size(void)
{
    if (!s_initialized || s_card == NULL) return 0;
    return s_card->csd.sector_size;
}

esp_err_t sd_raw_read_sectors(void *buf, uint32_t lba, uint32_t count)
{
    if (!s_initialized || s_card == NULL) return ESP_ERR_INVALID_STATE;
    if (buf == NULL || count == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = sdmmc_read_sectors(s_card, buf, lba, count);
    xSemaphoreGive(s_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read_sectors lba=%u count=%u failed: %s",
                 lba, count, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sd_raw_write_sectors(const void *buf, uint32_t lba, uint32_t count)
{
    if (!s_initialized || s_card == NULL) return ESP_ERR_INVALID_STATE;
    if (buf == NULL || count == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = sdmmc_write_sectors(s_card, buf, lba, count);
    xSemaphoreGive(s_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write_sectors lba=%u count=%u failed: %s",
                 lba, count, esp_err_to_name(ret));
    }
    return ret;
}

bool sd_raw_is_ready(void)
{
    return s_initialized && s_card != NULL;
}

esp_err_t sd_raw_sync(void)
{
    if (!s_initialized) return ESP_OK;
    // For an SD card, "sync" means ensuring that the current command has finished
    // and the card has left programming state.
    // The simplest approach is to issue an empty CMD13 (SEND_STATUS) and wait for
    // the ready bit. The sdmmc driver does this automatically before every next
    // command, so a short delay is enough.
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}
```

### 4.4. Critical SD Raw Implementation Details

1. **Read/write buffers must be DMA-capable.** The supplied `buf` must be in internal SRAM. If a pointer comes from external PSRAM (which this board does not have) or from a non-cacheable region, it will cause crashes or corruption. **In the MSC callback, the buffer is allocated by TinyUSB and is DMA-capable by default**, but still keep this under control.
2. **Buffer alignment:** at least 4 bytes, preferably 16 bytes (S3 cache line).
3. **No `vTaskDelay()` in the read/write hot path.** Use it only in `sync()` and init.
4. **The mutex is ALWAYS taken**, even for read-only operations, because the sdmmc driver is not reentrant.
5. **Check 4-bit mode** through the `host.flags` flag. If SDMMC falls back to 1-bit mode (bad pull-ups, bad routing), this must be logged as critical.
6. **Do NOT call `esp_vfs_fat_sdmmc_mount`** under any circumstances in this module. That would mix the ESP FATFS cache with direct MSC access.

## 5. FATFS Wrapper (for Wi-Fi Mode)

Used only when the device is in Wi-Fi mode. This is a separate module to avoid the temptation to mix it with raw access.

```c
// sd_fatfs.h
#pragma once
#include "esp_err.h"

esp_err_t sd_fatfs_init(void);    // mount /sdcard
esp_err_t sd_fatfs_deinit(void);  // unmount with flush
```

```c
// sd_fatfs.c - short version
#include "sd_fatfs.h"
#include "bsp_pins.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

static const char *TAG = "sd_fatfs";
static const char MOUNT_POINT[] = "/sdcard";
static sdmmc_card_t *s_card = NULL;

esp_err_t sd_fatfs_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = BSP_SD_CLK;
    slot_config.cmd = BSP_SD_CMD;
    slot_config.d0 = BSP_SD_D0;
    slot_config.d1 = BSP_SD_D1;
    slot_config.d2 = BSP_SD_D2;
    slot_config.d3 = BSP_SD_D3;
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // NEVER format automatically!
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sd_fatfs_deinit(void)
{
    if (s_card == NULL) return ESP_OK;
    // CRITICAL: unmount flushes FATFS buffers by itself.
    // NEVER call esp_restart() without unmount!
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    return ret;
}
```

**Key rule:** when switching Wi-Fi -> MSC, always call `sd_fatfs_deinit()` **before** `esp_restart()`. Otherwise the FATFS cache will not be flushed to the card, and on the next USB connection the host will see "Volume was not properly unmounted".

## 6. USB MSC Through TinyUSB

### 6.1. TinyUSB Configuration

`tusb_config.h` - overrides for esp_tinyusb defaults:

```c
// tusb_config.h
#pragma once

#define CFG_TUSB_RHPORT0_MODE      OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE     64

// MSC class only
#define CFG_TUD_MSC                1
#define CFG_TUD_CDC                0
#define CFG_TUD_HID                0

// MSC buffer size: the larger it is, the fewer USB round-trips are needed.
// On the T-Dongle S3 without PSRAM, this is the balance between performance and RAM:
// 8192 is optimal and consumes 16 KB (TinyUSB double buffering).
#define CFG_TUD_MSC_EP_BUFSIZE     8192

// TinyUSB logs through UART
// 0 = no log, 1 = error, 2 = warning, 3 = info
// Use 2 for debugging and 1 in production
#define CFG_TUSB_DEBUG             2
```

### 6.2. USB Descriptors

```c
// usb_descriptors.c - critical part!
#include "tusb.h"

#define USB_VID  0xCAFE  // Replace with your own VID (CAFE is fine for testing)
#define USB_PID  0x4001  // PID for the MSC device

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // NOT 0x0210! Many embedded hosts do not like BOS.
    .bDeviceClass       = 0x00,    // Specified in the interface
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

// String descriptors - neutral, without mentioning "ESP32" / "Espressif"
char const *string_desc_arr[] = {
    (const char[]) {0x09, 0x04},  // 0: English (0x0409)
    "MyDevice",                    // 1: Manufacturer
    "Wireless Drive",              // 2: Product
    "000000000001",                // 3: Serial - generated from MAC
};

// Configuration descriptor: one MSC interface, two endpoints (IN/OUT)
enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define EPNUM_MSC_OUT     0x01
#define EPNUM_MSC_IN      0x81

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};
```

**Critical descriptor details:**

1. **`bcdUSB = 0x0200`**, not 0x0210. USB 2.1 requires a BOS descriptor, which many embedded USB hosts (including 3D printers) do not understand and fail on.
2. **`bcdDevice` must be unique:** when the descriptor changes, Windows will remember the device as new. This is required on the first launch.
3. **The serial number** must be unique for each device instance, otherwise Windows will confuse devices. It is better to generate it from the MAC address:
   ```c
   uint8_t mac[6];
   esp_efuse_mac_get_default(mac);
   snprintf(serial_buf, sizeof(serial_buf), "%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   ```
4. **String descriptors must be in English** (LangID 0x0409). Do not use Cyrillic or emoji; many hosts crash on them.
5. **Endpoint MaxPacketSize = 64** for Full Speed. High Speed would use 512, but the S3 is FS only.

### 6.3. MSC Callbacks

This is the core. TinyUSB calls these callbacks in the context of its own task (`usbd_task`), which has priority 5 by default in ESP-IDF.

```c
// usb_msc.c
#include "tusb.h"
#include "sd_raw.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "usb_msc";

// "Ejected" flag set when the host requests it through START_STOP_UNIT
static bool s_ejected = false;

// ===== Inquiry callback =====
// The host calls SCSI INQUIRY as the first command. We must identify ourselves.
// These strings are written to the Inquiry response (16 bytes vendor + 16 bytes product + 4 bytes rev).
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], 
                        uint8_t product_id[16], uint8_t product_rev[4])
{
    (void) lun;
    const char vid[] = "MyDevice";   // exactly 8 characters, pad with spaces if needed
    const char pid[] = "Wireless Drive  ";  // exactly 16 characters
    const char rev[] = "1.00";        // exactly 4 characters

    memcpy(vendor_id,  vid, 8);
    memcpy(product_id, pid, 16);
    memcpy(product_rev, rev, 4);
}

// ===== Test Unit Ready callback =====
// The host polls this often to check device readiness.
// Return false ONLY if the card is physically missing or we are "ejected".
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void) lun;

    if (s_ejected) {
        // Tell the host that media is absent
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }

    return sd_raw_is_ready();
}

// ===== Capacity callback - CRITICALLY IMPORTANT =====
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void) lun;

    *block_count = sd_raw_get_sector_count();   // physical card capacity!
    *block_size  = sd_raw_get_sector_size();    // usually 512

    // IMPORTANT: if *block_count = 0, the host sees "media absent".
    // This is normal behavior for an empty slot.
}

// ===== Start/Stop callback =====
// The host sends SCSI START_STOP_UNIT when the user performs "Eject" / "Safe remove".
// load_eject=1 + start=0 means the user requested eject.
// IMPORTANT: wait for the card flush!
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, 
                           bool start, bool load_eject)
{
    (void) lun;
    (void) power_condition;

    if (load_eject) {
        if (!start) {
            // User clicked "eject" in the OS
            ESP_LOGI(TAG, "Host requested eject");
            sd_raw_sync();           // wait for writes to finish
            s_ejected = true;
        } else {
            // load - reverse signal (rarely used)
            s_ejected = false;
        }
    }

    return true;
}

// ===== READ callback - hot path =====
// TinyUSB requests N bytes starting from lba+offset.
// offset != 0 can happen when EP_BUFSIZE is smaller than the requested byte count and this is a continuation.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, 
                          void *buffer, uint32_t bufsize)
{
    (void) lun;

    // In current TinyUSB versions, bufsize is always a multiple of 512 and offset == 0 at sector start.
    if (offset != 0) {
        // Unexpected case - unaligned request. This should not happen with bufsize=8192.
        ESP_LOGW(TAG, "Unaligned read: lba=%u offset=%u size=%u", lba, offset, bufsize);
        return -1;
    }

    uint32_t sector_count = bufsize / 512;

    int64_t t_start = esp_timer_get_time();
    esp_err_t err = sd_raw_read_sectors(buffer, lba, sector_count);
    int64_t dt = esp_timer_get_time() - t_start;

    // Log slow reads for diagnostics
    if (dt > 50000) {
        ESP_LOGW(TAG, "Slow read: lba=%u cnt=%u took %lld us", 
                 lba, sector_count, dt);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read failed lba=%u: %s", lba, esp_err_to_name(err));
        return -1;
    }

    return bufsize;
}

// ===== WRITE callback =====
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, 
                           uint8_t *buffer, uint32_t bufsize)
{
    (void) lun;

    if (offset != 0) {
        ESP_LOGW(TAG, "Unaligned write: lba=%u offset=%u size=%u", lba, offset, bufsize);
        return -1;
    }

    uint32_t sector_count = bufsize / 512;

    int64_t t_start = esp_timer_get_time();
    esp_err_t err = sd_raw_write_sectors(buffer, lba, sector_count);
    int64_t dt = esp_timer_get_time() - t_start;

    if (dt > 100000) {  // writes are slower than reads, so the threshold is higher
        ESP_LOGW(TAG, "Slow write: lba=%u cnt=%u took %lld us", 
                 lba, sector_count, dt);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write failed lba=%u: %s", lba, esp_err_to_name(err));
        return -1;
    }

    return bufsize;
}

// ===== SCSI command callback =====
// For other SCSI commands (Mode Sense, Request Sense, etc.).
// TinyUSB handles most of them itself; we only need to say "unsupported"
// for unknown commands.
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], 
                        void *buffer, uint16_t bufsize)
{
    (void) lun;
    (void) buffer;
    (void) bufsize;

    // Tell the host: command is not supported
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

// ===== Write protect callback =====
// Return false (not write-protected) while in normal mode.
bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void) lun;
    return true;
}
```

### 6.4. USB Initialization

```c
// usb_msc_init.c
#include "tinyusb.h"
#include "esp_log.h"
#include "sd_raw.h"

static const char *TAG = "usb_init";

esp_err_t usb_msc_init(void)
{
    // SD raw must already be initialized at this point
    if (!sd_raw_is_ready()) {
        ESP_LOGE(TAG, "SD not ready, init it first");
        return ESP_ERR_INVALID_STATE;
    }

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

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "USB MSC initialized, sectors=%u, total=%llu MB",
             sd_raw_get_sector_count(),
             (uint64_t)sd_raw_get_sector_count() * sd_raw_get_sector_size() / (1024 * 1024));

    return ESP_OK;
}
```

## 7. FreeRTOS Task and Priority Management

T-Dongle S3 is dual core. Distribution:

| Task | Priority | Core | Stack |
|---|---|---|---|
| `tusb_device_task` (TinyUSB USB stack) | 5 | 0 | 4096 |
| `tusbd_msc_task` (TinyUSB MSC class) | 4 | 0 | 4096 |
| `wifi_task` (only in Wi-Fi mode) | 23 | 0 | - (internal) |
| `httpd_task` (only in Wi-Fi mode) | 5 | 1 | 4096 |
| `app_main` / button monitor | 1 | 1 | 4096 |
| `idle_task_0`, `idle_task_1` | 0 | 0/1 | - |

**Critical rule**: the USB task is on core 0, and Wi-Fi/HTTP are also initially pinned by Espressif to core 0, but in this design they **do not coexist**, so there is no conflict. User tasks (button monitoring, LED) run on core 1 so they do not interfere with USB.

Required in `sdkconfig.defaults`:

```
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=n
CONFIG_ESP_TASK_WDT_INIT=n         # disable WDT on app task in debug mode
CONFIG_TINYUSB_TASK_PRIORITY=5
CONFIG_TINYUSB_TASK_AFFINITY_CPU0=y
CONFIG_TINYUSB_MSC_BUFSIZE=8192
CONFIG_TINYUSB_DEBUG_LEVEL=2
CONFIG_FATFS_LFN_HEAP=y            # for FATFS mode - long filenames
CONFIG_FATFS_MAX_LFN=255
```

## 8. Main Module main.c

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp_pins.h"
#include "sd_raw.h"
#include "sd_fatfs.h"
#include "usb_msc.h"
#include "wifi_server.h"

static const char *TAG = "main";

typedef enum { MODE_MSC, MODE_WIFI } device_mode_t;

static device_mode_t read_next_mode(void)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) return MODE_MSC;
    uint8_t val = MODE_MSC;
    nvs_get_u8(h, "mode", &val);
    nvs_close(h);
    return (device_mode_t)val;
}

static void write_next_mode(device_mode_t m)
{
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "mode", (uint8_t)m);
    nvs_commit(h);
    nvs_close(h);
}

// Poll the button for 500 ms at startup. If it is held, force Wi-Fi mode.
static bool button_held_at_boot(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_BTN_MODE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    for (int i = 0; i < 10; i++) {
        if (gpio_get_level(BSP_BTN_MODE) != 0) return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;  // held the whole time
}

// Button monitor task in the active mode
static void button_monitor_task(void *arg)
{
    device_mode_t current = (device_mode_t)(uintptr_t)arg;
    while (1) {
        if (gpio_get_level(BSP_BTN_MODE) == 0) {
            // Held - wait 2 sec for confirmation
            int held_ms = 0;
            while (gpio_get_level(BSP_BTN_MODE) == 0 && held_ms < 2000) {
                vTaskDelay(pdMS_TO_TICKS(50));
                held_ms += 50;
            }
            if (held_ms >= 2000) {
                ESP_LOGI(TAG, "Mode switch requested");
                // Prepare for reboot
                if (current == MODE_WIFI) {
                    sd_fatfs_deinit();       // CRITICAL: flush!
                    write_next_mode(MODE_MSC);
                } else {
                    sd_raw_sync();
                    sd_raw_deinit();
                    write_next_mode(MODE_WIFI);
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    // 1. NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Determine mode
    device_mode_t mode = read_next_mode();
    if (button_held_at_boot()) {
        ESP_LOGI(TAG, "Button held at boot - forcing Wi-Fi mode");
        mode = MODE_WIFI;
    }

    // 3. Start selected mode
    if (mode == MODE_MSC) {
        ESP_LOGI(TAG, "Starting USB MSC mode");
        ESP_ERROR_CHECK(sd_raw_init());
        ESP_ERROR_CHECK(usb_msc_init());
    } else {
        ESP_LOGI(TAG, "Starting Wi-Fi mode");
        ESP_ERROR_CHECK(sd_fatfs_init());
        ESP_ERROR_CHECK(wifi_server_start());
    }

    // 4. Mode switch button monitoring
    xTaskCreatePinnedToCore(button_monitor_task, "btn_mon", 4096,
                            (void*)(uintptr_t)mode, 1, NULL, 1);

    // app_main exits; tasks keep running
}
```

## 9. Testing and Acceptance

### 9.1. USB MSC Acceptance Criteria

| Test | Method | Criterion |
|---|---|---|
| Linux connection | `dmesg -w` during connection | From "new full-speed USB device" to "Attached SCSI removable disk" in **< 2 sec** |
| No "extends beyond EOD" | `dmesg` after mount | The message **does not appear** |
| No "Volume was not properly unmounted" | After device reboot | The message **does not appear** on the first connection after reboot |
| Windows 10/11 connection | Connect and wait for it to appear in File Explorer | < 5 sec |
| 3D printer connection | Connect to the printer (Bambu/Prusa/Creality) | The printer sees the flash drive and reads .gcode files |
| Read speed | `dd if=/dev/sda of=/dev/null bs=1M count=100` | >= 3 MB/s |
| Write speed | `dd if=/dev/zero of=/dev/sda bs=1M count=100` | >= 1.5 MB/s |
| Safe removal | OS eject -> `dmesg` | No errors, and no "improperly unmounted" after reconnection |
| Single-sector read time | `tud_msc_read10_cb` logs | < 5 ms on average |

### 9.2. Mode Switching Acceptance Criteria

| Test | Criterion |
|---|---|
| MSC -> Wi-Fi -> MSC, 50 cycles | FAT is not corrupted, files remain in place |
| Write file through host -> switch to Wi-Fi -> file visible over HTTP | File is identical |
| Write through HTTP -> switch to MSC -> file visible to host | File is identical |
| Sudden power loss during write | After reboot, FAT is consistent (may require fsck, but there must be no catastrophic corruption) |

### 9.3. Diagnostic Logs

In debug mode, the monitor must show:

- `sdmmc_card_print_info` dump at startup (shows 4-bit mode, frequency, size)
- TinyUSB logs at level 2 (warnings)
- Read/write callback timing measurements with 50/100 ms thresholds

## 10. Potential Issues Checklist

| Symptom | Likely Cause | Solution |
|---|---|---|
| "extends beyond EOD" | `block_count` is not equal to physical capacity | Use `s_card->csd.capacity` |
| "Volume was not properly unmounted" | FATFS was not unmounted before restart | Call `sd_fatfs_deinit()` before `esp_restart()` |
| DID_TIME_OUT in SCSI | 1-bit SDMMC or USB task stalls | Check the 4BIT flag, move Wi-Fi to another core, or disable it |
| 3D printer does not see the device | bcdUSB 0x0210 or unsuitable inquiry strings | Use bcdUSB = 0x0200 and simple ASCII strings |
| Windows takes a long time to recognize the device | "extends beyond EOD" plus retries | Fix capacity and do not use FATFS in parallel |
| Crash in `tud_msc_read10_cb` | Buffer is not DMA-capable | Should not happen with TinyUSB default allocation; check tusb_config |
| Card does not initialize in 4-bit mode | Bad D1-D3 pull-ups or an error in slot_config | Check `SDMMC_HOST_FLAG_4BIT` after init |
| "device descriptor read/64, error -110" | USB stack crashes on init | Check that Wi-Fi is FULLY disabled before `tinyusb_driver_install` |
