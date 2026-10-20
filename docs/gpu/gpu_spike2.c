/* Spike #2 for a GPU backend: the three things the study of DuckStation (see 01-05 in this folder) says we must
 * prove on this device before designing anything.
 *
 * Why not trust spike #1: it measured SDL_RenderGeometry (opaque, no VRAM feedback, driver-side batching) and the
 * numbers looked free. This one measures the shape we would actually ship:
 *
 *   A. Correctness: VRAM-as-texture with a CLUT indirection - fetch the packed index byte, then fetch the palette
 *      entry from another row of the same texture. If this does not round-trip exactly, nothing else matters.
 *   B. Correctness: GL_ARM_shader_framebuffer_fetch (the ARM/Mali sibling of EXT_shader_framebuffer_fetch). This is
 *      DuckStation's accurate path for per-pixel semi-transparency and mask-bit testing, and it is the difference
 *      between "two passes over every translucent polygon" and "free". Read the destination, blend, write.
 *   C. Cost: per-frame milliseconds for (a) 2400 textured triangles with two dependent fetches per pixel and
 *      framebuffer fetch on, (b) the full-VRAM read copy (glBlitFramebuffer 1024x512), (c) a 256x256 dirty-rect
 *      upload (glTexSubImage2D). Those are the three per-frame costs of the design, measured with glFinish so the
 *      driver cannot queue work behind the timer.
 *
 * Every number is printed as measured, including failures. Run it while nothing else uses the GPU.
 */
#include <SDL2/SDL.h>
/* GLES3 headers on purpose: the device only *reports* ES 3.2, so the interesting comparison is
 * glBlitFramebuffer (ES3) versus glCopyTexSubImage2D (ES2 fallback) for the VRAM read copy.
 * Shaders below stay at GLSL ES 1.00 (attribute/varying/gl_FragColor) and must still compile. */
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VRAM_W 1024
#define VRAM_H 512
#define TRIS 2400
#define FRAMES 100

