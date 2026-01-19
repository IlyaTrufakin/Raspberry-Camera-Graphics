#include "camera_stream.h"

#include <algorithm>
#include <iostream>
#include <sys/mman.h>

#include <libcamera/control_ids.h>
#include <libcamera/property_ids.h>
#include <libcamera/controls.h>

static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

static void dumpCameraControls(const Camera& camera) {
    const ControlInfoMap& controls = camera.controls();
    std::cout << "Camera controls (" << controls.size() << "):" << std::endl;
    for (const auto& entry : controls) {
        const ControlId* id = entry.first;
        const ControlInfo& info = entry.second;
        std::cout << "  " << id->name()
                  << " type=" << static_cast<int>(id->type())
                  << " min=" << info.min().toString()
                  << " max=" << info.max().toString()
                  << " def=" << info.def().toString()
                  << std::endl;
    }
}


CameraStream::CameraStream() {
}

CameraStream::~CameraStream() {
    stop();

    // Освободить mmap буферы
    for (auto& pair : mapped_buffers_) {
        munmap(pair.second, 0);
    }
    mapped_buffers_.clear();

    if (allocator_) {
        delete allocator_;
        allocator_ = nullptr;
    }

    if (camera_) {
        camera_->release();
        camera_.reset();
    }

    if (camera_manager_) {
        camera_manager_->stop();
    }
}

