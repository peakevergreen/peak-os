#ifndef PEAK_FPU_H
#define PEAK_FPU_H

#include "types.h"

#if defined(__aarch64__)
/* q0-q31 plus FPSR/FPCR, rounded up to a 16-byte boundary. */
#define FPU_STATE_SIZE 528
#else
#define FPU_STATE_SIZE 512
#endif

/* Enable the architecture floating-point unit. */
void fpu_init(void);

/* Per-task floating-point state helpers; buffers are 16-byte aligned. */
void fpu_save(void *state);
void fpu_restore(const void *state);
void fpu_clear(void *state);

#endif
