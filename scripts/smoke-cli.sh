#!/usr/bin/env bash
# Smoke checks that do not require a live QEMU session.
# PEAK_SKIP_ISO=1 skips rebuild when build/peak-os.iso already exists (CI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:$PATH"

if [[ "${PEAK_SKIP_ISO:-}" == "1" ]]; then
  echo "==> skip iso rebuild (PEAK_SKIP_ISO=1)"
  test -f build/peak-os.iso
else
  echo "==> build iso"
  make iso
fi

echo "==> scripts present"
test -f docs/CLI.md
test -f boot/peak.conf
test ! -e limine.conf
test ! -e scripts/peak-host-proxy.py
test ! -e scripts/peak-ssh

echo "==> builtin count in ubin registry"
count=$(grep -c '^UBIN_CMD' kernel/user/ubin_cmds.def || true)
echo "    registered commands: $count"
test "$count" -ge 90
grep -q 'test_ubin_registry' Makefile
grep -q 'test_agent_tools' Makefile
grep -q 'test_cli_awk' Makefile
grep -q 'test_cli_sed' Makefile
grep -q 'test_cli_jq' Makefile
grep -q 'test_cli_zip' Makefile
grep -q 'test_desktop_titles' Makefile
grep -q 'shell_parse_pipeline' kernel/shell_split.c
grep -q 'SHELL_PIPE_MAX' kernel/include/shell_split.h
grep -q 'nslookup' kernel/user/ubin_cmds.def
grep -q 'printf' kernel/user/ubin_cmds.def
grep -q 'test' kernel/user/ubin_cmds.def
grep -q 'boot_verify_manifest_sig' boot/common/verify_kernel.c

echo "==> Pass 19 text utils"
grep -q 'fold' kernel/user/ubin_cmds.def
grep -q 'rev' kernel/user/ubin_cmds.def
grep -q 'od' kernel/user/ubin_cmds.def
grep -q 'split' kernel/user/ubin_cmds.def
grep -q 'paste' kernel/user/ubin_cmds.def
grep -q 'nl' kernel/user/ubin_cmds.def
grep -q 'tac' kernel/user/ubin_cmds.def
grep -q 'xargs' kernel/user/ubin_cmds.def
grep -q 'utils_text5' Makefile

echo "==> Pass 20 sys utils"
grep -q 'hostname' kernel/user/ubin_cmds.def
grep -q 'uptime' kernel/user/ubin_cmds.def
grep -q 'whoami' kernel/user/ubin_cmds.def
grep -q 'cal' kernel/user/ubin_cmds.def
grep -q 'gzip' kernel/user/ubin_cmds.def
grep -q 'timeout' kernel/user/ubin_cmds.def
grep -q 'watch' kernel/user/ubin_cmds.def
grep -q 'utils_sys2' Makefile

echo "==> Pass 21 shell UX"
grep -q 'alias' kernel/user/ubin_cmds.def
grep -q 'history' kernel/user/ubin_cmds.def
grep -q 'shell_alias_init' kernel/shell.c
grep -q 'shell_history_add' kernel/shell_history.c

echo "==> Pass 22 VFS errno"
grep -q 'vfs_last_error' kernel/vfs.c
grep -q 'peak_strerror' kernel/util.c
grep -q 'test_vfs' Makefile

echo "==> Pass 23 net diag"
grep -q 'traceroute' kernel/user/ubin_cmds.def
grep -q 'host' kernel/user/ubin_cmds.def
grep -q 'utraceroute_main' kernel/user/utils_net.c

echo "==> Pass 24 TLS/WebPKI"
grep -q 'tlsinfo' kernel/user/ubin_cmds.def
grep -q 'tls_err_name' kernel/net/tls_util.c
grep -q 'webpki_root_sha256' kernel/net/webpki.c

echo "==> Pass 25 containers"
grep -q 'ctr_build' kernel/ctr_build.c
grep -q 'test_ctr_path' Makefile

echo "==> Pass 26 agent tools"
grep -q 'fs.grep' kernel/agent_tools.c
grep -q 'net.ping' kernel/agent_tools.c

