/* Definitive combat: the hit callouts (WEAK! / RESIST! / OVERFINISH!), the big slanted word that pops over the target
 * when a hit lands. Drawn the way the battle draws its own effects: flat PS1 primitives queued on the 3D layer
 * (0x1004) from a layer callback, at the target's projected place, timed by the battle's ticks (so the battle speed
 * and the animation mods carry it). No texture: the letters are slabs (POLY_F4 / POLY_G4), so nothing is uploaded to
 * the VRAM, whose free areas change from battle to battle, and the word stays sharp at any internal scale.
 *
 * The feel (after Persona 3 Reload's callout): the word does not fade in, it is cut in. It starts as a flat, wide
 * streak with a white slash across it, snaps up past its size and settles; holds still at the place of the impact
 * (it does not follow the target); then is squeezed flat along its own slant, the white going first, so the last
 * frames are two streaks of its colour. RESIST! is the dull one: it slams down from too big, shudders and stops.
 * Purely visual: no random number, nothing the battle reads, nothing in a save state (a loaded state has no callout). */
#include "common.h"
#include "object.h"
#include "heap.h"
#include "gfx.h"
#include "fightstg.h"

#include "definitive.h"

#define CALLOUT_LAYER 0x1004 /* the models' layer: draw offset (160, 120), so the screen is -160..159 x -120..119 */
#define GLYPH_H 16
#define GLYPH_GAP 2
#define SLANT_COS 250 /* the word climbs 12 degrees to the right (8.8) */
#define SLANT_SIN 53
#define ITALIC 56 /* the letters lean 0.22 of their height (8.8) */
#define SHADOW_OFS 2
#define TARGET_HEIGHT 2200 /* world units from the target's feet up to its body (about 62 a pixel at the battle's usual distance) */

/* A slab: the quad between two horizontal lines, top edge left..right at y0, bottom edge left..right at y1. */
typedef struct {
    s8 y0, tl, tr, y1, bl, br;
} Slab;

typedef struct {
    char c;
    u8 w, n;
    Slab s[5];
} Glyph;

