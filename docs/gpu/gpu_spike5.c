/* Spike #5: the combination that the previous spikes point to, tested as one thing.
 *
 * What is known so far, all measured on this device:
 *   - VRAM as GL_RGB5_A1: upload/readback are 2-3x cheaper and bit-exact, BUT gl_LastFragColorARM returns the
 *     channels rotated (spike #4: blue came back as red, and the mask bit did not survive), so per-pixel
 *     semi-transparency and mask testing - the whole reason to want framebuffer fetch - break.
 *   - VRAM as GL_RGBA8: framebuffer fetch was exact in both tests (spike #2), but the CPU would have to expand
 *     its uint16_t VRAM buffer to RGBA8 before every upload, and readbacks are 32-bit.
 *
 * GLES allows the type GL_UNSIGNED_SHORT_5_5_5_1 with format GL_RGBA against an RGBA8 internal format: the
 * driver does the 5-5-5-1 <-> 8-8-8-8 expansion. If that works, the storage is RGBA8 (so framebuffer fetch is
 * the good one) while *the interface to the CPU stays 16 bits* (so no repack, and readbacks stay small).
 * This spike tests exactly that, in this order:
 *   A. upload from uint16 via 5_5_5_1 into an RGBA8 texture, read back via 5_5_5_1: bit-exact?
 *   B. identity framebuffer fetch on that RGBA8 target: does it return what was cleared?
 *   C. the mask bit through framebuffer fetch: does a marked pixel survive?
 *   D. the blend: blue background + 50% red foreground = (16,0,16) of 31?
 *   E. cost: upload of a 128 KB dirty rect, readback of one PAL field, one battle-sized batch.
 */
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VRAM_W 1024
#define VRAM_H 512
#define FRAMES 100

static void check_gl(const char *where)
{
    GLenum e;

    while ((e = glGetError()) != GL_NO_ERROR)
        printf("  [gl error] %s: 0x%x\n", where, (unsigned)e);
}

static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

static GLuint compile(GLenum type, const char *src, const char *name)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    char log[2048] = {0};

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, sizeof(log) - 1, NULL, log);
        printf("  [shader FAILED] %s: %s\n", name, log);
        return 0;
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src, const char *name)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src, name), fs, p;
    GLint ok = 0;
    char log[2048] = {0};

    if (vs == 0)
        return 0;
    fs = compile(GL_FRAGMENT_SHADER, fs_src, name);
    if (fs == 0)
        return 0;
    p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p, sizeof(log) - 1, NULL, log);
        printf("  [link FAILED] %s: %s\n", name, log);
        return 0;
    }
    return p;
}

static const char *FULLSCREEN_VS =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4((a_pos / vec2(512.0, -256.0)) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";

static const char *IDENTITY_FS =
    "#extension GL_ARM_shader_framebuffer_fetch : require\n"
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_FragColor = gl_LastFragColorARM; }\n";

static const char *FB_FS =
    "#extension GL_ARM_shader_framebuffer_fetch : require\n"
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec4 bg = gl_LastFragColorARM;\n"
    "  if (bg.a > 0.5) discard;\n"
    "  vec4 fg = vec4(1.0, 0.0, 0.0, 0.5);\n"
    "  gl_FragColor = (bg * 0.5) + (fg * 0.5);\n"
    "}\n";

static const char *BATCH_FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 texel = floor(v_uv);\n"
    "  vec4 t = texture2D(u_vram, (texel + 0.5) / vec2(1024.0, 512.0));\n"
    "  float index = mod(t.r * 31.0 + 0.5, 16.0);\n"
    "  gl_FragColor = texture2D(u_vram, (vec2(index, mod(texel.y, 256.0)) + 0.5) / vec2(1024.0, 512.0));\n"
    "}\n";