echo "==> Pass 27 desktop chrome"
grep -q 'start_filter' kernel/gui/desktop_menus.c
grep -q 'desktop_snap_hint' kernel/gui/desktop.c

echo "==> Pass 28 terminal UX"
grep -q 'term_find_next' kernel/gui/desktop_terminal.c

echo "==> Pass 29 files/notepad UX"
grep -q 'files_del_arm' kernel/gui/desktop_files.c

echo "==> Pass 48 images UX"
grep -q 'desktop_files_goto' kernel/gui/desktop_files.c
grep -q 'img_refresh_index' kernel/gui/desktop_images.c
grep -q 'desktop_images_drag' kernel/gui/desktop_images.c

echo "==> Pass 50 files clipboard"
grep -q 'files_paste_into_cwd' kernel/gui/desktop_files.c
grep -q 'files_clip_cut' kernel/gui/desktop_files.c
grep -q 'files_draw_status' kernel/gui/desktop_files.c
grep -q 'CTX_ACT_FILES_PASTE' kernel/gui/desktop_internal.h

echo "==> Pass 30 browser JS UX"
grep -q 'browser_bookmarks_init' kernel/gui/browser_bookmarks.c

echo "==> Pass 51 browser tab chrome"
grep -q 'browser_restore_closed_tab' kernel/gui/browser_nav.c
grep -q 'browser_has_closed_tab' kernel/gui/browser_nav.c
grep -q 'hit_tab_close_x' kernel/gui/browser_draw.c
grep -q 'CTX_ACT_BROWSER_RESTORE' kernel/gui/desktop_internal.h

echo "==> Pass 31 settings/theme"
grep -q 'settings_draw_scale_preview' kernel/gui/desktop_settings.c
grep -q 'test_wallpaper_cache' Makefile

echo "==> Pass 32 notify/clipboard"
grep -q 'notify_history_count' kernel/notify.c
grep -q 'clipboard_get_previous' kernel/clipboard.c

echo "==> Pass 33 net/disks apps"
grep -q 'desktop_netexp_ctx_menu' kernel/gui/desktop_netexp.c
grep -q 'desktop_netctl_ctx_menu' kernel/gui/desktop_netctl.c

echo "==> Pass 34 sysmon"
grep -q 'sysmon_snapshot' kernel/sysmon.c
grep -q 'sysmon_export' kernel/sysmon.c
grep -q 'sysmon_sparkline_legend' kernel/sysmon.c

echo "==> Pass 35 heap/sched"
grep -q 'heap_oom_count' kernel/heap.c
grep -q 'heap_get_freelist_stats' kernel/heap.c
grep -q 'sched_sort_tasks' kernel/sched.c

echo "==> Pass 36 peakvec/blobstore"
grep -q 'peakvec' kernel/user/ubin_cmds.def
grep -q 'peakvec_stats' kernel/peakvec.c
grep -q 'blobstore_check' kernel/blobstore.c

echo "==> Pass 46 jq-lite"
grep -q 'jq' kernel/user/ubin_cmds.def
grep -q 'ujq_main' kernel/user/utils_text7.c
grep -q 'test_cli_jq' Makefile

echo "==> Pass 37 input/console"
grep -q 'keyboard_set_repeat' kernel/keyboard.c
grep -q 'console_scroll_find_next' kernel/console_scroll.c

echo "==> Pass 39 awk-lite"
grep -q 'awk' kernel/user/ubin_cmds.def
grep -q 'uawk_main' kernel/user/utils_text6.c
grep -q 'test_cli_awk' Makefile

echo "==> Pass 47 zip/unzip"
grep -q 'zip' kernel/user/ubin_cmds.def
grep -q 'unzip' kernel/user/ubin_cmds.def
grep -q 'utils_zip' Makefile
grep -q 'test_cli_zip' Makefile
grep -q 'PEAKZIP1' kernel/user/utils_zip.c

echo "==> Pass 40 sed depth"
grep -q 'parse_addr' kernel/user/utils_text3.c
grep -q 'subst_global' kernel/user/utils_text3.c
grep -q 'translit_line' kernel/user/utils_text3.c
grep -q 'test_cli_sed' Makefile

