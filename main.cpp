#include "camera_stream.h"
#include "hud_overlay.h"
#include "modbus_client.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <iostream>
#include <cstring>
#include <chrono>

// GL_UNPACK_ROW_LENGTH_EXT может быть недоступно
#ifndef GL_UNPACK_ROW_LENGTH_EXT
#define GL_UNPACK_ROW_LENGTH_EXT 0x0CF2
#endif

// Структура для DRM ресурсов
struct DRMResources {
    int fd = -1;
    drmModeModeInfo mode;
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    drmModeCrtc *saved_crtc = nullptr;

    gbm_device *gbm_dev = nullptr;
    gbm_surface *gbm_surf = nullptr;

    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;
};

// Vertex shader для видео
const char* video_vs_src = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

// Fragment shader для видео (grayscale)
const char* video_fs_src = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTextureY;

void main() {
    float y = texture2D(uTextureY, vTexCoord).r;
    gl_FragColor = vec4(y, y, y, 1.0);
}
)";

bool initDRM(DRMResources& drm) {
    // Открытие DRM устройства
    drm.fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm.fd < 0) {
        std::cerr << "Failed to open DRM device" << std::endl;
        return false;
    }

    // Получение ресурсов
    drmModeRes *resources = drmModeGetResources(drm.fd);
    if (!resources) {
        std::cerr << "Failed to get DRM resources" << std::endl;
        return false;
    }

    // Поиск активного коннектора
    drmModeConnector *connector = nullptr;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm.fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            drm.connector_id = connector->connector_id;
            drm.mode = connector->modes[0];
            break;
        }
        drmModeFreeConnector(connector);
        connector = nullptr;
    }

    if (!connector) {
        std::cerr << "No connected connector found" << std::endl;
        drmModeFreeResources(resources);
        return false;
    }

    // Получение encoder и CRTC
    drmModeEncoder *encoder = drmModeGetEncoder(drm.fd, connector->encoder_id);
    if (encoder) {
        drm.crtc_id = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
    }

    drm.saved_crtc = drmModeGetCrtc(drm.fd, drm.crtc_id);

    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);

    std::cout << "DRM initialized: " << drm.mode.hdisplay << "x" << drm.mode.vdisplay << std::endl;
    return true;
}

bool initEGL(DRMResources& drm) {
    // Создание GBM устройства
    drm.gbm_dev = gbm_create_device(drm.fd);
    if (!drm.gbm_dev) {
        std::cerr << "Failed to create GBM device" << std::endl;
        return false;
    }

    // Создание GBM поверхности
    drm.gbm_surf = gbm_surface_create(drm.gbm_dev,
                                      drm.mode.hdisplay,
                                      drm.mode.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!drm.gbm_surf) {
        std::cerr << "Failed to create GBM surface" << std::endl;
        return false;
    }

    // Инициализация EGL
    drm.egl_display = eglGetDisplay((EGLNativeDisplayType)drm.gbm_dev);
    if (drm.egl_display == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get EGL display" << std::endl;
        return false;
    }

    if (!eglInitialize(drm.egl_display, nullptr, nullptr)) {
        std::cerr << "Failed to initialize EGL" << std::endl;
        return false;
    }

    // Выбор конфигурации
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(drm.egl_display, config_attribs, &egl_config, 1, &num_configs)) {
        std::cerr << "Failed to choose EGL config" << std::endl;
        return false;
    }

    // Создание контекста
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    drm.egl_context = eglCreateContext(drm.egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (drm.egl_context == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return false;
    }

    // Создание поверхности
    drm.egl_surface = eglCreateWindowSurface(drm.egl_display, egl_config,
                                             (EGLNativeWindowType)drm.gbm_surf, nullptr);
    if (drm.egl_surface == EGL_NO_SURFACE) {
        std::cerr << "Failed to create EGL surface" << std::endl;
        return false;
    }

    // Активация контекста
    if (!eglMakeCurrent(drm.egl_display, drm.egl_surface, drm.egl_surface, drm.egl_context)) {
        std::cerr << "Failed to make EGL context current" << std::endl;
        return false;
    }

    std::cout << "EGL initialized" << std::endl;
    return true;
}

void cleanupDRM(DRMResources& drm) {
    if (drm.saved_crtc) {
        drmModeSetCrtc(drm.fd, drm.saved_crtc->crtc_id, drm.saved_crtc->buffer_id,
                       drm.saved_crtc->x, drm.saved_crtc->y,
                       &drm.connector_id, 1, &drm.saved_crtc->mode);
        drmModeFreeCrtc(drm.saved_crtc);
    }

    if (drm.egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(drm.egl_display, drm.egl_context);
    }
    if (drm.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(drm.egl_display, drm.egl_surface);
    }
    if (drm.egl_display != EGL_NO_DISPLAY) {
        eglTerminate(drm.egl_display);
    }

    if (drm.gbm_surf) {
        gbm_surface_destroy(drm.gbm_surf);
    }
    if (drm.gbm_dev) {
        gbm_device_destroy(drm.gbm_dev);
    }

    if (drm.fd >= 0) {
        close(drm.fd);
    }
}

