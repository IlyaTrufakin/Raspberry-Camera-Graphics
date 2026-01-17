#include "drm_display.h"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>

DRMDisplay::~DRMDisplay() {
    cleanup();
}

bool DRMDisplay::initialize() {
    fd_ = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        std::cerr << "Failed to open DRM device" << std::endl;
        return false;
    }

    drmModeRes* resources = drmModeGetResources(fd_);
    if (!resources) {
        std::cerr << "Failed to get DRM resources" << std::endl;
        return false;
    }

    drmModeConnector* connector = nullptr;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd_, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            connector_id_ = connector->connector_id;
            mode_ = connector->modes[0];
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

    drmModeEncoder* encoder = drmModeGetEncoder(fd_, connector->encoder_id);
    if (encoder) {
        crtc_id_ = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
    }

    saved_crtc_ = drmModeGetCrtc(fd_, crtc_id_);

    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);

    gbm_dev_ = gbm_create_device(fd_);
    if (!gbm_dev_) {
        std::cerr << "Failed to create GBM device" << std::endl;
        return false;
    }

    gbm_surf_ = gbm_surface_create(gbm_dev_,
                                   mode_.hdisplay,
                                   mode_.vdisplay,
                                   GBM_FORMAT_XRGB8888,
                                   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surf_) {
        std::cerr << "Failed to create GBM surface" << std::endl;
        return false;
    }

    egl_display_ = eglGetDisplay((EGLNativeDisplayType)gbm_dev_);
    if (egl_display_ == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get EGL display" << std::endl;
        return false;
    }

    if (!eglInitialize(egl_display_, nullptr, nullptr)) {
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
    if (!eglChooseConfig(egl_display_, config_attribs, &egl_config, 1, &num_configs)) {
        std::cerr << "Failed to choose EGL config" << std::endl;
        return false;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_context_ = eglCreateContext(egl_display_, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return false;
    }

    egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config,
                                          (EGLNativeWindowType)gbm_surf_, nullptr);
    if (egl_surface_ == EGL_NO_SURFACE) {
        std::cerr << "Failed to create EGL surface" << std::endl;
        return false;
    }

    if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
        std::cerr << "Failed to make EGL context current" << std::endl;
        return false;
    }

    std::cout << "DRM initialized: " << mode_.hdisplay << "x" << mode_.vdisplay << std::endl;
    std::cout << "EGL initialized" << std::endl;
    return true;
}

void DRMDisplay::swapBuffers() {
    eglSwapBuffers(egl_display_, egl_surface_);

    gbm_bo* bo = gbm_surface_lock_front_buffer(gbm_surf_);
    uint32_t fb_id;
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);

    drmModeAddFB(fd_, mode_.hdisplay, mode_.vdisplay, 24, 32,
                 pitch, handle, &fb_id);

    drmModeSetCrtc(fd_, crtc_id_, fb_id, 0, 0,
                   &connector_id_, 1, &mode_);

    gbm_surface_release_buffer(gbm_surf_, bo);
}

void DRMDisplay::cleanup() {
    if (saved_crtc_) {
        drmModeSetCrtc(fd_, saved_crtc_->crtc_id, saved_crtc_->buffer_id,
                       saved_crtc_->x, saved_crtc_->y,
                       &connector_id_, 1, &saved_crtc_->mode);
        drmModeFreeCrtc(saved_crtc_);
        saved_crtc_ = nullptr;
    }

    if (egl_context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display_, egl_context_);
        egl_context_ = EGL_NO_CONTEXT;
    }
    if (egl_surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
    }
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }

    if (gbm_surf_) {
        gbm_surface_destroy(gbm_surf_);
        gbm_surf_ = nullptr;
    }
    if (gbm_dev_) {
        gbm_device_destroy(gbm_dev_);
        gbm_dev_ = nullptr;
    }

    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}
