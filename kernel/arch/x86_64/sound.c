#include "sound.h"
#include "timer.h"
#include "util.h"

static int beep_active;
static uint64_t beep_until;
static uint32_t queue_freq[4];
static uint32_t queue_ms[4];
static int queue_len;
static int queue_head;

void sound_init(void) {
    beep_active = 0;
    beep_until = 0;
    queue_len = 0;
    queue_head = 0;
}

static void speaker_off(void) {
    uint8_t tmp = inb(0x61) & (uint8_t)~3;
    outb(0x61, tmp);
}

static void speaker_on(uint32_t freq) {
    if (freq < 20)
        freq = 20;
    if (freq > 20000)
        freq = 20000;
    uint32_t div = 1193180u / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    if ((tmp & 3) != 3)
        outb(0x61, tmp | 3);
}

static void beep_start_next(void) {
    if (queue_len <= 0) {
        beep_active = 0;
        speaker_off();
        return;
    }
    uint32_t freq = queue_freq[queue_head];
    uint32_t ms = queue_ms[queue_head];
    queue_head = (queue_head + 1) % 4;
    queue_len--;
    speaker_on(freq);
    uint64_t wait = (ms + 9) / 10;
    if (wait < 1)
        wait = 1;
    beep_until = timer_ticks() + wait;
    beep_active = 1;
}

void sound_poll(void) {
    if (!beep_active)
        return;
    if (timer_ticks() >= beep_until) {
        speaker_off();
        beep_active = 0;
        beep_start_next();
    }
}

void sound_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (queue_len >= 4)
        return;
    int slot = (queue_head + queue_len) % 4;
    queue_freq[slot] = freq_hz;
    queue_ms[slot] = duration_ms;
    queue_len++;
    if (!beep_active)
        beep_start_next();
}

void sound_ui_click(void) {
    sound_beep(880, 30);
}

void sound_ui_notify(void) {
    sound_beep(660, 40);
    sound_beep(880, 60);
}
