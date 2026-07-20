#ifndef PEAK_SHELL_H
#define PEAK_SHELL_H

#include "types.h"

enum os_mode {
    MODE_CLI = 0,
    MODE_GUI = 1,
};

void shell_init(void);
void shell_run_once(void);       /* process one line if available */
void shell_feed_char(char c);    /* feed char into line buffer (GUI terminal) */
void shell_redraw_prompt(void);
enum os_mode shell_mode(void);
void shell_set_mode(enum os_mode mode);
void shell_execute(const char *line);

#endif
