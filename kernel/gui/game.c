#include "game.h"
#include "fb.h"
#include "theme.h"
#include "util.h"

/*
 * Peak Runner — move with A/D, jump, collect loot into your pack.
 * Steeper ridges, wild animals. Integer math only.
 */

#define WORLD_CELL   16
#define PEAK_SPAN    52
#define HEIGHT_BASE  58
#define HEIGHT_AMP   78
#define GRAVITY      1
#define JUMP_VEL     14
#define MOVE_SPEED   4
#define MAX_LOOT     14
#define MAX_ANIMALS  10
#define MOVE_LATCH   8

enum item_kind {
    ITEM_BERRY = 0,
    ITEM_CRYSTAL,
    ITEM_PINECONE,
    ITEM_FEATHER,
    ITEM_COIN,
    ITEM_COUNT
};

enum animal_kind {
    AN_RABBIT = 0,
    AN_DEER,
    AN_FOX,
    AN_BIRD,
    AN_COUNT
};

struct loot {
    int alive;
    int32_t x;
    enum item_kind kind;
};

struct animal {
    int alive;
    int32_t x;
    int y_off;
    enum animal_kind kind;
    int dir;
    int spook;
};

static int32_t cam_x;
static int32_t player_x;
static int32_t player_y;
static int32_t player_vy;
static int facing;
static int grounded;
static int jump_queued;
static int left_latch, right_latch;
static uint64_t distance;
static int score;
static int needs_redraw;
static uint32_t view_w;
static int32_t spawned_to;

static int pack[ITEM_COUNT];
static struct loot loot[MAX_LOOT];
static struct animal animals[MAX_ANIMALS];

static const char *item_name(enum item_kind k) {
    static const char *n[ITEM_COUNT] = {
        "berry", "crystal", "pine", "feather", "coin"
    };
    if (k < 0 || k >= ITEM_COUNT)
        return "?";
    return n[k];
}