echo "==> Pass 43 human sizes"
grep -q 'peak_hsize_fmt' kernel/user/utils_file.c
grep -q 'peak_has_flag(argc, argv, "-h")' kernel/user/utils_file.c
grep -q 'KiB/MiB' docs/CLI.md

echo "==> Pass 42 symlinks"
grep -q 'VFS_SYMLINK' kernel/include/vfs.h
grep -q 'vfs_resolve' kernel/vfs.c
grep -q 'readlink' kernel/user/ubin_cmds.def
grep -q 'PEAK_ELOOP' kernel/include/peak_errno.h

echo "==> Pass 53 HTTP/2 client depth"
grep -q 'HTTP2_BODY_MAX' kernel/include/http2.h
grep -q 'hpack_decode_block' kernel/net/http2.c
grep -q 'http2_last_meta' kernel/net/http2.c
grep -q 'net_http_last_h2' kernel/include/net.h
grep -q 'net_http_last_body_truncated' kernel/net/http.c
grep -q 'show_headers' kernel/user/utils_net.c
grep -q 'HTTP/2' docs/network.md

echo "==> Pass 45 pager depth"
grep -q 'pager_find_next' kernel/user/utils_pager.c
grep -q 'less_main' kernel/user/utils_pager.c

echo "==> Wave 2 subsystem markers"
grep -q 'files_copy_sel_path' kernel/gui/desktop_files.c
grep -q 'browser_new_tab' kernel/gui/browser_nav.c
grep -q 'browser_close_tab' kernel/gui/browser_nav.c
grep -q 'browser_select_tab' kernel/gui/browser_nav.c
grep -q 'http2_get' kernel/net/http2.c
grep -q 'test_http_tcp' Makefile
grep -q 'webapi_install' kernel/gui/webapi.c
grep -q 'test_webapi' Makefile
grep -q 'tls_session_get' kernel/net/tls_session.c
grep -q 'tls_session_put' kernel/net/tls_session.c
grep -q 'test_tls' Makefile
grep -q 'peakvec_query' kernel/peakvec.c
grep -q 'test_peakvec' Makefile
grep -q 'fs.stat' kernel/agent_tools.c
grep -q 'mem.recall' kernel/agent_tools.c
grep -q 'audit.tail' kernel/agent_tools.c

echo "==> Pass 49 agent GUI UX"
grep -q 'AGENT_INPUT_MAX 256' kernel/gui/desktop_agent.c
grep -q 'agent_transcript_note_audit' kernel/agent.c
grep -q 'AGENT_TLINES 48' kernel/agent.c
grep -q 'agent_approval_draw' kernel/gui/desktop_agent.c

echo "==> Pass 52 settings keyboard a11y"
grep -q 'desktop_settings_key' kernel/gui/desktop_settings.c
grep -q 'settings_draw_focus_ring' kernel/gui/desktop_settings.c
grep -q 'settings_kfocus' kernel/gui/desktop_settings.c

echo "==> Pass 44 uname flags"
grep -q 'bootinfo_init' kernel/boot.c
grep -q 'bootinfo_format_version' kernel/bootinfo.c
grep -q 'peak_has_flag(argc, argv, "-a")' kernel/user/utils_sys.c

echo "==> Pass 41 VFS modes"
grep -q VFS_MODE_FILE kernel/include/vfs.h
grep -q vfs_chmod kernel/vfs.c
grep -q uchmod_main kernel/user/utils_file.c
grep -q chmod kernel/user/ubin_cmds.def
grep -q vfs_mode_string kernel/vfs.c

echo "==> Pass 55 TLS session resume"
grep -q '0x0029' kernel/net/tls_clienthello.c
grep -q 'tls_session_entry_info' kernel/net/tls_session.c
grep -q 'session_cache:' kernel/user/utils_net.c
grep -q 'PSK-lite' docs/network.md

echo "==> theme names"
grep -q evergreen kernel/theme.c
grep -q midnight kernel/theme.c
grep -q amber kernel/theme.c

echo "==> cell metrics"
grep -q fb_cell_h kernel/fb.c
grep -q fb_cell_h kernel/console.c

