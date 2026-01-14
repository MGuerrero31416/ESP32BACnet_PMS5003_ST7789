#ifndef GPIO_INTERFACE_H
#define GPIO_INTERFACE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Function to read GPIO 35 state
bool gpio_35_read(void);

// Function to initialize GPIOs
void gpio_interface_init(void);

#ifdef __cplusplus
}
#endif

#endif // GPIO_INTERFACE_H
