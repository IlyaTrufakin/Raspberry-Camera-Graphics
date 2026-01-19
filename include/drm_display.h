#pragma once

#include <EGL/egl.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <chrono>
#include <cstddef>

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

    bool setSwapInterval(int interval);
    void setProfileEnabled(bool enable);
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
    gbm_bo* current_bo_ = nullptr;
    bool crtc_set_ = false;
    bool page_flip_pending_ = false;
    int swap_interval_ = 1;

    bool profile_enabled_ = false;
    std::chrono::steady_clock::time_point profile_last_{};
    size_t swap_samples_ = 0;
    double prof_swap_total_ms_ = 0.0;
    double prof_egl_swap_ms_ = 0.0;
    double prof_lock_ms_ = 0.0;
    double prof_addfb_ms_ = 0.0;
    double prof_setcrtc_ms_ = 0.0;
    double prof_release_ms_ = 0.0;
};
