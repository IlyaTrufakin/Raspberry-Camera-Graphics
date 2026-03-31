#pragma once

#include "camera_stream.h"
#include "config.h"
#include "drm_display.h"
#include "hud_overlay.h"
#include "modbus_client.h"
#include "video_renderer.h"

#include <map>
#include <mutex>
#include <string>
#include <chrono>

class App {
public:
    int run(const std::string& config_path);

private:
    bool loadConfiguration(const std::string& path);
    bool initialize();
    void runLoop();
    void shutdown();

    CameraConfig buildCameraConfig() const;
    void updateHud(const std::string& fps_text);
    void updateLineObjects();
    void initializeExposureControl();
    void updateAdaptiveExposure(FrameBuffer* frame);
    void refreshModbusTextCache();
    void buildStaticHudCaches();
    float computeLeftEdge() const;
    float computeRightEdge() const;

    AppConfig config_{};
    DRMDisplay display_{};
    CameraStream camera_{};
    HUDOverlay hud_{};
    VideoRenderer renderer_{};
    ModbusClient modbus_{};
    bool use_modbus_ = false;
    std::mutex modbus_text_mutex_;
    std::map<std::string, std::string> modbus_text_cache_;
    std::vector<TextPosition> static_rect_text_cache_;
    std::vector<TextPosition> status_bit_text_cache_;
    bool exposure_control_active_ = false;
    int exposure_time_us_ = 0;
    float exposure_gain_ = 1.0f;
    int exposure_recovery_boost_frames_ = 0;
    uint64_t exposure_frame_counter_ = 0;
    std::chrono::steady_clock::time_point exposure_last_debug_;
};
