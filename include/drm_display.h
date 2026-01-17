#pragma once

#include <EGL/egl.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

class DRMDisplay {
public:
    DRMDisplay() = default;
    ~DRMDisplay();

    bool initialize();
    void cleanup();

    int width() const { return mode_.hdisplay; }
    int height() const { return mode_.vdisplay; }

    EGLDisplay eglDisplay() const { return egl_display_; }
    EGLSurface eglSurface() const { return egl_surface_; }

    void swapBuffers();

private:
    int fd_ = -1;
    drmModeModeInfo mode_{};
    uint32_t connector_id_ = 0;
    uint32_t crtc_id_ = 0;
    drmModeCrtc* saved_crtc_ = nullptr;

    gbm_device* gbm_dev_ = nullptr;
    gbm_surface* gbm_surf_ = nullptr;

    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
};
