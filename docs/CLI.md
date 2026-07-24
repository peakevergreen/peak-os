# Peak CLI utilities

Peak ships a deep `/bin` utility pack as kernel builtins (not separate ELF binaries yet).

## Quoting

The shell splits on spaces and supports `"double"` and `'single'` quotes (quotes are stripped). Example:

```
ask "create fib.c"
js -e '1+2*3'
```

Unclosed quotes treat the remainder of the line as one argument. Max 16 argv slots.

## Pipes and redirection

Operators `|`, `>`, `>>`, and `<` work outside quotes (spaces optional around them):

```
echo hello > out.txt
echo more >> out.txt
cat < out.txt
echo hello world | grep hello
seq 1 5 | wc
```

Limits: up to 4 pipeline stages; captured pipe/redirect buffers are capped at 8 KiB.
`cat` / `head` / `tail` / `wc` / `grep` accept `-` (or omit the path) to read shell stdin from `<` or a pipe.

## Globs

Basename globs `*` and `?` expand against the directory of each argument (cwd if no `/`):

```
ls *.c
rm /tmp/peak-*.log
```

No match leaves the pattern unchanged. Max 16 argv slots after expansion.

## Navigation
| Command | Notes |
|---------|-------|
| `pwd` | print working directory |
| `cd [path]` | change directory (default workspace) |
| `ls [-l] [path]` | list directory |
| `tree [path]` | directory tree |
| `find <dir> -name <name>` | basename search |

## Files
| Command | Notes |
|---------|-------|
| `mkdir [-p] <path>` | create directory (parents always) |
| `touch <path>` | create empty file |
| `rm [-rf] <path>` | remove file or tree |
| `cp [-r] <src> <dst>` | copy |
| `mv <src> <dst>` | rename/move |
| `ln <target> <link>` | hard link |
| `stat <path>` | metadata |
| `du [path]` | tree byte size |
| `df` | VFS node + memory pages |
| `truncate <path> <n>` | resize (max 4096) |

## Text
| Command | Notes |
|---------|-------|
| `cat` `head` `tail` `wc` | file viewers (`head`/`tail` `-n N`) |
| `grep <pat> <file>` | substring match |
| `diff` `sort` `uniq` `cut` `tr` `sed` `cmp` | text filters (stdin/`-` ok) |
| `basename` `dirname` `realpath` | path helpers |
| `hexdump` `strings` | binary helpers |
| `echo` `clear` `edit` | misc (`edit` loads file; `:w` `:q` `:wq` `:p`; `/pat`) |

## System / meta
| Command | Notes |
|---------|-------|
| `theme list\|set\|next` | CLI+GUI themes |
| `wallpaper list\|set\|none\|next` | Desktop background (binary PPM P6) |
| `scale [1-4]` | UI glyph scale |
| `date` `free` `env` `export` `which` `seq` `sleep` | |
| `top` `sysmon` | live system monitor (sparklines; `q` quit, `-n` once) |
| `ps` | list kernel tasks/threads |
| `kill <pid or name>` | mark READY/BLOCKED task zombie (not idle/self) |
| `true` `false` `sh` | exit status helpers; nested `ush>` loop |
| `js -e 'code'` / `js file.js` | Peak JS CLI — [browser-js.md](browser-js.md) |
| `help` `man <cmd>` | categorized help (`-h` / `--help` on most utils) |
| `ask` `audit` `memory` `policy` `peak` `gui` | agent + desktop |
| `privacy` | `persist` / `net-allow` / `kill-switch` — [privacy.md](privacy.md) |

`gui` enters the desktop; **Ctrl+Alt+Esc** returns to CLI.

## Network / containers
| Command | Notes |
|---------|-------|
| `ifconfig` `ping` `wget` `curl` | IPv4 + HTTP — [network.md](network.md) |
| `tar -c` / `tar -x` | ustar archive create/extract (64 KiB cap) |
| `ctr` `ctrd` | Dockerfile staging / static HTTP (not OCI) — [containers.md](containers.md) |

Prompt shows cwd: `peak:/home/dev/workspace> `
