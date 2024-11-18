#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

void gpio_handler_init(void);
int gpio_handler_read_adc_channel0(void);
int gpio_handler_read_adc_channel3(void);
void gpio_handler_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // GPIO_HANDLER_H
