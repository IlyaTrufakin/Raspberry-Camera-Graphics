#pragma once

#include "camera_stream.h"
#include "config.h"
#include "drm_display.h"
#include "hud_overlay.h"
#include "modbus_client.h"
#include "video_renderer.h"

#include <string>

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
    void updateCrosshair();
    float computeLeftEdge() const;
    float computeRightEdge() const;

    AppConfig config_{};
    DRMDisplay display_{};
    CameraStream camera_{};
    HUDOverlay hud_{};
    VideoRenderer renderer_{};
    ModbusClient modbus_{};
    bool use_modbus_ = false;
};