echo "==> CLI front-buffer scroll invariant"
grep -q 'console_scroll_plan' kernel/console.c
grep -q 'fb->addr' kernel/console.c
grep -q 'keep CLI on front' kernel/console.c

echo "==> boot logo + status compaction"
grep -q console_boot_logo kernel/console.c
grep -q 'cols - used - tag_len - 1' kernel/console.c

if [[ "${PEAK_SKIP_HOST_TESTS:-}" != "1" ]]; then
  echo "==> phase7/host unit tests"
  make test-host
else
  echo "==> skip host tests (PEAK_SKIP_HOST_TESTS=1)"
fi

echo "==> Wave 3 Pass 59 grep depth"
grep -q 'grep_line_match' kernel/user/utils_text.c
grep -q 'grep_walk_cb' kernel/user/utils_text.c
grep -q 'test_cli_grep' Makefile
grep -q '\[-i\] \[-n\] \[-v\] \[-r\]' docs/CLI.md

echo "==> Wave 3 Pass 60 find depth"
grep -q 'find_icase_eq' kernel/user/utils_sys.c
grep -q 'find_walk_rec' kernel/user/utils_sys.c
grep -q 'test_cli_find' Makefile
grep -q 'maxdepth' docs/CLI.md

echo "==> Wave 3 Pass 61 sort/uniq/wc flags"
grep -q 'sort_key_cmp' kernel/user/utils_text2.c
grep -q 'count_prefix' kernel/user/utils_text2.c
grep -q 'show_l' kernel/user/utils_text.c
grep -q 'test_cli_sortflags' Makefile
grep -q 'sort.*`-r`/`-n`/`-u`' docs/CLI.md


echo "==> Wave 3 Pass 62 dd/sync"
grep -q 'udd_main' kernel/user/utils_file.c
grep -q 'usync_main' kernel/user/utils_file.c
grep -q 'UBIN_CMD("dd"' kernel/user/ubin_cmds.def
grep -q 'blockdev_flush' kernel/user/utils_file.c
grep -q 'dd if=' docs/CLI.md


echo "==> Wave 3 Pass 63 file magic"
grep -q ufile_main kernel/user/utils_file.c
grep -q file_describe kernel/user/utils_file.c
grep -q test_cli_filemagic Makefile
grep -q 'file <path>' docs/CLI.md


echo "==> Wave 3 Pass 64 date/printf"
grep -q date_format kernel/user/utils_sys.c
grep -q test_cli_date Makefile
grep -q '+%s' docs/CLI.md
grep -q 'escapes:' kernel/user/utils_text4.c


echo "==> Wave 3 Pass 65 pipe buffer"
grep -q 'SHELL_CAPTURE_MAX  32768' kernel/include/shell_split.h
grep -q '32 KiB' docs/CLI.md
grep -q 'pipe output truncated' kernel/shell_dispatch.c


echo "==> Wave 3 Pass 66 net CLI"
grep -q 'net_dns_resolve_aaaa' kernel/net/dns.c
grep -q 'dns_lookup_aaaa_dig' kernel/user/utils_net.c
grep -q 'post_body' kernel/user/utils_net.c
grep -q 'IPv4-only' kernel/user/utils_net.c



echo "==> Wave 3 Pass 77 containers depth"
grep -q 'ENV %s' kernel/ctr_build.c
grep -q 'X-Peak-Env' kernel/ctr_http.c
grep -q 'ctr_image_expose' kernel/ctr.c
grep -q 'Pass 77' docs/containers.md

echo "==> Wave 3 Pass 76 agent tools"
grep -q 'agent_tool_fs_tree' kernel/agent_tools.c
grep -q 'agent_tool_sys_ps' kernel/agent_tools.c
grep -q 'INTENT_TREE' kernel/agent_planner.c
grep -q 'fs.tree' kernel/vfs_peakfs.c

echo "==> Wave 3 Pass 75 browser downloads/bookmarks"
grep -q 'browser_download_save' kernel/gui/browser_bookmarks.c
grep -q 'browser_bookmark_remove' kernel/gui/browser_bookmarks.c
grep -q 'CTX_ACT_BROWSER_DOWNLOAD' kernel/gui/desktop_internal.h
grep -q 'Save to Downloads' kernel/gui/browser_nav.c

