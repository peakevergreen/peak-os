#include "mouse.h"
#include "fb.h"
#include "irq.h"
#include "util.h"
#if defined(__x86_64__)
#include "pic.h"
#endif

static int32_t mx, my;
static uint8_t buttons;
static uint8_t left_pressed, left_released;
static uint8_t right_pressed, right_released;
static int8_t wheel_accum;
static uint8_t cycle;
#if defined(__x86_64__)
static uint8_t packet[4];
#endif
static int have_wheel;

void mouse_inject(int32_t dx, int32_t dy, uint8_t btns, int8_t wheel);

#if defined(__x86_64__)
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

static void mouse_irq(void) {
    uint8_t data = inb(0x60);
    int pkt_len = have_wheel ? 4 : 3;

    switch (cycle) {
    case 0:
        if ((data & 0x08) == 0 || (data & 0xC0) != 0) {
            cycle = 0;
            return;
        }
        packet[0] = data;
        cycle++;
        break;
    case 1:
        packet[1] = data;
        cycle++;
        break;
    case 2:
        packet[2] = data;
        if (!have_wheel) {
            cycle = 0;
            goto apply;
        }
        cycle++;
        break;
    case 3:
        packet[3] = data;
        cycle = 0;
        goto apply;
    default:
        cycle = 0;
        break;
    }
    return;

apply: {
        int8_t wheel = 0;
        int32_t dx = (int8_t)packet[1];
        int32_t dy = -(int8_t)packet[2];
        if (have_wheel && pkt_len == 4)
            wheel = (int8_t)packet[3];
        mouse_inject(dx, dy, (uint8_t)(packet[0] & 0x07), wheel);
    }
}
#endif
void mouse_inject(int32_t dx, int32_t dy, uint8_t btns, int8_t wheel) {
    struct framebuffer *fb = fb_get();
    uint8_t prev = buttons;
    buttons = btns & 7;
    if ((buttons & 1) && !(prev & 1)) left_pressed = 1;
    if (!(buttons & 1) && (prev & 1)) left_released = 1;
    if ((buttons & 2) && !(prev & 2)) right_pressed = 1;
    if (!(buttons & 2) && (prev & 2)) right_released = 1;
    wheel_accum += wheel;
    mx += dx;
    my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (fb && fb->addr && fb->width && (uint32_t)mx >= fb->width)
        mx = (int32_t)fb->width - 1;
    if (fb && fb->addr && fb->height && (uint32_t)my >= fb->height)
        my = (int32_t)fb->height - 1;
}

void mouse_init(void) {
    mx = 100;
    my = 100;
    buttons = 0;
    cycle = 0;
    left_pressed = left_released = 0;
    right_pressed = right_released = 0;
    wheel_accum = 0;
    have_wheel = 0;

#if defined(__x86_64__)
    mouse_wait(1);
    outb(0x64, 0xA8);
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

    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(100); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(80); mouse_read();
    mouse_write(0xF2);
    mouse_read();
    uint8_t id = mouse_read();
    if (id == 3 || id == 4)
        have_wheel = 1;

    mouse_write(0xF4);
    mouse_read();

    irq_install(12, mouse_irq);
    pic_unmask(2);
    pic_unmask(12);
#else
    have_wheel = 1;
#endif
}

void mouse_poll(struct mouse_state *out) {
    out->x = mx;
    out->y = my;
    out->buttons = buttons;
    out->left_pressed = left_pressed;
    out->left_released = left_released;
    out->right_pressed = right_pressed;
    out->right_released = right_released;
    out->wheel = wheel_accum;
    wheel_accum = 0;
}

void mouse_clear_clicks(void) {
    left_pressed = 0;
    left_released = 0;
    right_pressed = 0;
    right_released = 0;
}

int mouse_buttons_any(void) {
    return buttons || left_pressed || right_pressed || wheel_accum;
}
