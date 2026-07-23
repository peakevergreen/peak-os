#ifdef PEAK_HOST_TEST
#include "vfs_path_util.h"
#include <string.h>
#else
#include "vfs_path_util.h"
#include "util.h"
#endif

int peakfs_path_allowed_for_profile(const char *path, int profile) {
    if (!path || path[0] != '/')
        return 0;
    /* Reject ".." components. */
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == '/' || path[i + 2] == '\0'))
            return 0;
    }
    if (profile <= 0)
        return 0; /* private / ephemeral */
    if (profile == 1) {
        size_t pl = 5; /* "/home" */
        return strncmp(path, "/home", pl) == 0 &&
               (path[pl] == '\0' || path[pl] == '/');
    }
    static const char *const prefixes[] = {
        "/home", "/etc/peak", "/var/peak", NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = strlen(prefixes[i]);
        if (strncmp(path, prefixes[i], pl) == 0 &&
            (path[pl] == '\0' || path[pl] == '/'))
            return 1;
    }
    return 0;
}
