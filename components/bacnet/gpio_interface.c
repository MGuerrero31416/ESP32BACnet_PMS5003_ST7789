[file name]: gpio_interface.c
[file content begin]
#include "gpio_interface.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO";

#define BUTTON_GPIO GPIO_NUM_35

void gpio_interface_init(void)
{
    // Configure GPIO 35 as input with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "GPIO interface initialized, button on GPIO %d", BUTTON_GPIO);
}

bool gpio_35_read(void)
{
    // Read GPIO 35 state
    // TTGO button is active-low: pressed = LOW (0), released = HIGH (1)
    int state = gpio_get_level(BUTTON_GPIO);
    
    // Return true if button is pressed (active-low, so pressed = 0)
    return (state == 0);
}
[file content end]