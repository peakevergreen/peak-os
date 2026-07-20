#include "keyboard.h"
#include "idt.h"
#include "pic.h"
#include "util.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define BUF_SIZE 256

static char ring[BUF_SIZE];
static volatile uint32_t head, tail;
static int shift;

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

static void push_char(char c) {
    uint32_t next = (head + 1) % BUF_SIZE;
    if (next == tail)
        return;
    ring[head] = c;
    head = next;
}

static void keyboard_irq(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0x2A || sc == 0x36) {
        shift = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift = 0;
        return;
    }
    if (sc & 0x80)
        return; /* key release */

    char c = 0;
    if (sc < sizeof(scancode_set1))
        c = shift ? scancode_set1_shift[sc] : scancode_set1[sc];
    if (c)
        push_char(c);
}

void keyboard_init(void) {
    head = tail = 0;
    shift = 0;
    irq_install(1, keyboard_irq);
    pic_unmask(1);
}

int keyboard_has_char(void) {
    return head != tail;
}

char keyboard_try_getchar(void) {
    if (head == tail)
        return 0;
    char c = ring[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}

char keyboard_getchar(void) {
    while (!keyboard_has_char())
        __asm__ volatile ("hlt");
    return keyboard_try_getchar();
}