int main() {
    // Инициализация DRM и EGL
    DRMResources drm;
    if (!initDRM(drm)) {
        return 1;
    }
    if (!initEGL(drm)) {
        cleanupDRM(drm);
        return 1;
    }

    // Инициализация камеры
    CameraStream camera;
    CameraConfig cam_config;
    cam_config.width = 1012;
    cam_config.height = 760;
    cam_config.buffer_count = 2;
    cam_config.pixel_format = "YUV420";

    if (!camera.initialize(cam_config)) {
        std::cerr << "Failed to initialize camera" << std::endl;
        cleanupDRM(drm);
        return 1;
    }

    if (!camera.start()) {
        std::cerr << "Failed to start camera" << std::endl;
        cleanupDRM(drm);
        return 1;
    }

    // Инициализация HUD
    HUDOverlay hud;
    if (!hud.initialize(drm.mode.hdisplay, drm.mode.vdisplay)) {
        std::cerr << "Failed to initialize HUD" << std::endl;
        cleanupDRM(drm);
        return 1;
    }

    // Настройка прицела
    CrosshairConfig crosshair;
    crosshair.enabled = true;
    crosshair.color = Color(0.0f, 1.0f, 0.0f, 0.8f);  // Зеленый
    crosshair.line_length = 0.05f;
    crosshair.line_width = 2.0f;
    hud.setCrosshairConfig(crosshair);

    // Инициализация Modbus клиента (опционально)
    ModbusClient modbus;
    bool use_modbus = false;

    // Раскомментируйте для использования Modbus:
    // if (modbus.connect("192.168.1.100", 502)) {
    //     modbus.registerVariable("speed", 0);
    //     modbus.registerVariable("altitude", 1);
    //     use_modbus = true;
    // }

    // Компиляция шейдеров для видео
    GLuint video_vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(video_vs, 1, &video_vs_src, nullptr);
    glCompileShader(video_vs);

    GLuint video_fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(video_fs, 1, &video_fs_src, nullptr);
    glCompileShader(video_fs);

    GLuint video_program = glCreateProgram();
    glAttachShader(video_program, video_vs);
    glAttachShader(video_program, video_fs);
    glLinkProgram(video_program);

    glDeleteShader(video_vs);
    glDeleteShader(video_fs);

    // Создание текстуры для видео
    GLuint video_texture;
    glGenTextures(1, &video_texture);
    glBindTexture(GL_TEXTURE_2D, video_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Создание VBO для видео квада
    float quad_vertices[] = {
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 0.0f
    };

    GLuint video_vbo;
    glGenBuffers(1, &video_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, video_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    // Настройка viewport
    glViewport(0, 0, drm.mode.hdisplay, drm.mode.vdisplay);

    std::cout << "Starting main loop..." << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;

    // Главный цикл
    while (true) {
        // Получение кадра с камеры
        FrameBuffer* frame = camera.getNextFrame();

        if (frame) {
            uint32_t stride;
            uint8_t* frame_data = camera.getFrameData(frame, stride);

            if (frame_data) {
                // Загрузка текстуры
                glBindTexture(GL_TEXTURE_2D, video_texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                if (stride == camera.getWidth()) {
                    // Stride совпадает - копируем напрямую
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                                camera.getWidth(), camera.getHeight(), 0,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, frame_data);
                } else {
                    // Есть padding - используем GL_UNPACK_ROW_LENGTH_EXT
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                                camera.getWidth(), camera.getHeight(), 0,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, frame_data);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
                }

                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }

            camera.returnFrame(frame);
        }

        // Чтение Modbus данных (каждый кадр или с интервалом)
        if (use_modbus && frame_count % 10 == 0) {
            modbus.readVariables();
        }

        // Очистка буфера
        glClear(GL_COLOR_BUFFER_BIT);

        // Отрисовка видео
        glUseProgram(video_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, video_texture);
        glUniform1i(glGetUniformLocation(video_program, "uTextureY"), 0);

        glBindBuffer(GL_ARRAY_BUFFER, video_vbo);
        GLint posLoc = glGetAttribLocation(video_program, "aPosition");
        GLint texLoc = glGetAttribLocation(video_program, "aTexCoord");

        glEnableVertexAttribArray(posLoc);
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(posLoc);
        glDisableVertexAttribArray(texLoc);

        // Отрисовка HUD
        hud.render();

        // Swap буферов
        eglSwapBuffers(drm.egl_display, drm.egl_surface);

        // Подсчет FPS
        frame_count++;
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_time).count();
        if (elapsed >= 1) {
            std::cout << "FPS: " << frame_count << std::endl;
            frame_count = 0;
            last_time = current_time;
        }

        // Небольшая задержка для стабилизации
        usleep(1000);
    }

    // Очистка
    camera.stop();
    glDeleteTextures(1, &video_texture);
    glDeleteBuffers(1, &video_vbo);
    glDeleteProgram(video_program);

    cleanupDRM(drm);

    return 0;
}
