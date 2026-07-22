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
#define PEAK_SERIAL_LOG_LEVEL SERIAL_LOG_WARN
#endif

static inline void serial_write_str(const char *s) {
    (void)s;
}

static inline void serial_log(enum serial_log_level level, const char *msg) {
    (void)level;
    (void)msg;
}

static inline void serial_log_once(enum serial_log_level level, const char *key,
                                   const char *msg) {
    (void)level;
    (void)key;
    (void)msg;
}

static inline void serial_log_rl(enum serial_log_level level,
                                 uint32_t interval_ticks, const char *msg) {
    (void)level;
    (void)interval_ticks;
    (void)msg;
}

static inline void serial_log_secret(enum serial_log_level level,
                                     const char *label, const void *data,
                                     size_t len) {
    (void)level;
    (void)label;
    (void)data;
    (void)len;
}

#endif
