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

Limits: up to 4 pipeline stages; captured pipe/redirect buffers are capped at 32 KiB.
`cat` / `head` / `tail` / `wc` / `grep` accept `-` (or omit the path) to read shell stdin from `<` or a pipe.

## Globs

Basename globs `*` and `?` expand against the directory of each argument (cwd if no `/`):

```
ls *.c
rm /tmp/peak-*.log
```

No match leaves the pattern unchanged. Max 16 argv slots after expansion.

## History

Command lines persist under `/var/peak/history`. **Up/Down** or **Ctrl-P/N** recall prior commands in the line editor. **`history`** prints numbered entries; **`!!`** repeats the last command; **`!n`** expands history entry *n* (optional suffix: `!! | wc`). History is recorded **after** expansion so `!!` does not poison the ring.

**Tab** completes the token under the caret: first-word `/bin` names, otherwise path entries in the current or specified directory.

**`alias`** lists or sets aliases persisted at `/var/peak/aliases` (`alias ll=ls`). Aliases expand the first word before pipeline parse.

**`cd -`** switches to `$OLDPWD` (updated on every successful `cd`).

On failure the prompt shows exit status without changing the cwd prefix: `peak:/home/dev/workspace [1]> ` (success omits the bracket).

## Navigation
| Command | Notes |
|---------|-------|
| `pwd` | print working directory |
| `cd [path]` | change directory (default workspace) |
| `ls [-l] [-h] [path]` | list directory (`-lh` human KiB/MiB; symlinks: l / @; `-l` shows `-> target`) |
| `tree [path]` | directory tree |
| `find <dir> [-name <name>] [-iname <pat>] [-type f|d] [-maxdepth N]` | basename / icase / type / depth |

## Files
| Command | Notes |
|---------|-------|
| `mkdir [-p] <path>` | create directory (parents always) |
| `touch <path>` | create empty file |
| `rm [-rf] <path>` | remove file or tree |
| `cp [-r] <src> <dst>` | copy |
| `mv <src> <dst>` | rename/move |
| `ln [-s] <target> <link>` | hard link or symlink (`-s`) |
| `readlink <path>` | print symlink target (no follow) |
| `chmod <mode> <path>…` | octal (`755`) or symbolic (`u+x`, `g-w`, `a=rx`) |
| `stat <path>` | metadata (mode; heap vs blob for files) |
| `du [-h] [path]` | tree byte size (`-h` KiB/MiB) |
| `df [-h]` | VFS inodes, RAM, PeakDisk/Blobstore status (`-h` KiB/MiB) |
| `truncate <path> <n>` | resize (max 4096) |
| `dd if=<in> of=<out> [bs=N] [count=N]` | lite block copy (default bs=512; 8 KiB total cap) |
| `sync` | flush block device when ATA/SD present |
| `file <path>…` | magic sniff (ELF, PPM P6, BMP, PEAKZIP1, PEAKGZ1, text vs data) |

## Text
| Command | Notes |
|---------|-------|
| `cat` `head` `tail` `wc` | file viewers (`head`/`tail` `-n N`; `wc` `-l`/`-w`/`-c` select fields) |
| `grep [-i] [-n] [-v] [-r] <pat> [path...]` | substring match; `-i` case-fold, `-n` line numbers, `-v` invert, `-r` recurse dirs; multi-file prints `path:` |
| `diff` `sort` `uniq` `cut` `tr` `sed` `cmp` | text filters (stdin/`-` ok; `sort` `-r`/`-n`/`-u`, `uniq` `-c`) |
| `sed` | sed-lite: `[N\|[N,M]] s/old/new/[g]`, `y/from/to/`, `d`, `p`, `-n` (8 KiB) |
| `fold` `rev` `nl` `tac` | wrap lines (`-w`), reverse chars/lines, number lines |
| `od` `split` `paste` | byte dump (`-tx1`/`-to1`), split by bytes (`-b`), merge two files |
| `xargs` | build `/bin` argv from stdin tokens (pipe/`<`; max 12 tokens) |
| `awk` | awk-lite: `-F fs`, `$0`/`$n`, `NR`/`NF`, `/pat/ { print … }` (8 KiB) |
| `jq` | jq-lite: `.key`, `.[]`, `keys`, `length`, compact print (8 KiB) |
| `basename` `dirname` `realpath` | path helpers |
| `hexdump` `strings` | binary helpers |
| `sha256sum` `md5sum` `base64` | digests and base64 encode/decode (`-d`; 64 KiB cap) |
| `less` | pager with page-up (`b`), go top/bottom (`g`/`G`), forward search (`/pat`) |
| `more` | simple pager (space next page, q quit) |
| `echo` `printf` `tee` `yes` `clear` `edit` | misc (`printf` `%s %d %u %x` + `\\n` `\\t` `\\\\`; `tee` stdin→stdout+files `-a`; `test`/`[` predicates; `yes` bounded) |

