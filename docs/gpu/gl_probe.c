/* What GL does this console actually give us through SDL2? The GPU study (see docs/gpu/README.md)
 * needs four answers before any backend design:
 *   1. Which GLES version is exposed (2.0 vs 3.x changes texelFetch/instancing/VAO availability).
 *   2. Whether GL_EXT_shader_framebuffer_fetch exists - DuckStation treats it as the accurate path
 *      for per-pixel semi-transparency and mask-bit testing.
 *   3. Whether the fragment stage has highp float. Mediump would break texel coordinates in a
 *      1024-wide VRAM texture, which is the whole point of sampling VRAM as a texture.
 *   4. Frame timing of an actual FBO round trip, so "the GPU is fast enough" is measured, not assumed.
 *
 * Prints, then exits. Takes the screen for a second (SDL window), so it must not run while the game is up.
 */
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static void print_gl_strings(const char *tag)
{
    printf("[%s] GL_VERSION  : %s\n", tag, (const char *)glGetString(GL_VERSION));
    printf("[%s] GL_VENDOR   : %s\n", tag, (const char *)glGetString(GL_VENDOR));
    printf("[%s] GL_RENDERER : %s\n", tag, (const char *)glGetString(GL_RENDERER));
    printf("[%s] GL_SL_VER   : %s\n", tag, (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
}

static void print_precision(const char *tag)
{
    static const struct {
        const char *name;
        GLenum type;
    } types[] = {{"LOW_FLOAT", GL_LOW_FLOAT}, {"MEDIUM_FLOAT", GL_MEDIUM_FLOAT}, {"HIGH_FLOAT", GL_HIGH_FLOAT}};
    static const struct {
        const char *name;
        GLenum shader;
    } stages[] = {{"frag", GL_FRAGMENT_SHADER}, {"vert", GL_VERTEX_SHADER}};
    int s, t;

    for (s = 0; s < 2; s++) {
        for (t = 0; t < 3; t++) {
            GLint range[2] = {0, 0}, bits = 0;

            glGetShaderPrecisionFormat(stages[s].shader, types[t].type, range, &bits);
            printf("[%s] precision %s %-12s: %d bits, range 2^%d..2^%d\n", tag, stages[s].name, types[t].name,
                   (int)bits, (int)range[0], (int)range[1]);
        }
    }
}

static void print_extension(const char *tag, const char *ext)
{
    const char *list = (const char *)glGetString(GL_EXTENSIONS);

    printf("[%s] extension %-40s: %s\n", tag, ext,
           (list != NULL && (strstr(list, ext) != NULL)) ? "YES" : "no");
}

static void print_extends(const char *tag)
{
    static const char *interesting[] = {
        "GL_EXT_shader_framebuffer_fetch",
        "GL_EXT_shader_framebuffer_fetch_non_coherent",
        "GL_ARM_shader_framebuffer_fetch",
        "GL_EXT_blend_func_extended",
        "GL_EXT_discard_framebuffer",
        "GL_EXT_multisampled_render_to_texture",
        "GL_OES_rgb8_rgba8",
        "GL_OES_depth24",
        "GL_EXT_texture_format_BGRA8888",
        "GL_EXT_unpack_subimage",
        "GL_OES_mapbuffer",
        "GL_OES_vertex_array_object",
        "GL_EXT_instanced_arrays",
        "GL_NV_draw_buffers",
        "GL_EXT_color_buffer_half_float",
        "GL_ARM_mali_program_binary",
        "GL_ARM_mali_shader_binary",
    };
    unsigned i;

    for (i = 0; i < sizeof(interesting) / sizeof(interesting[0]); i++)
        print_extension(tag, interesting[i]);
}

/* Minimal shaders: sample a 1024x512 RGBA8 texture that lives in an FBO, nearest, and write it out.
 * That is exactly the "VRAM as a texture" shape, so the timing below is the shape we would ship. */
static GLuint make_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};

        glGetShaderInfoLog(s, sizeof(log) - 1, NULL, log);
        printf("shader compile failed: %s\n", log);
    }
    return s;
}

static const char *VS =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

/* precision highp is the point of the test: without it the 0..1023 texel coordinates break.
 * The clock-free path is just texture2D with NEAREST, no texelFetch (that is GLES3 only). */
static const char *FS =
    "precision highp float;\n"
    "uniform sampler2D u_vram;\n"
    "uniform vec2 u_size;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 texel = floor(v_uv);\n"
    "  vec4 a = texture2D(u_vram, (texel + 0.5) / u_size);\n"
    "  /* the CLUT indirection: split the packed index out of the texel, then fetch the palette row */\n"
    "  float idx = floor(a.r * 255.0 + 0.5);\n"
    "  vec2 pal = vec2(idx, 0.0);\n"
    "  gl_FragColor = texture2D(u_vram, (pal + 0.5) / u_size);\n"
    "}\n";