bool CameraStream::initialize(const CameraConfig& config) {
    // Инициализация CameraManager
    camera_manager_ = std::make_unique<CameraManager>();
    int ret = camera_manager_->start();
    if (ret) {
        std::cerr << "Failed to start camera manager" << std::endl;
        return false;
    }

    // Получить список камер
    auto cameras = camera_manager_->cameras();
    if (cameras.empty()) {
        std::cerr << "No cameras available" << std::endl;
        return false;
    }

    // Выбрать первую камеру
    camera_ = cameras[0];
    if (camera_->acquire()) {
        std::cerr << "Failed to acquire camera" << std::endl;
        return false;
    }

    std::cout << "Using camera: " << camera_->id() << std::endl;
    if (config.controls_dump) {
        dumpCameraControls(*camera_);
    }

    // Сформировать конфигурацию
    config_ = camera_->generateConfiguration({config.role});
    if (!config_) {
        std::cerr << "Failed to generate configuration" << std::endl;
        return false;
    }

    StreamConfiguration &streamConfig = config_->at(0);
    streamConfig.size.width = config.width;
    streamConfig.size.height = config.height;
    streamConfig.pixelFormat = PixelFormat::fromString("YUV420");
    streamConfig.bufferCount = config.buffer_count;

    CameraConfiguration::Status validation = config_->validate();
    if (validation == CameraConfiguration::Invalid) {
        std::cerr << "Camera configuration invalid" << std::endl;
        return false;
    }

    if (validation == CameraConfiguration::Adjusted) {
        std::cout << "Camera configuration adjusted to:" << std::endl;
        std::cout << "  Size: " << streamConfig.size.width << "x" << streamConfig.size.height << std::endl;
        std::cout << "  Format: " << streamConfig.pixelFormat.toString() << std::endl;
        std::cout << "  Stride: " << streamConfig.stride << std::endl;
    }

    if (camera_->configure(config_.get())) {
        std::cerr << "Failed to configure camera" << std::endl;
        return false;
    }

    width_ = streamConfig.size.width;
    height_ = streamConfig.size.height;
    stride_ = streamConfig.stride;

    Rectangle roi_rect;
    bool use_roi = false;
    if (config.roi_enabled) {
        Rectangle base_rect(0, 0, width_, height_);
        auto crop_max = camera_->properties().get(properties::ScalerCropMaximum);
        if (crop_max) {
            base_rect = *crop_max;
        } else {
            auto sensor = camera_->properties().get(properties::PixelArraySize);
            if (sensor) {
                base_rect = Rectangle(0, 0, sensor->width, sensor->height);
            }
        }

        int x = 0;
        int y = 0;
        int w = static_cast<int>(base_rect.width);
        int h = static_cast<int>(base_rect.height);

        if (config.roi_auto && config.roi_target_aspect > 0.0f) {
            float target_aspect = config.roi_target_aspect;
            float sensor_aspect = static_cast<float>(base_rect.width) /
                                  static_cast<float>(base_rect.height);

            if (sensor_aspect > target_aspect) {
                w = static_cast<int>(static_cast<float>(base_rect.height) * target_aspect);
                if (w < 1) w = 1;
                x = (static_cast<int>(base_rect.width) - w) / 2;
                y = 0;
                h = static_cast<int>(base_rect.height);
            } else {
                h = static_cast<int>(static_cast<float>(base_rect.width) / target_aspect);
                if (h < 1) h = 1;
                y = (static_cast<int>(base_rect.height) - h) / 2;
                x = 0;
                w = static_cast<int>(base_rect.width);
            }
        } else {
            float rx = clamp01(config.roi_x);
            float ry = clamp01(config.roi_y);
            float rw = clamp01(config.roi_width);
            float rh = clamp01(config.roi_height);

            if (rw > 0.0f && rh > 0.0f) {
                x = static_cast<int>(rx * base_rect.width);
                y = static_cast<int>(ry * base_rect.height);
                w = static_cast<int>(rw * base_rect.width);
                h = static_cast<int>(rh * base_rect.height);
            }
        }

        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > static_cast<int>(base_rect.width)) {
            w = static_cast<int>(base_rect.width) - x;
        }
        if (y + h > static_cast<int>(base_rect.height)) {
            h = static_cast<int>(base_rect.height) - y;
        }

        if (w > 0 && h > 0) {
            roi_rect = Rectangle(base_rect.x + x, base_rect.y + y,
                                 static_cast<unsigned int>(w), static_cast<unsigned int>(h));
            use_roi = true;
            std::cout << "ROI enabled: x=" << roi_rect.x << " y=" << roi_rect.y
                      << " w=" << roi_rect.width << " h=" << roi_rect.height
                      << " (base " << base_rect.width << "x" << base_rect.height << ")"
                      << std::endl;
        }
    }

    // Выделить буферы
    allocator_ = new FrameBufferAllocator(camera_);
    Stream *stream = streamConfig.stream();

    ret = allocator_->allocate(stream);
    if (ret < 0) {
        std::cerr << "Failed to allocate buffers" << std::endl;
        return false;
    }

    std::cout << "Allocated " << allocator_->buffers(stream).size() << " buffers" << std::endl;

    // Создать requests и mmap
    const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator_->buffers(stream);
    for (unsigned int i = 0; i < buffers.size(); ++i) {
        std::unique_ptr<Request> request = camera_->createRequest();
        if (!request) {
            std::cerr << "Failed to create request" << std::endl;
            return false;
        }

        request->controls().set(controls::AeEnable, config.ae_enable);
        request->controls().set(controls::AwbEnable, config.awb_enable);
        if (!config.ae_enable) {
            if (config.exposure_time_us > 0) {
                request->controls().set(controls::ExposureTime,
                                        static_cast<int64_t>(config.exposure_time_us));
            }
            if (config.analogue_gain > 0.0f) {
                request->controls().set(controls::AnalogueGain, config.analogue_gain);
            }
        }
        if (config.exposure_compensation != 0) {
            request->controls().set(controls::ExposureValue, config.exposure_compensation);
        }
        if (config.frame_duration_us > 0) {
            int64_t limits[2] = {
                static_cast<int64_t>(config.frame_duration_us),
                static_cast<int64_t>(config.frame_duration_us)
            };
            request->controls().set(controls::FrameDurationLimits, Span<const int64_t, 2>(limits));
        }
        if (config.ae_flicker_mode >= 0) {
            request->controls().set(controls::AeFlickerMode, config.ae_flicker_mode);
        }
        if (config.exposure_time_mode >= 0) {
            request->controls().set(controls::ExposureTimeMode, config.exposure_time_mode);
        }
        if (config.analogue_gain_mode >= 0) {
            request->controls().set(controls::AnalogueGainMode, config.analogue_gain_mode);
        }
        if (config.noise_reduction_mode >= 0) {
            request->controls().set(controls::draft::NoiseReductionMode, config.noise_reduction_mode);
        }
        if (config.hdr_mode >= 0) {
            request->controls().set(controls::HdrMode, config.hdr_mode);
        }
        if (config.sync_mode >= 0) {
            request->controls().set(controls::rpi::SyncMode, config.sync_mode);
        }
        if (config.sync_frames >= 0) {
            request->controls().set(controls::rpi::SyncFrames, config.sync_frames);
        }
        if (config.colour_temperature >= 0) {
            request->controls().set(controls::ColourTemperature, config.colour_temperature);
        }
        if (config.ae_flicker_period >= 0) {
            request->controls().set(controls::AeFlickerPeriod, config.ae_flicker_period);
        }
        if (config.stats_output_enable >= 0) {
            request->controls().set(controls::rpi::StatsOutputEnable, config.stats_output_enable != 0);
        }
        if (config.cnn_enable_input_tensor >= 0) {
            request->controls().set(controls::rpi::CnnEnableInputTensor, config.cnn_enable_input_tensor != 0);
        }
        if (config.colour_gains_set) {
            request->controls().set(controls::ColourGains,
                                    Span<const float, 2>(config.colour_gains));
        }
        if (config.colour_correction_matrix_set) {
            request->controls().set(controls::ColourCorrectionMatrix,
                                    Span<const float, 9>(config.colour_correction_matrix));
        }
        if (config.ae_metering >= 0) {
            request->controls().set(controls::AeMeteringMode, config.ae_metering);
        }
        if (config.ae_constraint >= 0) {
            request->controls().set(controls::AeConstraintMode, config.ae_constraint);
        }
        if (config.ae_exposure_mode >= 0) {
            request->controls().set(controls::AeExposureMode, config.ae_exposure_mode);
        }
        if (config.awb_mode >= 0) {
            request->controls().set(controls::AwbMode, config.awb_mode);
        }
        if (config.brightness != 0.0f) {
            request->controls().set(controls::Brightness, config.brightness);
        }
        if (config.contrast != 0.0f) {
            request->controls().set(controls::Contrast, config.contrast);
        }
        if (config.saturation != 0.0f) {
            request->controls().set(controls::Saturation, config.saturation);
        }
        if (config.sharpness != 0.0f) {
            request->controls().set(controls::Sharpness, config.sharpness);
        }

        if (use_roi) {
            request->controls().set(controls::ScalerCrop, roi_rect);
        }

        FrameBuffer *buffer = buffers[i].get();
        if (request->addBuffer(stream, buffer)) {
            std::cerr << "Failed to add buffer to request" << std::endl;
            return false;
        }

        std::map<int, size_t> fd_sizes;
        for (const auto& plane : buffer->planes()) {
            size_t size = static_cast<size_t>(plane.offset + plane.length);
            int fd = plane.fd.get();
            auto it = fd_sizes.find(fd);
            if (it == fd_sizes.end() || size > it->second) {
                fd_sizes[fd] = size;
            }
        }

        for (const auto& entry : fd_sizes) {
            int fd = entry.first;
            size_t size = entry.second;
            if (mapped_buffers_.find(fd) != mapped_buffers_.end()) {
                continue;
            }
            void *mapped = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
            if (mapped != MAP_FAILED) {
                mapped_buffers_[fd] = mapped;
            } else {
                std::cerr << "Failed to mmap buffer" << std::endl;
                return false;
            }
        }

        pending_requests_[buffer] = request.get();
        requests_.push_back(std::move(request));
    }

    std::cout << "Camera initialized: " << width_ << "x" << height_
              << " stride=" << stride_ << std::endl;

    return true;
}