int main(void)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint tex, fb, vbo;
    unsigned short *vram16, *back16;
    unsigned short got;
    static const GLfloat quad[] = {0, 0, 0, 0, VRAM_W, 0, VRAM_W, 0, 0, VRAM_H, 0, VRAM_H, VRAM_W, VRAM_H, VRAM_W,
                                   VRAM_H};
    int i, f;

    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("spike5", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                           SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
    if (win == NULL) {
        printf("window: %s\n", SDL_GetError());
        return 1;
    }
    ctx = SDL_GL_CreateContext(win);
    if (ctx == NULL) {
        printf("context: %s\n", SDL_GetError());
        return 1;
    }
    printf("GL_VERSION: %s\n\n", (const char *)glGetString(GL_VERSION));

    vram16 = malloc((size_t)VRAM_W * VRAM_H * 2);
    back16 = malloc((size_t)VRAM_W * VRAM_H * 2);
    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        int x = i % VRAM_W, y = i / VRAM_W;

        vram16[i] = (unsigned short)((x & 31) | ((y & 31) << 5) | (((x ^ y) & 31) << 10) | (((x + y) & 1) << 15));
    }

    /* RGBA8 storage, 16-bit interface. */
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VRAM_W, VRAM_H, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, vram16);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    check_gl("texImage2D RGBA8 from 5551");
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    printf("A. RGBA8 target fed with GL_UNSIGNED_SHORT_5_5_5_1\n   FBO status 0x%x (%s)\n",
           (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER),
           glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");

    memset(back16, 0, (size_t)VRAM_W * VRAM_H * 2);
    glReadPixels(0, 0, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, back16);
    check_gl("readPixels 5551 from RGBA8");
    {
        int mismatches = 0;

        for (i = 0; i < VRAM_W * VRAM_H; i++)
            if (back16[i] != vram16[i])
                mismatches++;
        printf("   round trip of all %d texels: %d mismatches (%s)\n", VRAM_W * VRAM_H, mismatches,
               mismatches == 0 ? "BIT-EXACT" : "NOT bit-exact");
    }

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    glViewport(0, 0, VRAM_W, VRAM_H);

#define PAINT_REGIONS()                                                                                                \
    do {                                                                                                               \
        glEnable(GL_SCISSOR_TEST);                                                                                     \
        glScissor(0, 0, 64, 64);                                                                                       \
        glClearColor(0.0f, 0.0f, 1.0f, 0.0f); /* blue, mask clear */                                                   \
        glClear(GL_COLOR_BUFFER_BIT);                                                                                  \
        glScissor(64, 0, 64, 64);                                                                                      \
        glClearColor(0.0f, 1.0f, 0.0f, 1.0f); /* green, mask set */                                                    \
        glClear(GL_COLOR_BUFFER_BIT);                                                                                  \
        glDisable(GL_SCISSOR_TEST);                                                                                    \
    } while (0)

    /* B: identity fbfetch. */
    {
        GLuint ident = link_program(FULLSCREEN_VS, IDENTITY_FS, "identity");

        printf("\nB. identity framebuffer fetch on the RGBA8 target\n");
        if (ident) {
            PAINT_REGIONS();
            glUseProgram(ident);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glFinish();
            glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
            printf("   blue pixel  (want 0x%04x, b=31)          : 0x%04x %s\n", (unsigned short)(31 << 10), got,
                   got == (unsigned short)(31 << 10) ? "PASS" : "DIFFERS");
            glReadPixels(74, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
            printf("   green+mask  (want 0x%04x, g=31, mask=1)  : 0x%04x %s\n",
                   (unsigned short)((31 << 5) | (1 << 15)), got,
                   got == (unsigned short)((31 << 5) | (1 << 15)) ? "PASS" : "DIFFERS");
        }
    }

    /* C and D: mask bit and blend. */
    {
        GLuint prog = link_program(FULLSCREEN_VS, FB_FS, "fb");

        printf("\nC/D. mask bit and semi-transparency through framebuffer fetch\n");
        if (prog) {
            float r, g, b;

            PAINT_REGIONS();
            glUseProgram(prog);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glFinish();
            glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
            r = (float)(got & 31), g = (float)((got >> 5) & 31), b = (float)((got >> 10) & 31);
            printf("   blue bg + 50%% red fg : r=%d/31 g=%d/31 b=%d/31 mask=%d : %s\n", (int)r, (int)g, (int)b,
                   (int)((got >> 15) & 1), (r >= 15 && r <= 16 && g == 0 && b >= 15 && b <= 16) ? "PASS" : "FAIL");
            glReadPixels(74, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
            printf("   masked (green) pixel : r=%d g=%d/31 b=%d mask=%d : %s\n", (int)(got & 31),
                   (int)((got >> 5) & 31), (int)((got >> 10) & 31), (int)((got >> 15) & 1),
                   ((got & 31) <= 2 && ((got >> 5) & 31) >= 29) ? "PASS (survived)" : "FAIL");
        }
    }

    /* E: cost. */
    printf("\nE. cost of the 16-bit interface on an RGBA8 target\n");
    check_gl("before timings");
    if (1) {
        GLuint prog = link_program(FULLSCREEN_VS, BATCH_FS, "batch");
        const int cols = 40, rows = 60;
        GLfloat *verts = malloc((size_t)cols * rows * 6 * 4 * sizeof(GLfloat));
        GLuint batch_vbo;
        int v = 0, cx, cy;
        double t0, total = 0;

        for (cy = 0; cy < rows; cy++) {
            for (cx = 0; cx < cols; cx++) {
                float x0 = (float)(cx * 16), y0 = (float)(cy * 8);
                GLfloat tri[6][4] = {{x0, y0, 0, 0},         {x0 + 16, y0, 16, 0},     {x0, y0 + 8, 0, 8},
                                     {x0 + 16, y0, 16, 0},   {x0 + 16, y0 + 8, 16, 8}, {x0, y0 + 8, 0, 8}};

                memcpy(&verts[v], tri, sizeof(tri));
                v += 6 * 4;
            }
        }
        glGenBuffers(1, &batch_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, batch_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)cols * rows * 6 * 4 * sizeof(GLfloat)), verts,
                     GL_STATIC_DRAW);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glFinish();
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, cols * rows * 6);
            glDrawArrays(GL_TRIANGLES, 0, cols * rows * 6);
            glDrawArrays(GL_TRIANGLES, 0, cols * rows * 6);
            glDrawArrays(GL_TRIANGLES, 0, cols * rows * 6);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   1 draw x 4800 tris, 2 CLUT fetches (RGBA8+5551 io): %.3f ms (RGBA8/8888 was 1.215, 5551 was 1.05)\n",
               total / FRAMES / 4.0);
        free(verts);
        check_gl("batch");
    }

    {
        static unsigned short rect[256 * 256];
        double t0, total = 0;

        for (i = 0; i < 256 * 256; i++)
            rect[i] = (unsigned short)(i * 7);
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, (f * 7) & 511, (f * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_SHORT_5_5_5_1, rect);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ((f + 1) * 7) & 511, ((f + 1) * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_SHORT_5_5_5_1, rect);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ((f + 2) * 7) & 511, ((f + 2) * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_SHORT_5_5_5_1, rect);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ((f + 3) * 7) & 511, ((f + 3) * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_SHORT_5_5_5_1, rect);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   upload 256x256 from uint16 (128 KB)               : %.3f ms (5551 target: 0.657, RGBA8/8888: 1.424)\n",
               total / FRAMES / 4.0);
        check_gl("upload");
    }

    {
        static unsigned short field[320 * 240];
        double t0, total = 0;

        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, field);
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, field);
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, field);
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, field);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   glReadPixels 320x240 as 5551 (153 KB)             : %.3f ms (5551 target: 0.726, RGBA8/8888: 1.953)\n",
               total / FRAMES / 4.0);
        check_gl("readback");
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
