#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

// Версия для OpenGL ES 1.1 (без шейдеров)
// Для Raspberry Pi с устаревшими драйверами

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

    // Ищем режим 1920x1080
    drmModeModeInfo mode;
    bool found_1080p = false;
    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].hdisplay == 1920 && conn->modes[i].vdisplay == 1080) {
            mode = conn->modes[i];
            found_1080p = true;
            printf("Found 1920x1080 mode: %s\n", mode.name);
            break;
        }
    }

    if (!found_1080p) {
        printf("1920x1080 not found, using default mode: %dx%d\n",
               conn->modes[0].hdisplay, conn->modes[0].vdisplay);
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
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,  // GLES 1.x
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(egl_display, cfg_attr, &cfg, 1, &n);

    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(egl_display, cfg, EGL_NO_CONTEXT, ctx_attr);
    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, cfg, (EGLNativeWindowType)gbm_surf, nullptr);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, ctx);

    glViewport(0, 0, mode.hdisplay, mode.vdisplay);

    // Настройка OpenGL ES 1.1
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* ===== FIRST FRAME FOR DRM ===== */
    glClearColor(0, 0, 0, 1);
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

    printf("Display initialized: %dx%d\n", mode.hdisplay, mode.vdisplay);

    /* ================= RENDER LOOP ================= */
    int frame_count = 0;

    while (true) {
        glClear(GL_COLOR_BUFFER_BIT);

        // Включаем 2D рендеринг
        glDisable(GL_TEXTURE_2D);

        // Рисуем прицел (красный крест)
        glColor4f(1.0f, 0.0f, 0.0f, 0.9f);

        GLfloat h_line[] = {
            -0.05f, -0.005f,
            -0.05f,  0.005f,
             0.05f,  0.005f,
             0.05f, -0.005f
        };

        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, h_line);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        GLfloat v_line[] = {
            -0.005f, -0.05f,
            -0.005f,  0.05f,
             0.005f,  0.05f,
             0.005f, -0.05f
        };

        glVertexPointer(2, GL_FLOAT, 0, v_line);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        // HUD элемент в левом верхнем углу (зеленая рамка)
        glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
        GLfloat box[] = {
            -0.95f, 0.85f,
            -0.80f, 0.85f,
            -0.80f, 0.91f,
            -0.95f, 0.91f
        };
        glVertexPointer(2, GL_FLOAT, 0, box);
        glDrawArrays(GL_LINE_LOOP, 0, 4);

        // Фон для HUD элемента (черный полупрозрачный)
        glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        // HUD элемент в правом верхнем углу
        glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
        GLfloat box2[] = {
            0.70f, 0.85f,
            0.85f, 0.85f,
            0.85f, 0.91f,
            0.70f, 0.91f
        };
        glVertexPointer(2, GL_FLOAT, 0, box2);
        glDrawArrays(GL_LINE_LOOP, 0, 4);

        glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        // Желтые метки в нижних углах
        glColor4f(1.0f, 1.0f, 0.0f, 0.8f);
        GLfloat label1[] = {
            -0.95f, -0.95f,
            -0.65f, -0.95f,
            -0.65f, -0.90f,
            -0.95f, -0.90f
        };
        glVertexPointer(2, GL_FLOAT, 0, label1);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        GLfloat label2[] = {
            0.50f, -0.95f,
            0.80f, -0.95f,
            0.80f, -0.90f,
            0.50f, -0.90f
        };
        glVertexPointer(2, GL_FLOAT, 0, label2);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableClientState(GL_VERTEX_ARRAY);

        eglSwapBuffers(egl_display, egl_surface);

        frame_count++;
        usleep(16666); // ~60 FPS
    }

    /* ================= CLEANUP ================= */
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, ctx);
    eglTerminate(egl_display);
    gbm_surface_destroy(gbm_surf);
    gbm_device_destroy(gbm);
    close(drm_fd);

    return 0;
}
