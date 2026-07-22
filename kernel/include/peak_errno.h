#ifndef PEAK_ERRNO_H
#define PEAK_ERRNO_H

/*
 * Peak kernel errno-like codes (always negative).
 * Used on net + VFS hot paths; callers should test != 0 / == 0.
 */
#define PEAK_OK           0
#define PEAK_EINVAL      -1   /* invalid argument / bad path */
#define PEAK_ENOENT      -2   /* not found */
#define PEAK_ENOMEM      -3   /* out of memory */
#define PEAK_EEXIST      -4   /* already exists */
#define PEAK_ENOTDIR     -5   /* not a directory */
#define PEAK_EISDIR      -6   /* is a directory */
#define PEAK_ENOSPC      -7   /* buffer or table full */
#define PEAK_EIO         -8   /* I/O or format error */
#define PEAK_EACCES      -9   /* permission denied */
#define PEAK_ETIMEOUT    -10  /* timed out */
#define PEAK_ENETDOWN    -11  /* network down / not ready */
#define PEAK_ENOTCONN    -12  /* not connected / wrong TCP state */
#define PEAK_ENOBUFS     -13  /* packet or segment too large */
#define PEAK_ENETUNREACH -14  /* next-hop MAC / ARP unresolved */
#define PEAK_EBUSY       -15  /* slots or listeners exhausted */
#define PEAK_EDHCP       -16  /* DHCP-only mode failure */
#define PEAK_EAGAIN      -17  /* try again (non-blocking empty) */

#endif
