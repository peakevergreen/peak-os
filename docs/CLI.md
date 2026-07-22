# Peak CLI utilities

Peak ships a deep `/bin` utility pack as kernel builtins (not separate ELF binaries yet).

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
| `cat` `head` `tail` `wc` | file viewers |
| `grep <pat> <file>` | substring match |
| `hexdump` `strings` | binary helpers |
| `echo` `clear` `edit` | misc |

## System / meta
| Command | Notes |
|---------|-------|
| `theme list\|set\|next` | CLI+GUI themes |
| `wallpaper list\|set\|none\|next` | Desktop background (binary PPM P6) |
| `scale [1-4]` | UI glyph scale |
| `date` `free` `env` `export` `which` `seq` `sleep` | |
| `top` `sysmon` | live system monitor (sparklines; `q` quit, `-n` once) |
| `help` `man <cmd>` | categorized help |
| `ask` `audit` `memory` `policy` `peak` `gui` | agent + desktop |

Prompt shows cwd: `peak:/home/dev/workspace> `
