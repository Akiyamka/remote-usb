// SPDX-License-Identifier: MIT
//
// APA102 driver — implementation.
//
// Frame layout (per the APA102 datasheet, see docs/APA102_2020_256_6A.pdf):
//   - Start frame: 4 bytes of 0x00
//   - LED frame (one per LED): [0xE0 | brightness5] [B] [G] [R]
//   - End frame: enough '1' bits to clock past the last LED;
//     4 bytes of 0xFF is safely sufficient for a single-LED string.
//
// The T-Dongle's LED uses BGR order (docs/hardware.md), and APA102 is also
// natively BGR, so the public `r,g,b` arguments map straight onto the byte
// positions documented above.

#include "bsp_led.h"
#include "bsp_pins.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_led";

// APA102 is forgiving on clock; 4 MHz is well within spec and leaves enough
// headroom that we don't have to worry about board parasitics.
#define BSP_LED_SPI_HZ          (4 * 1000 * 1000)
// SPI3_HOST is reserved for the LED so the LCD can keep SPI2_HOST (matching
// the working wi-fi-drive reference where the 40 MHz ST7735 link lives on
// SPI2). APA102 is happy at 4 MHz on either peripheral.
#define BSP_LED_SPI_HOST        SPI3_HOST

// Default brightness used by bsp_led_set_rgb(). 8/31 ≈ 25 %.
#define BSP_LED_DEFAULT_BRIGHT  8

// Frame size: 4 start + 4 LED + 4 end = 12 bytes for one LED.
#define BSP_LED_FRAME_BYTES     12

static spi_device_handle_t s_spi;
static bool s_inited;

esp_err_t bsp_led_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_LED_DI,
        .miso_io_num = -1,
        .sclk_io_num = BSP_LED_CI,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LED_FRAME_BYTES,
    };

    esp_err_t err = spi_bus_initialize(BSP_LED_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = BSP_LED_SPI_HZ,
        .mode = 0,             // APA102 latches on the rising edge of CLK
        .spics_io_num = -1,    // no CS line on APA102
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    err = spi_bus_add_device(BSP_LED_SPI_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        spi_bus_free(BSP_LED_SPI_HOST);
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "APA102 LED initialised (DI=%d CI=%d @%d Hz)",
             BSP_LED_DI, BSP_LED_CI, BSP_LED_SPI_HZ);

    // Make sure we don't inherit a stale colour from a warm reboot.
    return bsp_led_off();
}

esp_err_t bsp_led_set_rgb_brightness(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t brightness)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (brightness > 31) {
        brightness = 31;
    }

    uint8_t frame[BSP_LED_FRAME_BYTES] = {
        // Start frame
        0x00, 0x00, 0x00, 0x00,
        // LED frame: global byte then B, G, R (native APA102 order = BGR)
        (uint8_t)(0xE0 | brightness), b, g, r,
        // End frame
        0xFF, 0xFF, 0xFF, 0xFF,
    };

    spi_transaction_t t = {
        .length = BSP_LED_FRAME_BYTES * 8,
        .tx_buffer = frame,
    };
    return spi_device_transmit(s_spi, &t);
}

esp_err_t bsp_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return bsp_led_set_rgb_brightness(r, g, b, BSP_LED_DEFAULT_BRIGHT);
}

esp_err_t bsp_led_off(void)
{
    return bsp_led_set_rgb_brightness(0, 0, 0, 0);
}