bool CameraStream::start() {
    if (running_) {
        return true;
    }

    if (camera_->start()) {
        std::cerr << "Failed to start camera" << std::endl;
        return false;
    }

    for (auto &request : requests_) {
        if (camera_->queueRequest(request.get())) {
            std::cerr << "Failed to queue request" << std::endl;
            camera_->stop();
            return false;
        }
    }

    running_ = true;
    std::cout << "Camera started" << std::endl;
    return true;
}

void CameraStream::stop() {
    if (!running_) {
        return;
    }

    if (camera_) {
        camera_->stop();
    }

    running_ = false;
    std::cout << "Camera stopped" << std::endl;
}

FrameBuffer* CameraStream::getNextFrame() {
    if (!running_) {
        return nullptr;
    }

    std::vector<Request*> completed_reqs;
    for (auto &pair : pending_requests_) {
        if (pair.second->status() == Request::RequestComplete) {
            completed_reqs.push_back(pair.second);
        }
    }

    if (completed_reqs.empty()) {
        return nullptr;
    }

    Request *completed_req = completed_reqs.back();
    FrameBuffer *frame = completed_req->buffers().begin()->second;

    for (size_t i = 0; i < completed_reqs.size() - 1; i++) {
        completed_reqs[i]->reuse(Request::ReuseBuffers);
        camera_->queueRequest(completed_reqs[i]);
    }

    return frame;
}

