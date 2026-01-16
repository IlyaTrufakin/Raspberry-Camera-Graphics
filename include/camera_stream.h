#pragma once

#include <libcamera/libcamera.h>
#include <array>
#include <functional>
#include <map>
#include <memory>

using namespace libcamera;

// Конфигурация потока камеры
struct CameraConfig {
    uint32_t width = 1012;
    uint32_t height = 760;
    uint32_t buffer_count = 2;
    std::string pixel_format = "YUV420";
    StreamRole role = StreamRole::Viewfinder;
    bool ae_enable = true;
    bool awb_enable = true;
    bool controls_dump = false;
    int exposure_time_us = 0;
    float analogue_gain = 0.0f;
    float brightness = 0.0f;
    float contrast = 0.0f;
    float saturation = 0.0f;
    float sharpness = 0.0f;
    float exposure_compensation = 0.0f;
    int frame_duration_us = 0;
    int ae_metering = -1;
    int ae_constraint = -1;
    int ae_exposure_mode = -1;
    int awb_mode = -1;
    int ae_flicker_mode = -1;
    int exposure_time_mode = -1;
    int analogue_gain_mode = -1;
    int noise_reduction_mode = -1;
    int hdr_mode = -1;
    int sync_mode = -1;
    int sync_frames = -1;
    int colour_temperature = -1;
    int ae_flicker_period = -1;
    int stats_output_enable = -1;
    int cnn_enable_input_tensor = -1;
    std::array<float, 2> colour_gains = {0.0f, 0.0f};
    bool colour_gains_set = false;
    std::array<float, 9> colour_correction_matrix = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    bool colour_correction_matrix_set = false;
    bool roi_enabled = false;
    float roi_x = 0.0f;
    float roi_y = 0.0f;
    float roi_width = 1.0f;
    float roi_height = 1.0f;
    bool roi_auto = false;
    float roi_target_aspect = 0.0f;
};

// Захват видео через libcamera
class CameraStream {
public:
    CameraStream();
    ~CameraStream();

    // Инициализация камеры и буферов
    bool initialize(const CameraConfig& config);

    // Запуск потока
    bool start();

    // Остановка потока
    void stop();

    // Получить следующий готовый кадр (если есть)
    FrameBuffer* getNextFrame();

    // Вернуть кадр обратно в очередь
    void returnFrame(FrameBuffer* frame);

    // Параметры текущего потока
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint32_t getStride() const { return stride_; }

    // Получить данные Y-плоскости и шаг
    uint8_t* getFrameData(FrameBuffer* frame, uint32_t& stride);

    struct PlaneData {
        uint8_t* data = nullptr;
        uint32_t stride = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    bool getFramePlanes(FrameBuffer* frame, PlaneData& y, PlaneData& u, PlaneData& v);

private:
    std::unique_ptr<CameraManager> camera_manager_;
    std::shared_ptr<Camera> camera_;
    std::unique_ptr<CameraConfiguration> config_;
    FrameBufferAllocator* allocator_ = nullptr;

    std::vector<std::unique_ptr<Request>> requests_;
    std::map<FrameBuffer*, Request*> pending_requests_;
    std::map<int, void*> mapped_buffers_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t stride_ = 0;

    bool running_ = false;
};
