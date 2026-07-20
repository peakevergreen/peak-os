#include "mouse.h"
#include "fb.h"
#include "idt.h"
#include "pic.h"
#include "util.h"

static int32_t mx, my;
static uint8_t buttons;
static uint8_t left_pressed, left_released;
static uint8_t cycle;
static int8_t packet[3];

static void mouse_wait(int type) {
    int timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (inb(0x64) & 1)
                return;
        }
    } else {
        while (timeout--) {
            if (!(inb(0x64) & 2))
                return;
        }
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, val);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static void mouse_irq(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t data = inb(0x60);

    switch (cycle) {
    case 0:
        if (!(data & 0x08))
            return; /* wait for sync bit */
        packet[0] = (int8_t)data;
        cycle++;
        break;
    case 1:
        packet[1] = (int8_t)data;
        cycle++;
        break;
    case 2: {
        packet[2] = (int8_t)data;
        cycle = 0;

        uint8_t prev = buttons;
        buttons = (uint8_t)(packet[0] & 0x07);
        if ((buttons & 1) && !(prev & 1))
            left_pressed = 1;
        if (!(buttons & 1) && (prev & 1))
            left_released = 1;

        int32_t dx = packet[1];
        int32_t dy = -packet[2];
        if (packet[0] & 0x10)
            dx = (int32_t)(int8_t)packet[1];
        if (packet[0] & 0x20)
            dy = -(int32_t)(int8_t)packet[2];

        struct framebuffer *fb = fb_get();
        mx += dx;
        my += dy;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (fb->width && (uint32_t)mx >= fb->width)
            mx = (int32_t)fb->width - 1;
        if (fb->height && (uint32_t)my >= fb->height)
            my = (int32_t)fb->height - 1;
        break;
    }
    }
}

void mouse_init(void) {
    mx = 100;
    my = 100;
    buttons = 0;
    cycle = 0;
    left_pressed = left_released = 0;

    mouse_wait(1);
    outb(0x64, 0xA8); /* enable aux */
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60);
    status |= 0x02;
    status &= (uint8_t)~0x20;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    mouse_write(0xF6); /* defaults */
    mouse_read();
    mouse_write(0xF4); /* enable */
    mouse_read();

    irq_install(12, mouse_irq);
    pic_unmask(2);  /* cascade */
    pic_unmask(12);
}

void mouse_poll(struct mouse_state *out) {
    out->x = mx;
    out->y = my;
    out->buttons = buttons;
    out->left_pressed = left_pressed;
    out->left_released = left_released;
}

void mouse_clear_clicks(void) {
    left_pressed = 0;
    left_released = 0;
}
