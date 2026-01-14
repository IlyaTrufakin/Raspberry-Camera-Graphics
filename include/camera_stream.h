#pragma once

#include <libcamera/libcamera.h>
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
