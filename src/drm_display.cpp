#include "drm_display.h"

#include <iostream>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

DRMDisplay::~DRMDisplay() {
    cleanup();
}

bool DRMDisplay::initialize() {
    drmModeRes* resources = nullptr;
    drmModeConnector* connector = nullptr;
    
    for (int i = 0; i < 4; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        resources = drmModeGetResources(fd);
        if (!resources) {
            close(fd);
            continue;
        }

        for (int j = 0; j < resources->count_connectors; j++) {
            connector = drmModeGetConnector(fd, resources->connectors[j]);
            if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
                fd_ = fd;
                connector_id_ = connector->connector_id;
                mode_ = connector->modes[0];
                std::cout << "Using DRM device: " << path << std::endl;
                break;
            }
            if (connector) {
                drmModeFreeConnector(connector);
                connector = nullptr;
            }
        }

        if (connector) {
            break; // Found a valid connector
        }

        drmModeFreeResources(resources);
        resources = nullptr;
        close(fd);
        fd_ = -1;
    }

    if (fd_ < 0 || !connector || !resources) {
        std::cerr << "Failed to find any DRM device with a connected display" << std::endl;
        return false;
    }

    drmModeEncoder* encoder = drmModeGetEncoder(fd_, connector->encoder_id);
    if (encoder) {
        crtc_id_ = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
    } else {
        // Fallback if encoder_id is 0
        for (int i = 0; i < connector->count_encoders; i++) {
            encoder = drmModeGetEncoder(fd_, connector->encoders[i]);
            if (encoder) {
                crtc_id_ = encoder->crtc_id;
                drmModeFreeEncoder(encoder);
                break;
            }
        }
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

    eglSwapInterval(egl_display_, 0);

    std::cout << "DRM initialized: " << mode_.hdisplay << "x" << mode_.vdisplay << std::endl;
    std::cout << "EGL initialized" << std::endl;
    return true;
}

void DRMDisplay::swapBuffers() {
    eglSwapBuffers(egl_display_, egl_surface_);

    gbm_bo* bo = gbm_surface_lock_front_buffer(gbm_surf_);
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

    fb_id = get_fb_id(bo);

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
        flags |= DRM_MODE_PAGE_FLIP_ASYNC;
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

    if (current_bo_) {
        gbm_surface_release_buffer(gbm_surf_, current_bo_);
    }
    current_bo_ = bo;
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