/* A heavy 16-pixel sans, stroke 4, corners cut by a pixel; the italic and the slant come from the transform. */
static const Glyph glyphs[] = {
    { 'W', 16, 4, { { 0, 0, 4, 16, 2, 6 }, { 0, 6, 10, 16, 2, 6 }, { 0, 6, 10, 16, 10, 14 }, { 0, 12, 16, 16, 10, 14 } } },
    { 'E', 10, 4, { { 0, 0, 4, 16, 0, 4 }, { 0, 4, 10, 4, 4, 10 }, { 6, 4, 9, 10, 4, 9 }, { 12, 4, 10, 16, 4, 10 } } },
    { 'A', 13, 3, { { 0, 4, 8, 16, 0, 4 }, { 0, 5, 9, 16, 9, 13 }, { 9, 3, 10, 12, 2, 11 } } },
    { 'K', 12, 3, { { 0, 0, 4, 16, 0, 4 }, { 0, 8, 12, 9, 3, 7 }, { 7, 4, 8, 16, 8, 12 } } },
    { '!', 5, 2, { { 0, 0, 5, 10, 1, 4 }, { 12, 1, 4, 16, 1, 4 } } },
    { 'R', 12, 5, { { 0, 0, 4, 16, 0, 4 }, { 0, 4, 10, 4, 4, 10 }, { 1, 8, 12, 9, 8, 12 }, { 6, 4, 10, 10, 4, 10 },
                    { 10, 5, 9, 16, 8, 12 } } },
    { 'S', 11, 5, { { 0, 1, 11, 4, 1, 11 }, { 1, 0, 4, 9, 0, 4 }, { 6, 1, 10, 10, 1, 10 }, { 7, 7, 11, 15, 7, 11 },
                    { 12, 0, 10, 16, 0, 10 } } },
    { 'I', 4, 1, { { 0, 0, 4, 16, 0, 4 } } },
    { 'T', 12, 2, { { 0, 0, 12, 4, 0, 12 }, { 4, 4, 8, 16, 4, 8 } } },
    { 'O', 12, 4, { { 1, 0, 4, 15, 0, 4 }, { 1, 8, 12, 15, 8, 12 }, { 0, 1, 11, 4, 1, 11 }, { 12, 1, 11, 16, 1, 11 } } },
    { 'V', 13, 2, { { 0, 0, 4, 16, 4, 8 }, { 0, 9, 13, 16, 5, 9 } } },
    { 'F', 10, 3, { { 0, 0, 4, 16, 0, 4 }, { 0, 4, 10, 4, 4, 10 }, { 6, 4, 9, 10, 4, 9 } } },
    { 'N', 13, 3, { { 0, 0, 4, 16, 0, 4 }, { 0, 9, 13, 16, 9, 13 }, { 0, 0, 5, 16, 8, 13 } } },
    { 'H', 12, 3, { { 0, 0, 4, 16, 0, 4 }, { 0, 8, 12, 16, 8, 12 }, { 6, 4, 8, 10, 4, 8 } } },
    /* the rest of what def_poly_text's callers write (the title's difficulty question) */
    { 'C', 11, 3, { { 0, 1, 11, 4, 1, 11 }, { 1, 0, 4, 15, 0, 4 }, { 12, 1, 11, 16, 1, 11 } } },
    { 'Y', 13, 3, { { 0, 0, 4, 9, 4, 8 }, { 0, 9, 13, 9, 4, 8 }, { 8, 4, 8, 16, 4, 8 } } },
    { 'U', 12, 3, { { 0, 0, 4, 15, 0, 4 }, { 0, 8, 12, 15, 8, 12 }, { 12, 1, 11, 16, 1, 11 } } },
    { 'D', 12, 4, { { 0, 0, 4, 16, 0, 4 }, { 0, 4, 10, 4, 4, 10 }, { 12, 4, 10, 16, 4, 10 }, { 2, 8, 12, 14, 8, 12 } } },
    { 'G', 12, 5, { { 0, 1, 11, 4, 1, 11 }, { 1, 0, 4, 15, 0, 4 }, { 12, 1, 11, 16, 1, 11 }, { 8, 8, 12, 14, 8, 12 },
                    { 7, 6, 12, 10, 6, 12 } } },
    { 'M', 15, 4, { { 0, 0, 4, 16, 0, 4 }, { 0, 11, 15, 16, 11, 15 }, { 0, 4, 7, 10, 6, 9 }, { 0, 8, 11, 10, 6, 9 } } },
    { 'B', 12, 5, { { 0, 0, 4, 16, 0, 4 }, { 0, 4, 10, 4, 4, 10 }, { 6, 4, 10, 10, 4, 10 }, { 12, 4, 10, 16, 4, 10 },
                    { 1, 8, 12, 15, 8, 12 } } },
    { 'L', 10, 2, { { 0, 0, 4, 16, 0, 4 }, { 12, 4, 10, 16, 4, 10 } } },
    { '2', 11, 5, { { 0, 0, 10, 4, 0, 10 }, { 2, 7, 11, 8, 7, 11 }, { 6, 1, 10, 10, 1, 10 }, { 8, 0, 4, 14, 0, 4 },
                    { 12, 0, 11, 16, 0, 11 } } },
    { '3', 11, 4, { { 0, 0, 10, 4, 0, 10 }, { 6, 2, 10, 10, 2, 10 }, { 12, 0, 10, 16, 0, 10 }, { 1, 7, 11, 15, 7, 11 } } },
    { '?', 10, 4, { { 0, 0, 10, 4, 0, 10 }, { 2, 6, 10, 8, 6, 10 }, { 6, 3, 9, 10, 3, 7 }, { 12, 3, 7, 16, 3, 7 } } },
};
#define SPACE_W 6

