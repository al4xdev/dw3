/* What a textured span costs on the handheld, piece by piece: the same 4-bit CLUT fetch the rasteriser does, run over
 * spans shaped like a battle's (about 14 pixels), with and without the memory traffic. Build with the port's flags and
 * run on the console; it prints ns per pixel for each variant. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VRAM_W 1024
#define VRAM_H 512
#define SPANS 10000
#define LEN 14

static uint16_t vram[VRAM_W * VRAM_H];
static uint16_t dst[VRAM_W * VRAM_H];

static long long now_ns(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* A 4-bit fetch exactly as gpu_fetch does it for depth 1. */
static inline uint16_t fetch4(const uint16_t *row, const uint16_t *clut, int tex_x, int clut_x, int u) {
    uint16_t px = row[(tex_x + (u >> 2)) & 1023];

    return clut[clut_x + ((px >> ((u & 3) * 4)) & 0xF)];
}

int main(int argc, char **argv) {
    const int reps = argc > 1 ? atoi(argv[1]) : 30;
    const uint16_t set_mask = 0;
    uint32_t seed = 12345;
    long long t0, t1;
    int r;

    for (int i = 0; i < VRAM_W * VRAM_H; i++) {
        vram[i] = (uint16_t)(i * 2654435761u);
        dst[i] = 0x1234;
    }

    /* Variant 1: fetch, store: what a GPU_COPY span does. */
    {
        long long ns_total = 0;
        long long px_total = 0;

        for (r = 0; r < reps; r++) {
            const uint16_t *clut = &vram[(r * 37 & 255) * VRAM_W];
            t0 = now_ns();
            for (int s = 0; s < SPANS; s++) {
                int y = (s * 7 + r) & 239;
                int xs = (s * 13 + r) & 300;
                int tex_x = (s * 3) & 255, clut_x = (s * 16) & 240;
                int tex_y = 256 + ((s * 5) & 127);
                int u0 = (s * 11) & 255, v0 = (s * 17) & 255;
                int u = u0, v = v0;
                uint16_t *out = &dst[y * VRAM_W + xs];

                for (int i = 0; i < LEN; i++) {
                    const uint16_t *row = &vram[((tex_y + v) & 511) * VRAM_W];
                    uint16_t t = fetch4(row, clut, tex_x, clut_x, u);

                    out[i] = (uint16_t)(t | set_mask);
                    u += 1;
                    v += 1;
                }
            }
            t1 = now_ns();
            ns_total += t1 - t0;
            px_total += (long long)SPANS * LEN;
        }
        printf("fetch + store      %6.1f ns/px  (%d spans of %d, %d reps)\n", (double)ns_total / px_total, SPANS, LEN,
               reps);
    }

    /* Variant 2: the same loop with the fetch removed: the address and store cost alone. */
    {
        long long ns_total = 0;
        long long px_total = 0;

        for (r = 0; r < reps; r++) {
            t0 = now_ns();
            for (int s = 0; s < SPANS; s++) {
                int y = (s * 7 + r) & 239;
                int xs = (s * 13 + r) & 300;
                int u0 = (s * 11) & 255, v0 = (s * 17) & 255;
                int u = u0, v = v0;
                uint16_t *out = &dst[y * VRAM_W + xs];

                for (int i = 0; i < LEN; i++) {
                    out[i] = (uint16_t)(u + v);
                    u += 1;
                    v += 1;
                }
            }
            t1 = now_ns();
            ns_total += t1 - t0;
            px_total += (long long)SPANS * LEN;
        }
        printf("store only         %6.1f ns/px\n", (double)ns_total / px_total);
    }

    /* Variant 3: fetch with a prefetch of the row a pixel later: does the latency hide? */
    {
        long long ns_total = 0;
        long long px_total = 0;

        for (r = 0; r < reps; r++) {
            const uint16_t *clut = &vram[(r * 37 & 255) * VRAM_W];
            t0 = now_ns();
            for (int s = 0; s < SPANS; s++) {
                int y = (s * 7 + r) & 239;
                int xs = (s * 13 + r) & 300;
                int tex_x = (s * 3) & 255, clut_x = (s * 16) & 240;
                int tex_y = 256 + ((s * 5) & 127);
                int u = (s * 11) & 255, v = (s * 17) & 255;
                uint16_t *out = &dst[y * VRAM_W + xs];

                for (int i = 0; i < LEN; i++) {
                    const uint16_t *row = &vram[((tex_y + v) & 511) * VRAM_W];
                    uint16_t t;

                    __builtin_prefetch(row + ((tex_x + (u >> 2)) & 1023), 0, 0);
                    t = fetch4(row, clut, tex_x, clut_x, u);
                    out[i] = (uint16_t)(t | set_mask);
                    u += 1;
                    v += 1;
                }
            }
            t1 = now_ns();
            ns_total += t1 - t0;
            px_total += (long long)SPANS * LEN;
        }
        printf("fetch + prefetch   %6.1f ns/px\n", (double)ns_total / px_total);
    }

    /* Variant 4: fetch where v does not change (one texture row per span): is the row change the cost? */
    {
        long long ns_total = 0;
        long long px_total = 0;

        for (r = 0; r < reps; r++) {
            const uint16_t *clut = &vram[(r * 37 & 255) * VRAM_W];
            t0 = now_ns();
            for (int s = 0; s < SPANS; s++) {
                int y = (s * 7 + r) & 239;
                int xs = (s * 13 + r) & 300;
                int tex_x = (s * 3) & 255, clut_x = (s * 16) & 240;
                int tex_y = 256 + ((s * 5) & 127);
                int u = (s * 11) & 255;
                uint16_t *out = &dst[y * VRAM_W + xs];
                const uint16_t *row = &vram[tex_y * VRAM_W];

                for (int i = 0; i < LEN; i++) {
                    out[i] = (uint16_t)(fetch4(row, clut, tex_x, clut_x, u) | set_mask);
                    u += 1;
                }
            }
            t1 = now_ns();
            ns_total += t1 - t0;
            px_total += (long long)SPANS * LEN;
        }
        printf("fetch, one row     %6.1f ns/px\n", (double)ns_total / px_total);
    }
    (void)seed;
    return 0;
}
