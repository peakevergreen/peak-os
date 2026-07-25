static void find_reprint_edit(void) {
    console_putc('\r'); build_prompt(); console_write(prompt_buf); console_write(line);
    edit_paint_len = (uint32_t)strlen(prompt_buf) + line_len;
}
static void find_show_hit(void) {
    const char *hit = console_scrollback_line(find_hit_line);
    if (!hit) return;
    console_putc('\n');
    console_printf("  scrollback[%d]: %s\n", find_hit_line + 1, hit);
    find_reprint_edit();
}
static void find_close(void) {
    find_active = 0; find_len = 0; find_needle[0] = '\0';
    find_hit_line = find_hit_col = -1;
    find_reprint_edit();
}
static void find_next(void) {
    if (find_len <= 0) return;
    find_needle[find_len] = '\0';
    int li = find_hit_line, co = find_hit_col;
    if (li < 0) { li = find_from_line; co = find_from_col; }
    if (console_scrollback_find(find_needle, &find_hit_line, &find_hit_col, li, co) ||
        console_scrollback_find(find_needle, &find_hit_line, &find_hit_col, 0, -1))
        find_show_hit();
    else {
        console_putc('\n');
        console_printf("  scrollback: no match for \"%s\"\n", find_needle);
        find_reprint_edit();
    }
}

static void handle_key(int key) {
    if (!key) return;
    if (find_active) {
        if (key == 27) { find_close(); return; }
        if (key == '\n' || key == KEY_TAB) { find_next(); return; }
        if (key == '\b' || key == 127) {
            if (find_len > 0) find_needle[--find_len] = '\0';
            find_hit_line = find_hit_col = -1;
            console_putc('\r'); build_prompt(); console_write(prompt_buf);
            console_write("Find: "); console_write(find_needle); console_putc(' ');
            return;
        }
        if (key >= 32 && key < 127 && find_len + 1 < (int)sizeof(find_needle)) {
            find_needle[find_len++] = (char)key; find_needle[find_len] = '\0';
            find_hit_line = find_hit_col = -1; console_putc((char)key);
            return;
        }
        return;
    }
    if (key == 6 || (keyboard_ctrl_down() && (key == 'f' || key == 'F'))) {
        find_active = 1; find_len = 0; find_needle[0] = '\0';
        find_hit_line = find_hit_col = -1;
        find_from_line = console_scrollback_line_count() > 0 ? console_scrollback_line_count() - 1 : 0;
        find_from_col = -1;
        console_putc('\n'); build_prompt(); console_write(prompt_buf); console_write("Find: ");
        edit_paint_len = (uint32_t)strlen(prompt_buf) + 6;
        return;
    }

    if (key == 1) { /* Ctrl+A — select all */