typedef struct {
    const char *word;
    u8 fill_top[3], fill_bottom[3]; /* the letters' face, top to bottom */
    u8 shadow[3];                   /* the hard drop shadow: the callout's colour */
    u8 outline[3];
    u8 slam;                 /* 0: cut in and pop (the hit went through); 1: slam down and shudder (it did not) */
    s16 size;                /* 8.8 */
    s16 peak;                /* 8.8: how far past its height the pop goes */
    u8 in, hold, out;        /* ticks (1/60 s) */
} Style;

static const Style styles[DEF_CALLOUT_COUNT] = {
    [DEF_CALLOUT_WEAK] = { "WEAK!", { 255, 255, 255 }, { 196, 236, 255 }, { 0, 168, 255 }, { 8, 16, 56 }, 0, 256, 300, 10,
                           32, 8 },
    [DEF_CALLOUT_RESIST] = { "RESIST!", { 236, 236, 244 }, { 168, 172, 200 }, { 92, 100, 156 }, { 16, 16, 40 }, 1, 224,
                             256, 7, 28, 8 },
    [DEF_CALLOUT_OVERFINISH] = { "OVERFINISH!", { 255, 255, 255 }, { 255, 216, 200 }, { 248, 24, 40 }, { 56, 0, 8 }, 0,
                                 256, 330, 12, 44, 9 },
    /* PRD 4 layer A: the Rare Target, shown as its battle starts. */
    [DEF_CALLOUT_RARE] = { "RARE!", { 255, 255, 240 }, { 255, 236, 140 }, { 232, 160, 0 }, { 56, 32, 0 }, 0, 256, 300, 10, 60,
                           10 },
    /* PRD 4 layer H: a DRI simulation's next stage comes in (definitive_dri_screen.c, def_dri_stage_enters). */
    [DEF_CALLOUT_EVOLVE] = { "DIGIVOLVE!", { 255, 255, 255 }, { 200, 255, 220 }, { 24, 200, 96 }, { 0, 48, 16 }, 0, 256, 300,
                             10, 60, 10 },
};

static struct {
    int kind;   /* DEF_CALLOUT_*, 0: none */
    int tick;
    int side;   /* the side hit */
    int placed; /* x, y hold the place of the impact */
    int x, y;
} callout;

/* What the frame being drawn looks like: the word's size along and across its slant (8.8), its slide along it, and
 * which of its parts show. */
typedef struct {
    int sx, sy, slide, shake;
    int flash;  /* the shadow is still white */
    int fill;   /* the face shows */
    int slash;  /* 8.8 length of the white cut across the word, 0: none */
    int slash_level;
} Pose;

static const Glyph *glyph_of(char c) {
    unsigned i;

    for (i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++) {
        if (glyphs[i].c == c) {
            return &glyphs[i];
        }
    }
    return NULL;
}

static int word_width(const char *word) {
    int w = -GLYPH_GAP;

    for (; *word != '\0'; word++) {
        w += glyph_of(*word)->w + GLYPH_GAP;
    }
    return w;
}

static int lerp(int a, int b, int t, int n) {
    return a + (b - a) * t / n;
}

