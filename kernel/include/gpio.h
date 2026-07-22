#ifndef PEAK_GPIO_H
#define PEAK_GPIO_H

#include "types.h"

/* Portable GPIO surface. Raspberry Pi implements these; other platforms stub. */
void platform_gpio_set_output(unsigned pin);
void platform_gpio_write(unsigned pin, int val);
int  platform_gpio_read(unsigned pin);

#endif
