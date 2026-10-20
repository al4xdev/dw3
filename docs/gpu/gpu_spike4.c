/* Spike #4: can the VRAM live on the GPU as real 16-bit 5-5-5-1, instead of RGBA8?
 *
 * Why this matters more than it looks. DuckStation stores VRAM as RGBA8 and re-quantises to 5551 in the shader
 * (gpu_hw_shadergen.cpp:68-85) because it wants true-colour output and upscale filters. We want neither: the PS1
 * VRAM *is* 16 bits, our software renderer already holds it as uint16_t, and the checkpoint dump is that buffer.
 * If the GPU can hold exactly that format, then:
 *   - uploading VRAM is a memcpy of the array we already have (no CPU repack to RGBA8);
 *   - the mask bit is the alpha bit, as on the real hardware;
 *   - reading VRAM back for the present/checkpoint is bit-exact against the software renderer, so the oracle
 *     comparison is a memcmp instead of a tolerance test.
 *
 * Three questions, in order:
 *   A. Is GL_RGB5_A1 colour-renderable and can it be fed with GL_UNSIGNED_SHORT_5_5_5_1 straight from uint16_t?
 *   B. Does a round trip (upload -> FBO -> glReadPixels) come back bit-identical for all 65536 values?
 *   C. What does it cost compared with the RGBA8 path measured in spike #2 (1.22 ms draw, 1.42 ms/256 KB upload,
 *      1.95 ms present readback)?
 */
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VRAM_W 1024
#define VRAM_H 512
#define FRAMES 100

/* GL_RGB5_A1 is core in ES 2.0 (as an unsized internal format) and in ES 3.x; the headers we build against
 * (GLES3/gl3.h) only define the ES3 name. */
#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif

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

/* CLUT indirection straight out of the 5551 target: no unpacking of an RGBA8 texel, the hardware already gives
 * back 5/5/5/1-normalised channels, and the index is the low 5 bits of red as on the real thing. */
static const char *CLUT_FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 texel = floor(gl_FragCoord.xy);\n"
    "  vec4 t = texture2D(u_vram, (texel + 0.5) / vec2(1024.0, 512.0));\n"
    "  float index = floor(t.r * 31.0 + 0.5);\n"
    "  gl_FragColor = texture2D(u_vram, (vec2(index, 400.0) + 0.5) / vec2(1024.0, 512.0));\n"
    "}\n";

/* Identity: write back exactly what the framebuffer fetch gave us. If this does not reproduce the cleared
 * colour, the problem is in what gl_LastFragColorARM returns on a 5551 target, not in the blending. */
static const char *IDENTITY_FS =
    "#extension GL_ARM_shader_framebuffer_fetch : require\n"
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_FragColor = gl_LastFragColorARM; }\n";

/* Framebuffer fetch + semi-transparency on a 5551 destination. */
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

