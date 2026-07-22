#include "keyboard.h"
#include "irq.h"
#include "util.h"
#include "random.h"
#if defined(__x86_64__)
#include "pic.h"
#endif

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define BUF_SIZE 256

static int ring[BUF_SIZE];
static volatile uint32_t head, tail;
static int shift;
static int ctrl;
static int alt;
static int e0; /* extended scancode prefix */

static void push_key(int k) {
    if (!k)
        return;
    uint32_t next = (head + 1) % BUF_SIZE;
    if (next == tail)
        return;
    ring[head] = k;
    head = next;
}

void keyboard_inject(int key) {
    push_key(key);
}

void keyboard_set_modifiers(int s, int c, int a) {
    shift = s;
    ctrl = c;
    alt = a;
}

#if defined(__x86_64__)
static const char scancode_set1[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' '
};

static const char scancode_set1_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' '
};

static void keyboard_irq(void) {
    uint8_t sc = inb(KBD_DATA);
    random_mix_irq((uint64_t)sc ^ ((uint64_t)head << 8));

    if (sc == 0xE0) {
        e0 = 1;
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift = 1;
        e0 = 0;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift = 0;
        e0 = 0;
        return;
    }
    if (sc == 0x1D) {
        ctrl = 1;
        e0 = 0;
        return;
    }
    if (sc == 0x9D) {
        ctrl = 0;
        e0 = 0;
        return;
    }
    if (sc == 0x38) {
        alt = 1;
        e0 = 0;
        return;
    }
    if (sc == 0xB8) {
        alt = 0;
        e0 = 0;
        return;
    }
    if (sc & 0x80) {
        if (sc == 0xB8)
            alt = 0;
        e0 = 0;
        return; /* key release */
    }

    if (e0) {
        e0 = 0;
        if (sc == 0x38) {
            alt = 1;
            return;
        }
        switch (sc) {
        case 0x4B: push_key(KEY_LEFT); return;
        case 0x4D: push_key(KEY_RIGHT); return;
        case 0x48: push_key(KEY_UP); return;
        case 0x50: push_key(KEY_DOWN); return;
        case 0x47: push_key(KEY_HOME); return;
        case 0x4F: push_key(KEY_END); return;
        case 0x53: push_key(KEY_DELETE); return;
        default: return;
        }
    }

    if (sc == 0x0F) { /* Tab */
        push_key(KEY_TAB);
        return;
    }
    if (sc == 0x3E) { /* F4 */
        push_key(KEY_F4);
        return;
    }

    /* Non-extended nav cluster (some VMs) */
    if (sc == 0x4B) { push_key(KEY_LEFT); return; }
    if (sc == 0x4D) { push_key(KEY_RIGHT); return; }
    if (sc == 0x47) { push_key(KEY_HOME); return; }
    if (sc == 0x4F) { push_key(KEY_END); return; }
    if (sc == 0x53) { push_key(KEY_DELETE); return; }

    char c = 0;
    if (sc < sizeof(scancode_set1))
        c = shift ? scancode_set1_shift[sc] : scancode_set1[sc];
    if (c) {
        if (ctrl && c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 1); /* Ctrl+A = 1 ... */
        else if (ctrl && c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 1);
        push_key((unsigned char)c);
    }
}
#endif /* __x86_64__ */

void keyboard_init(void) {
    head = tail = 0;
    shift = 0;
    ctrl = 0;
    alt = 0;
    e0 = 0;
#if defined(__x86_64__)
    irq_install(1, keyboard_irq);
    pic_unmask(1);
#endif
}

int keyboard_ctrl_down(void) {
    return ctrl;
}

int keyboard_shift_down(void) {
    return shift;
}

int keyboard_alt_down(void) {
    return alt;
}

int keyboard_has_char(void) {
    return head != tail;
}

int keyboard_try_getkey(void) {
    if (head == tail)
        return 0;
    int k = ring[tail];
    tail = (tail + 1) % BUF_SIZE;
    return k;
}

char keyboard_try_getchar(void) {
    if (head == tail)
        return 0;
    int k = ring[tail];
    if (k >= 256)
        return 0; /* leave special for try_getkey */
    tail = (tail + 1) % BUF_SIZE;
    return (char)k;
}

char keyboard_getchar(void) {
    for (;;) {
        int k = keyboard_try_getkey();
        if (k > 0 && k < 128)
            return (char)k;
        if (!k)
            hlt();
    }
}
