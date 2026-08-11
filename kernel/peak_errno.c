#include "peak_errno.h"

const char *peak_errno_str(int err) {
    switch (err) {
    case PEAK_OK: return "ok";
    case PEAK_EINVAL: return "invalid argument";
    case PEAK_ENOENT: return "not found";
    case PEAK_ENOMEM: return "out of memory";
    case PEAK_EEXIST: return "already exists";
    case PEAK_ENOTDIR: return "not a directory";
    case PEAK_EISDIR: return "is a directory";
    case PEAK_ENOSPC: return "no space";
    case PEAK_EIO: return "I/O error";
    case PEAK_EACCES: return "permission denied";
    case PEAK_ETIMEOUT: return "timed out";
    case PEAK_ENETDOWN: return "network down";
    case PEAK_ENOTCONN: return "not connected";
    case PEAK_ENOBUFS: return "buffer too large";
    case PEAK_ENETUNREACH: return "network unreachable";
    case PEAK_EBUSY: return "busy";
    case PEAK_EDHCP: return "DHCP failed";
    case PEAK_EAGAIN: return "try again";
    case PEAK_ELOOP: return "too many symlink levels";
    case PEAK_EFAULT: return "bad address";
    default: return "unknown error";
    }
}