echo "==> Wave 3 Pass 74 VFS stream export"
grep -q 'vfs_export_ramdisk_stream' kernel/vfs_peakfs.c
grep -q 'VFS_EXPORT_STREAM_CHUNK' kernel/include/vfs.h
grep -q 'vfs_export_last_error' kernel/peakdisk.c
grep -q 'vfs_export_ramdisk_stream' docs/peakvec.md

echo "==> Wave 3 Pass 73 TLS PSK binder"
grep -q 'tls13_compute_psk_binder' kernel/net/tls_psk.c
grep -q 'tls13_note_resumption_master' kernel/net/tls13.c
grep -q 'res_master_len' kernel/include/tls_session.h
grep -q 'binder=hkdf' kernel/user/utils_net.c
grep -q 'HKDF/HMAC binder' docs/network.md

echo "==> Wave 3 Pass 72 files/notepad a11y"
grep -q files_draw_focus_ring kernel/gui/desktop_files.c
grep -q np_draw_focus_ring kernel/gui/desktop_notepad.c

echo "==> Wave 3 Pass 71 terminal tabs"
grep -q TERM_MAX_TABS kernel/gui/desktop_terminal.c
grep -q term_draw_tab_strip kernel/gui/desktop_terminal.c

echo "==> Wave 3 Pass 70 keyboard snap"
grep -q 'desktop_snap_apply(focus, 1)' kernel/gui/desktop.c
grep -q 'KEY_LEFT' kernel/gui/desktop.c

echo "==> Wave 3 Pass 69 notify clip UI"
grep -q 'desktop_draw_notify_history' kernel/gui/desktop_overlays.c
grep -q 'desktop_terminal_paste_previous' kernel/gui/desktop_terminal.c
grep -q 'Ctrl+Shift+V' kernel/gui/desktop_overlays.c

echo "==> Wave 3 Pass 68 files breadcrumbs"
grep -q files_crumb_click kernel/gui/desktop_files.c
grep -q files_draw_crumbs kernel/gui/desktop_files.c

echo "==> Wave 3 Pass 67 start menu pointer"
grep -q 'desktop_menus_start_hover' kernel/gui/desktop_menus.c
grep -q 'desktop_menus_start_wheel' kernel/gui/desktop_menus.c




echo "==> Wave 4 Pass 90 help shortcut ux"
grep -q help_filter kernel/gui/desktop_overlays.c
grep -q desktop_help_focus_hint kernel/gui/desktop_overlays.c
grep -q desktop_help_filter_key kernel/gui/desktop_overlays.c

echo "==> Wave 4 Pass 91 monitor export ux"
grep -q monitor_export_btn kernel/gui/monitor.c
grep -q monitor_export_click_at kernel/gui/monitor.c

echo "==> Wave 4 Pass 92 netexp netctl depth"
grep -q netexp_draw_conn_table kernel/gui/desktop_netexp.c
grep -q tls_session_used_count kernel/gui/desktop_netctl.c

echo "==> Wave 4 Pass 93 disks peakdisk ux"
grep -q peakdisk_save_progress_pct kernel/peakdisk.c
grep -q disks_format_capacity_line kernel/gui/desktop_disks.c

echo "==> Wave 4 Pass 94 peakvec namespace"
grep -q peakvec_namespace_list kernel/user/utils_agent.c
grep -q agent_transcript_note_recall kernel/agent.c

echo "==> Wave 4 Pass 95 agent gui search"
grep -q agent_transcript_filter kernel/agent.c
grep -q agent_transcript_filter_key kernel/agent.c

echo "==> Wave 4 Pass 97 heap sched gui"
grep -q sched_note_gui_load kernel/sched.c
grep -q desktop_compose_note_gui_tick kernel/gui/desktop_compose.c

echo "==> Wave 4 Pass 96 nc traceroute depth"
grep -q nc_listen_mode kernel/user/utils_net.c
grep -q 'hop timeout / loss' kernel/user/utils_net.c

