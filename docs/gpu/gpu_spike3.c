/* Spike #3: how would a GL backend get the frame onto the panel?
 *
 * Spike #2 found the rasterizer side is cheap (1.2-1.6 ms for a battle frame's worth of textured triangles,
 * 3.5 ms even with framebuffer-fetch blending), but also that SDL_GL_SwapWindow reported ~7 ms per swap. That
 * number is either the cost of a KMS page flip (fine: it is the frame pacer, the panel can only show 50/s) or
 * SDL overhead (not fine). This spike separates those, and measures the alternative the software path uses
 * today: render to an offscreen target, read it back, and write it to /dev/fb0.
 *
 *   1. swaps/second with vsync on  -> if ~50-60, the swap is paced by the panel, not by the driver.
 *   2. swaps/second with vsync off -> if still ~50-60, KMSDRM always waits for the flip.
 *   3. glReadPixels of 320x240 (one PAL field) -> the readback that a "keep the fb0 present" design needs.
 *   4. mmap'd /dev/fb0 write of a 640x480 16-bit image -> the baseline both paths are compared against.
 */
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/* Fullscreen triangle in clip space; no textures needed for this spike. */
static const char *VS =
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *FS =
    "precision mediump float;\n"
    "uniform float u_frame;\n"
    "void main() { gl_FragColor = vec4(u_frame, 0.25, 0.75, 1.0); }\n";

int main(void)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint prog, vbo, fb, tex;
    GLint loc_frame;
    int swap_interval;
    int f;

    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("spike3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
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
    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());

    {
        static const GLfloat tri[] = {-1, -1, 3, -1, -1, 3};
        GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);

        glShaderSource(vs, 1, &VS, NULL);
        glCompileShader(vs);
        glShaderSource(fs, 1, &FS, NULL);
        glCompileShader(fs);
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glUseProgram(prog);
        loc_frame = glGetUniformLocation(prog, "u_frame");
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
    }

    /* 1 and 2: swaps per second, with and without vsync. */
    for (swap_interval = 0; swap_interval <= 1; swap_interval++) {
        double t0, elapsed;
        int swaps = 0;

        SDL_GL_SetSwapInterval(swap_interval);
        glViewport(0, 0, 640, 480);
        SDL_GL_SwapWindow(win);
        t0 = now_ms();
        while ((elapsed = now_ms() - t0) < 2000.0) {
            glUniform1f(loc_frame, (float)(swaps & 1));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            SDL_GL_SwapWindow(win);
            swaps++;
        }
        printf("[swap] vsync %s: %d swaps in %.0f ms = %.1f swaps/s (%.2f ms per swap)\n",
               swap_interval ? "on " : "off", swaps, elapsed, swaps * 1000.0 / elapsed, elapsed / swaps);
    }

    /* The VRAM-shaped offscreen target, for the readback test below. */
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    printf("[fbo] status 0x%x\n", (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER));

    /* 3: readback of one PAL field (320x240) from the VRAM target, which is what a fb0-presenting design needs. */
    {
        static unsigned char pixels[320 * 240 * 4];
        double t0, total = 0;

        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glViewport(0, 0, 1024, 512);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        t0 = now_ms();
        for (f = 0; f < 100; f++) {
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glFinish();
            total += now_ms() - t0;
            t0 = now_ms();
        }
        printf("[readback] glReadPixels 320x240 RGBA (3 per sync, /3): %.3f ms each\n", total / 100.0 / 3.0);
        printf("           first pixel: %02x %02x %02x %02x\n", pixels[0], pixels[1], pixels[2], pixels[3]);
    }

    /* 4: the baseline - write a 640x480 16bpp image into /dev/fb0 the way the software present does today. */
    {
        int fd = open("/dev/fb0", O_RDWR);
        unsigned char *fbp;
        unsigned short *image;

        if (fd < 0) {
            printf("[fb0] open failed\n");
        } else {
            const size_t bytes = (size_t)640 * 480 * 2;
            double t0, total = 0;

            fbp = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            image = malloc(bytes);
            memset(image, 0xC0, bytes);
            if (fbp == MAP_FAILED) {
                printf("[fb0] mmap failed\n");
            } else {
                t0 = now_ms();
                for (f = 0; f < 100; f++) {
                    memcpy(fbp, image, bytes);
                    memcpy(fbp, image, bytes);
                    memcpy(fbp, image, bytes);
                    memcpy(fbp, image, bytes);
                    total += now_ms() - t0;
                    t0 = now_ms();
                }
                printf("[fb0] memcpy 640x480x2 into mmap (3 per sync, /3): %.3f ms each\n", total / 100.0 / 3.0);
                munmap(fbp, bytes);
            }
            free(image);
            close(fd);
        }
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
