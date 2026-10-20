/* Spike #6: the last piece of the present design.
 *
 * Spike #5 tried to feed an RGBA8 target with GL_UNSIGNED_SHORT_5_5_5_1 and the driver answered
 * GL_INVALID_OPERATION (0x502) - that combination is not legal, my mistake (the ES3 table maps
 * RGBA + 5_5_5_1 to a RGB5_A1 internal format, not RGBA8). So the VRAM target stays RGBA8 fed with
 * 8-8-8-8, because that is the only format where framebuffer fetch behaved correctly (spike #2).
 *
 * Which leaves the present: the CPU has to get 640x480 of pixels out of the GPU every frame. Two routes:
 *   (a) read back 320x240 RGBA (the display area) and let the existing NEON path do the 2x nearest scale
 *       and the 15->16 bit conversion: 1.95 ms readback + ~0.77 ms fb0 write.
 *   (b) let the GPU do the 2x nearest scale and the conversion in a display shader, render into a RGB565
 *       target, and read *that* back as GL_UNSIGNED_SHORT_5_6_5: one memcpy to /dev/fb0, no CPU work.
 * This spike measures (b) and compares.
 */
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define VRAM_W 1024
#define VRAM_H 512
#define DISP_W 640
#define DISP_H 480
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

/* Positions in destination texels; the shader divides the sampled coordinate by 2 to get 2x nearest. */
static const char *DISPLAY_VS =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_pos;\n"
    "void main() {\n"
    "  v_pos = a_pos;\n"
    "  gl_Position = vec4((a_pos / vec2(320.0, -240.0)) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";

static const char *DISPLAY_FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "varying vec2 v_pos;\n"
    "void main() {\n"
    "  vec2 src = floor(v_pos * 0.5);\n" /* 2x nearest */
    "  gl_FragColor = texture2D(u_vram, (src + 0.5) / vec2(1024.0, 512.0));\n"
    "}\n";

int main(void)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint vram, vram_fb, disp, disp_fb;
    unsigned short *vram16;
    int i, f;

    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("spike6", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISP_W, DISP_H,
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

    /* VRAM as RGBA8, fed with expanded pixels (8-8-8-8) - the format that made framebuffer fetch work. */
    {
        unsigned char *rgba = malloc((size_t)VRAM_W * VRAM_H * 4);

        vram16 = malloc((size_t)VRAM_W * VRAM_H * 2);
        for (i = 0; i < VRAM_W * VRAM_H; i++) {
            int x = i % VRAM_W, y = i / VRAM_W;
            unsigned short v = (unsigned short)((x & 31) | ((y & 31) << 5) | (((x ^ y) & 31) << 10) | (1 << 15));

            vram16[i] = v;
            /* the 5551 -> RGBA8 expansion the CPU has to do before uploading: r,g,b in 0..255, a = mask bit */
            rgba[i * 4 + 0] = (unsigned char)(((v & 31) * 255 + 15) / 31);
            rgba[i * 4 + 1] = (unsigned char)((((v >> 5) & 31) * 255 + 15) / 31);
            rgba[i * 4 + 2] = (unsigned char)((((v >> 10) & 31) * 255 + 15) / 31);
            rgba[i * 4 + 3] = (unsigned char)((v >> 15) & 1 ? 255 : 0);
        }
        glGenTextures(1, &vram);
        glBindTexture(GL_TEXTURE_2D, vram);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VRAM_W, VRAM_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &vram_fb);
        glBindFramebuffer(GL_FRAMEBUFFER, vram_fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vram, 0);
        printf("VRAM RGBA8 FBO: 0x%x\n", (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER));
        free(rgba);
        check_gl("vram tex");
    }

    /* The display target: RGB565, 640x480, GPU-scaled. */
    glGenTextures(1, &disp);
    glBindTexture(GL_TEXTURE_2D, disp);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, DISP_W, DISP_H, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    check_gl("display tex RGB565");
    glGenFramebuffers(1, &disp_fb);
    glBindFramebuffer(GL_FRAMEBUFFER, disp_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, disp, 0);
    printf("RGB565 display FBO: 0x%x (%s)\n", (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER),
           glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE: RGB565 is renderable"
                                                                              : "INCOMPLETE");
    check_gl("display fbo");

    /* Draw the display pass and read it back as 5_6_5, then memcpy to /dev/fb0. */
    {
        GLuint prog = link_program(DISPLAY_VS, DISPLAY_FS, "display");
        static const GLfloat quad[] = {0, 0, DISP_W, 0, 0, DISP_H, DISP_W, DISP_H};
        GLuint vbo;
        static unsigned short frame[DISP_W * DISP_H];
        double t0, total = 0;
        int fd;
        unsigned char *fbp = NULL;

        if (!prog) {
            printf("display program FAILED\n");
            return 1;
        }
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "u_vram"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram);
        glBindFramebuffer(GL_FRAMEBUFFER, disp_fb);
        glViewport(0, 0, DISP_W, DISP_H);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        check_gl("display draw");

        glReadPixels(0, 0, DISP_W, DISP_H, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, frame);
        check_gl("readpixels 565");
        printf("first pixels read back: %04x %04x %04x %04x\n", frame[0], frame[1], frame[2], frame[3]);

        fd = open("/dev/fb0", O_RDWR);
        if (fd >= 0) {
            size_t bytes = (size_t)DISP_W * DISP_H * 2;

            fbp = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        }

        /* (a) readback of the display-sized RGB565 frame + memcpy to fb0 */
        t0 = now_ms();
        for (f = 0; f < FRAMES; f++) {
            glBindFramebuffer(GL_FRAMEBUFFER, disp_fb);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glReadPixels(0, 0, DISP_W, DISP_H, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, frame);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("\n[a] GPU scale to RGB565 + readback 640x480x2        : %.3f ms\n", total / FRAMES);

        if (fbp != NULL && fbp != MAP_FAILED) {
            size_t bytes = (size_t)DISP_W * DISP_H * 2;

            total = 0;
            t0 = now_ms();
            for (f = 0; f < FRAMES; f++) {
                memcpy(fbp, frame, bytes);
                total += now_ms() - t0;
                t0 = now_ms();
            }
            printf("[a] memcpy RGB565 -> /dev/fb0                       : %.3f ms\n", total / FRAMES);
            munmap(fbp, bytes);
        }
        if (fd >= 0)
            close(fd);

        /* (b) the variant measured in spike #3: readback 320x240 RGBA and scale on the CPU. */
        {
            static unsigned char small[320 * 240 * 4];

            total = 0;
            t0 = now_ms();
            for (f = 0; f < FRAMES; f++) {
                glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, small);
                glFinish();
                total += now_ms() - t0;
                t0 = now_ms();
            }
            printf("[b] readback 320x240 RGBA (CPU does 2x + 15->16)     : %.3f ms\n", total / FRAMES);
        }
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
