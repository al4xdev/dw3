/* Spike #7: which 320x240 target makes the per-frame readback cheapest?
 *
 * The data so far (all measured):
 *   - readback of 320x240 RGBA from an RGBA8 target: 1.95 ms
 *   - readback of 320x240 5-5-5-1 from an RGB5_A1 target: 0.73 ms
 *   - readback of 640x480 RGB565 from an RGB565 target: 4.38 ms
 *   - readback of 320x240 RGBA *from an RGB565 target* (spike #6, by mistake): 28.1 ms
 * The last one says the cost is not bytes: it is whether the format read matches the format stored. A
 * mismatched readback (RGB565 storage, RGBA request) goes through a slow conversion path.
 *
 * So: draw the display-area image into 320x240 targets of different formats and read each back in its own
 * format. The winner is what the present path should use. VRAM itself stays RGBA8, because that is the only
 * format where framebuffer fetch behaved correctly (spike #2) - the display target is a separate, small target.
 */
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VRAM_W 1024
#define VRAM_H 512
#define DISP_W 320
#define DISP_H 240
#define FRAMES 100

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

static const char *VS =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_pos;\n"
    "void main() {\n"
    "  v_pos = a_pos;\n"
    "  gl_Position = vec4((a_pos / vec2(160.0, -120.0)) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";

/* Reads the VRAM texture and writes the display area: nearest, one texel per pixel, no scaling (the CPU
 * already does the 2x on the present path today, and doing it here would cost fill rate for nothing). */
static const char *FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_pos;\n"
    "void main() {\n"
    "  vec2 src = floor(v_pos);\n"
    "  vec4 c = texture2D(u_vram, (src + 0.5) / vec2(1024.0, 512.0));\n"
    "  gl_FragColor = vec4(c.rgb, c.a);\n"
    "}\n";

typedef struct {
    const char *name;
    GLenum internal;
    GLenum format;
    GLenum type;
    size_t bytes_per_pixel;
} Variant;

int main(void)
{
    static const Variant variants[] = {
        {"RGBA8   / UNSIGNED_BYTE        (8-8-8-8)", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4},
        {"RGB5_A1 / UNSIGNED_SHORT_5_5_5_1 (16-bit)", GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2},
        {"RGB565  / UNSIGNED_SHORT_5_6_5  (16-bit)", GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2},
    };
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint vram, vram_fb, prog, vbo;
    unsigned char *rgba;
    void *readback;
    int v, i, f;

    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("spike7", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
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

    /* VRAM: RGBA8 (framebuffer fetch works only here), filled with a pattern. */
    rgba = malloc((size_t)VRAM_W * VRAM_H * 4);
    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        int x = i % VRAM_W, y = i / VRAM_W;

        rgba[i * 4 + 0] = (unsigned char)((x * 8) & 255);
        rgba[i * 4 + 1] = (unsigned char)((y * 8) & 255);
        rgba[i * 4 + 2] = (unsigned char)((x ^ y) & 255);
        rgba[i * 4 + 3] = 255;
    }
    glGenTextures(1, &vram);
    glBindTexture(GL_TEXTURE_2D, vram);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VRAM_W, VRAM_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &vram_fb);
    glBindFramebuffer(GL_FRAMEBUFFER, vram_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vram, 0);
    free(rgba);

    prog = link_program(VS, FS, "display");
    if (!prog) {
        return 1;
    }
    {
        static const GLfloat quad[] = {0, 0, DISP_W, 0, 0, DISP_H, DISP_W, DISP_H};

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram);
    }
    readback = malloc((size_t)DISP_W * DISP_H * 4);

    for (v = 0; v < 3; v++) {
        GLuint tex, fb;
        double t0, total = 0, draw_total = 0;
        GLenum st;

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, variants[v].internal, DISP_W, DISP_H, 0, variants[v].format, variants[v].type,
                     NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        st = glGetError();
        if (st != GL_NO_ERROR) {
            printf("%-45s : texture creation failed (0x%x)\n", variants[v].name, (unsigned)st);
            continue;
        }
        glGenFramebuffers(1, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            printf("%-45s : FBO incomplete (0x%x)\n", variants[v].name, (unsigned)st);
            continue;
        }

        glViewport(0, 0, DISP_W, DISP_H);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); /* warm up */
        glReadPixels(0, 0, DISP_W, DISP_H, variants[v].format, variants[v].type, readback);
        glFinish();
        check_gl("warmup");

        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glReadPixels(0, 0, DISP_W, DISP_H, variants[v].format, variants[v].type, readback);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        /* Rough split: same loop without the readback. */
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glFinish();
            draw_total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("%-45s : draw+readback %.3f ms | draw alone %.3f ms | readback ~%.3f ms\n", variants[v].name,
               total / FRAMES, draw_total / FRAMES, (total - draw_total) / FRAMES);
        check_gl(variants[v].name);
    }

    free(readback);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
