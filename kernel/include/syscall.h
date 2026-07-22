#ifndef PEAK_SYSCALL_H
#define PEAK_SYSCALL_H

#include "types.h"
#include "idt.h"

#define SYS_exit   0
#define SYS_write  1
#define SYS_read   2
#define SYS_open   3
#define SYS_close  4
#define SYS_brk    5
#define SYS_exec   6
#define SYS_getpid 7
#define SYS_agent  8
#define SYS_listdir 9
#define SYS_peakvec 10
#define SYS_peakgui 11

void syscall_init(void);
void syscall_handler(struct interrupt_frame *frame);

#endif
