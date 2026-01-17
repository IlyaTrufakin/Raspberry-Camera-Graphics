#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <unistd.h>

bool App::loadConfiguration(const std::string& path) {
    bool ok = loadConfig(path, config_);
    if (!ok) {
        std::cout << "Config file not found, using defaults" << std::endl;
    }
    return ok;
}

CameraConfig App::buildCameraConfig() const {
    CameraConfig cam{};
    cam.width = config_.video.width;
    cam.height = config_.video.height;
    cam.buffer_count = config_.video.buffer_count;
    cam.pixel_format = config_.video.pixel_format;

    cam.ae_enable = config_.camera.ae_enable;
    cam.awb_enable = config_.camera.awb_enable;
    cam.controls_dump = config_.camera.controls_dump;
    cam.exposure_time_us = config_.camera.exposure_time_us;
    cam.analogue_gain = config_.camera.analogue_gain;
    cam.brightness = config_.camera.brightness;
    cam.contrast = config_.camera.contrast;
    cam.saturation = config_.camera.saturation;
    cam.sharpness = config_.camera.sharpness;
    cam.exposure_compensation = config_.camera.exposure_compensation;
    cam.frame_duration_us = config_.camera.frame_duration_us;
    cam.ae_metering = config_.camera.ae_metering;
    cam.ae_constraint = config_.camera.ae_constraint;
    cam.ae_exposure_mode = config_.camera.ae_exposure_mode;
    cam.awb_mode = config_.camera.awb_mode;
    cam.ae_flicker_mode = config_.camera.ae_flicker_mode;
    cam.exposure_time_mode = config_.camera.exposure_time_mode;
    cam.analogue_gain_mode = config_.camera.analogue_gain_mode;
    cam.noise_reduction_mode = config_.camera.noise_reduction_mode;
    cam.hdr_mode = config_.camera.hdr_mode;
    cam.sync_mode = config_.camera.sync_mode;
    cam.sync_frames = config_.camera.sync_frames;
    cam.colour_temperature = config_.camera.colour_temperature;
    cam.ae_flicker_period = config_.camera.ae_flicker_period;
    cam.stats_output_enable = config_.camera.stats_output_enable;
    cam.cnn_enable_input_tensor = config_.camera.cnn_enable_input_tensor;
    cam.colour_gains = config_.camera.colour_gains;
    cam.colour_gains_set = config_.camera.colour_gains_set;
    cam.colour_correction_matrix = config_.camera.colour_correction_matrix;
    cam.colour_correction_matrix_set = config_.camera.colour_correction_matrix_set;

    cam.roi_enabled = config_.roi.enabled;
    cam.roi_x = config_.roi.x;
    cam.roi_y = config_.roi.y;
    cam.roi_width = config_.roi.width;
    cam.roi_height = config_.roi.height;
    cam.roi_auto = config_.roi.auto_fit;

    if (cam.roi_auto) {
        float left_edge = computeLeftEdge();
        float right_edge = computeRightEdge();
        if (right_edge > left_edge) {
            float center_width = right_edge - left_edge;
            cam.roi_target_aspect =
                (center_width * static_cast<float>(display_.width())) / static_cast<float>(display_.height());
        }
    }

    if (cam.roi_auto && cam.roi_target_aspect > 0.0f) {
        float target_aspect = cam.roi_target_aspect;
        int new_width = static_cast<int>(std::round(static_cast<float>(cam.height) * target_aspect));
        if (new_width < 2) {
            new_width = 2;
        }
        new_width &= ~1;
        if (new_width > 0) {
            cam.width = static_cast<uint32_t>(new_width);
        }
    }

    return cam;
}

float App::computeLeftEdge() const {
    float left_edge = 0.0f;
    if (config_.panel_left.enabled) {
        left_edge = std::min(1.0f, std::max(0.0f, config_.panel_left.x + config_.panel_left.width));
    }
    return left_edge;
}

float App::computeRightEdge() const {
    float right_edge = 1.0f;
    if (config_.panel_right.enabled) {
        right_edge = std::min(1.0f, std::max(0.0f, config_.panel_right.x));
    }
    return right_edge;
}

