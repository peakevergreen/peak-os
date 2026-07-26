# Peak CLI utilities

Peak ships a deep `/bin` utility pack as kernel builtins (not separate ELF binaries yet).

## Quoting

The shell splits on spaces and supports `"double"` and `'single'` quotes (quotes are stripped). Example:

```
ask "create fib.c"
js -e '1+2*3'
```

Unclosed quotes treat the remainder of the line as one argument. Max 24 argv slots per stage.

Shell builtins (not in `/bin`): `export NAME=val`, `read NAME` (one line from stdin into env), `unset NAME`.

## Pipes and redirection

Operators `|`, `>`, `>>`, `<`, `2>`, and `2>>` work outside quotes (spaces optional around them).
`2>`/`2>>` are lite: they capture the same console stream as `>` (no separate stderr).

Lists: `cmd1 && cmd2` runs `cmd2` only if `cmd1` succeeds; `cmd1 || cmd2` runs `cmd2` only if `cmd1` fails.

```
echo hello > out.txt
echo more >> out.txt
cat < out.txt
echo hello world | grep hello
seq 1 5 | wc
false && echo no
true || echo skipped
true && echo yes
expr 2 + 2
echo value | read MYVAR
unset MYVAR
```

Limits: up to 6 pipeline stages; captured pipe/redirect buffers are capped at 32 KiB.
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
| `find <dir> [-name pat] [-iname pat] [-type f|d] [-maxdepth N] [-print0] [-exec cmd {} ;]` | basename/icase/type/depth; `-print0` NUL-separated; `-exec` bounded (8) |

## Files
| Command | Notes |
|---------|-------|
| `mkdir [-p] <path>` | create directory (parents always) |
| `touch <path>` | create empty file |
| `rm [-rf] <path>` | remove file or tree |
| `cp [-r] [--promote-blob] <src> <dst>` | copy (blob-aware via ranged I/O; `--promote-blob` stores dest in blobstore when available) |
| `mv <src> <dst>` | rename/move |
| `ln [-sf] [-s] <target> <link>` | hard link or symlink (`-s`; `-f` force replace) |
| `readlink <path>` | print symlink target (no follow) |
| `chmod <mode> <path>...` | octal or symbolic (`u+x`, `go-w`, `a=rx`, …) |
| `stat [-c '%s'/'%n'] <path>` | metadata or lite `-c` format (`%s` size, `%n` name) |
| `du [-h] [-s] [path]` | tree byte size (`-h` KiB/MiB; `-s` summary bytes only) |
| `df [-h]` | VFS inodes, RAM, PeakDisk/Blobstore status (`-h` KiB/MiB + capacity honesty note) |
| `truncate <path> <n>` | resize (max 4096) |
| `mktemp [TEMPLATE]` | create unique temp path under `/tmp` (lite) |
| `install [-D] [-m mode] <src> <dst>` | copy with optional parent create (`-D`) and mode |
| `dd if=<in> of=<out> [bs=N] [count=N]` | lite block copy (default bs=512; 32 KiB total cap) |
| `sync` | flush block device when ATA/SD present |
| `file <path>…` | magic sniff (ELF, PPM P6, BMP, PEAKZIP1, PEAKGZ1, text vs data) |

## Text
| Command | Notes |
|---------|-------|
| `cat` `head` `tail` `wc` | file viewers (`head`/`tail` `-n N` or `-c N` bytes; `wc` `-l`/`-w`/`-c`) |
| `grep [-i] [-n] [-v] [-r] [-c] [-l] [-o] [-A N] [-B N] <pat> [path...]` | substring match; `-c` count, `-l` filenames, `-o` only match, `-A`/`-B` context lite (≤32) |
| `diff [-u] <a> <b>` `patch <target> [patch\|-]` | line diff; unified `-u`; patch lite applies `+`/`-` hunks |
| `sort` `uniq` `join` `comm` `cut` `tr` `sed` `cmp` | text filters (stdin/`-` ok; `sort` `-r`/`-n`/`-u`, `uniq` `-c`; `join` `-1`/`-2`/`-t`; `comm` `-1`/`-2`/`-3` suppress) |
| `sed` | sed-lite: `[N\|[N,M]] s/old/new/[g]`, `y/from/to/`, `d`, `p`, `q`, `=`, `-n` (32 KiB) |
| `fold` `rev` `nl` `tac` | wrap lines (`-w`), reverse chars/lines, number lines |
| `fmt` `column` `expand` `unexpand` | paragraph reflow (`fmt -w`); column align (`-t` tab input); tab↔space (`-t tabstop`, default 8) |
| `od` `split` `paste` | byte dump (`-tx1`/`-to1`), split by bytes (`-b`), merge two files |
| `xargs` | build `/bin` argv from stdin (`-0` null-delimited, `-n N` batch, `-I repl` replace; max 12 tokens) |
| `awk` | awk-lite: `-F fs`, `$0`/`$n`, `NR`/`NF`, `BEGIN`/`END`, vars `a-z`, `/pat/ { print … }` (32 KiB) |
| `jq` | jq-lite: `.key`, `.[]`, `.key.keys` nested, `keys`, `length`, compact print (32 KiB) |
| `basename` `dirname` `realpath` | path helpers |
| `hexdump` `strings` `xxd` | binary helpers (`xxd` 8 KiB cap) |
| `shuf` `cksum` | shuffle lines (8 KiB); CRC32 + byte count |
| `sha256sum` `md5sum` `sha1sum` `base64` `basenc` | digests and base64/base32 encode/decode (`-c` check mode; 64 KiB cap) |
| `less` | pager with page-up (`b`), go top/bottom (`g`/`G`), forward search (`/pat`) |
| `more` | simple pager (space next page, q quit) |
| `echo` `printf` `expr` `tee` `yes` `clear` `edit` | misc (`printf` `%s %d %u %x` + `\\n` `\\t` `\\\\`; `expr` INT OP INT; `tee` stdin→stdout+files `-a`; `test`/`[` predicates; `yes` bounded) |

