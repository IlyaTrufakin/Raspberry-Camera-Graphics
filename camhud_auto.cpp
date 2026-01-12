#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <iostream>
#include <memory>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <libcamera/libcamera.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>

using namespace libcamera;

/* ================= AUTO-DETECT GL ES VERSION ================= */

void print_gl_info()
{
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* glsl_version = glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("GL Renderer  : %s\n", renderer);
    printf("GL Version   : %s\n", version);
    printf("GLSL Version : %s\n", glsl_version);
}

/* ================= SHADERS (GLES 2.0/3.x compatible) ================= */

// Vertex shader для видео
const char *video_vs_src = R"(
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

// Fragment shader для YUV420 -> RGB конверсии
// Без #version = GLSL ES 1.00 (совместимо с GLES 2.0+)
const char* video_fs_src = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTextureY;
uniform sampler2D uTextureU;
uniform sampler2D uTextureV;

void main() {
    // ITU-R BT.601 YUV to RGB conversion
    float y = texture2D(uTextureY, vTexCoord).r;
    float u = texture2D(uTextureU, vTexCoord).r - 0.5;
    float v = texture2D(uTextureV, vTexCoord).r - 0.5;

    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;

    gl_FragColor = vec4(r, g, b, 1.0);
}
)";

// Vertex shader для overlay графики
const char *overlay_vs_src = R"(
attribute vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Fragment shader для overlay (с переменным цветом)
const char* overlay_fs_src = R"(
precision mediump float;
uniform vec4 uColor;
void main() {
    gl_FragColor = uColor;
}
)";

GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        printf("Shader compile error: %s\n", log);
    }
    return s;
}

GLuint make_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        printf("Program link error: %s\n", log);
    }

    return p;
}

// Отрисовка прицела (крест в центре)
void draw_crosshair(GLuint prog, GLint aPos, GLint uColor)
{
    float size = 0.05f;
    float thickness = 0.005f;

    glUseProgram(prog);
    glUniform4f(uColor, 1.0f, 0.0f, 0.0f, 0.9f); // Красный

    // Горизонтальная линия
    float h_line[] = {
        -size, -thickness,
        size, -thickness,
        -size, thickness,
        size, thickness
    };

    glEnableVertexAttribArray(aPos);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, h_line);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Вертикальная линия
    float v_line[] = {
        -thickness, -size,
        thickness, -size,
        -thickness, size,
        thickness, size
    };

    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, v_line);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(aPos);
}

// HUD блок с рамкой
void draw_hud_box(GLuint prog, GLint aPos, GLint uColor, float x, float y, float w, float h)
{
    glUseProgram(prog);

    // Черный полупрозрачный фон
    float bg[] = { x, y, x + w, y, x, y + h, x + w, y + h };
    glUniform4f(uColor, 0.0f, 0.0f, 0.0f, 0.6f);
    glEnableVertexAttribArray(aPos);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, bg);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Зеленая рамка
    float border[] = { x, y, x + w, y, x + w, y + h, x, y + h };
    glUniform4f(uColor, 0.0f, 1.0f, 0.0f, 1.0f);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, border);
    glDrawArrays(GL_LINE_LOOP, 0, 4);

    glDisableVertexAttribArray(aPos);
}

