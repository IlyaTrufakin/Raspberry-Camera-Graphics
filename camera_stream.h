#pragma once

#include <libcamera/libcamera.h>
#include <memory>
#include <map>
#include <functional>

using namespace libcamera;

// Параметры конфигурации камеры
struct CameraConfig {
    uint32_t width = 1012;
    uint32_t height = 760;
    uint32_t buffer_count = 2;
    std::string pixel_format = "YUV420";
    StreamRole role = StreamRole::Viewfinder;
};

// Класс для работы с видеопотоком камеры
class CameraStream {
public:
    CameraStream();
    ~CameraStream();

    // Инициализация камеры с заданной конфигурацией
    bool initialize(const CameraConfig& config);

    // Запуск потока
    bool start();

    // Остановка потока
    void stop();

    // Получение следующего кадра (неблокирующий)
    FrameBuffer* getNextFrame();

    // Возврат кадра обратно в очередь
    void returnFrame(FrameBuffer* frame);

    // Получение размеров кадра
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint32_t getStride() const { return stride_; }

    // Получение указателя на данные Y-plane
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
