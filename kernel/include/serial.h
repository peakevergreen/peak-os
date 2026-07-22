#ifndef PEAK_SERIAL_H
#define PEAK_SERIAL_H

#include "types.h"

#ifndef PEAK_DEV_INSECURE_RNG
#define PEAK_DEV_INSECURE_RNG 0
#endif

enum serial_log_level {
    SERIAL_LOG_ERROR = 0,
    SERIAL_LOG_WARN  = 1,
    SERIAL_LOG_INFO  = 2,
    SERIAL_LOG_DEBUG = 3,
};

#ifndef PEAK_SERIAL_LOG_LEVEL
#if PEAK_DEV_INSECURE_RNG
#define PEAK_SERIAL_LOG_LEVEL SERIAL_LOG_DEBUG
#else
#define PEAK_SERIAL_LOG_LEVEL SERIAL_LOG_WARN
#endif
#endif

void serial_init(void);
void serial_write(char c);
void serial_write_str(const char *s);

void serial_set_log_level(enum serial_log_level level);
enum serial_log_level serial_get_log_level(void);
void serial_log(enum serial_log_level level, const char *msg);
void serial_log_once(enum serial_log_level level, const char *key, const char *msg);
void serial_log_rl(enum serial_log_level level, uint32_t interval_ticks, const char *msg);

/* Never prints seed/canary/passphrase bytes in release builds. */
void serial_log_secret(enum serial_log_level level, const char *label,
                       const void *data, size_t len);

#endif
