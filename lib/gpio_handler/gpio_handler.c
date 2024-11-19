#include "gpio_handler.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "GPIO_Handler";

#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL_0       ADC_CHANNEL_0    // GPIO36
#define ADC_CHANNEL_3       ADC_CHANNEL_3    // GPIO39

static adc_oneshot_unit_handle_t adc1_handle;

void gpio_handler_init(void) {
    // Configurazione dell'unità ADC
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc1_handle));

    // Configurazione dei canali ADC
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &chan_cfg));
}

int gpio_handler_read_adc_channel0(void) {
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &raw));
    return raw;
}

int gpio_handler_read_adc_channel3(void) {
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw));
    return raw;
}

void gpio_handler_deinit(void) {
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}
