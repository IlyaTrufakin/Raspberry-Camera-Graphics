#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <iostream>
#include <memory>
#include <map>
#include <sys/mman.h>
#include <poll.h>

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

/* ================= SHADERS ================= */

// Vertex shader для видео (fullscreen quad)
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
const char* video_fs_src = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTextureY;
uniform sampler2D uTextureU;
uniform sampler2D uTextureV;

void main() {
    float y = texture2D(uTextureY, vTexCoord).r;
    float u = texture2D(uTextureU, vTexCoord).r - 0.5;
    float v = texture2D(uTextureV, vTexCoord).r - 0.5;

    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;

    gl_FragColor = vec4(r, g, b, 1.0);
}
)";

// Vertex shader для графики
const char *overlay_vs_src = R"(
attribute vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Fragment shader для графики (цветные примитивы)
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

/* ================= BITMAP FONT ================= */
// Простой 5x7 bitmap font для ASCII символов
// Каждый символ - 5 байт (5 колонок, 7 рядов)
const unsigned char font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space (32)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0 (48)
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A (65)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

// Рисуем один символ в позиции (x, y) с заданным размером
void draw_char(GLuint prog, GLint aPos, GLint uColor, float x, float y, char c, float size, float r, float g, float b, float a)
{
    if (c < 32 || c > 90) c = 32; // Поддерживаем только ASCII 32-90

    const unsigned char *glyph = font5x7[c - 32];
    float pixel_size = size / 7.0f; // Размер одного пикселя

    glUseProgram(prog);
    glUniform4f(uColor, r, g, b, a);
    glEnableVertexAttribArray(aPos);

    // Рисуем каждый пиксель символа
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (glyph[col] & (1 << row)) {
                float px = x + col * pixel_size;
                float py = y - row * pixel_size;

                float quad[] = {
                    px, py,
                    px + pixel_size, py,
                    px, py - pixel_size,
                    px + pixel_size, py - pixel_size
                };

                glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, quad);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }
    }

    glDisableVertexAttribArray(aPos);
}

// Рисуем строку текста
void draw_text(GLuint prog, GLint aPos, GLint uColor, float x, float y, const char* text, float size, float r, float g, float b, float a)
{
    float char_width = size * 6.0f / 7.0f; // Ширина символа + промежуток
    float cur_x = x;

    for (int i = 0; text[i] != '\0'; i++) {
        draw_char(prog, aPos, uColor, cur_x, y, text[i], size, r, g, b, a);
        cur_x += char_width;
    }
}

// Рисуем число
void draw_number(GLuint prog, GLint aPos, GLint uColor, float x, float y, int value, float size, float r, float g, float b, float a)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    draw_text(prog, aPos, uColor, x, y, buffer, size, r, g, b, a);
}

// Рисуем прицел (крест)
void draw_crosshair(GLuint prog, GLint aPos, GLint uColor)
{
    float size = 0.05f;
    float thickness = 0.005f;

    // Горизонтальная линия
    float h_line[] = {
        -size, -thickness,
        size, -thickness,
        -size, thickness,
        size, thickness
    };

    glUseProgram(prog);
    glUniform4f(uColor, 1.0f, 0.0f, 0.0f, 0.9f); // Красный

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

// Рисуем прямоугольник с текстовым значением (HUD элемент)
void draw_hud_value(GLuint prog, GLint aPos, GLint uColor, float x, float y, int value)
{
    // Фон для значения
    float w = 0.15f, h = 0.08f;
    float bg[] = {
        x, y,
        x + w, y,
        x, y + h,
        x + w, y + h
    };

    glUseProgram(prog);
    glUniform4f(uColor, 0.0f, 0.0f, 0.0f, 0.6f); // Черный полупрозрачный фон

    glEnableVertexAttribArray(aPos);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, bg);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Рамка
    glUniform4f(uColor, 0.0f, 1.0f, 0.0f, 1.0f); // Зеленая рамка
    float border[] = {
        x, y,
        x + w, y,
        x + w, y + h,
        x, y + h
    };
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 0, border);
    glDrawArrays(GL_LINE_LOOP, 0, 4);

    glDisableVertexAttribArray(aPos);

    // Рисуем числовое значение внутри
    draw_number(prog, aPos, uColor, x + 0.01f, y + 0.065f, value, 0.04f, 1.0f, 1.0f, 1.0f, 1.0f);
}

