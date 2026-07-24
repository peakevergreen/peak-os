#include "font_render.h"

extern const uint8_t font8x16[256][16];

/* Small MRU glyph span cache: geometry only (fg is applied at draw). */
#define FONT_GLYPH_SLOTS 16
#define FONT_INK_SPANS   16 /* font8x16 coalesces to ≤11 spans */

struct ink_span {
    uint8_t row;   /* top glyph row */
    uint8_t start; /* bit column */
    uint8_t run;   /* width in bits */
    uint8_t rows;  /* height in glyph rows (vertical coalesce) */
};

struct glyph_slot {
    char c;
    uint8_t valid;
    uint8_t span_n;
    uint32_t scale;
    struct ink_span spans[FONT_INK_SPANS];
};

static struct glyph_slot glyph_slots[FONT_GLYPH_SLOTS];
static uint8_t glyph_age[FONT_GLYPH_SLOTS]; /* higher = more recently used */
static uint8_t glyph_clock;
static int glyph_cache_live; /* 0 after invalidate */

static uint32_t bg_x, bg_y, bg_color, bg_cw, bg_ch;
static int bg_valid;

void font_render_invalidate(void) {
    glyph_cache_live = 0;
    bg_valid = 0;
}

static struct glyph_slot *glyph_lookup(char c, uint32_t scale) {
    if (!glyph_cache_live)
        return 0;
    for (int i = 0; i < FONT_GLYPH_SLOTS; i++) {
        if (glyph_slots[i].valid && glyph_slots[i].c == c &&
            glyph_slots[i].scale == scale) {
            glyph_age[i] = ++glyph_clock;
            return &glyph_slots[i];
        }
    }
    return 0;
}

static struct glyph_slot *glyph_evict_slot(void) {
    int victim = 0;
    if (!glyph_cache_live) {
        for (int i = 0; i < FONT_GLYPH_SLOTS; i++) {
            glyph_slots[i].valid = 0;
            glyph_slots[i].span_n = 0;
            glyph_age[i] = 0;
        }
        glyph_cache_live = 1;
        glyph_clock = 0;
        return &glyph_slots[0];
    }
    uint8_t oldest = glyph_age[0];
    for (int i = 1; i < FONT_GLYPH_SLOTS; i++) {
        if (glyph_age[i] < oldest) {
            oldest = glyph_age[i];
            victim = i;
        }
    }
    return &glyph_slots[victim];
}

/* Prefer extending an existing span one row above with same start/run. */
static int try_vertical_coalesce(struct glyph_slot *slot, uint8_t row,
                                 uint8_t start, uint8_t run) {
    for (int i = (int)slot->span_n - 1; i >= 0; i--) {
        struct ink_span *s = &slot->spans[i];
        if (s->start == start && s->run == run &&
            (uint8_t)(s->row + s->rows) == row) {
            s->rows++;
            return 1;
        }
        /* Spans are emitted left-to-right per row; once past prior row, stop. */
        if ((uint8_t)(s->row + s->rows) < row)
            break;
    }
    return 0;
}

static struct glyph_slot *rebuild_ink_cache(char c, uint32_t scale) {
    struct glyph_slot *slot = glyph_evict_slot();
    slot->c = c;
    slot->scale = scale;
    slot->span_n = 0;
    slot->valid = 0;
    const uint8_t *glyph = font8x16[(uint8_t)c];
    int truncated = 0;
    for (uint8_t row = 0; row < 16 && !truncated; row++) {
        uint8_t bits = glyph[row];
        uint8_t col = 0;
        while (col < 8) {
            while (col < 8 && !(bits & (0x80 >> col)))
                col++;
            if (col >= 8)
                break;
            uint8_t start = col;
            while (col < 8 && (bits & (0x80 >> col)))
                col++;
            uint8_t run = (uint8_t)(col - start);
            if (try_vertical_coalesce(slot, row, start, run))
                continue;
            if (slot->span_n >= FONT_INK_SPANS) {
                truncated = 1;
                break;
            }
            slot->spans[slot->span_n].row = row;
            slot->spans[slot->span_n].start = start;
            slot->spans[slot->span_n].run = run;
            slot->spans[slot->span_n].rows = 1;
            slot->span_n++;
        }
    }
    if (truncated) {
        /* Leave invalid so the draw path uses the bitmap fallback. */
        slot->span_n = 0;
        slot->valid = 0;
        return 0;
    }
    slot->valid = 1;
    int idx = (int)(slot - glyph_slots);
    glyph_age[idx] = ++glyph_clock;
    return slot;
}

static void draw_spans(const struct glyph_slot *slot, uint32_t x, uint32_t y,
                       uint32_t fg, uint32_t scale, uint32_t cw,
                       font_fill_fn fill) {
    uint32_t gh = 16 * scale;
    for (uint8_t i = 0; i < slot->span_n; i++) {
        const struct ink_span *s = &slot->spans[i];
        uint32_t px = x + (uint32_t)s->start * scale;
        uint32_t py = y + (uint32_t)s->row * scale;
        uint32_t rw = (uint32_t)s->run * scale;
        uint32_t rh = (uint32_t)s->rows * scale;
        if (scale == 1) {
            uint32_t hx = px + 1;
            uint32_t hy = py + 1;
            if (hx + rw <= x + cw && hy + rh <= y + gh)
                fill(hx, hy, rw, rh, 0x00181818);
        }
        fill(px, py, rw, rh, fg);
    }
}

static void draw_glyph_uncached(char c, uint32_t x, uint32_t y, uint32_t fg,
                                uint32_t scale, uint32_t cw, font_fill_fn fill) {
    const uint8_t *glyph = font8x16[(uint8_t)c];
    uint32_t gh = 16 * scale;
    for (uint8_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        uint8_t col = 0;
        while (col < 8) {
            while (col < 8 && !(bits & (0x80 >> col)))
                col++;
            if (col >= 8)
                break;
            uint8_t start = col;
            while (col < 8 && (bits & (0x80 >> col)))
                col++;
            uint32_t px = x + (uint32_t)start * scale;
            uint32_t py = y + (uint32_t)row * scale;
            uint32_t rw = (uint32_t)(col - start) * scale;
            uint32_t rh = scale;
            if (scale == 1) {
                uint32_t hx = px + 1;
                uint32_t hy = py + 1;
                if (hx + rw <= x + cw && hy + rh <= y + gh)
                    fill(hx, hy, rw, rh, 0x00181818);
            }
            fill(px, py, rw, rh, fg);
        }
    }
}

void font_render_cell_bg(uint32_t x, uint32_t y, uint32_t cw, uint32_t ch,
                         uint32_t bg, font_fill_fn fill) {
    if (bg_valid && bg_x == x && bg_y == y && bg_color == bg &&
        bg_cw == cw && bg_ch == ch)
        return;
    fill(x, y, cw, ch, bg);
    bg_x = x;
    bg_y = y;
    bg_color = bg;
    bg_cw = cw;
    bg_ch = ch;
    bg_valid = 1;
}

void font_render_glyph(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t scale,
                       uint32_t cw, uint32_t ch, font_fill_fn fill) {
    (void)ch;
    struct glyph_slot *slot = glyph_lookup(c, scale);
    if (!slot)
        slot = rebuild_ink_cache(c, scale);
    if (!slot) {
        draw_glyph_uncached(c, x, y, fg, scale, cw, fill);
        return;
    }
    draw_spans(slot, x, y, fg, scale, cw, fill);
}