void CameraStream::returnFrame(FrameBuffer* frame) {
    if (!frame || !running_) {
        return;
    }

    auto it = pending_requests_.find(frame);
    if (it != pending_requests_.end()) {
        Request *request = it->second;
        request->reuse(Request::ReuseBuffers);
        camera_->queueRequest(request);
    }
}

uint8_t* CameraStream::getFrameData(FrameBuffer* frame, uint32_t& stride) {
    if (!frame) {
        stride = 0;
        return nullptr;
    }

    const FrameBuffer::Plane &plane = frame->planes()[0];
    auto it = mapped_buffers_.find(plane.fd.get());
    if (it == mapped_buffers_.end()) {
        stride = 0;
        return nullptr;
    }

    stride = stride_;
    return static_cast<uint8_t*>(it->second) + plane.offset;
}

bool CameraStream::getFramePlanes(FrameBuffer* frame, PlaneData& y, PlaneData& u, PlaneData& v) {
    if (!frame) {
        return false;
    }

    const auto& planes = frame->planes();
    if (planes.size() < 3) {
        return false;
    }

    const FrameBuffer::Plane& plane_y = planes[0];
    const FrameBuffer::Plane& plane_u = planes[1];
    const FrameBuffer::Plane& plane_v = planes[2];

    auto it_y = mapped_buffers_.find(plane_y.fd.get());
    auto it_u = mapped_buffers_.find(plane_u.fd.get());
    auto it_v = mapped_buffers_.find(plane_v.fd.get());
    if (it_y == mapped_buffers_.end() ||
        it_u == mapped_buffers_.end() ||
        it_v == mapped_buffers_.end()) {
        return false;
    }

    uint32_t y_height = height_;
    uint32_t y_width = width_;
    uint32_t uv_height = height_ / 2;
    uint32_t uv_width = width_ / 2;
    if (y_height == 0 || y_width == 0 || uv_height == 0 || uv_width == 0) {
        return false;
    }

    uint32_t y_stride = plane_y.length / y_height;
    uint32_t u_stride = plane_u.length / uv_height;
    uint32_t v_stride = plane_v.length / uv_height;
    if (y_stride == 0 || u_stride == 0 || v_stride == 0) {
        return false;
    }

    y.data = static_cast<uint8_t*>(it_y->second) + plane_y.offset;
    y.stride = y_stride;
    y.width = y_width;
    y.height = y_height;

    u.data = static_cast<uint8_t*>(it_u->second) + plane_u.offset;
    u.stride = u_stride;
    u.width = uv_width;
    u.height = uv_height;

    v.data = static_cast<uint8_t*>(it_v->second) + plane_v.offset;
    v.stride = v_stride;
    v.width = uv_width;
    v.height = uv_height;

    return true;
}
