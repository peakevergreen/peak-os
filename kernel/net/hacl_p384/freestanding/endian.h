#ifndef PEAK_HACL_ENDIAN_H
#define PEAK_HACL_ENDIAN_H

/*
 * Portable endian helpers for HACL* host and freestanding builds.
 * Placed ahead of system <endian.h> via -I.../freestanding so Linux CI
 * does not depend on glibc feature-test macros for le64toh/htobe64.
 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define htobe16(x) (x)
#define be16toh(x) (x)
#define htole16(x) __builtin_bswap16(x)
#define le16toh(x) __builtin_bswap16(x)
#define htobe32(x) (x)
#define be32toh(x) (x)
#define htole32(x) __builtin_bswap32(x)
#define le32toh(x) __builtin_bswap32(x)
#define htobe64(x) (x)
#define be64toh(x) (x)
#define htole64(x) __builtin_bswap64(x)
#define le64toh(x) __builtin_bswap64(x)
#else
/* Little-endian (x86_64, aarch64 LE, freestanding Peak targets). */
#define htole16(x) (x)
#define le16toh(x) (x)
#define htobe16(x) __builtin_bswap16(x)
#define be16toh(x) __builtin_bswap16(x)
#define htole32(x) (x)
#define le32toh(x) (x)
#define htobe32(x) __builtin_bswap32(x)
#define be32toh(x) __builtin_bswap32(x)
#define htole64(x) (x)
#define le64toh(x) (x)
#define htobe64(x) __builtin_bswap64(x)
#define be64toh(x) __builtin_bswap64(x)
#endif

#endif