echo "==> Wave 4 Pass 89 theme preview"
grep -q settings_draw_theme_chrome_preview kernel/gui/desktop_settings.c
grep -q settings_theme_preview kernel/gui/desktop_settings.c

echo "==> Wave 4 Pass 88 files dnd openwith"
grep -q desktop_files_drop_at kernel/gui/desktop_files.c
grep -q desktop_files_drag_begin_sel kernel/gui/desktop_files.c
grep -q desktop_files_drop_at kernel/gui/desktop.c

echo "==> Wave 4 Pass 87 js for-await"
grep -q parse_for_await_of kernel/js/js_parse.c
grep -q "for-await lite" tests/fixtures/js/for-await.js
grep -q 'eval module m2' tests/host/test_js.c

echo "==> Wave 4 Pass 86 browser ring3"
grep -q browser_isolation_dom_allowed kernel/gui/browser_isolation.c
grep -q 'ring-3 isolation unavailable' kernel/gui/browser_js.c
grep -q browser_isolation kernel/gui/browser_isolation.c

echo "==> Wave 4 Pass 85 dns negcache ux"
grep -q net_dns_last_negative_cached kernel/net/dns.c
grep -q 'cached negative' kernel/user/utils_net.c

echo "==> Wave 4 Pass 84 http body policy"
grep -q 'HTTP_BODY_MAX 32768' kernel/user/utils_net.c
grep -q '32768' docs/network.md

echo "==> Wave 4 Pass 83 text buffer depth"
grep -q 'READ_MAX 32768' kernel/user/utils_text6.c
grep -q JQ_SELECT kernel/user/utils_text7.c
grep -q '32 KiB' docs/CLI.md

echo "==> Wave 4 Pass 82 mktemp/install"
grep -q umktemp_main kernel/user/utils_file.c
grep -q uinstall_main kernel/user/utils_file.c
grep -q 'UBIN_CMD("mktemp"' kernel/user/ubin_cmds.def

echo "==> Wave 4 Pass 81 crypto pack"
grep -q 'void sha1(' kernel/net/crypto_hash.c
grep -q usha1sum_main kernel/user/utils_crypto.c
grep -q ubasenc_main kernel/user/utils_crypto.c
grep -q 'UBIN_CMD("sha1sum"' kernel/user/ubin_cmds.def

echo "==> Wave 4 Pass 80 head/tail/timeout"
grep -q 'byte_mode' kernel/user/utils_text.c
grep -q 'wall-clock limit exceeded' kernel/user/utils_sys2.c
grep -q '\`-c N\`' docs/CLI.md || grep -q '-c N' docs/CLI.md

echo "==> Wave 4 Pass 79 join/comm"
grep -q ujoin_main kernel/user/utils_text2.c
grep -q ucomm_main kernel/user/utils_text2.c
grep -q 'UBIN_CMD("join"' kernel/user/ubin_cmds.def
grep -q 'join' docs/CLI.md



echo "==> Wave 5 Pass 99 cli docs sync"
grep -q 'mktemp' docs/CLI.md
grep -q 'sha1sum' docs/CLI.md
grep -q 'basenc' docs/CLI.md
grep -q 'disksave' docs/CLI.md
grep -q 'reboot' docs/CLI.md


echo "==> Wave 5 Pass 108 chunked h2 post"
grep -q http_decode_chunked_body kernel/net/http_util.c
grep -q http2_request kernel/net/http2.c
grep -q 'http_decode_chunked_body(body' kernel/net/http.c

echo "==> Wave 5 Pass 107 curl ping"
grep -q head_only kernel/user/utils_net.c
grep -q 'strcmp(method, "HEAD")' kernel/net/http.c
grep -q '\`-c N\`' docs/CLI.md || grep -q '-c N' docs/CLI.md

echo "==> Wave 5 Pass 106 tar dd gzip"
grep -q 'mode = 3' kernel/user/utils_tar.c
grep -q 'DD_IO_MAX   (32 \* 1024)' kernel/user/utils_file.c
grep -q 'COMP_MAX (32 \* 1024)' kernel/user/utils_sys2.c
grep -q '32 KiB' docs/CLI.md

