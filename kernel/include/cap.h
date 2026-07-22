#ifndef PEAK_CAP_H
#define PEAK_CAP_H

#include "types.h"

/* Operation capabilities (subject-level). */
#define CAP_FS_READ       (1u << 0)
#define CAP_FS_WRITE      (1u << 1)
#define CAP_NET_CLIENT    (1u << 2)
#define CAP_NET_LISTEN    (1u << 3)
#define CAP_CLIPBOARD     (1u << 4)
#define CAP_AGENT         (1u << 5)
#define CAP_DISK_PERSIST  (1u << 6)
#define CAP_SETTINGS      (1u << 7)
#define CAP_DIAG          (1u << 8)
#define CAP_NET_LAN       (1u << 9) /* listen beyond localhost */
#define CAP_VEC           (1u << 10) /* PeakVec upsert/query */

#define CAP_DEFAULT_SHELL (CAP_FS_READ | CAP_FS_WRITE | CAP_CLIPBOARD | \
                           CAP_AGENT | CAP_DISK_PERSIST | CAP_SETTINGS | \
                           CAP_DIAG | CAP_VEC)
#define CAP_DEFAULT_BROWSER (CAP_FS_READ | CAP_CLIPBOARD)
#define CAP_DEFAULT_AGENT (CAP_FS_READ | CAP_FS_WRITE | CAP_AGENT | CAP_VEC)

void cap_init(void);
uint32_t cap_current(void);
void cap_set_current(uint32_t caps);
int cap_check(uint32_t need);

/* Network / privacy policy (RAM grants by default). */
void privacy_init(void);
int privacy_persist_profile(void); /* 0=private 1=workspace 2=full */
void privacy_set_persist_profile(int profile);
int privacy_net_kill_switch(void);
void privacy_set_net_kill_switch(int on);
int privacy_net_client_allowed(void);
void privacy_grant_net_client(int remember);
int privacy_net_listen_allowed(int lan);
void privacy_grant_net_listen(int lan, int remember);
int privacy_listeners_localhost_only(void);
void privacy_set_listeners_localhost_only(int on);

#endif
