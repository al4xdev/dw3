/* Spike #8: can we get a GLES context *offscreen*, without owning the display?
 *
 * The present design depends on it. Measured earlier: SDL's KMSDRM swap costs 17 ms (one 60 Hz frame of waiting, see
 * measurements.txt section 4), while today's path writes 640x480 into /dev/fb0 in 0.55-0.77 ms. A backend that takes a
 * window would trade a 1.2-2.8 ms rasteriser for a 17 ms wait. So the renderer must run offscreen and hand the picture
 * back for the fb0 present - which needs an EGL context with no window at all.
 *
 * Device facts this relies on: libEGL.so, libGLESv2.so and libgbm.so are all symlinks to libmali.so, and /dev/dri/card0
 * exists. Three things are checked here:
 *   A. EGL_PLATFORM_GBM_KHR + a pbuffer surface + a GLES context, with no DRM master and no mode change.
 *   B. That the framebuffer/texture path works there: an RGBA8 1024x512 FBO, a draw, glReadPixels back.
 *   C. That it works while EmulationStation owns the display (the state a game runs in is the opposite, but if it
 *      survives this, it certainly survives that).
 */
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Old EGL headers (this sysroot's) do not define the KHR platform constant, and libEGL exports
 * eglGetPlatformDisplayEXT only through eglGetProcAddress. */
#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#define VRAM_W 1024
#define VRAM_H 512

static double now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static const char *VS =
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *FS =
    "precision highp float;\n"
    "uniform vec4 u_col;\n"
    "void main() { gl_FragColor = u_col; }\n";

int main(void)
{
    int fd, major = 0, minor = 0;
    struct gbm_device *gbm;
    EGLDisplay dpy;
    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    EGLSurface surf;
    EGLContext ctx;
    EGLint api = EGL_OPENGL_ES_API;
    static const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                      EGL_DEPTH_SIZE, 16, EGL_NONE};
    static const EGLint pb_attr[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("open /dev/dri/card0 failed (errno %d); trying renderD128\n", errno);
        fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            printf("no DRI node at all (errno %d)\n", errno);
            return 1;
        }
    }
    gbm = gbm_create_device(fd);
    if (gbm == NULL) {
        printf("gbm_create_device failed: this libmali has no GBM backend\n");
        return 1;
    }
    printf("A. gbm device ok (backend '%s')\n", gbm_device_get_backend_name(gbm));

    {
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

        if (get_platform_display == NULL) {
            printf("   eglGetPlatformDisplayEXT is not in this libEGL\n");
            return 1;
        }
        dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    }
    if (dpy == EGL_NO_DISPLAY) {
        printf("   eglGetPlatformDisplayEXT(GBM) failed: 0x%x\n", (unsigned)eglGetError());
        return 1;
    }
    if (!eglInitialize(dpy, &major, &minor)) {
        printf("   eglInitialize failed: 0x%x\n", (unsigned)eglGetError());
        return 1;
    }
    printf("   EGL %d.%d, vendor '%s', version '%s'\n", major, minor, eglQueryString(dpy, EGL_VENDOR),
           eglQueryString(dpy, EGL_VERSION));
    if (!eglBindAPI(api)) {
        printf("   eglBindAPI(ES) failed: 0x%x\n", (unsigned)eglGetError());
        return 1;
    }
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
        printf("   eglChooseConfig found no pbuffer config (0x%x)\n", (unsigned)eglGetError());
        return 1;
    }
    surf = eglCreatePbufferSurface(dpy, cfg, pb_attr);
    if (surf == EGL_NO_SURFACE) {
        printf("   eglCreatePbufferSurface failed: 0x%x\n", (unsigned)eglGetError());
        return 1;
    }
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        printf("   eglCreateContext failed: 0x%x\n", (unsigned)eglGetError());
        return 1;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        printf("   eglMakeCurrent failed: 0x%x\n", (unsigned)eglGetError());
        return 1;
    }
    printf("   context current: %s | %s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

    /* B: the VRAM-shaped target, a draw, and a readback. */
    {
        GLuint tex = 0, fb = 0, prog, vs, fs, vbo;
        static unsigned char px[VRAM_W * VRAM_H * 4];
        static const GLfloat tri[] = {-1, -1, 3, -1, -1, 3};
        GLint ok = 0;

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VRAM_W, VRAM_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glGenFramebuffers(1, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        printf("B. FBO 1024x512 RGBA8: 0x%x (%s)\n", (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER),
               glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? "complete" : "INCOMPLETE");
        vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &VS, NULL);
        glCompileShader(vs);
        fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &FS, NULL);
        glCompileShader(fs);
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        glUseProgram(prog);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
        glViewport(0, 0, VRAM_W, VRAM_H);
        glUniform4f(glGetUniformLocation(prog, "u_col"), 0.25f, 0.5f, 0.75f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        printf("   draw + readback: program %s, pixel (%d,%d,%d,%d) : %s\n", ok ? "linked" : "FAILED", px[0], px[1],
               px[2], px[3], (px[0] == 64 && px[1] == 128 && px[2] == 191) ? "PASS" : "FAIL");

        /* C: cost of a full readback here, for comparison with the 1.95 ms measured through SDL. */
        {
            double t0 = now_ms();
            int i;

            for (i = 0; i < 100; i++) {
                glReadPixels(0, 0, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
                glFinish();
            }
            printf("C. glReadPixels 1024x512 RGBA (4 per sync, /4): %.3f ms each\n", (now_ms() - t0) / 100.0);
        }
        {
            double t0 = now_ms();
            int i;
            static unsigned short field[320 * 240];

            for (i = 0; i < 100; i++) {
                glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, field);
                glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, field);
                glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, field);
                glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, field);
                glFinish();
            }
            printf("   glReadPixels 320x240 RGBA (4 per sync, /4): %.3f ms each\n", (now_ms() - t0) / 400.0);
        }
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
