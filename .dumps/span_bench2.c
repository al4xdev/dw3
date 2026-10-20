/* The same 4-bit fetch with the VRAM's row stride swept: a stride that is a multiple of the cache line count (1024 u16
 * = 2048 B = 32 lines) puts every texture row in the same L1 set and the vertical walk thrashes it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_STRIDE 1152
#define ROWS 512
#define SPANS 10000
#define LEN 14

static uint16_t vram[MAX_STRIDE * ROWS];
static uint16_t dst[1024 * 512];

static long long now_ns(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static inline uint16_t fetch4(const uint16_t *row, const uint16_t *clut, int tex_x, int clut_x, int u) {
    uint16_t px = row[(tex_x + (u >> 2)) & 1023];

    return clut[clut_x + ((px >> ((u & 3) * 4)) & 0xF)];
}

static double run(int stride, int reps) {
    long long ns_total = 0, px_total = 0;

    for (int r = 0; r < reps; r++) {
        const uint16_t *clut = &vram[256 * stride];
        long long t0 = now_ns();

        for (int s = 0; s < SPANS; s++) {
            int y = (s * 7 + r) & 239;
            int xs = (s * 13 + r) & 300;
            int tex_x = (s * 3) & 255, clut_x = (s * 16) & 240;
            int tex_y = 256 + ((s * 5) & 127);
            int u = (s * 11) & 255, v = (s * 17) & 255;
            uint16_t *out = &dst[y * 1024 + xs];

            for (int i = 0; i < LEN; i++) {
                const uint16_t *row = &vram[((tex_y + v) & 511) * stride];

                out[i] = fetch4(row, clut, tex_x, clut_x, u);
                u += 1;
                v += 1;
            }
        }
        ns_total += now_ns() - t0;
        px_total += (long long)SPANS * LEN;
    }
    return (double)ns_total / px_total;
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 20;
    static const int strides[] = {1024, 1032, 1040, 1056, 1088, 1152};

    for (int i = 0; i < MAX_STRIDE * ROWS; i++) {
        vram[i] = (uint16_t)(i * 2654435761u);
    }
    for (int i = 0; i < 1024 * 512; i++) {
        dst[i] = 0x1234;
    }
    for (unsigned k = 0; k < sizeof strides / sizeof strides[0]; k++) {
        printf("stride %4d u16 (%5d B): %6.1f ns/px\n", strides[k], strides[k] * 2, run(strides[k], reps));
    }
    return 0;
}