static void check_gl(const char *where)
{
    GLenum e;

    while ((e = glGetError()) != GL_NO_ERROR)
        printf("  [gl error] %s: 0x%x\n", where, (unsigned)e);
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

static GLuint make_texture(GLenum internal, int w, int h, const void *data)
{
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static GLuint make_fbo(GLuint tex)
{
    GLuint fb;

    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("  [fbo] incomplete for tex %u\n", (unsigned)tex);
        return 0;
    }
    return fb;
}

/* Full-target quad. Positions in texels 0..VRAM_W x 0..VRAM_H so the shader can use gl_FragCoord, like the real
 * batch shader does (it needs the integer destination coordinates for dithering and the mask bit). */
static const char *FULLSCREEN_VS =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4((a_pos / vec2(512.0, -256.0)) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";

/* A: the CLUT indirection, exactly the shape of gpu_hw_shadergen.cpp:2881-2919: read the packed texel, pull the
 * index out of the right channel, then read the palette row of the same texture. NEAREST sampling at
 * (texel + 0.5) / size, since GLES2 has no texelFetch (this device does have ES 3.2, but the shader must also
 * prove the GLES2 fallback works). */
static const char *CLUT_FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 texel = floor(gl_FragCoord.xy);\n"
    "  vec4 tdata = texture2D(u_vram, (texel + 0.5) / vec2(1024.0, 512.0));\n"
    "  float index = floor(tdata.r * 255.0 + 0.5);\n"
    "  vec4 color = texture2D(u_vram, (vec2(index, 400.0) + 0.5) / vec2(1024.0, 512.0));\n"
    "  gl_FragColor = color;\n"
    "}\n";

/* B: framebuffer fetch. gl_LastFragColorARM is what DuckStation uses when only GL_ARM_shader_framebuffer_fetch is
 * present (util/shadergen.cpp:676-679); it needs no inout/output rewrite, which is what makes it usable from a
 * GLSL ES 1.00 shader. Implements PS1 transparency mode 0 (half background + half foreground) per pixel, plus the
 * mask-bit test as a discard. */
static const char *FBFETCH_FS =
    "#extension GL_ARM_shader_framebuffer_fetch : require\n"
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec4 bg = gl_LastFragColorARM;\n"
    "  if (bg.a > 0.5) discard;\n" /* check-mask: a marked pixel cannot be overwritten */
    "  vec4 fg = vec4(1.0, 0.0, 0.0, 0.5);\n" /* semi-transparent red texel */
    "  gl_FragColor = (bg * 0.5) + (fg * 0.5);\n"
    "}\n";

/* C: the batch shader's shape - two dependent fetches per pixel, textured, no blending, for the timing loop. */
static const char *BATCH_FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 texel = floor(v_uv);\n"
    "  vec4 tdata = texture2D(u_vram, (texel + 0.5) / vec2(1024.0, 512.0));\n"
    "  float index = mod(tdata.r * 255.0, 16.0);\n"
    "  vec2 pal = vec2(index, mod(texel.y, 256.0));\n"
    "  gl_FragColor = texture2D(u_vram, (pal + 0.5) / vec2(1024.0, 512.0));\n"
    "}\n";

/* The same batch, but doing what a semi-transparent PS1 polygon does: read the destination through
 * gl_LastFragColorARM, apply the mask-bit test, and blend. This is the shape we would ship, so its cost is
 * the number that matters; the opaque variant above is only the floor. */
static const char *BATCH_FB_FS =
    "#extension GL_ARM_shader_framebuffer_fetch : require\n"
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec4 bg = gl_LastFragColorARM;\n"
    "  if (bg.a > 0.5) discard;\n"
    "  vec2 texel = floor(v_uv);\n"
    "  vec4 tdata = texture2D(u_vram, (texel + 0.5) / vec2(1024.0, 512.0));\n"
    "  float index = mod(tdata.r * 255.0 + 0.5, 16.0);\n"
    "  vec4 fg = texture2D(u_vram, (vec2(index, mod(texel.y, 256.0)) + 0.5) / vec2(1024.0, 512.0));\n"
    "  vec4 out_col = (bg * 0.5) + (fg * 0.5);\n"
    "  gl_FragColor = vec4(out_col.rgb, tdata.a);\n"
    "}\n";

static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

int main(void)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint vram, vram_copy, fb, copy_fb;
    GLuint clut_prog = 0, fbfetch_prog = 0, batch_prog = 0;
    unsigned char *pattern;
    int i, f;

    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("spike2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
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
    printf("GL_VERSION: %s\n", (const char *)glGetString(GL_VERSION));

    /* The VRAM: RGBA8 1024x512 (r=index/colour, a=mask bit), like gpu_hw.cpp:50 with VRAM_RT_FORMAT. */
    pattern = malloc((size_t)VRAM_W * VRAM_H * 4);
    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        int x = i % VRAM_W, y = i / VRAM_W;

        /* Row 400 is the palette: entry n gets colour (n*16, 255-n*16, 128, 1). Everywhere else, the red channel
         * holds a palette index so the CLUT lookup has something to find. */
        if (y == 400)
            pattern[i * 4 + 0] = (unsigned char)(x * 16 > 255 ? 255 : x * 16);
        else
            pattern[i * 4 + 0] = (unsigned char)(x & 15);
        pattern[i * 4 + 1] = (unsigned char)(y == 400 ? 255 - ((x * 16) & 255) : 0);
        pattern[i * 4 + 2] = 128;
        pattern[i * 4 + 3] = 255;
    }
    vram = make_texture(GL_RGBA, VRAM_W, VRAM_H, pattern);
    fb = make_fbo(vram);
    vram_copy = make_texture(GL_RGBA, VRAM_W, VRAM_H, NULL);
    copy_fb = make_fbo(vram_copy);

    /* --- A: CLUT indirection round-trips ------------------------------------------------------------- */
    clut_prog = link_program(FULLSCREEN_VS, CLUT_FS, "clut");
    printf("\nA. CLUT indirection through the same texture: %s\n", clut_prog ? "program linked" : "FAILED");
    if (clut_prog) {
        static const GLfloat quad[] = {0, 0, 0, 0, VRAM_W, 0, VRAM_W, 0, 0, VRAM_H, 0, VRAM_H, VRAM_W, VRAM_H, VRAM_W,
                                       VRAM_H};
        GLuint vbo;
        unsigned char got[4];

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glUseProgram(clut_prog);
        glUniform1i(glGetUniformLocation(clut_prog, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, copy_fb);
        glViewport(0, 0, VRAM_W, VRAM_H);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        /* Texel (5, 10) of the pattern holds index 5, so the output must be palette entry 5 = (80, 175, 128). */
        glReadPixels(5, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
        printf("   texel (5,10): index 5 -> palette (80,175,128); read back (%d,%d,%d) : %s\n", got[0], got[1],
               got[2], (got[0] == 80 && got[1] == 175 && got[2] == 128) ? "PASS" : "FAIL");
        check_gl("clut draw");
    }

    /* --- B: framebuffer fetch ---------------------------------------------------------------- */
    fbfetch_prog = link_program(FULLSCREEN_VS, FBFETCH_FS, "fbfetch");
    printf("\nB. GL_ARM_shader_framebuffer_fetch: %s\n", fbfetch_prog ? "compiled and linked" : "UNAVAILABLE");
    if (fbfetch_prog) {
        static const GLfloat quad[] = {0, 0, 0, 0, VRAM_W, 0, VRAM_W, 0, 0, VRAM_H, 0, VRAM_H, VRAM_W, VRAM_H, VRAM_W,
                                       VRAM_H};
        GLuint vbo;
        unsigned char got[4];

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glUseProgram(fbfetch_prog);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, fb); /* render into the VRAM texture itself: this is the feedback loop */
        glViewport(0, 0, VRAM_W, VRAM_H);
        glClearColor(0.0f, 0.0f, 1.0f, 0.0f); /* opaque blue background, mask bit clear */
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        glReadPixels(100, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
        /* blue*0.5 + red*0.5 = (0.5, 0, 0.5) = (128, 0, 128) */
        printf("   blue bg + 50%% red fg = expect (128,0,128); got (%d,%d,%d) : %s\n", got[0], got[1], got[2],
               (abs(got[0] - 128) <= 2 && got[1] <= 2 && abs(got[2] - 128) <= 2) ? "PASS" : "FAIL");

        /* The mask bit: paint alpha=1 on a rectangle, then try to overwrite it through the fbfetch shader. */
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 64, 64);
        glClearColor(0.0f, 1.0f, 0.0f, 1.0f); /* marked pixel */
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
        printf("   marked pixel survives the fbfetch draw (expect still green 0,255,0): got (%d,%d,%d) : %s\n",
               got[0], got[1], got[2], (got[0] <= 2 && got[1] >= 253 && got[2] <= 2) ? "PASS" : "FAIL");
        check_gl("fbfetch");
    }

    /* --- C: cost of the three per-frame operations -------------------------------------------- */
    batch_prog = link_program(FULLSCREEN_VS, BATCH_FS, "batch");
    printf("\nC. per-frame cost (glFinish every frame, so the numbers are the GPU's, not the queue's)\n");
    if (batch_prog) {
        /* 2400 small triangles: a grid of quads across 640x480, roughly the prim count of a busy battle frame. */
        const int cols = 40, rows = 60; /* 2400 quads = 4800 triangles */
        GLfloat *verts = malloc((size_t)cols * rows * 6 * 4 * sizeof(GLfloat));
        GLuint vbo;
        int v = 0, cx, cy;

        for (cy = 0; cy < rows; cy++) {
            for (cx = 0; cx < cols; cx++) {
                float x0 = (float)(cx * 16), y0 = (float)(cy * 8), x1 = x0 + 16, y1 = y0 + 8;
                float u0 = (float)((cx * 7) & 255), w0 = (float)((cy * 11) & 255);

                /* two triangles per quad, uv in texel space, wrapping like a texture page */
                GLfloat tri[6][4] = {{x0, y0, u0, w0},         {x1, y0, u0 + 16, w0},
                                     {x0, y1, u0, w0 + 8},     {x1, y0, u0 + 16, w0},
                                     {x1, y1, u0 + 16, w0 + 8}, {x0, y1, u0, w0 + 8}};

                memcpy(&verts[v], tri, sizeof(tri));
                v += 6 * 4;
            }
        }
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)cols * rows * 6 * 4 * sizeof(GLfloat)), verts,
                     GL_STATIC_DRAW);
        glUseProgram(batch_prog);
        glUniform1i(glGetUniformLocation(batch_prog, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glViewport(0, 0, VRAM_W, VRAM_H);

        /* Baseline first: the first run of this spike printed ~1.6 ms for three completely different
         * operations, which is the glFinish round trip, not the operation. Every measurement below
         * subtracts this constant mentally, and does four operations per sync so it is divided by four. */
        glFinish();
        {
            double t0 = now_ms(), total = 0;

            for (f = 0; f < FRAMES; f++) {
                glFinish();
                total += now_ms() - t0;
                t0 = now_ms();
            }
            printf("   baseline: empty loop + glFinish                       : %.3f ms\n", total / FRAMES);
        }

        /* One draw call for the whole frame. */
        glFinish();
        {
            double t0 = now_ms(), total = 0;

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
            printf("   1 draw x %d textured triangles, 2 CLUT fetches/px    : %.3f ms per draw\n", cols * rows * 2,
                   total / FRAMES / 4.0);
        }

        check_gl("batch");
        free(verts);
    }

    /* The same geometry, one draw call per triangle: what "no batching" costs. */
    {
        const int quads = 2400;
        GLfloat *verts = calloc((size_t)quads * 6 * 4, sizeof(GLfloat));
        GLuint vbo;
        double t0, total = 0;
        int q, v = 0;

        for (q = 0; q < quads; q++) {
            float x0 = (float)((q % 40) * 16), y0 = (float)((q / 40) * 8);
            GLfloat tri[6][4] = {{x0, y0, 0, 0},         {x0 + 16, y0, 16, 0},     {x0, y0 + 8, 0, 8},
                                 {x0 + 16, y0, 16, 0},   {x0 + 16, y0 + 8, 16, 8}, {x0, y0 + 8, 0, 8}};

            memcpy(&verts[v], tri, sizeof(tri));
            v += 6 * 4;
        }
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)quads * 6 * 4 * sizeof(GLfloat)), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glFinish();
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            int t;

            glClear(GL_COLOR_BUFFER_BIT);
            for (t = 0; t < quads * 2; t++)
                glDrawArrays(GL_TRIANGLES, t * 3, 3);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   %d individual draw calls (no batching)               : %.3f ms per frame\n", quads * 2,
               total / FRAMES);
        check_gl("individual draws");
        free(verts);
    }

    /* Full-VRAM read copy: this is what keeps the "sample the texture you are drawing to" problem away. */
    glFinish();
    {
        double t0 = now_ms(), total = 0;

        for (f = 0; f < FRAMES; f++) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copy_fb);
            glBlitFramebuffer(0, 0, VRAM_W, VRAM_H, 0, 0, VRAM_W, VRAM_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBlitFramebuffer(0, 0, VRAM_W, VRAM_H, 0, 0, VRAM_W, VRAM_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBlitFramebuffer(0, 0, VRAM_W, VRAM_H, 0, 0, VRAM_W, VRAM_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBlitFramebuffer(0, 0, VRAM_W, VRAM_H, 0, 0, VRAM_W, VRAM_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   glBlitFramebuffer full 1024x512 VRAM copy            : %.3f ms each\n", total / FRAMES / 4.0);
        check_gl("blit");
    }

    /* The same read copy with the ES2 API (no glBlitFramebuffer in GLES2): copy from the FBO into the read
     * texture. If this is slower, it is the price of an ES2-only path. */
    glFinish();
    {
        double t0 = now_ms(), total = 0;

        for (f = 0; f < FRAMES; f++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glBindTexture(GL_TEXTURE_2D, vram_copy);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, VRAM_W, VRAM_H);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, VRAM_W, VRAM_H);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, VRAM_W, VRAM_H);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, VRAM_W, VRAM_H);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   glCopyTexSubImage2D full 1024x512 (ES2 fallback)     : %.3f ms each\n", total / FRAMES / 4.0);
        check_gl("copytexsubimage");
    }

    /* Dirty-rect upload from the CPU (LoadImage / texture update from the decompiled game). */
    glFinish();
    {
        static unsigned char rect[256 * 256 * 4];
        double t0, total = 0;

        memset(rect, 0x5A, sizeof(rect));
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glBindTexture(GL_TEXTURE_2D, vram);
            glTexSubImage2D(GL_TEXTURE_2D, 0, (f * 7) & 511, (f * 3) & 255, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE,
                            rect);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ((f + 1) * 7) & 511, ((f + 1) * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_BYTE, rect);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ((f + 2) * 7) & 511, ((f + 2) * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_BYTE, rect);
            glTexSubImage2D(GL_TEXTURE_2D, 0, ((f + 3) * 7) & 511, ((f + 3) * 3) & 255, 256, 256, GL_RGBA,
                            GL_UNSIGNED_BYTE, rect);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   glTexSubImage2D 256x256 (256 KB) upload              : %.3f ms each\n", total / FRAMES / 4.0);
        check_gl("subimage");
    }

    /* --- D: the semi-transparent shape, and the present ----------------------------------------- */
    {
        GLuint fb_prog = link_program(FULLSCREEN_VS, BATCH_FB_FS, "batch_fbfetch");
        const int cols = 40, rows = 60;

        printf("\nD. semi-transparency and present\n");
        if (fb_prog) {
            GLfloat *verts = malloc((size_t)cols * rows * 6 * 4 * sizeof(GLfloat));
            GLuint vbo;
            int v = 0, cx, cy;
            double t0, total = 0;

            for (cy = 0; cy < rows; cy++) {
                for (cx = 0; cx < cols; cx++) {
                    float x0 = (float)(cx * 16), y0 = (float)(cy * 8);
                    GLfloat tri[6][4] = {{x0, y0, 0, 0},       {x0 + 16, y0, 16, 0},     {x0, y0 + 8, 0, 8},
                                         {x0 + 16, y0, 16, 0}, {x0 + 16, y0 + 8, 16, 8}, {x0, y0 + 8, 0, 8}};

                    memcpy(&verts[v], tri, sizeof(tri));
                    v += 6 * 4;
                }
            }
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)cols * rows * 6 * 4 * sizeof(GLfloat)), verts,
                         GL_STATIC_DRAW);
            glUseProgram(fb_prog);
            glUniform1i(glGetUniformLocation(fb_prog, "u_vram"), 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, vram);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glViewport(0, 0, VRAM_W, VRAM_H);
            glDisable(GL_BLEND); /* fbfetch does the blending itself */
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
            printf("   same batch + fbfetch blend + mask discard            : %.3f ms per draw (opaque was 1.243)\n",
                   total / FRAMES / 4.0);
            check_gl("fbfetch batch");
            free(verts);
        } else {
            printf("   fbfetch batch program FAILED\n");
        }
    }

    /* Present: sample the VRAM texture into the 640x480 window, swap with vsync off. This replaces the
     * ~1.0 ms NEON 16->RGB565 conversion + write() to /dev/fb0 that the software path does today. */
    {
        GLuint prog = link_program(FULLSCREEN_VS, BATCH_FS, "present");
        static const GLfloat quad[] = {0, 0, 0, 0, 640, 0, 640, 0, 0, 480, 0, 480, 640, 480, 640, 480};
        GLuint vbo;
        double t0, total = 0;

        SDL_GL_SetSwapInterval(0);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 640, 480);
        glFinish();
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            int k;

            for (k = 0; k < 4; k++) {
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                SDL_GL_SwapWindow(win);
            }
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("   present: 640x480 quad + SDL_GL_SwapWindow (vsync off): %.3f ms each\n", total / FRAMES / 4.0);
        check_gl("present");
    }

    printf("\nBudget: 20 ms per frame at 50 Hz. Software rasterizer today: ~18-19 ms of that.\n");

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
