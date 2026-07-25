#ifndef PEAK_SHELL_H
#define PEAK_SHELL_H

#include "types.h"
#include "vfs.h"
#include "peak_errno.h"

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
/* Mutates line in place (quote-split). Pass a writable buffer. */
void shell_execute(char *line);

const char *shell_getcwd(void);
int  shell_chdir(const char *path);
int  shell_resolve_path(const char *in, char *out, size_t out_len);

/* Stdin path for `<` / pipe (NULL if none). "-" in utils reads this. */
void shell_set_stdin_path(const char *path);
const char *shell_stdin_path(void);

/* env */
int  shell_env_set(const char *name, const char *val);
const char *shell_env_get(const char *name);
void shell_env_list(void);

/* help */
void shell_help_topics(void);
void shell_help_cmd(const char *cmd);

/* Default env + cwd; called from shell_init(). */
void shell_builtins_init(void);

/* Command history (/var/peak/history). */
void shell_history_init(void);
void shell_history_add(const char *line);
const char *shell_history_last(void);
int  shell_history_prev(char *line, size_t line_cap);
int  shell_history_next(char *line, size_t line_cap);
void shell_history_reset_browse(void);
void shell_history_list(void);

/* Last pipeline exit status (0 = success). */
void shell_set_last_status(int rc);
int  shell_last_status(void);

/* Path resolve errors (peak_errno codes). */
int  shell_last_path_errno(void);
void shell_perror_path(const char *ctx, const char *path);

#endif