int main()
{
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

    // Используем максимальное доступное разрешение
    drmModeModeInfo mode = conn->modes[0]; // По умолчанию первый режим (обычно самый большой)

    printf("Using display mode: %dx%d @ %dHz\n",
           mode.hdisplay, mode.vdisplay, mode.vrefresh);

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

    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(egl_display, cfg, EGL_NO_CONTEXT, ctx_attr);
    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, cfg, (EGLNativeWindowType)gbm_surf, nullptr);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, ctx);

    // Отключаем VSync для минимальной задержки
    eglSwapInterval(egl_display, 0);

    glViewport(0, 0, mode.hdisplay, mode.vdisplay);

    /* ================= LIBCAMERA SETUP ================= */
    std::unique_ptr<CameraManager> camera_manager = std::make_unique<CameraManager>();
    camera_manager->start();

    if (camera_manager->cameras().empty()) {
        fprintf(stderr, "No cameras found\n");
        return 1;
    }

    std::shared_ptr<Camera> camera = camera_manager->cameras()[0];
    camera->acquire();

    // Конфигурация камеры - используем разрешение дисплея
    std::unique_ptr<CameraConfiguration> config =
        camera->generateConfiguration({ StreamRole::VideoRecording });

    StreamConfiguration &streamConfig = config->at(0);
    streamConfig.size.width = mode.hdisplay;
    streamConfig.size.height = mode.vdisplay;
    streamConfig.pixelFormat = PixelFormat::fromString("YUV420");

    // Минимальная буферизация для низкой задержки
    streamConfig.bufferCount = 2; // Минимум буферов

    config->validate();
    camera->configure(config.get());

    // Создаем буферы
    FrameBufferAllocator *allocator = new FrameBufferAllocator(camera);
    Stream *stream = streamConfig.stream();
    allocator->allocate(stream);

    std::vector<std::unique_ptr<Request>> requests;
    for (const std::unique_ptr<FrameBuffer> &buffer : allocator->buffers(stream)) {
        std::unique_ptr<Request> request = camera->createRequest();
        request->addBuffer(stream, buffer.get());
        requests.push_back(std::move(request));
    }

    /* ================= GL SETUP ================= */
    GLuint video_prog = make_program(video_vs_src, video_fs_src);
    GLuint overlay_prog = make_program(overlay_vs_src, overlay_fs_src);

    GLint video_aPos = glGetAttribLocation(video_prog, "aPos");
    GLint video_aTexCoord = glGetAttribLocation(video_prog, "aTexCoord");
    GLint video_uTextureY = glGetUniformLocation(video_prog, "uTextureY");
    GLint video_uTextureU = glGetUniformLocation(video_prog, "uTextureU");
    GLint video_uTextureV = glGetUniformLocation(video_prog, "uTextureV");

    GLint overlay_aPos = glGetAttribLocation(overlay_prog, "aPos");
    GLint overlay_uColor = glGetUniformLocation(overlay_prog, "uColor");

    // Создаем текстуры для YUV420
    GLuint textures[3];
    glGenTextures(3, textures);

    // Предварительно выделяем память для текстур
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mode.hdisplay, mode.vdisplay, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mode.hdisplay / 2, mode.vdisplay / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, textures[2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mode.hdisplay / 2, mode.vdisplay / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    // Fullscreen quad для видео
    float quad_vertices[] = {
        -1.0f, -1.0f,  0.0f, 1.0f,  // bottom-left
         1.0f, -1.0f,  1.0f, 1.0f,  // bottom-right
        -1.0f,  1.0f,  0.0f, 0.0f,  // top-left
         1.0f,  1.0f,  1.0f, 0.0f   // top-right
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* ===== FIRST FRAME FOR DRM ===== */
    glClearColor(0.0, 0.0, 0.2, 1.0); // Темно-синий для видимости
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(egl_display, egl_surface);

    gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surf);
    uint32_t fb;
    drmModeAddFB(
        drm_fd,
        mode.hdisplay,
        mode.vdisplay,
        24, 32,
        gbm_bo_get_stride(bo),
        gbm_bo_get_handle(bo).u32,
        &fb
    );

    drmModeSetCrtc(
        drm_fd,
        crtc_id,
        fb,
        0, 0,
        &conn->connector_id,
        1,
        &mode
    );

    /* ================= START CAMERA ================= */

    // Mapping для хранения requests
    std::map<FrameBuffer *, std::unique_ptr<Request>> pending_requests;

    camera->start();
    for (std::unique_ptr<Request> &request : requests) {
        FrameBuffer *buffer = request->buffers().begin()->second;
        camera->queueRequest(request.get());
        pending_requests[buffer] = std::move(request);
    }

    printf("Camera started. Resolution: %dx%d (low latency mode)\n", mode.hdisplay, mode.vdisplay);

    /* ================= RENDER LOOP ================= */
    int frame_count = 0;
    int demo_value = 0;
    FrameBuffer *last_frame = nullptr;

    gbm_bo *previous_bo = bo;
    uint32_t previous_fb = fb;

    while (true) {
        // Находим любой завершенный request
        Request *completed_req = nullptr;

        for (auto &pair : pending_requests) {
            if (pair.second && pair.second->status() == Request::RequestComplete) {
                completed_req = pair.second.get();
                break; // Берем первый попавшийся готовый кадр
            }
        }

        // Если получили кадр, загружаем его в текстуры
        if (completed_req) {
            last_frame = completed_req->buffers().begin()->second;

            const FrameBuffer::Plane &y_plane = last_frame->planes()[0];
            const FrameBuffer::Plane &u_plane = last_frame->planes()[1];
            const FrameBuffer::Plane &v_plane = last_frame->planes()[2];

            // Мапим весь буфер один раз (все плоскости в одном буфере)
            size_t total_size = y_plane.offset + y_plane.length;
            if (u_plane.offset + u_plane.length > total_size)
                total_size = u_plane.offset + u_plane.length;
            if (v_plane.offset + v_plane.length > total_size)
                total_size = v_plane.offset + v_plane.length;

            void *mapped = mmap(nullptr, total_size, PROT_READ, MAP_SHARED, y_plane.fd.get(), 0);

            if (mapped != MAP_FAILED) {
                // Указатели на начало каждой плоскости с учетом offset
                uint8_t *y_src = (uint8_t*)mapped + y_plane.offset;
                uint8_t *u_src = (uint8_t*)mapped + u_plane.offset;
                uint8_t *v_src = (uint8_t*)mapped + v_plane.offset;

                // Вычисляем stride из length (для YUV420: stride = length / height)
                uint32_t y_stride = y_plane.length / streamConfig.size.height;
                uint32_t u_stride = u_plane.length / (streamConfig.size.height / 2);
                uint32_t v_stride = v_plane.length / (streamConfig.size.height / 2);

                // Загружаем в текстуры напрямую с использованием GL_UNPACK_ROW_LENGTH_EXT
                // если stride совпадает, или с построчным копированием если нет
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, textures[0]);
                if (y_stride == streamConfig.size.width) {
                    // Stride совпадает - обновляем напрямую без копирования
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   streamConfig.size.width, streamConfig.size.height,
                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, y_src);
                } else {
                    // Есть padding - используем GL_UNPACK_ROW_LENGTH_EXT
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, y_stride);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   streamConfig.size.width, streamConfig.size.height,
                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, y_src);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
                }

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, textures[1]);
                if (u_stride == streamConfig.size.width / 2) {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   streamConfig.size.width / 2, streamConfig.size.height / 2,
                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, u_src);
                } else {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, u_stride);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   streamConfig.size.width / 2, streamConfig.size.height / 2,
                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, u_src);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
                }

                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, textures[2]);
                if (v_stride == streamConfig.size.width / 2) {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   streamConfig.size.width / 2, streamConfig.size.height / 2,
                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, v_src);
                } else {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, v_stride);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                   streamConfig.size.width / 2, streamConfig.size.height / 2,
                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, v_src);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
                }

                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

                munmap(mapped, total_size);
            }

            // Возвращаем request обратно
            completed_req->reuse(Request::ReuseBuffers);
            camera->queueRequest(completed_req);

            // Рендерим только когда получили новый кадр
        } else {
            // Нет нового кадра - короткая задержка чтобы не перегружать CPU
            usleep(100); // 0.1ms
            continue;
        }

        // Очищаем экран
        glClear(GL_COLOR_BUFFER_BIT);

        // 1. Рендерим видео
        if (last_frame) {
            glUseProgram(video_prog);

            glUniform1i(video_uTextureY, 0);
            glUniform1i(video_uTextureU, 1);
            glUniform1i(video_uTextureV, 2);

            glEnableVertexAttribArray(video_aPos);
            glEnableVertexAttribArray(video_aTexCoord);
            glVertexAttribPointer(video_aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad_vertices);
            glVertexAttribPointer(video_aTexCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad_vertices + 2);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisableVertexAttribArray(video_aPos);
            glDisableVertexAttribArray(video_aTexCoord);
        }

        // 2. Рендерим HUD overlay поверх видео
        glUseProgram(overlay_prog);

        // Прицел в центре
        draw_crosshair(overlay_prog, overlay_aPos, overlay_uColor);

        // HUD элементы в углах
        draw_hud_value(overlay_prog, overlay_aPos, overlay_uColor, -0.95f, 0.85f, demo_value);
        draw_hud_value(overlay_prog, overlay_aPos, overlay_uColor, 0.70f, 0.85f, frame_count);

        // Текстовые метки
        draw_text(overlay_prog, overlay_aPos, overlay_uColor, -0.95f, -0.95f, "SPEED", 0.03f, 1.0f, 1.0f, 0.0f, 0.9f);
        draw_text(overlay_prog, overlay_aPos, overlay_uColor, 0.70f, -0.95f, "FPS", 0.03f, 1.0f, 1.0f, 0.0f, 0.9f);

        // Swap EGL buffers
        eglSwapBuffers(egl_display, egl_surface);

        // DRM page flip - обновляем экран
        gbm_bo *next_bo = gbm_surface_lock_front_buffer(gbm_surf);
        uint32_t next_fb;

        uint32_t handle = gbm_bo_get_handle(next_bo).u32;
        uint32_t pitch = gbm_bo_get_stride(next_bo);
        uint32_t offset = 0;

        int ret = drmModeAddFB2(
            drm_fd,
            mode.hdisplay,
            mode.vdisplay,
            GBM_FORMAT_XRGB8888,
            &handle,
            &pitch,
            &offset,
            &next_fb,
            0
        );

        if (ret) {
            fprintf(stderr, "Failed to create FB: %d\n", ret);
            gbm_surface_release_buffer(gbm_surf, next_bo);
            continue;
        }

        // Используем обычный SetCrtc вместо PageFlip для простоты
        ret = drmModeSetCrtc(
            drm_fd,
            crtc_id,
            next_fb,
            0, 0,
            &conn->connector_id,
            1,
            &mode
        );

        if (ret) {
            fprintf(stderr, "Failed to set CRTC: %d\n", ret);
        }

        // Освобождаем предыдущий буфер
        if (previous_fb) {
            drmModeRmFB(drm_fd, previous_fb);
        }
        if (previous_bo) {
            gbm_surface_release_buffer(gbm_surf, previous_bo);
        }

        previous_bo = next_bo;
        previous_fb = next_fb;

        frame_count++;
        demo_value = (demo_value + 1) % 100;
    }

    /* ================= CLEANUP ================= */
    camera->stop();
    camera->release();
    camera_manager->stop();

    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, ctx);
    eglTerminate(egl_display);
    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm);
    close(drm_fd);

    return 0;
}