static void pose_at(const Style *st, int tick, Pose *p) {
    int t = tick;

    p->sx = p->sy = 256;
    p->slide = p->shake = 0;
    p->flash = 0;
    p->fill = 1;
    p->slash = p->slash_level = 0;
    if (t < st->in) {
        if (st->slam) {
            /* too big and falling faster and faster into place */
            int k = t * t * 256 / (st->in * st->in);
            p->sx = p->sy = lerp(440, 256, k, 256);
        } else {
            int rise = st->in * 6 / 10; /* the ticks up to the peak */
            if (t < rise) {
                int u = 256 - (rise - t) * (rise - t) * 256 / (rise * rise); /* ease out */
                p->sy = lerp(40, st->peak, u, 256);
                p->sx = lerp(330, 240, u, 256);
            } else {
                p->sy = lerp(st->peak, 256, t - rise, st->in - rise);
                p->sx = lerp(240, 256, t - rise, st->in - rise);
            }
            p->slash = lerp(120, 420, t, st->in);
            p->slash_level = lerp(255, 0, t, st->in);
        }
        p->flash = t < 3;
        return;
    }
    t -= st->in;
    if (t < st->hold) {
        if (st->slam && t < 6) {
            static const s8 shudder[6] = { 3, -3, 2, -2, 1, -1 };
            p->shake = shudder[t];
        }
        return;
    }
    t -= st->hold;
    {
        int k = t * t * 256 / (st->out * st->out); /* ease in: it lingers, then is gone */
        p->sy = lerp(256, 16, k, 256);
        p->sx = lerp(256, 350, k, 256);
        p->slide = lerp(0, 10, k, 256);
        p->fill = t * 2 < st->out;
    }
}

/* A point of the word (pixels from its top left corner) to the layer's screen. */
static void to_screen(const Style *st, const Pose *p, int width, int x, int y, int *out_x, int *out_y) {
    int lx = x * 256 - width * 128;
    int ly = (y - GLYPH_H / 2) * 256;

    lx -= ly * ITALIC / 256;
    lx = lx * p->sx / 256 * st->size / 256 + p->slide * 256;
    ly = ly * p->sy / 256 * st->size / 256;
    *out_x = callout.x + p->shake + ((lx * SLANT_COS + ly * SLANT_SIN + 0x8000) >> 16);
    *out_y = callout.y + ((ly * SLANT_COS - lx * SLANT_SIN + 0x8000) >> 16);
}

/* One pass over the word's slabs, queued at `ot`: flat `top` colour, or top to `bottom` down the letters' height;
 * moved by (dx, dy); `grow` pushes each corner a pixel out (the outline). Returns the next free packet. */
static void *word_pass(void *packet, u32 *ot, const Style *st, const Pose *p, int dx, int dy, int grow, const u8 *top,
                       const u8 *bottom) {
    int width = word_width(st->word);
    int pen = 0;
    const char *c;

    for (c = st->word; *c != '\0'; c++) {
        const Glyph *g = glyph_of(*c);
        int i;

        for (i = 0; i < g->n; i++) {
            const Slab *s = &g->s[i];
            int x[4], y[4], cx, cy, k;

            to_screen(st, p, width, pen + s->tl, s->y0, &x[0], &y[0]);
            to_screen(st, p, width, pen + s->tr, s->y0, &x[1], &y[1]);
            to_screen(st, p, width, pen + s->bl, s->y1, &x[2], &y[2]);
            to_screen(st, p, width, pen + s->br, s->y1, &x[3], &y[3]);
            if (grow) {
                cx = (x[0] + x[1] + x[2] + x[3]) / 4;
                cy = (y[0] + y[1] + y[2] + y[3]) / 4;
                for (k = 0; k < 4; k++) {
                    x[k] += x[k] > cx ? 1 : -1;
                    y[k] += y[k] > cy ? 1 : -1;
                }
            }
            if (bottom != NULL) {
                POLY_G4 *poly = packet;
                u8 c0[3], c1[3];

                for (k = 0; k < 3; k++) {
                    c0[k] = lerp(top[k], bottom[k], s->y0, GLYPH_H);
                    c1[k] = lerp(top[k], bottom[k], s->y1, GLYPH_H);
                }
                setPolyG4(poly);
                setRGB0(poly, c0[0], c0[1], c0[2]);
                setRGB1(poly, c0[0], c0[1], c0[2]);
                setRGB2(poly, c1[0], c1[1], c1[2]);
                setRGB3(poly, c1[0], c1[1], c1[2]);
                setXY4(poly, x[0] + dx, y[0] + dy, x[1] + dx, y[1] + dy, x[2] + dx, y[2] + dy, x[3] + dx, y[3] + dy);
                addPrim(ot, poly);
                packet = poly + 1;
            } else {
                POLY_F4 *poly = packet;

                setPolyF4(poly);
                setRGB0(poly, top[0], top[1], top[2]);
                setXY4(poly, x[0] + dx, y[0] + dy, x[1] + dx, y[1] + dy, x[2] + dx, y[2] + dy, x[3] + dx, y[3] + dy);
                addPrim(ot, poly);
                packet = poly + 1;
            }
        }
        pen += g->w + GLYPH_GAP;
    }
    return packet;
}