## System / meta
| Command | Notes |
|---------|-------|
| `theme list\|set\|next` | CLI+GUI themes |
| `wallpaper list\|set\|none\|next` | Desktop background (binary PPM P6) |
| `scale [1-4]` | UI glyph scale |
| `date [+format]` (`+%s`, `+%Y-%m-%d` via RTC) `free` `env` `export` `which` `seq` `sleep` `time` | (`free`: PMM pages + heap used/free/frag/freelist/oom) |
| `hostname` `uptime` `whoami` `id` `cal` | identity + calendar |
| `uname [-asnmr]` | kernel identity (`-s` sysname, `-n` `$HOSTNAME`, `-r` release, `-m` machine, `-a` all + BootInfo platform) |
| `gzip` `gunzip` | Peak RLE compress/decompress (PEAKGZ1 `.gz`, 8 KiB cap) |
| `timeout` `watch` | run once with deadline note; repeat (`-n`, max 32 iters) |
| `top` `sysmon` | live system monitor (sparklines, legend; `q` quit, `r` reset, `e` export, `-n` once) |
| `ps` | task list (state, CPU ticks, age, wake, share %) sorted by CPU ticks |
| `kill <pid or name>` | mark READY/BLOCKED task zombie (not idle/self) |
| `true` `false` `test` `[` `sh` | exit status helpers; `test -f/-d/-e/-z/-n`, `= != -eq …`; nested `ush>` loop |
| `history` | numbered command history |
| `js -e 'code'` / `js file.js` | Peak JS CLI — [browser-js.md](browser-js.md) |
| `help` `man <cmd>` | categorized help (`-h` / `--help` on most utils) |
| `ask` `audit` `memory` `peakvec` `policy` `peak` `gui` | agent + desktop (`peakvec query …` for top-k search) |
| `privacy` | `persist` / `net-allow` / `kill-switch on --confirm` — [privacy.md](privacy.md) |

`gui` enters the desktop; press **Ctrl+Alt+Esc** anytime to return to CLI.

CLI scrollback search: **Ctrl+F**, type a needle, **Enter** for next match (128 lines retained).

## Network / containers
| Command | Notes |
|---------|-------|
| `ifconfig` `ping` `wget` `curl` | IPv4 + HTTP — [network.md](network.md) |
| `tlsinfo` | TLS trust summary, session cache, `-s` cache list, `-r` root digests, `-m` hostname test |
| `nslookup` `host` | DNS A lookup (dig-style QUESTION/ANSWER) |
| `traceroute` | Staged reachability: local → gateway → DNS → dest (TCP :80) |
| `nc` | TCP connect (`host port` or `host:port`; optional send + recv) |
| `tar -c` / `tar -x` | ustar archive create/extract (64 KiB cap) |
| `zip` `unzip` | PEAKZIP1 multi-file archive — store or RLE per entry (64 KiB / 32 files / 8 KiB each) |
| `ctr` `ctrd` | Dockerfile staging / static HTTP (not OCI) — [containers.md](containers.md) |

Prompt shows cwd: `peak:/home/dev/workspace> ` (append `[N]` after a non-zero exit).
