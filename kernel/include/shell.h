#ifndef PEAK_SHELL_H
#define PEAK_SHELL_H

#include "types.h"
#include "vfs.h"

enum os_mode {
    MODE_CLI = 0,
    MODE_GUI = 1,
};

void shell_init(void);
void shell_run_once(void);
void shell_feed_char(char c);
void shell_feed_key(int key); /* ASCII or KEY_* from keyboard.h */
void shell_redraw_prompt(void);
enum os_mode shell_mode(void);
void shell_set_mode(enum os_mode mode);
void shell_execute(const char *line);

const char *shell_getcwd(void);
int  shell_chdir(const char *path);
int  shell_resolve_path(const char *in, char *out, size_t out_len);

/* env */
int  shell_env_set(const char *name, const char *val);
const char *shell_env_get(const char *name);
void shell_env_list(void);

/* help */
void shell_help_topics(void);
void shell_help_cmd(const char *cmd);

/* Default env + cwd; called from shell_init(). */
void shell_builtins_init(void);

#endif