/* Two dependent fetches per pixel: the batch shape, for the timing loop. */
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
    GLuint tex, fb, prog_clut, prog_fb, prog_batch;
    unsigned short *vram16, *back16;
    int i, f;

    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("spike4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
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
    /* A pattern that exercises every 5-bit field and the mask bit, plus the palette row at y=400. */
    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        int x = i % VRAM_W, y = i / VRAM_W;

        if (y == 400)
            vram16[i] = (unsigned short)((x & 31) | (((31 - (x & 31)) & 31) << 5) | (16 << 10) | (1 << 15));
        else
            vram16[i] = (unsigned short)((x & 31) | ((y & 31) << 5) | (((x ^ y) & 31) << 10) | (((x + y) & 1) << 15));
    }

    /* --- A: is RGB5_A1 renderable, and does it take uint16 directly? --------------------------- */
    printf("A. VRAM as GL_RGB5_A1, uploaded as GL_UNSIGNED_SHORT_5_5_5_1\n");
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, VRAM_W, VRAM_H, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, vram16);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    check_gl("texImage2D 5551");
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);

        printf("   FBO status 0x%x : %s\n", (unsigned)st,
               st == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE (RGB5_A1 is colour-renderable)" : "NOT COMPLETE");
    }
    check_gl("fbo 5551");

    /* --- B: bit-exact round trip ------------------------------------------------------------- */
    printf("\nB. round trip upload -> FBO -> glReadPixels, all %d texels\n", VRAM_W * VRAM_H);
    memset(back16, 0, (size_t)VRAM_W * VRAM_H * 2);
    glReadPixels(0, 0, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, back16);
    check_gl("readPixels 5551");
    {
        int mismatches = 0;
        unsigned short first_bad = 0, got_bad = 0;

        for (i = 0; i < VRAM_W * VRAM_H; i++) {
            if (back16[i] != vram16[i]) {
                if (mismatches == 0) {
                    first_bad = vram16[i];
                    got_bad = back16[i];
                }
                mismatches++;
            }
        }
        printf("   %d mismatches out of %d (%s)\n", mismatches, VRAM_W * VRAM_H,
               mismatches == 0 ? "BIT-EXACT" : "NOT bit-exact");
        if (mismatches)
            printf("   first mismatch: wrote 0x%04x, read 0x%04x\n", first_bad, got_bad);
    }

    /* --- C: fbfetch blending on a 5551 target ------------------------------------------------ */
    prog_fb = link_program(FULLSCREEN_VS, FB_FS, "fb5551");
    printf("\nC. framebuffer fetch on a 5551 target: %s\n", prog_fb ? "linked" : "FAILED");
    if (prog_fb) {
        static const GLfloat quad[] = {0, 0, 0, 0, VRAM_W, 0, VRAM_W, 0, 0, VRAM_H, 0, VRAM_H, VRAM_W, VRAM_H, VRAM_W,
                                       VRAM_H};
        GLuint vbo;
        unsigned short got = 0;
        float r, g, b;

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glViewport(0, 0, VRAM_W, VRAM_H);

        /* Helper: paint the two 64x64 regions (blue = mask clear, green = mask set) and return to no scissor. */
        #define PAINT_REGIONS() \
            do { \
                glEnable(GL_SCISSOR_TEST); \
                glScissor(0, 0, 64, 64); \
                glClearColor(0.0f, 0.0f, 1.0f, 0.0f); \
                glClear(GL_COLOR_BUFFER_BIT); \
                glScissor(64, 0, 64, 64); \
                glClearColor(0.0f, 1.0f, 0.0f, 1.0f); \
                glClear(GL_COLOR_BUFFER_BIT); \
                glDisable(GL_SCISSOR_TEST); \
            } while (0)

        /* Step 1: an identity pass answers "what does framebuffer fetch return on this format?". */
        {
            GLuint prog_ident = link_program(FULLSCREEN_VS, IDENTITY_FS, "identity5551");

            if (prog_ident) {
                PAINT_REGIONS();
                glUseProgram(prog_ident);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glFinish();
                glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
                printf("   identity pass, blue pixel: 0x%04x (want 0x%04x: b=31)  : %s\n", got,
                       (unsigned short)(31 << 10), got == (unsigned short)(31 << 10) ? "PASS" : "diFFERS");
                glReadPixels(74, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
                printf("   identity pass, green pixel with mask: 0x%04x (want 0x%04x) : %s\n", got,
                       (unsigned short)((31 << 5) | (1 << 15)),
                       got == (unsigned short)((31 << 5) | (1 << 15)) ? "PASS" : "DIFFERS");
            }
        }

        /* Step 2: the blend. */
        PAINT_REGIONS();
        glUseProgram(prog_fb);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
        r = (float)(got & 31), g = (float)((got >> 5) & 31), b = (float)((got >> 10) & 31);
        printf("   blue bg + 50%% red fg : r=%d/31 g=%d/31 b=%d/31 mask=%d : %s\n", (int)r, (int)g, (int)b,
               (int)((got >> 15) & 1), (r >= 15 && r <= 16 && g == 0 && b >= 15 && b <= 16) ? "PASS" : "FAIL");
        glReadPixels(74, 10, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, &got);
        printf("   masked (green) pixel : r=%d g=%d/31 b=%d mask=%d : %s\n", (int)(got & 31),
               (int)((got >> 5) & 31), (int)((got >> 10) & 31), (int)((got >> 15) & 1),
               ((got & 31) <= 2 && ((got >> 5) & 31) >= 29 && ((got >> 15) & 1) == 1) ? "PASS (survived)" : "FAIL");
        check_gl("fb5551");
    }

    /* --- D: cost ----------------------------------------------------------------------------- */
    prog_batch = link_program(FULLSCREEN_VS, BATCH_FS, "batch5551");
    prog_clut = link_program(FULLSCREEN_VS, CLUT_FS, "clut5551");
    printf("\nD. cost\n");
    if (prog_batch) {
        const int cols = 40, rows = 60;
        GLfloat *verts = malloc((size_t)cols * rows * 6 * 4 * sizeof(GLfloat));
        GLuint vbo;
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
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)cols * rows * 6 * 4 * sizeof(GLfloat)), verts,
                     GL_STATIC_DRAW);
        glUseProgram(prog_batch);
        glUniform1i(glGetUniformLocation(prog_batch, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glViewport(0, 0, VRAM_W, VRAM_H);
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
        printf("   1 draw x 4800 textured tris, 2 CLUT fetches, 5551  : %.3f ms (RGBA8 was 1.215)\n",
               total / FRAMES / 4.0);
        check_gl("batch5551");
        free(verts);
    }

    /* Upload straight from the uint16 array that the software renderer already holds. */
    glFinish();
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
        printf("   upload 256x256 from uint16 (128 KB, no repack)      : %.3f ms (RGBA8 was 1.424)\n",
               total / FRAMES / 4.0);

        total = 0;
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, vram16);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   upload full VRAM 1024x512 from uint16 (1 MB)         : %.3f ms\n", total / FRAMES);
        check_gl("upload5551");
    }

    /* Present-style readback of one PAL field, now in 16-bit. */
    glFinish();
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
        printf("   glReadPixels 320x240 as 5551 (153 KB)               : %.3f ms (RGBA8 was 1.953)\n",
               total / FRAMES / 4.0);
        check_gl("readback5551");
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
