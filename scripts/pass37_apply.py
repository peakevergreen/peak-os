#!/usr/bin/env python3
"""Apply Pass 37 input/console polish. Run from repo root."""
from pathlib import Path

R = Path(__file__).resolve().parents[1]

def patch(path, old, new):
    p = R / path
    t = p.read_text()
    if old not in t:
        raise SystemExit(f"patch miss {path}")
    p.write_text(t.replace(old, new, 1))

def write(path, text):
    (R / path).write_text(text)

write("kernel/include/keyboard.h", """#ifndef PEAK_KEYBOARD_H
#define PEAK_KEYBOARD_H
#include "types.h"
#define KEY_LEFT 0x100
#define KEY_RIGHT 0x101
#define KEY_UP 0x102
#define KEY_DOWN 0x103
#define KEY_HOME 0x104
#define KEY_END 0x105
#define KEY_DELETE 0x106
#define KEY_TAB 0x107
#define KEY_F4 0x108
void keyboard_init(void);
void keyboard_inject(int key);
void keyboard_set_modifiers(int shift, int ctrl, int alt);
void keyboard_poll(void);
void keyboard_set_repeat(uint32_t delay_ticks, uint32_t rate_ticks);
void keyboard_repeat_key_down(int key);
void keyboard_repeat_key_up(int key);
int keyboard_has_char(void);
char keyboard_getchar(void);
char keyboard_try_getchar(void);
int keyboard_try_getkey(void);
int keyboard_ctrl_down(void);
int keyboard_shift_down(void);
int keyboard_alt_down(void);
#endif
""")

write("kernel/keyboard.c", open(R / "scripts/pass37_keyboard.c").read())

write("kernel/drivers/usb/usb_core.c", open(R / "scripts/pass37_usb_core.c").read())

write("kernel/console_scroll.c", open(R / "scripts/pass37_console_scroll.c").read())

patch("kernel/include/console_scroll.h",
      "uint64_t console_scroll_bytes(uint32_t pitch, uint32_t copy_rows);\n\n#endif",
      """uint64_t console_scroll_bytes(uint32_t pitch, uint32_t copy_rows);

int console_scroll_line_find(const char *line, const char *needle, int *col_out);
int console_scroll_find_next(const char *const *lines, int nlines,
                             const char *needle, int start_line, int start_col,
                             int *out_line, int *out_col);
#ifndef PEAK_HOST_TEST
#define CONSOLE_SCROLLBACK_LINES 128
#define CONSOLE_SCROLLBACK_COLS  256
void console_scrollback_reset(void);
void console_scrollback_note_char(char c);
int console_scrollback_line_count(void);
const char *console_scrollback_line(int idx);
int console_scrollback_find(const char *needle, int *out_line, int *out_col,
                             int start_line, int start_col);
#endif

#endif""")

patch("kernel/mouse.c",
      "void mouse_inject(int32_t dx, int32_t dy, uint8_t btns, int8_t wheel);",
      """void mouse_inject(int32_t dx, int32_t dy, uint8_t btns, int8_t wheel);

static int32_t mouse_accel_delta(int32_t d) {
    if (d == 0) return 0;
    int32_t sign = d < 0 ? -1 : 1, m = d < 0 ? -d : d;
    if (m <= 2) return d;
    m += m / 3;
    return sign * (m > 127 ? 127 : m);
}""")

patch("kernel/mouse.c", "    mx += dx;\n    my += dy;",
      "    mx += mouse_accel_delta(dx);\n    my += mouse_accel_delta(dy);")

patch("kernel/console.c",
      "    bg_color = t->bg;\n    console_clear();",
      "    bg_color = t->bg;\n    console_scrollback_reset();\n    console_clear();")

patch("kernel/console.c",
      "static void console_putc_screen(char c) {\n    if (shell_mode() == MODE_GUI) {\n        gui_term_putc(c);\n        return;\n    }\n    uint32_t cw = fb_cell_w();",
      "static void console_putc_screen(char c) {\n    if (shell_mode() == MODE_GUI) {\n        gui_term_putc(c);\n        return;\n    }\n    console_scrollback_note_char(c);\n    uint32_t cw = fb_cell_w();")

patch("kernel/boot.c",
      "    for (;;) {\n        if (shell_mode() == MODE_CLI)",
      "    for (;;) {\n        keyboard_poll();\n        if (shell_mode() == MODE_CLI)")

patch("kernel/gui/desktop.c",
      "        sound_poll();\n        platform_poll();",
      "        keyboard_poll();\n        sound_poll();\n        platform_poll();")

patch("kernel/shell.c", '#include "console.h"\n#include "fb.h"',
      '#include "console.h"\n#include "console_scroll.h"\n#include "fb.h"')

