#include "camera_stream.h"
#include "config.h"
#include "hud_overlay.h"
#include "modbus_client.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

#ifndef GL_UNPACK_ROW_LENGTH_EXT
#define GL_UNPACK_ROW_LENGTH_EXT 0x0CF2
#endif

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

const char* video_vs_src = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char* video_fs_src = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTextureY;

void main() {
    float y = texture2D(uTextureY, vTexCoord).r;
    gl_FragColor = vec4(y, y, y, 1.0);
}
)";

static bool initDRM(DRMResources& drm) {
    drm.fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm.fd < 0) {
        std::cerr << "Failed to open DRM device" << std::endl;
        return false;
    }

    drmModeRes *resources = drmModeGetResources(drm.fd);
    if (!resources) {
        std::cerr << "Failed to get DRM resources" << std::endl;
        return false;
    }

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

static bool initEGL(DRMResources& drm) {
    drm.gbm_dev = gbm_create_device(drm.fd);
    if (!drm.gbm_dev) {
        std::cerr << "Failed to create GBM device" << std::endl;
        return false;
    }

    drm.gbm_surf = gbm_surface_create(drm.gbm_dev,
                                      drm.mode.hdisplay,
                                      drm.mode.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!drm.gbm_surf) {
        std::cerr << "Failed to create GBM surface" << std::endl;
        return false;
    }

    drm.egl_display = eglGetDisplay((EGLNativeDisplayType)drm.gbm_dev);
    if (drm.egl_display == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get EGL display" << std::endl;
        return false;
    }

    if (!eglInitialize(drm.egl_display, nullptr, nullptr)) {
        std::cerr << "Failed to initialize EGL" << std::endl;
        return false;
    }

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

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    drm.egl_context = eglCreateContext(drm.egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (drm.egl_context == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return false;
    }

    drm.egl_surface = eglCreateWindowSurface(drm.egl_display, egl_config,
                                             (EGLNativeWindowType)drm.gbm_surf, nullptr);
    if (drm.egl_surface == EGL_NO_SURFACE) {
        std::cerr << "Failed to create EGL surface" << std::endl;
        return false;
    }

    if (!eglMakeCurrent(drm.egl_display, drm.egl_surface, drm.egl_surface, drm.egl_context)) {
        std::cerr << "Failed to make EGL context current" << std::endl;
        return false;
    }

    std::cout << "EGL initialized" << std::endl;
    return true;
}

static void cleanupDRM(DRMResources& drm) {
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
    AppConfig app_config;
    bool config_loaded = loadConfig("config.ini", app_config);
    if (!config_loaded) {
        std::cout << "Config file not found, using defaults" << std::endl;
    }

    DRMResources drm;
    if (!initDRM(drm)) {
        return 1;
    }
    if (!initEGL(drm)) {
        cleanupDRM(drm);
        return 1;
    }

    CameraStream camera;
    CameraConfig cam_config;
    cam_config.width = app_config.video.width;
    cam_config.height = app_config.video.height;
    cam_config.buffer_count = app_config.video.buffer_count;
    cam_config.pixel_format = app_config.video.pixel_format;

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

    HUDOverlay hud;
    if (!hud.initialize(drm.mode.hdisplay, drm.mode.vdisplay)) {
        std::cerr << "Failed to initialize HUD" << std::endl;
        cleanupDRM(drm);
        return 1;
    }

    hud.setCrosshairConfig(app_config.crosshair);
    hud.setPanelConfig(app_config.panel);

    ModbusClient modbus;
    bool use_modbus = false;
    if (app_config.modbus.enabled) {
        modbus.setUnitId(app_config.modbus.unit_id);
        if (modbus.connect(app_config.modbus.ip, app_config.modbus.port)) {
            for (const auto& entry : app_config.modbus.registers) {
                modbus.registerVariable(entry.first, entry.second);
            }
            use_modbus = true;
        } else {
            std::cerr << "Modbus connect failed, running without Modbus" << std::endl;
        }
    }

    int modbus_interval_ms = app_config.modbus.update_ms > 0 ? app_config.modbus.update_ms : 150;
    std::thread modbus_thread;
    if (use_modbus) {
        modbus_thread = std::thread([&modbus, modbus_interval_ms]() {
            while (true) {
                modbus.readVariables();
                std::this_thread::sleep_for(std::chrono::milliseconds(modbus_interval_ms));
            }
        });
        modbus_thread.detach();
    }

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

    GLuint video_texture;
    glGenTextures(1, &video_texture);
    glBindTexture(GL_TEXTURE_2D, video_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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

    glViewport(0, 0, drm.mode.hdisplay, drm.mode.vdisplay);

    std::cout << "Starting main loop..." << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    int current_fps = 0;
    std::string fps_text = "0";
    int16_t crosshair_offset_x = 0;
    int16_t crosshair_offset_y = 0;

    int hud_interval_ms = app_config.hud_update_ms > 0 ? app_config.hud_update_ms : 150;
    auto last_hud_update = std::chrono::steady_clock::now()
                           - std::chrono::milliseconds(hud_interval_ms);

    auto rebuildHudText = [&](const std::string& fps_value) {
        hud.clearTextPositions();
        for (const auto& item : app_config.static_texts) {
            hud.addTextPosition(TextPosition(item.x, item.y, item.text, item.scale, item.color));
        }

        for (const auto& item : app_config.dynamic_texts) {
            std::string text = "---";
            if (item.name == "fps") {
                text = fps_value;
            } else if (use_modbus) {
                uint16_t value = 0;
                if (modbus.getVariable(item.name, value)) {
                    text = std::to_string(value);
                } else {
                    text = "0";
                }
            }
            hud.addTextPosition(TextPosition(item.x, item.y, text, item.scale, item.color));
        }
    };

    while (true) {
        FrameBuffer* frame = camera.getNextFrame();

        if (frame) {
            uint32_t stride;
            uint8_t* frame_data = camera.getFrameData(frame, stride);

            if (frame_data) {
                glBindTexture(GL_TEXTURE_2D, video_texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                if (stride == camera.getWidth()) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                                camera.getWidth(), camera.getHeight(), 0,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, frame_data);
                } else {
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

        auto now = std::chrono::steady_clock::now();
        if (now - last_hud_update >= std::chrono::milliseconds(hud_interval_ms)) {
            if (use_modbus) {
                uint16_t raw_x = 0;
                uint16_t raw_y = 0;
                if (modbus.getVariable("crosshair_x", raw_x)) {
                    crosshair_offset_x = static_cast<int16_t>(raw_x);
                }
                if (modbus.getVariable("crosshair_y", raw_y)) {
                    crosshair_offset_y = static_cast<int16_t>(raw_y);
                }

                CrosshairConfig cross = app_config.crosshair;
                cross.center_x = 0.5f + static_cast<float>(crosshair_offset_x) / static_cast<float>(drm.mode.hdisplay);
                cross.center_y = 0.5f + static_cast<float>(crosshair_offset_y) / static_cast<float>(drm.mode.vdisplay);
                if (cross.center_x < 0.0f) cross.center_x = 0.0f;
                if (cross.center_x > 1.0f) cross.center_x = 1.0f;
                if (cross.center_y < 0.0f) cross.center_y = 0.0f;
                if (cross.center_y > 1.0f) cross.center_y = 1.0f;
                hud.setCrosshairConfig(cross);
            }

            rebuildHudText(fps_text);
            last_hud_update = now;
        }

        glClear(GL_COLOR_BUFFER_BIT);

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

        hud.render();

        eglSwapBuffers(drm.egl_display, drm.egl_surface);

        gbm_bo *bo = gbm_surface_lock_front_buffer(drm.gbm_surf);
        uint32_t fb_id;
        uint32_t handle = gbm_bo_get_handle(bo).u32;
        uint32_t pitch = gbm_bo_get_stride(bo);

        drmModeAddFB(drm.fd, drm.mode.hdisplay, drm.mode.vdisplay, 24, 32,
                     pitch, handle, &fb_id);

        drmModeSetCrtc(drm.fd, drm.crtc_id, fb_id, 0, 0,
                       &drm.connector_id, 1, &drm.mode);

        gbm_surface_release_buffer(drm.gbm_surf, bo);

        frame_count++;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
        if (elapsed >= 1) {
            current_fps = frame_count;
            fps_text = std::to_string(current_fps);
            std::cout << "FPS: " << current_fps << std::endl;
            frame_count = 0;
            last_time = now;
        }

        usleep(1000);
    }

    camera.stop();
    glDeleteTextures(1, &video_texture);
    glDeleteBuffers(1, &video_vbo);
    glDeleteProgram(video_program);

    cleanupDRM(drm);
    return 0;
}
