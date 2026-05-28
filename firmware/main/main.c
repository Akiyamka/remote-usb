#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "boot");

    // Phase 0 heartbeat: lets us verify the chip is actually running our
    // firmware after a fresh flash, even when we open `idf.py monitor` late
    // and miss the one-shot boot line above. Will be removed in Phase 1
    // once the LCD/LED give a visual liveness indicator.
    uint32_t tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "alive tick=%" PRIu32, ++tick);
    }
}