## System / meta
| Command | Notes |
|---------|-------|
| `theme list\|set\|next` | CLI+GUI themes |
| `wallpaper list\|set\|none\|next` | Desktop background (binary PPM P6) |
| `scale [1-4]` | UI glyph scale |
| `date [+format]` (`+%s`, `+%Y-%m-%d` via RTC) `free` `env` `export` `which` `seq` `sleep` `time` | (`free`: PMM pages + heap used/free/frag/freelist/oom) |
| `hostname` `uptime` `whoami` `id` `cal` | identity + calendar |
| `uname [-asnmr]` | kernel identity (`-s` sysname, `-n` `$HOSTNAME`, `-r` release, `-m` machine, `-a` all + BootInfo platform) |
| `gzip` `gunzip` | Peak RLE compress/decompress (PEAKGZ1 `.gz`, 32 KiB cap) |
| `timeout` | `<sec> <cmd>` wall limit (exit 124 if exceeded; no preemption) |
| `watch` | run once with deadline note; repeat (`-n`, max 32 iters) |
| `top` `sysmon` | live system monitor (sparklines, legend; `q` quit, `r` reset, `e` export, `-n` once) |
| `ps` | task list (state, CPU ticks, age, wake, share %) sorted by CPU ticks |
| `pgrep` `pidof` `dmesg` | find tasks by name substring; print PIDs; console scrollback ring (`dmesg -n N`) |
| `kill <pid or name>` | mark READY/BLOCKED task zombie (not idle/self) |
| `true` `false` `test` `[` `sh` | exit status helpers; `test -f/-d/-e/-z/-n`, `= != -eq …`; nested `ush>` loop |
| `history` | numbered command history |
| `js -e 'code'` / `js file.js` | Peak JS CLI — [browser-js.md](browser-js.md) |
| `help` `man <cmd>` | categorized help (`-h` / `--help` on most utils) |
| `ask` `audit` `memory` `peakvec` `policy` `peak` `gui` | agent + desktop (`peakvec query …` for top-k search) |
| `privacy` | `persist` / `net-allow` / `kill-switch on --confirm` — [privacy.md](privacy.md) |
| `reboot` | request platform reboot (QEMU/ACPI path when available) |
| `disksave` | PeakDisk save/export helper (VFS → PeakDisk image) |

`gui` enters the desktop; press **Ctrl+Alt+Esc** anytime to return to CLI.

CLI scrollback search: **Ctrl+F**, type a needle, **Enter** for next match (128 lines retained).

## Network / containers
| Command | Notes |
|---------|-------|
| `ifconfig` `ping [-c N]` `wget` `curl` `dnsflush` | IPv4 + HTTP (`dnsflush` clears DNS cache; CNAME follow up to 4 hops) — [network.md](network.md) |
| `tlsinfo` | TLS trust summary, HSTS host count, resume depth, `-s` cache list (ticket/PSK unavailable strings), `-r` root digests, `-m` hostname test |
| `nslookup` `host` | DNS A (`-6` AAAA diagnostic, `-x` PTR reverse); IPv4 routing only |
| `traceroute` | Staged reachability: local → gateway → DNS → dest (TCP :80) |
| `nc [-w sec]` | TCP connect or `-l` listen (800ms listen default, 4s connect; `-w` max 30s) |
| `ss [-t]` | lite netstat over kernel TCP conn table (`-t` tcp-only, default) |
| `tar -c` / `-x` / `-t [-v]` | ustar create/extract/list (64 KiB archive cap; `-v` verbose) |
| `zip` `unzip` | PEAKZIP1 multi-file archive — store or RLE per entry (64 KiB / 32 files / 8 KiB each) |
| `ctr` `ctrd` | Dockerfile staging / static HTTP (not OCI) — [containers.md](containers.md) |

Prompt shows cwd: `peak:/home/dev/workspace> ` (append `[N]` after a non-zero exit).
