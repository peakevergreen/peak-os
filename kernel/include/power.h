#ifndef PEAK_POWER_H
#define PEAK_POWER_H

#include "types.h"

void power_init(void);
/* QEMU/Bochs ACPI power-off; falls back to halt. */
void power_shutdown(void);
void power_reboot(void);

#endif
