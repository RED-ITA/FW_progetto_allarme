#include "gpio_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_Handler";

#define GPIO_DIGITAL_CHANNEL0   GPIO_NUM_36  // corrispondente a ADC_CHANNEL_0
#define GPIO_DIGITAL_CHANNEL3   GPIO_NUM_39  // corrispondente a ADC_CHANNEL_3

void gpio_handler_init(void) {
    // Configurazione dei pin come ingressi digitali
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_DIGITAL_CHANNEL0) | (1ULL << GPIO_DIGITAL_CHANNEL3),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

int gpio_handler_read_adc_channel0(void) {
    // Legge il livello digitale del pin e restituisce 100 se HIGH, 0 se LOW
    int level = gpio_get_level(GPIO_DIGITAL_CHANNEL0);
    return level ? 100 : 0;
}

int gpio_handler_read_adc_channel3(void) {
    // Legge il livello digitale del pin e restituisce 100 se HIGH, 0 se LOW
    int level = gpio_get_level(GPIO_DIGITAL_CHANNEL3);
    return level ? 100 : 0;
}

void gpio_handler_deinit(void) {
    // Reset dei pin per riportarli allo stato di default
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_DIGITAL_CHANNEL0));
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_DIGITAL_CHANNEL3));
}