int main()
{
    printf("=== CamHUD - Camera HUD Display ===\n\n");

    /* ================= DRM/KMS SETUP ================= */
    int drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        perror("Failed to open DRM device");
        return 1;
    }

    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) {
        fprintf(stderr, "Failed to get DRM resources\n");
        return 1;
    }

    drmModeConnector *conn = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes)
            break;
        drmModeFreeConnector(conn);
        conn = nullptr;
    }

    if (!conn) {
        fprintf(stderr, "No connected display found\n");
        return 1;
    }

    // Поиск режима 1920x1080
    drmModeModeInfo mode;
    bool found_1080p = false;
    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].hdisplay == 1920 && conn->modes[i].vdisplay == 1080) {
            mode = conn->modes[i];
            found_1080p = true;
            printf("Found 1920x1080 mode: %s @ %d Hz\n",
                   mode.name, mode.vrefresh);
            break;
        }
    }

    if (!found_1080p) {
        printf("1920x1080 not available, using: %dx%d @ %d Hz\n",
               conn->modes[0].hdisplay, conn->modes[0].vdisplay,
               conn->modes[0].vrefresh);
        mode = conn->modes[0];
    }

    drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
    uint32_t crtc_id = 0;
    for (int i = 0; i < res->count_crtcs; i++)
        if (enc->possible_crtcs & (1 << i))
            crtc_id = res->crtcs[i];

    /* ================= GBM ================= */
    gbm_device *gbm = gbm_create_device(drm_fd);
    gbm_surface *gbm_surf = gbm_surface_create(
        gbm,
        mode.hdisplay,
        mode.vdisplay,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
    );

    /* ================= EGL ================= */
    EGLDisplay egl_display = eglGetDisplay((EGLNativeDisplayType)gbm);
    eglInitialize(egl_display, nullptr, nullptr);

    EGLConfig cfg;
    EGLint n;
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(egl_display, cfg_attr, &cfg, 1, &n);

    // Пробуем GLES 3, если не получится - откатываемся на GLES 2
    EGLint ctx_attr_v3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(egl_display, cfg, EGL_NO_CONTEXT, ctx_attr_v3);

    if (ctx == EGL_NO_CONTEXT) {
        printf("GLES 3 not available, using GLES 2\n");
        EGLint ctx_attr_v2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        ctx = eglCreateContext(egl_display, cfg, EGL_NO_CONTEXT, ctx_attr_v2);
    } else {
        printf("Using GLES 3 context\n");
    }

    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, cfg,
                                                     (EGLNativeWindowType)gbm_surf, nullptr);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, ctx);

    glViewport(0, 0, mode.hdisplay, mode.vdisplay);

    // Вывод информации о GL
    print_gl_info();
    printf("\n");

    /* ================= LIBCAMERA SETUP ================= */
    std::unique_ptr<CameraManager> camera_manager = std::make_unique<CameraManager>();
    camera_manager->start();

    if (camera_manager->cameras().empty()) {
        fprintf(stderr, "WARNING: No cameras found - running in demo mode\n");
        // Продолжаем без камеры для демонстрации overlay
    }

    std::shared_ptr<Camera> camera;
    Stream *stream = nullptr;

    if (!camera_manager->cameras().empty()) {
        camera = camera_manager->cameras()[0];
        camera->acquire();

        printf("Camera: %s\n", camera->id().c_str());

        // Конфигурация для 1920x1080 YUV420
        std::unique_ptr<CameraConfiguration> config =
            camera->generateConfiguration({ StreamRole::VideoRecording });

        StreamConfiguration &streamConfig = config->at(0);
        streamConfig.size.width = 1920;
        streamConfig.size.height = 1080;
        streamConfig.pixelFormat = PixelFormat::fromString("YUV420");

        config->validate();
        camera->configure(config.get());

        FrameBufferAllocator *allocator = new FrameBufferAllocator(camera);
        stream = streamConfig.stream();
        allocator->allocate(stream);

        std::vector<std::unique_ptr<Request>> requests;
        for (const std::unique_ptr<FrameBuffer> &buffer : allocator->buffers(stream)) {
            std::unique_ptr<Request> request = camera->createRequest();
            request->addBuffer(stream, buffer.get());
            requests.push_back(std::move(request));
        }

        camera->start();
        for (std::unique_ptr<Request> &request : requests)
            camera->queueRequest(request.get());

        printf("Camera started: %dx%d YUV420\n\n",
               streamConfig.size.width, streamConfig.size.height);
    }

    /* ================= GL SETUP ================= */
    GLuint video_prog = make_program(video_vs_src, video_fs_src);
    GLuint overlay_prog = make_program(overlay_vs_src, overlay_fs_src);

    GLint video_aPos = glGetAttribLocation(video_prog, "aPos");
    GLint video_aTexCoord = glGetAttribLocation(video_prog, "aTexCoord");

    GLint overlay_aPos = glGetAttribLocation(overlay_prog, "aPos");
    GLint overlay_uColor = glGetUniformLocation(overlay_prog, "uColor");

    // Текстуры для YUV
    GLuint textures[3];
    glGenTextures(3, textures);
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* ===== FIRST FRAME FOR DRM ===== */
    glClearColor(0, 0, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(egl_display, egl_surface);

    gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    uint32_t fb;
    drmModeAddFB(
        drm_fd,
        mode.hdisplay, mode.vdisplay,
        24, 32,
        gbm_bo_get_stride(bo),
        gbm_bo_get_handle(bo).u32,
        &fb
    );

    drmModeSetCrtc(drm_fd, crtc_id, fb, 0, 0,
                   &conn->connector_id, 1, &mode);

    printf("Starting render loop (Press Ctrl+C to exit)...\n");

    /* ================= RENDER LOOP ================= */
    int frame_count = 0;
    int demo_speed = 0;
    int demo_altitude = 100;

    while (true) {
        glClear(GL_COLOR_BUFFER_BIT);

        // TODO: Render camera frame here (YUV textures)
        // Для демо просто темный фон

        // Рендерим HUD overlay
        glUseProgram(overlay_prog);

        // Прицел
        draw_crosshair(overlay_prog, overlay_aPos, overlay_uColor);

        // HUD блоки
        draw_hud_box(overlay_prog, overlay_aPos, overlay_uColor,
                     -0.98f, 0.83f, 0.20f, 0.10f);  // Левый верхний
        draw_hud_box(overlay_prog, overlay_aPos, overlay_uColor,
                     0.78f, 0.83f, 0.20f, 0.10f);   // Правый верхний
        draw_hud_box(overlay_prog, overlay_aPos, overlay_uColor,
                     -0.98f, -0.93f, 0.20f, 0.10f); // Левый нижний
        draw_hud_box(overlay_prog, overlay_aPos, overlay_uColor,
                     0.78f, -0.93f, 0.20f, 0.10f);  // Правый нижний

        eglSwapBuffers(egl_display, egl_surface);

        frame_count++;
        demo_speed = (demo_speed + 1) % 200;
        demo_altitude = 100 + (frame_count % 50);

        usleep(16666); // ~60 FPS
    }

    /* ================= CLEANUP ================= */
    if (camera) {
        camera->stop();
        camera->release();
    }
    camera_manager->stop();

    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, ctx);
    eglTerminate(egl_display);
    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm);
    close(drm_fd);

    return 0;
}