patch("kernel/shell.c",
      "static uint32_t edit_paint_len; /* last MODE_CLI painted prompt+line width */",
      """static uint32_t edit_paint_len;
static int find_active, find_len, find_hit_line = -1, find_hit_col = -1, find_from_line, find_from_col;
static char find_needle[64];""")

patch("kernel/shell.c",
      '    console_write("  Workspace: /home/dev/workspace  |  ask \\"...\\"  |  gui  |  theme\\n\\n");',
      '    console_write("  Ctrl+F search scrollback  |  gui → desktop (Ctrl+Alt+Esc back to CLI)\\n");\n'
      '    console_write("  Workspace: /home/dev/workspace  |  ask \\"...\\"  |  theme\\n\\n");')

shell_find = open(R / "scripts/pass37_shell_find.c").read()
patch("kernel/shell.c",
      "static void handle_key(int key) {\n    if (!key)\n        return;\n\n    if (key == 1) { /* Ctrl+A — select all */",
      shell_find)

patch("kernel/user/utils_sys.c",
      '        console_write("Already in desktop. Ctrl+Alt+Esc returns to CLI.\\n");',
      '        console_write("Already on desktop — press Ctrl+Alt+Esc to return to CLI.\\n");')
patch("kernel/user/utils_sys.c",
      '    console_write("Entering desktop... (Ctrl+Alt+Esc returns to CLI)\\n");',
      '    console_write("Entering desktop... Press Ctrl+Alt+Esc anytime to return to CLI.\\n");')
patch("kernel/user/utils_sys.c",
      '    console_write("Back in Peak CLI. Type \'help\'.\\n");',
      '    console_write("Back in CLI. Ctrl+Alt+Esc leaves desktop; type \'help\' or \'gui\'.\\n");')

patch("kernel/gui/desktop_login.c",
      '        fb_draw_string(tx, ty, "Esc skips splash", desktop_color_dim(), desktop_color_surface());',
      '        fb_draw_string(tx, ty, "Ctrl+Alt+Esc returns to CLI", desktop_color_dim(), desktop_color_surface());\n'
      '        ty += fb_cell_h() + desktop_u(6);\n'
      '        fb_draw_string(tx, ty, "Esc skips splash", desktop_color_dim(), desktop_color_surface());')

patch("kernel/gui/desktop_settings.c",
      '        fb_draw_string(tx, cy, "Ctrl+Alt+Esc leaves desktop.", desktop_color_dim(), desktop_color_bg());',
      '        fb_draw_string(tx, cy, "Ctrl+Alt+Esc — return to CLI", desktop_color_dim(), desktop_color_bg());')

patch("kernel/gui/desktop_overlays.c",
      '        { "Ctrl+Alt+Esc", "Leave desktop" },',
      '        { "Ctrl+Alt+Esc", "Return to CLI" },')

patch("kernel/shell_builtins.c",
      '    { "gui", "meta", "enter desktop (Ctrl+Alt+Esc leaves)" },',
      '    { "gui", "meta", "enter desktop (Ctrl+Alt+Esc returns to CLI)" },')

patch("docs/CLI.md",
      "`gui` enters the desktop; **Ctrl+Alt+Esc** returns to CLI.",
      "`gui` enters the desktop; press **Ctrl+Alt+Esc** anytime to return to CLI.\n\n"
      "CLI scrollback search: **Ctrl+F**, type a needle, **Enter** for next match (128 lines retained).")

patch("CHANGELOG.md",
      "- CLI line-edit clears trailing glyphs on shrink; `gui` hints use Ctrl+Alt+Esc",
      "- **Pass 37:** Keyboard repeat tuning (PS/2 typematic + software repeat for USB), mouse acceleration lite, CLI scrollback search (Ctrl+F, 128 lines), clearer Ctrl+Alt+Esc CLI↔desktop hints")

patch("tests/host/test_console_scroll.c",
      '           "large scroll bytes stay 64-bit");\n\n    if (fails) {',
      '           "large scroll bytes stay 64-bit");\n\n'
      '    int col = -1;\n'
      '    expect(console_scroll_line_find("hello world", "world", &col) == 1 && col == 6, "line find");\n'
      '    const char *lines[] = { "alpha", "beta gamma", "alphabet soup" };\n'
      '    int ol = -1, oc = -1;\n'
      '    expect(console_scroll_find_next(lines, 3, "gamma", 0, -1, &ol, &oc) == 1 && ol == 1, "find first");\n'
      '    expect(console_scroll_find_next(lines, 3, "missing", 0, -1, &ol, &oc) == 0, "find none");\n\n'
      '    if (fails) {')

print("pass37_apply: ok")
