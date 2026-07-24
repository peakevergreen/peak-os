#if defined(PEAK_HOST_TEST)
#include_next <assert.h>
#else
#ifndef PEAK_HACL_ASSERT_H
#define PEAK_HACL_ASSERT_H
#define assert(x) ((void)0)
#endif
#endif