bool App::initialize() {
    if (!display_.initialize()) {
        return false;
    }

    CameraConfig cam_config = buildCameraConfig();
    if (!camera_.initialize(cam_config)) {
        std::cerr << "Failed to initialize camera" << std::endl;
        return false;
    }
    if (!camera_.start()) {
        std::cerr << "Failed to start camera" << std::endl;
        return false;
    }

    if (!hud_.initialize(display_.width(), display_.height(), config_.hud_font_path)) {
        std::cerr << "Failed to initialize HUD" << std::endl;
        return false;
    }
    hud_.setCrosshairConfig(config_.crosshair);
    hud_.setPanelConfigs(config_.panel_left, config_.panel_right);

    if (config_.modbus.enabled) {
        modbus_.setUnitId(config_.modbus.unit_id);
        modbus_.setRegisterType(config_.modbus.register_type);
        modbus_.setDebug(config_.modbus.debug);
        modbus_.setAddressOffset(config_.modbus.address_offset);
        modbus_.setBlockRead(config_.modbus.block_read);
        modbus_.setResponseTimeoutMs(config_.modbus.response_timeout_ms);
        modbus_.setByteTimeoutMs(config_.modbus.byte_timeout_ms);
        modbus_.setInterRequestDelayMs(config_.modbus.inter_request_delay_ms);
        modbus_.setLogTimestamps(config_.modbus.log_timestamps);
        for (const auto& entry : config_.modbus.registers) {
            modbus_.registerVariable(entry.first, entry.second);
        }
        use_modbus_ = true;
        if (!modbus_.connect(config_.modbus.ip, config_.modbus.port)) {
            std::cerr << "Modbus connect failed, will retry" << std::endl;
        }
    }

    int modbus_interval_ms = config_.modbus.update_ms > 0 ? config_.modbus.update_ms : 150;
    int modbus_reconnect_ms = config_.modbus.reconnect_ms > 0 ? config_.modbus.reconnect_ms : 1000;
    int modbus_error_threshold = config_.modbus.error_threshold > 0 ? config_.modbus.error_threshold : 5;
    int modbus_error_backoff_ms =
        config_.modbus.error_backoff_ms > 0 ? config_.modbus.error_backoff_ms : 2000;
    if (use_modbus_) {
        std::thread([this, modbus_interval_ms, modbus_reconnect_ms,
                     modbus_error_threshold, modbus_error_backoff_ms]() {
            int consecutive_errors = 0;
            while (true) {
                if (!modbus_.isConnected()) {
                    if (!modbus_.connect(config_.modbus.ip, config_.modbus.port)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(modbus_reconnect_ms));
                        continue;
                    }
                }

                if (!modbus_.readVariables()) {
                    if (modbus_.lastErrorIsConnection()) {
                        modbus_.disconnect();
                        std::this_thread::sleep_for(std::chrono::milliseconds(modbus_reconnect_ms));
                        continue;
                    }
                    consecutive_errors++;
                    if (consecutive_errors >= modbus_error_threshold) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(modbus_error_backoff_ms));
                        consecutive_errors = 0;
                    }
                } else {
                    consecutive_errors = 0;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(modbus_interval_ms));
            }
        }).detach();
    }

    float left_edge = computeLeftEdge();
    float right_edge = computeRightEdge();
    if (right_edge <= left_edge) {
        left_edge = 0.0f;
        right_edge = 1.0f;
    }
    return renderer_.initialize(display_, config_, left_edge, right_edge);
}

void App::updateCrosshair() {
    if (!use_modbus_ || !config_.crosshair.modbus_override) {
        return;
    }

    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    if (modbus_.getVariable("crosshair_x", raw_x)) {
        raw_x = static_cast<uint16_t>(raw_x);
    }
    if (modbus_.getVariable("crosshair_y", raw_y)) {
        raw_y = static_cast<uint16_t>(raw_y);
    }

    CrosshairConfig cross = config_.crosshair;
    cross.center_x = 0.5f + static_cast<float>(static_cast<int16_t>(raw_x)) /
                                  static_cast<float>(display_.width());
    cross.center_y = 0.5f + static_cast<float>(static_cast<int16_t>(raw_y)) /
                                  static_cast<float>(display_.height());
    if (cross.center_x < 0.0f) cross.center_x = 0.0f;
    if (cross.center_x > 1.0f) cross.center_x = 1.0f;
    if (cross.center_y < 0.0f) cross.center_y = 0.0f;
    if (cross.center_y > 1.0f) cross.center_y = 1.0f;
    hud_.setCrosshairConfig(cross);
}

void App::updateHud(const std::string& fps_text) {
    hud_.clearTextPositions();
    hud_.clearRectPositions();

    for (const auto& item : config_.static_texts) {
        hud_.addTextPosition(TextPosition(item.x, item.y, item.text, item.scale, item.color));
    }

    for (const auto& rect : config_.static_rects) {
        hud_.addRectPosition(RectPosition{rect.x, rect.y, rect.width, rect.height, rect.color});
    }

    for (const auto& item : config_.dynamic_texts) {
        std::string text = "---";
        if (item.name == "fps") {
            text = fps_text;
        } else if (use_modbus_) {
            uint16_t value = 0;
            if (modbus_.getVariable(item.name, value)) {
                text = std::to_string(value);
            } else {
                text = "0";
            }
        }
        hud_.addTextPosition(TextPosition(item.x, item.y, text, item.scale, item.color));
    }

    if (use_modbus_) {
        for (const auto& bit_cfg : config_.status_bits) {
            uint16_t value = 0;
            bool ok = modbus_.getVariable(bit_cfg.name, value);
            bool bit_on = false;
            if (ok) {
                bit_on = ((value >> bit_cfg.bit) & 0x1) != 0;
            }
            Color c = bit_on ? bit_cfg.color_on : bit_cfg.color_off;
            hud_.addRectPosition(RectPosition{bit_cfg.x, bit_cfg.y,
                                              bit_cfg.width, bit_cfg.height, c});
        }
    }
}

void App::runLoop() {
    std::cout << "Starting main loop..." << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    int current_fps = 0;
    std::string fps_text = "0";

    int hud_interval_ms = config_.hud_update_ms > 0 ? config_.hud_update_ms : 150;
    auto last_hud_update = std::chrono::steady_clock::now()
                           - std::chrono::milliseconds(hud_interval_ms);

    while (true) {
        FrameBuffer* frame = camera_.getNextFrame();
        if (frame) {
            renderer_.uploadFrame(camera_, frame, config_);
            camera_.returnFrame(frame);
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_hud_update >= std::chrono::milliseconds(hud_interval_ms)) {
            updateCrosshair();
            updateHud(fps_text);
            last_hud_update = now;
        }

        renderer_.draw(hud_);

        frame_count++;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
        if (elapsed >= 1) {
            current_fps = frame_count;
            fps_text = std::to_string(current_fps);
            frame_count = 0;
            last_time = now;
        }

        usleep(1000);
    }
}

void App::shutdown() {
    camera_.stop();
    renderer_.shutdown();
    display_.cleanup();
}

int App::run(const std::string& config_path) {
    loadConfiguration(config_path);
    if (!initialize()) {
        shutdown();
        return 1;
    }
    runLoop();
    shutdown();
    return 0;
}