int main(void)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint fb, tex, prog, vs, fs, vbo;
    GLint loc_pos, loc_uv, loc_vram, loc_size;
    GLenum err;
    int ext_count = 0, i;
    const char *exts;

    printf("=== GL probe (SDL2 %d.%d.%d) ===\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());

    win = SDL_CreateWindow("probe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                           SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
    if (win == NULL) {
        printf("window: %s\n", SDL_GetError());
        return 1;
    }
    ctx = SDL_GL_CreateContext(win);
    if (ctx == NULL) {
        printf("GL context (ES 2.0): %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(win, ctx);

    print_gl_strings("es2");
    exts = (const char *)glGetString(GL_EXTENSIONS);
    if (exts != NULL) {
        /* Just the count, so the interesting list below stays readable. */
        for (i = 0; exts[i] != '\0'; i++)
            if (exts[i] == ' ')
                ext_count++;
        printf("[es2] %d extensions\n", ext_count + 1);
    }
    print_extends("es2");
    print_precision("es2");
    {
        GLint v = 0;

        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
        printf("[es2] GL_MAX_TEXTURE_SIZE   : %d\n", (int)v);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &v);
        printf("[es2] MAX_RENDERBUFFER_SIZE : %d\n", (int)v);
        glGetIntegerv(GL_MAX_VARYING_VECTORS, &v);
        printf("[es2] MAX_VARYING_VECTORS   : %d\n", (int)v);
    }
    {
        GLint dims[2] = {0, 0};

        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dims);
        printf("[es2] MAX_VIEWPORT_DIMS     : %dx%d\n", (int)dims[0], (int)dims[1]);
    }

    /* Can we get an ES 3.x context at all? If yes, texelFetch/VAO/instancing are on the table. */
    {
        SDL_GLContext ctx3;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        ctx3 = SDL_GL_CreateContext(win);
        if (ctx3 != NULL) {
            SDL_GL_MakeCurrent(win, ctx3);
            print_gl_strings("es3");
            SDL_GL_DeleteContext(ctx3);
        } else {
            printf("[es3] ES 3.0 context: %s\n", SDL_GetError());
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_MakeCurrent(win, ctx);
    }

    /* Build the VRAM-shaped pipeline: an RGBA8 FBO of 1024x512, sampled nearest, twice per pixel. */
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    printf("[fbo] 1024x512 RGBA8 status: 0x%x (complete=0x%x)\n", (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER),
           (unsigned)GL_FRAMEBUFFER_COMPLETE);

    vs = make_shader(GL_VERTEX_SHADER, VS);
    fs = make_shader(GL_FRAGMENT_SHADER, FS);
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    {
        GLint ok = 0;

        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        printf("[prog] link: %s\n", ok ? "ok" : "FAILED");
    }
    glUseProgram(prog);
    loc_pos = glGetAttribLocation(prog, "a_pos");
    loc_uv = glGetAttribLocation(prog, "a_uv");
    loc_vram = glGetUniformLocation(prog, "u_vram");
    loc_size = glGetUniformLocation(prog, "u_size");

    /* One screen quad, 640x480 pixels, each pixel doing two dependent fetches into the 1024x512 texture. */
    {
        static const GLfloat quad[] = {-1, -1, 0, 480, 1, -1, 640, 480, -1, 1, 0, 0, 1, 1, 640, 0};

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray((GLuint)loc_pos);
        glVertexAttribPointer((GLuint)loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
        glEnableVertexAttribArray((GLuint)loc_uv);
        glVertexAttribPointer((GLuint)loc_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                              (void *)(2 * sizeof(GLfloat)));
        glUniform1i(loc_vram, 0);
        glUniform2f(loc_size, 1024.0f, 512.0f);
    }
    while ((err = glGetError()) != GL_NO_ERROR)
        printf("[err] 0x%x\n", (unsigned)err);

    {
        /* 200 frames of a 640x480 quad with 2 dependent fetches per pixel; read one pixel at the end via
         * glReadPixels so the driver cannot skip the work. */
        Uint64 t0, t1;
        unsigned char px[4] = {0, 0, 0, 0};
        int f;

        glFinish();
        t0 = SDL_GetPerformanceCounter();
        for (f = 0; f < 200; f++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glViewport(0, 0, 1024, 512);
            glClear(GL_COLOR_BUFFER_BIT);
            glViewport(0, 0, 1024, 512);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        glFinish();
        t1 = SDL_GetPerformanceCounter();
        printf("[time] 200 frames of 640x480 with 2 fetches/px into 1024x512 FBO: %.3f ms/frame\n",
               (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency() / 200.0);

        glReadPixels(512, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        printf("[readback] %d %d %d %d\n", px[0], px[1], px[2], px[3]);
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