static uint32_t hash_u(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static int ridge_h(int peak_i) {
    if (peak_i < 0)
        peak_i = -peak_i;
    uint32_t r = hash_u((uint32_t)peak_i * 0x9E3779B9u);
    int rise = (int)(r % (uint32_t)(HEIGHT_AMP + 1));
    if ((r & 7) == 0)
        rise = HEIGHT_AMP;
    else if ((r & 3) == 0)
        rise = (rise * 5) / 6;
    else
        rise = (rise * 3) / 4;
    return HEIGHT_BASE + rise;
}

static int smoothstep_u(int t, int max) {
    if (t <= 0)
        return 0;
    if (t >= max)
        return max;
    int s = (t * 256) / max;
    int s2 = (s * s * (3 * 256 - 2 * s)) / (256 * 256);
    return (s2 * max) / 256;
}

static int lerp(int a, int b, int t, int max) {
    return a + ((b - a) * t) / max;
}

static int terrain_h(int32_t wx) {
    if (wx < 0)
        wx = 0;
    int cell = (int)(wx / WORLD_CELL);
    int peak0 = cell / PEAK_SPAN;
    int local = cell - peak0 * PEAK_SPAN;
    int t = smoothstep_u(local, PEAK_SPAN);
    return lerp(ridge_h(peak0), ridge_h(peak0 + 1), t, PEAK_SPAN);
}

static uint32_t height_to_px(int ht, uint32_t range) {
    int rel = ht - (HEIGHT_BASE - 10);
    if (rel < 0)
        rel = 0;
    int max_rel = HEIGHT_AMP + 20;
    if (rel > max_rel)
        rel = max_rel;
    return (uint32_t)((rel * (int)range) / max_rel);
}

static void try_add_loot(int32_t wx, enum item_kind kind) {
    for (int i = 0; i < MAX_LOOT; i++) {
        if (loot[i].alive)
            continue;
        loot[i].alive = 1;
        loot[i].x = wx;
        loot[i].kind = kind;
        return;
    }
}

static void try_add_animal(int32_t wx, enum animal_kind kind, int dir) {
    for (int i = 0; i < MAX_ANIMALS; i++) {
        if (animals[i].alive)
            continue;
        animals[i].alive = 1;
        animals[i].x = wx;
        animals[i].kind = kind;
        animals[i].dir = dir ? dir : 1;
        animals[i].y_off = 0;
        animals[i].spook = 0;
        return;
    }
}

static void spawn_ahead(void) {
    int32_t want = player_x + 900;
    while (spawned_to < want) {
        int32_t wx = spawned_to + 40;
        uint32_t r = hash_u((uint32_t)wx ^ 0xA5A5u);
        spawned_to += 70 + (int32_t)(r % 90);

        if ((r & 3) != 3)
            try_add_loot(wx, (enum item_kind)(r % ITEM_COUNT));
        if ((r & 7) < 3)
            try_add_animal(wx + 30, (enum animal_kind)((r >> 8) % AN_COUNT),
                           (r & 0x100) ? 1 : -1);
    }
}

/* --- Input / simulation --- */

void game_reset(void) {
    cam_x = 0;
    player_x = 80;
    player_y = terrain_h(player_x);
    player_vy = 0;
    facing = 1;
    grounded = 1;
    jump_queued = 0;
    left_latch = right_latch = 0;
    distance = 0;
    score = 0;
    view_w = 0;
    spawned_to = 0;
    memset(pack, 0, sizeof(pack));
    memset(loot, 0, sizeof(loot));
    memset(animals, 0, sizeof(animals));
    spawn_ahead();
    needs_redraw = 1;
}

void game_input(char c) {
    if (c == 'a' || c == 'A') {
        left_latch = MOVE_LATCH;
        facing = -1;
    } else if (c == 'd' || c == 'D') {
        right_latch = MOVE_LATCH;
        facing = 1;
    } else if (c == ' ' || c == 'w' || c == 'W' || c == 'j' || c == 'J') {
        if (grounded)
            jump_queued = 1;
    } else if (c == 'r' || c == 'R') {
        game_reset();
    }
    needs_redraw = 1;
}

static void collect_nearby(void) {
    for (int i = 0; i < MAX_LOOT; i++) {
        if (!loot[i].alive)
            continue;
        int32_t dx = loot[i].x - player_x;
        if (dx < 0)
            dx = -dx;
        if (dx < 18) {
            pack[loot[i].kind]++;
            score += 10 + (int)loot[i].kind * 5;
            loot[i].alive = 0;
        }
    }
}

static void update_animals(void) {
    for (int i = 0; i < MAX_ANIMALS; i++) {
        struct animal *a = &animals[i];
        if (!a->alive)
            continue;

        int32_t dx = a->x - player_x;
        int32_t adx = dx < 0 ? -dx : dx;

        /* Spook when player close */
        if (adx < 70) {
            a->spook = 20;
            if (dx > 0)
                a->dir = 1;
            else
                a->dir = -1;
            /* Fox bumps you slightly */
            if (a->kind == AN_FOX && adx < 16 && grounded) {
                player_x -= facing * 6;
                if (player_x < 0)
                    player_x = 0;
            }
            /* Bird drops a feather sometimes */
            if (a->kind == AN_BIRD && a->spook == 20 && (hash_u((uint32_t)a->x) & 3) == 0)
                try_add_loot(a->x, ITEM_FEATHER);
        }

        int spd = 2;
        if (a->spook) {
            spd = 5;
            a->spook--;
        }
        if (a->kind == AN_BIRD) {
            spd = 3 + (a->spook ? 2 : 0);
            a->y_off = ((int)(a->x / 8) & 1) ? 8 : 0;
        } else if (a->kind == AN_DEER) {
            spd = 3 + (a->spook ? 3 : 0);
        } else if (a->kind == AN_RABBIT) {
            spd = 2 + (a->spook ? 4 : 0);
            a->y_off = ((int)(a->x / 5) & 1) ? 4 : 0;
        }

        a->x += a->dir * spd;
        if (a->x < cam_x - 40 || a->x > player_x + 1200)
            a->alive = 0;
    }
}

void game_tick(void) {
    if (left_latch > 0) {
        player_x -= MOVE_SPEED;
        left_latch--;
        facing = -1;
    }
    if (right_latch > 0) {
        player_x += MOVE_SPEED;
        right_latch--;
        facing = 1;
    }
    if (player_x < 0)
        player_x = 0;

    distance = (uint64_t)(player_x / WORLD_CELL);
    spawn_ahead();

    int ground = terrain_h(player_x);

    if (jump_queued && grounded) {
        player_vy = JUMP_VEL;
        grounded = 0;
        jump_queued = 0;
    }

    player_vy -= GRAVITY;
    player_y += player_vy;

    if (player_y <= ground) {
        player_y = ground;
        player_vy = 0;
        grounded = 1;
    } else {
        grounded = 0;
    }

    collect_nearby();
    update_animals();

    /* Camera follows player — keep them ~1/3 from left */
    if (view_w > 0) {
        int32_t target = player_x - (int32_t)(view_w / 3);
        if (target < 0)
            target = 0;
        if (cam_x < target)
            cam_x += (target - cam_x + 1) / 2;
        else if (cam_x > target + 8)
            cam_x -= (cam_x - target) / 2;
    }

    needs_redraw = 1;
}

uint64_t game_distance(void) {
    return distance;
}

int game_wants_redraw(void) {
    return needs_redraw;
}

void game_clear_redraw(void) {
    needs_redraw = 0;
}

/* --- Rendering --- */

static uint32_t blend(uint32_t a, uint32_t b, int u256) {
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return fb_rgb((uint8_t)(ar + ((br - ar) * u256) / 256),
                  (uint8_t)(ag + ((bg - ag) * u256) / 256),
                  (uint8_t)(ab + ((bb - ab) * u256) / 256));
}

static void draw_hills(uint32_t x, uint32_t y_min, uint32_t base_y, uint32_t w,
                       uint32_t range, int32_t scroll, int step, int amp256,
                       uint32_t color, int snow) {
    for (uint32_t col = 0; col < w; col += (uint32_t)step) {
        int ht = terrain_h(scroll + (int32_t)col);
        uint32_t gh = (height_to_px(ht, range) * (uint32_t)amp256) / 256;
        if (gh < 3)
            gh = 3;
        uint32_t gy = base_y - gh;
        if (gy < y_min)
            gy = y_min;
        uint32_t cw = (uint32_t)step;
        if (col + cw > w)
            cw = w - col;
        fb_fill_rect(x + col, gy, cw, base_y - gy, color);
        if (snow && ht >= HEIGHT_BASE + HEIGHT_AMP - 6 && gh > 12)
            fb_fill_rect(x + col, gy, cw, 2, theme_get()->fg);
    }
}

static int32_t world_to_sx(int32_t wx, uint32_t origin_x) {
    return (int32_t)origin_x + (wx - cam_x);
}

static void draw_loot(uint32_t ox, uint32_t y, uint32_t base_y, uint32_t near_range) {
    const struct peak_theme *t = theme_get();
    for (int i = 0; i < MAX_LOOT; i++) {
        if (!loot[i].alive)
            continue;
        int32_t sx = world_to_sx(loot[i].x, ox);
        if (sx < (int32_t)ox - 10 || sx > (int32_t)(ox + view_w) + 10)
            continue;
        uint32_t lift = height_to_px(terrain_h(loot[i].x), near_range);
        uint32_t foot = base_y - lift - 4;
        uint32_t col;
        switch (loot[i].kind) {
        case ITEM_BERRY: col = t->danger; break;
        case ITEM_CRYSTAL: col = t->accent; break;
        case ITEM_PINECONE: col = t->dim; break;
        case ITEM_FEATHER: col = t->fg; break;
        default: col = t->cursor; break;
        }
        fb_fill_rect((uint32_t)sx, foot - 6, 5, 5, col);
        fb_fill_rect((uint32_t)sx + 1, foot - 8, 3, 2, t->fg);
        (void)y;
    }
}

static void draw_animals(uint32_t ox, uint32_t y, uint32_t base_y, uint32_t near_range) {
    const struct peak_theme *t = theme_get();
    for (int i = 0; i < MAX_ANIMALS; i++) {
        struct animal *a = &animals[i];
        if (!a->alive)
            continue;
        int32_t sx = world_to_sx(a->x, ox);
        if (sx < (int32_t)ox - 20 || sx > (int32_t)(ox + view_w) + 20)
            continue;
        uint32_t lift = height_to_px(terrain_h(a->x), near_range);
        uint32_t foot = base_y - lift - (uint32_t)a->y_off;
        if (foot < y + 16)
            foot = y + 16;

        if (a->kind == AN_BIRD) {
            fb_fill_rect((uint32_t)sx, foot - 14, 10, 4, t->fg);
            fb_fill_rect((uint32_t)sx + 2, foot - 18, 6, 4, t->dim);
        } else if (a->kind == AN_DEER) {
            fb_fill_rect((uint32_t)sx, foot - 16, 14, 8, t->dim);
            fb_fill_rect((uint32_t)sx + 10, foot - 22, 6, 6, t->fg);
            fb_fill_rect((uint32_t)sx + 12, foot - 28, 2, 6, t->fg); /* antler */
        } else if (a->kind == AN_FOX) {
            fb_fill_rect((uint32_t)sx, foot - 12, 12, 7, t->danger);
            fb_fill_rect((uint32_t)sx + (a->dir > 0 ? 8 : 0), foot - 16, 5, 5, t->danger);
        } else { /* rabbit */
            fb_fill_rect((uint32_t)sx, foot - 8, 7, 6, t->fg);
            fb_fill_rect((uint32_t)sx + 2, foot - 14, 2, 6, t->fg);
            fb_fill_rect((uint32_t)sx + 5, foot - 14, 2, 6, t->fg);
        }
    }
}

static void draw_runner(uint32_t ox, uint32_t y, uint32_t base_y, uint32_t near_range) {
    int32_t sx = world_to_sx(player_x, ox);
    uint32_t lift = height_to_px(player_y, near_range);
    uint32_t foot = base_y - lift;
    if (foot < y + 24)
        foot = y + 24;

    uint32_t s = fb_ui_scale();
    uint32_t rw = 8 + s * 2;
    uint32_t rh = 12 + s * 2;
    uint32_t rx = (uint32_t)sx;
    uint32_t ry = foot - rh;
    if (ry < y)
        ry = y;

    const struct peak_theme *t = theme_get();
    if (grounded) {
        int stride = ((int)(player_x / 6) & 1);
        uint32_t leg_y = ry + rh - 2;
        fb_fill_rect(rx + 1, leg_y, 3, stride ? 5 : 3, t->dim);
        fb_fill_rect(rx + rw - 5, leg_y, 3, stride ? 3 : 5, t->dim);
    }
    fb_fill_rect(rx + 1, ry + 5, rw - 2, rh - 7, t->accent);
    fb_fill_rect(rx + 2, ry, rw - 4, 6, t->fg);
    /* face direction marker */
    if (facing > 0)
        fb_fill_rect(rx + rw - 3, ry + 2, 2, 2, t->cursor);
    else
        fb_fill_rect(rx + 1, ry + 2, 2, 2, t->cursor);
}

static void draw_pack_hud(uint32_t x, uint32_t y, const struct peak_theme *t) {
    char line[64];
    uint32_t ch = fb_cell_h();
    snprintf(line, sizeof(line), "Dist %lu  Score %d", distance, score);
    fb_draw_string(x + 8, y + 4, line, t->fg, t->bg);

    snprintf(line, sizeof(line), "A/D move  W/Space jump  R reset");
    fb_draw_string(x + 8, y + 4 + ch, line, t->dim, t->bg);

    /* Pack row */
    size_t o = 0;
    char packline[80];
    o += (size_t)snprintf(packline + o, sizeof(packline) - o, "Pack:");
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (pack[i] <= 0)
            continue;
        o += (size_t)snprintf(packline + o, sizeof(packline) - o, " %s×%d",
                              item_name((enum item_kind)i), pack[i]);
        if (o >= sizeof(packline) - 1)
            break;
    }
    if (o <= 5)
        snprintf(packline, sizeof(packline), "Pack: (empty)");
    fb_draw_string(x + 8, y + 4 + ch * 2, packline, t->accent, t->bg);
}

void game_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w < 48 || h < 48)
        return;

    view_w = w;
    const struct peak_theme *t = theme_get();

    uint32_t sky_h = (h * 52) / 100;
    fb_fill_rect(x, y, w, sky_h, t->bg);
    fb_fill_rect(x, y + sky_h, w, h - sky_h, blend(t->bg, t->surface, 70));

    uint32_t base_y = y + h;
    uint32_t far_range = (h * 22) / 100;
    uint32_t near_range = (h * 40) / 100;
    if (far_range < 12)
        far_range = 12;
    if (near_range < 22)
        near_range = 22;

    draw_hills(x, y, base_y, w, far_range, cam_x / 2, 4, 170,
               blend(t->title, t->bg, 45), 0);
    draw_hills(x, y, base_y, w, near_range, cam_x, 2, 256,
               blend(t->accent, t->dim, 95), 1);

    draw_loot(x, y, base_y, near_range);
    draw_animals(x, y, base_y, near_range);
    draw_runner(x, y, base_y, near_range);
    draw_pack_hud(x, y, t);

    needs_redraw = 0;
}
