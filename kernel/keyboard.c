#include "keyboard.h"
#include "irq.h"
#include "timer.h"
#include "util.h"
#include "random.h"
#if defined(__x86_64__)
#include "pic.h"
#endif

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define BUF_SIZE 256
#define KBD_PS2_TYPEMATIC 0x00

static int ring[BUF_SIZE];
static volatile uint32_t head, tail;
static int shift, ctrl, alt, e0;
static int repeat_key;
static uint32_t repeat_delay_ticks = 25;
static uint32_t repeat_rate_ticks = 3;
static uint64_t repeat_next_tick;
#if defined(__x86_64__) && !defined(PEAK_HOST_TEST)
static uint16_t ps2_held_id;
#endif

static int key_repeatable(int k) {
    if (k == KEY_TAB || k == KEY_F4) return 0;
    if (k >= KEY_LEFT && k <= KEY_DELETE) return 1;
    if (k >= 32 && k < 127) return 1;
    return 0;
}

static void push_key(int k) {
    if (!k) return;
    uint32_t next = (head + 1) % BUF_SIZE;
    if (next == tail) return;
    ring[head] = k;
    head = next;
}

void keyboard_set_repeat(uint32_t delay_ticks, uint32_t rate_ticks) {
    if (delay_ticks < 5) delay_ticks = 5;
    if (rate_ticks < 1) rate_ticks = 1;
    repeat_delay_ticks = delay_ticks;
    repeat_rate_ticks = rate_ticks;
}

void keyboard_repeat_key_down(int key) {
    if (!key_repeatable(key)) return;
    repeat_key = key;
    repeat_next_tick = timer_ticks() + repeat_delay_ticks;
}

void keyboard_repeat_key_up(int key) {
    /* Clear on any key-up: remapped peak keys (Shift+letter) may not match
     * the stored repeat_key after modifiers change, which stuck autorepeat. */
    (void)key;
    repeat_key = 0;
}

void keyboard_repeat_clear(void) {
    repeat_key = 0;
}

void keyboard_poll(void) {
    if (!repeat_key) return;
    uint64_t now = timer_ticks();
    if (now < repeat_next_tick) return;
    push_key(repeat_key);
    repeat_next_tick = now + repeat_rate_ticks;
}

void keyboard_inject(int key) { push_key(key); }

void keyboard_set_modifiers(int s, int c, int a) {
    shift = s; ctrl = c; alt = a;
}

#if defined(__x86_64__) && !defined(PEAK_HOST_TEST)
static int kbd_wait_read(void) {
    for (int t = 100000; t; t--)
        if (inb(KBD_STATUS) & 1) return inb(KBD_DATA);
    return 0;
}

static void kbd_write_cmd(uint8_t val) {
    for (int t = 100000; t; t--)
        if (!(inb(KBD_STATUS) & 2)) break;
    outb(KBD_DATA, val);
}

static void kbd_set_typematic(uint8_t rate) {
    kbd_write_cmd(0xF3); (void)kbd_wait_read();
    kbd_write_cmd(rate); (void)kbd_wait_read();
}

static const char scancode_set1[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};
static const char scancode_set1_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static int scancode_to_peak(uint8_t sc, int extended) {
    if (extended) {
        switch (sc) {
        case 0x4B: return KEY_LEFT; case 0x4D: return KEY_RIGHT;
        case 0x48: return KEY_UP; case 0x50: return KEY_DOWN;
        case 0x47: return KEY_HOME; case 0x4F: return KEY_END;
        case 0x53: return KEY_DELETE; default: return 0;
        }
    }
    if (sc == 0x0F) return KEY_TAB;
    if (sc == 0x3E) return KEY_F4;
    if (sc == 0x4B) return KEY_LEFT;
    if (sc == 0x4D) return KEY_RIGHT;
    if (sc == 0x47) return KEY_HOME;
    if (sc == 0x4F) return KEY_END;
    if (sc == 0x53) return KEY_DELETE;
    if (sc >= sizeof(scancode_set1)) return 0;
    char c = shift ? scancode_set1_shift[sc] : scancode_set1[sc];
    if (!c) return 0;
    if (ctrl && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
    else if (ctrl && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
    return (unsigned char)c;
}

static void keyboard_irq(void) {
    uint8_t sc = inb(KBD_DATA);
    random_mix_irq((uint64_t)sc ^ ((uint64_t)head << 8));
    if (sc == 0xE0) { e0 = 1; return; }
    if (sc == 0x2A || sc == 0x36) { shift = 1; e0 = 0; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; e0 = 0; return; }
    if (sc == 0x1D) { ctrl = 1; e0 = 0; return; }
    if (sc == 0x9D) { ctrl = 0; e0 = 0; return; }
    if (sc == 0x38) { alt = 1; e0 = 0; return; }
    if (sc == 0xB8) { alt = 0; e0 = 0; return; }
    int release = (sc & 0x80) != 0;
    uint8_t code = release ? (uint8_t)(sc & 0x7F) : sc;
    int extended = e0; e0 = 0;
    uint16_t id = (uint16_t)((extended ? 0x100 : 0) | code);
    if (release) {
        if (ps2_held_id == id) ps2_held_id = 0;
        int pk = scancode_to_peak(code, extended);
        if (pk) keyboard_repeat_key_up(pk);
        return;
    }
    if (ps2_held_id == id) return;
    ps2_held_id = id;
    if (extended && code == 0x38) { alt = 1; return; }
    int pk = scancode_to_peak(code, extended);
    if (!pk) return;
    push_key(pk);
    keyboard_repeat_key_down(pk);
}
#endif

void keyboard_init(void) {
    head = tail = 0; shift = ctrl = alt = e0 = 0; repeat_key = 0;
#if defined(__x86_64__) && !defined(PEAK_HOST_TEST)
    ps2_held_id = 0;
    kbd_set_typematic(KBD_PS2_TYPEMATIC);
    irq_install(1, keyboard_irq);
    pic_unmask(1);
#endif
}

int keyboard_ctrl_down(void) { return ctrl; }
int keyboard_shift_down(void) { return shift; }
int keyboard_alt_down(void) { return alt; }
int keyboard_has_char(void) { return head != tail; }

int keyboard_try_getkey(void) {
    if (head == tail) return 0;
    int k = ring[tail];
    tail = (tail + 1) % BUF_SIZE;
    return k;
}

char keyboard_try_getchar(void) {
    if (head == tail) return 0;
    int k = ring[tail];
    if (k >= 256) return 0;
    tail = (tail + 1) % BUF_SIZE;
    return (char)k;
}

char keyboard_getchar(void) {
    for (;;) {
        int k = keyboard_try_getkey();
        if (k > 0 && k < 128) return (char)k;
        if (!k) hlt();
    }
}