echo "==> Wave 5 Pass 105 diff patch sha"
grep -q upatch_main kernel/user/utils_text2.c
grep -q 'peak_has_flag(argc, argv, "-u")' kernel/user/utils_text2.c
grep -q hash_check kernel/user/utils_crypto.c
grep -q 'UBIN_CMD("patch"' kernel/user/ubin_cmds.def

echo "==> Wave 5 Pass 104 pgrep dmesg"
grep -q upgrep_main kernel/user/utils_monitor.c
grep -q udmesg_main kernel/user/utils_monitor.c
grep -q console_scrollback_line_count kernel/user/utils_monitor.c
grep -q 'UBIN_CMD("dmesg"' kernel/user/ubin_cmds.def

echo "==> Wave 5 Pass 103 shuf cksum xxd"
grep -q 'UBIN_CMD("shuf"' kernel/user/ubin_cmds.def
grep -q ushuf_main kernel/user/utils_text8.c
grep -q crc32_update kernel/user/utils_text8.c
grep -q uxxd_main kernel/user/utils_text8.c
grep -q utils_text8 Makefile

echo "==> Wave 5 Pass 102 xargs find"
grep -q 'xargs_tokenize' kernel/user/utils_text5.c
grep -q 'FIND_EXEC_MAX' kernel/user/utils_sys.c
grep -q 'print0' kernel/user/utils_sys.c
grep -q '\`-print0\`' docs/CLI.md || grep -q '-print0' docs/CLI.md

echo "==> Wave 5 Pass 101 grep flags"
grep -q 'count_only' kernel/user/utils_text.c
grep -q 'ctx_before' kernel/user/utils_text.c
grep -q '\`-o\`' docs/CLI.md || grep -q '-o' docs/CLI.md

echo "==> Wave 5 Pass 100 shell scripting"
grep -q uexpr_main kernel/user/utils_text4.c
grep -q 'UBIN_CMD("expr"' kernel/user/ubin_cmds.def
grep -q SHELL_REDIR_ERR kernel/include/shell_split.h
grep -q '&&' docs/CLI.md
grep -q '2>' docs/CLI.md

echo "==> Wave 4 Pass 98 lock-in"
grep -q 'Wave 4 Pass 79' scripts/smoke-cli.sh
grep -q 'Wave 4 Pass 89' scripts/smoke-cli.sh
grep -q 'Wave 4 Pass 97' scripts/smoke-cli.sh
grep -q 'Enhancement wave 4' docs/ROADMAP.md
grep -q 'Wave 4 / Pass 98' scripts/security-checklist.md

echo "==> Wave 3 Pass 78 lock-in"
grep -q 'Wave 3 Pass 59' scripts/smoke-cli.sh
grep -q 'Wave 3 Pass 73' scripts/smoke-cli.sh
grep -q 'Wave 3 Pass 77' scripts/smoke-cli.sh
grep -q 'Enhancement wave 3' docs/ROADMAP.md
grep -q 'Wave 3 / Pass 78' scripts/security-checklist.md

echo "==> security / purity markers"
grep -q 'copy_from_user' kernel/syscall.c
grep -qi 'tls certificate unverified' kernel/net/tls.c kernel/net/tls_handshake.c
grep -q 'agent_approve_write' kernel/agent.c
grep -q 'hlt_if_enabled' kernel/net/*.c
grep -q 'blockdev_flush' kernel/peakdisk.c
grep -q 'peak_bootinfo' kernel/boot.c
grep -q 'net_attempt_stats_reset' kernel/gui/desktop.c
grep -q 'PeakBrowser/1' kernel/net/*.c
! grep -q 'COM2\|COM3\|0x2F8\|0x3E8' kernel/agent.c kernel/user/ubin.c
! grep -rq 'limine\.h\|LIMINE_' kernel boot --include='*.c' --include='*.h' --include='*.S' --include='*.ld'

echo "==> purity"
chmod +x scripts/purity-check.sh
./scripts/purity-check.sh

echo "OK — smoke passed (run make smoke-bios / smoke-uefi for boot)"
