#ifndef PEAK_VFS_PATH_UTIL_H
#define PEAK_VFS_PATH_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stddef.h>
#else
#include "types.h"
#endif

/* profile: 0=private/ephemeral, 1=workspace (/home only), 2=full */
int peakfs_path_allowed_for_profile(const char *path, int profile);

#endif
