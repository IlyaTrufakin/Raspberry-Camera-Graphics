#include "drm_display.h"

#include <chrono>
#include <iostream>
#include <sys/select.h>
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

bool DRMDisplay::setSwapInterval(int interval) {
    if (egl_display_ == EGL_NO_DISPLAY) {
        return false;
    }
    if (interval < 0) {
        interval = 0;
    }
    swap_interval_ = interval;
    bool ok = eglSwapInterval(egl_display_, interval) == EGL_TRUE;
    std::cout << "eglSwapInterval(" << interval << ") -> " << (ok ? "OK" : "FAIL") << std::endl;
    return ok;
}

void DRMDisplay::setProfileEnabled(bool enable) {
    profile_enabled_ = enable;
    swap_samples_ = 0;
    prof_swap_total_ms_ = 0.0;
    prof_egl_swap_ms_ = 0.0;
    prof_lock_ms_ = 0.0;
    prof_addfb_ms_ = 0.0;
    prof_setcrtc_ms_ = 0.0;
    prof_release_ms_ = 0.0;
    profile_last_ = std::chrono::steady_clock::now();
}

void DRMDisplay::swapBuffers() {
    auto t0 = std::chrono::steady_clock::now();
    auto e0 = t0;
    eglSwapBuffers(egl_display_, egl_surface_);
    auto e1 = std::chrono::steady_clock::now();

    auto l0 = std::chrono::steady_clock::now();
    gbm_bo* bo = gbm_surface_lock_front_buffer(gbm_surf_);
    auto l1 = std::chrono::steady_clock::now();
    uint32_t fb_id = 0;
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);

    struct DrmFb {
        int fd;
        uint32_t fb_id;
    };

    auto fb_destroy = [](gbm_bo* bo, void* data) {
        auto* fb = static_cast<DrmFb*>(data);
        if (!fb) {
            return;
        }
        if (fb->fb_id) {
            drmModeRmFB(fb->fd, fb->fb_id);
        }
        delete fb;
    };

    auto get_fb_id = [&](gbm_bo* bo) {
        auto* fb = static_cast<DrmFb*>(gbm_bo_get_user_data(bo));
        if (fb) {
            return fb->fb_id;
        }
        auto* new_fb = new DrmFb{fd_, 0};
        if (drmModeAddFB(fd_, mode_.hdisplay, mode_.vdisplay, 24, 32,
                         pitch, handle, &new_fb->fb_id) != 0) {
            delete new_fb;
            return static_cast<uint32_t>(0);
        }
        gbm_bo_set_user_data(bo, new_fb, fb_destroy);
        return new_fb->fb_id;
    };

    auto a0 = std::chrono::steady_clock::now();
    fb_id = get_fb_id(bo);
    auto a1 = std::chrono::steady_clock::now();

    auto s0 = std::chrono::steady_clock::now();
    if (!crtc_set_) {
        drmModeSetCrtc(fd_, crtc_id_, fb_id, 0, 0,
                       &connector_id_, 1, &mode_);
        crtc_set_ = true;
    } else {
        auto page_flip_handler = [](int fd, unsigned int frame, unsigned int sec,
                                    unsigned int usec, void* data) {
            bool* pending = static_cast<bool*>(data);
            if (pending) {
                *pending = false;
            }
        };

        int flags = DRM_MODE_PAGE_FLIP_EVENT;
#ifdef DRM_MODE_PAGE_FLIP_ASYNC
        if (swap_interval_ == 0) {
            flags |= DRM_MODE_PAGE_FLIP_ASYNC;
        }
#endif
        int ret = drmModePageFlip(fd_, crtc_id_, fb_id, flags, &page_flip_pending_);
        if (ret == 0) {
            page_flip_pending_ = true;
            drmEventContext ev{};
            ev.version = DRM_EVENT_CONTEXT_VERSION;
            ev.page_flip_handler = page_flip_handler;
            while (page_flip_pending_) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(fd_, &fds);
                if (select(fd_ + 1, &fds, nullptr, nullptr, nullptr) < 0) {
                    break;
                }
                drmHandleEvent(fd_, &ev);
            }
        } else {
            drmModeSetCrtc(fd_, crtc_id_, fb_id, 0, 0,
                           &connector_id_, 1, &mode_);
        }
    }
    auto s1 = std::chrono::steady_clock::now();

    auto r0 = std::chrono::steady_clock::now();
    if (current_bo_) {
        gbm_surface_release_buffer(gbm_surf_, current_bo_);
    }
    current_bo_ = bo;
    auto r1 = std::chrono::steady_clock::now();

    if (profile_enabled_) {
        auto t1 = r1;
        prof_swap_total_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
        prof_egl_swap_ms_ += std::chrono::duration<double, std::milli>(e1 - e0).count();
        prof_lock_ms_ += std::chrono::duration<double, std::milli>(l1 - l0).count();
        prof_addfb_ms_ += std::chrono::duration<double, std::milli>(a1 - a0).count();
        prof_setcrtc_ms_ += std::chrono::duration<double, std::milli>(s1 - s0).count();
        prof_release_ms_ += std::chrono::duration<double, std::milli>(r1 - r0).count();
        swap_samples_++;

        if (t1 - profile_last_ >= std::chrono::seconds(1)) {
            double div = swap_samples_ > 0 ? static_cast<double>(swap_samples_) : 1.0;
            std::cout << "DRM swap avg ms: total=" << (prof_swap_total_ms_ / div)
                      << " egl=" << (prof_egl_swap_ms_ / div)
                      << " lock=" << (prof_lock_ms_ / div)
                      << " addfb=" << (prof_addfb_ms_ / div)
                      << " setcrtc=" << (prof_setcrtc_ms_ / div)
                      << " release=" << (prof_release_ms_ / div)
                      << std::endl;
            swap_samples_ = 0;
            prof_swap_total_ms_ = 0.0;
            prof_egl_swap_ms_ = 0.0;
            prof_lock_ms_ = 0.0;
            prof_addfb_ms_ = 0.0;
            prof_setcrtc_ms_ = 0.0;
            prof_release_ms_ = 0.0;
            profile_last_ = t1;
        }
    }
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
        if (current_bo_) {
            gbm_surface_release_buffer(gbm_surf_, current_bo_);
            current_bo_ = nullptr;
        }
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
