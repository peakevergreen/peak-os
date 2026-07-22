#ifndef PEAK_PRIVACY_H
#define PEAK_PRIVACY_H

#include "types.h"

/* Session-scoped network / persistence policy (RAM grants by default). */

void privacy_init(void);
void privacy_clear_session(void);

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
