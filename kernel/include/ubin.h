#ifndef PEAK_UBIN_H
#define PEAK_UBIN_H

#include "types.h"

struct ubin_entry {
    const char *name;
    int (*main)(int, char **);
};

/* Seed /bin/<name> placeholder files from the built-in registry. */
void ubin_seed_vfs(void);

/* Run a built-in by full path (/bin/name). Returns -999 if unknown. */
int ubin_run(const char *path, int argc, char **argv);

#endif