/* The white cut across the word as it comes in: a thin additive quad along the slant, `p->slash` of the word long. */
static void *slash_pass(void *packet, u32 *ot, const Style *st, const Pose *p) {
    POLY_F4 *poly = packet;
    DR_TPAGE *tpage = (DR_TPAGE *)(poly + 1);
    Pose flat = *p;
    int width = word_width(st->word);
    int x[4], y[4];
    int v = p->slash_level;

    flat.sx = p->slash;
    flat.sy = 256;
    to_screen(st, &flat, width, 0, GLYPH_H / 2 - 1, &x[0], &y[0]);
    to_screen(st, &flat, width, width, GLYPH_H / 2 - 1, &x[1], &y[1]);
    to_screen(st, &flat, width, 0, GLYPH_H / 2 + 2, &x[2], &y[2]);
    to_screen(st, &flat, width, width, GLYPH_H / 2 + 2, &x[3], &y[3]);
    setPolyF4(poly);
    setSemiTrans(poly, 1);
    setRGB0(poly, v, v, v);
    setXY4(poly, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
    addPrim(ot, poly);
    SetDrawTPage(tpage, 0, 1, getTPage(1, 1, 0, 0)); /* abr 1: the quad adds to what is behind it */
    addPrim(ot, tpage);
    return tpage + 1;
}

/* Where the hit landed: the chest of the side's acting member, kept on the screen and clear of the HUD. */
static void place(GfxLayer *layer, const Style *st) {
    FightstgSlots *slots = (FightstgSlots *)heap_objects.find(0x14, -1, -1);
    FightstgModelParams *params = slots != NULL ? slots->get_params(slots, callout.side != 0 ? 0x10 : 0) : NULL;
    int half = word_width(st->word) * st->size / 512 + 8;

    callout.x = callout.side != 0 ? -60 : 60;
    callout.y = -10;
    if (params != NULL) {
        SVECTOR in, out;

        in.vx = params->pos.x;
        in.vy = params->pos.y - TARGET_HEIGHT;
        in.vz = params->pos.z;
        fightstg_battle.to_screen(layer, &in, &out);
        callout.x = out.vx;
        callout.y = out.vy;
    }
    if (callout.x < -156 + half) {
        callout.x = -156 + half;
    }
    if (callout.x > 156 - half) {
        callout.x = 156 - half;
    }
    if (callout.y < -48) {
        callout.y = -48;
    }
    if (callout.y > 70) {
        callout.y = 70;
    }
    callout.placed = 1;
}

/* The layer's callback: queues this frame of the callout in front of the layer's scene (ordering table entry 0, as
 * the battle's own screen-space effects). A primitive queued later is drawn earlier, so the passes go front to back. */
static void callout_draw(void *data, GfxLayer *layer, s32 arg) {
    static const u8 white[3] = { 255, 255, 255 };
    const Style *st;
    Pose p;
    u32 *ot;
    void *packet;

    (void)data;
    (void)arg;
    if (callout.kind <= 0 || callout.kind >= DEF_CALLOUT_COUNT) {
        return;
    }
    st = &styles[callout.kind];
    if (!callout.placed) {
        place(layer, st);
    }
    pose_at(st, callout.tick, &p);
    ot = layer->get_ot_entry(layer, 0);
    packet = gfx_module.funcs.get_packet();
    if (p.slash != 0) {
        packet = slash_pass(packet, ot, st, &p);
    }
    if (p.fill) {
        packet = word_pass(packet, ot, st, &p, 0, 0, 0, st->fill_top, st->fill_bottom);
    } else {
        /* the face is gone: its place is a second streak of the callout's colour */
        packet = word_pass(packet, ot, st, &p, -SHADOW_OFS, -SHADOW_OFS, 0, st->shadow, NULL);
    }
    packet = word_pass(packet, ot, st, &p, SHADOW_OFS, SHADOW_OFS, 0, p.flash ? white : st->shadow, NULL);
    if (p.fill) {
        packet = word_pass(packet, ot, st, &p, 0, 0, 1, st->outline, NULL);
        packet = word_pass(packet, ot, st, &p, SHADOW_OFS, SHADOW_OFS, 1, st->outline, NULL);
    }
    gfx_module.funcs.set_packet(packet);
}

void def_callout_show(int kind, int side) {
    if (kind <= 0 || kind >= DEF_CALLOUT_COUNT) {
        callout.kind = 0;
        return;
    }
    if (callout.kind == kind && callout.side == side) {
        /* another hit of the same blow: the word stays where it is, its hold starting again */
        const Style *st = &styles[kind];
        if (callout.tick > st->in && callout.tick < st->in + st->hold) {
            callout.tick = st->in;
            return;
        }
    }
    callout.kind = kind;
    callout.side = side;
    callout.tick = 0;
    callout.placed = 0;
}

void def_callout_frame(void) {
    const Style *st;
    GfxLayer *layer;

    if (callout.kind == 0) {
        return;
    }
    st = &styles[callout.kind];
    layer = gfx_module.funcs.get_layer(CALLOUT_LAYER);
    if (layer == NULL || callout.tick >= st->in + st->hold + st->out) {
        callout.kind = 0;
        return;
    }
    layer->append_callback(layer, callout_draw, NULL);
    callout.tick += fightstg_battle.frames;
}

/* Plain text in the callouts' letters, for a screen where the game's font is not there (the title): upright with the
 * letters' lean, left end at x and top at y on `layer`'s ordering table entry `depth`, `size` 8.8 of the 16-pixel
 * letters, one flat colour. Capitals, 2, 3, ! and ? only; another character is a space. Returns the width drawn. As
 * every primitive here, a later call is drawn under an earlier one. */
int def_poly_text(GfxLayer *layer, s32 depth, int x, int y, int size, const char *text, const u8 *rgb) {
    u32 *ot = layer->get_ot_entry(layer, depth);
    POLY_F4 *poly = gfx_module.funcs.get_packet();
    int pen = 0;

    for (; *text != '\0'; text++) {
        const Glyph *g = glyph_of(*text);
        int i, k;

        if (g == NULL) {
            pen += SPACE_W + GLYPH_GAP;
            continue;
        }
        for (i = 0; i < g->n; i++) {
            const Slab *s = &g->s[i];
            const int px[4] = { s->tl, s->tr, s->bl, s->br }, py[4] = { s->y0, s->y0, s->y1, s->y1 };
            short v[8];

            for (k = 0; k < 4; k++) {
                int lx = (pen + px[k]) * 256 + (GLYPH_H - py[k]) * ITALIC; /* the top leans right of the foot */
                v[k * 2] = x + ((lx * size + 0x8000) >> 16);
                v[k * 2 + 1] = y + ((py[k] * size + 0x80) >> 8);
            }
            setPolyF4(poly);
            setRGB0(poly, rgb[0], rgb[1], rgb[2]);
            setXY4(poly, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            addPrim(ot, poly);
            poly++;
        }
        pen += g->w + GLYPH_GAP;
    }
    gfx_module.funcs.set_packet(poly);
    return pen * size >> 8;
}
