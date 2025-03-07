#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#ifdef __cplusplus
extern "C" {
#else
  // Se non siamo in C++ e non abbiamo C99 (o superiore), definiamo il tipo bool
  #if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
    typedef enum { false, true } bool;
  #else
    #include <stdbool.h>
  #endif
#endif

void gpio_handler_init(void);
int gpio_handler_read_adc_channel0(void);
int gpio_handler_read_adc_channel3(void);
void gpio_handler_deinit(void);

// Funzioni per la gestione del pulsante di reset
void gpio_handler_configure_reset_button(void);
bool gpio_handler_is_reset_button_pressed(void);

#ifdef __cplusplus
}
#endif

#endif // GPIO_HANDLER_H