#ifndef PEAK_SERIAL_H
#define PEAK_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_write(char c);
void serial_write_str(const char *s);

#endif
