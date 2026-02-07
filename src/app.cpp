#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>


static std::string formatWithDecimals(int16_t value, int decimals) {
    if (decimals <= 0) {
        return std::to_string(value);
    }
    double scaled = static_cast<double>(value);
    for (int i = 0; i < decimals; ++i) {
        scaled /= 10.0;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << scaled;
    return oss.str();
}

bool App::loadConfiguration(const std::string& path) {
    bool ok = loadConfig(path, config_);
    if (!ok) {
        std::cout << "Config file not found, using defaults" << std::endl;
    } else {
        std::cout << "Config loaded: " << path << std::endl;
        std::cout << "Video transform: flip_horizontal="
                  << (config_.video.flip_horizontal ? "true" : "false")
                  << " flip_vertical="
                  << (config_.video.flip_vertical ? "true" : "false")
                  << " rotate=" << config_.video.rotate
                  << std::endl;
    }
    return ok;
}

CameraConfig App::buildCameraConfig() const {
    CameraConfig cam{};
    cam.width = config_.video.width;
    cam.height = config_.video.height;
    cam.buffer_count = config_.video.buffer_count;

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
    CrosshairConfig cross = config_.crosshair;
    cross.h_limit_left = computeLeftEdge();
    cross.h_limit_right = computeRightEdge();
    hud_.setCrosshairConfig(cross);
    hud_.setPanelConfigs(config_.panel_left, config_.panel_right);
    buildStaticHudCaches();

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
        modbus_.setKeepLastOnError(config_.modbus.keep_last_on_error);
        modbus_.setResetAfterErrors(config_.modbus.reset_after_errors);
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

                refreshModbusTextCache();
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
    if (!renderer_.initialize(display_, config_, left_edge, right_edge)) {
        return false;
    }
    return true;
}

void App::buildStaticHudCaches() {
    static_rect_text_cache_.clear();
    status_bit_text_cache_.clear();

    std::vector<TextPosition> static_texts;
    static_texts.reserve(config_.static_texts.size());
    for (const auto& item : config_.static_texts) {
        static_texts.emplace_back(item.x, item.y, item.text, item.scale, item.color);
    }

    std::vector<RectPosition> static_rects;
    static_rects.reserve(config_.static_rects.size());
    for (const auto& rect : config_.static_rects) {
        static_rects.push_back(RectPosition{rect.x, rect.y, rect.width, rect.height, rect.color});
    }

    auto buildText = [&](float rect_x, float rect_y, float rect_w, float rect_h,
                         const std::string& text, const Color& text_color,
                         int text_align, float text_padding, float text_scale,
                         TextPosition& out) -> bool {
        if (text.empty() || rect_w <= 0.0f || rect_h <= 0.0f) {
            return false;
        }
        float rect_x_px = rect_x * display_.width();
        float rect_y_px = rect_y * display_.height();
        float rect_w_px = rect_w * display_.width();
        float rect_h_px = rect_h * display_.height();

        float pad_px = std::max(0.0f, text_padding) * display_.width();
        float scale = text_scale;
        float base_w = 0.0f, base_ascent = 0.0f, base_descent = 0.0f;
        if (scale <= 0.0f) {
            if (hud_.measureText(text, 1.0f, base_w, base_ascent, base_descent)) {
                float base_h = base_ascent + base_descent;
                float target_h = rect_h_px * 0.8f;
                float max_w = std::max(0.0f, rect_w_px - 2.0f * pad_px);
                float scale_h = (base_h > 0.0f) ? (target_h / base_h) : 1.0f;
                float scale_w = (base_w > 0.0f) ? (max_w / base_w) : scale_h;
                scale = std::max(0.01f, std::min(scale_h, scale_w));
            } else {
                scale = 0.4f;
            }
        }

        float text_w = 0.0f, text_ascent = 0.0f, text_descent = 0.0f;
        if (!hud_.measureText(text, scale, text_w, text_ascent, text_descent)) {
            return false;
        }

        float rect_center_y = rect_y_px + rect_h_px * 0.5f;
        float optical_shift = (text_ascent + text_descent) * config_.hud_text_vshift;
        float baseline_y = rect_center_y - (text_ascent - text_descent) * 0.5f - optical_shift;

        float x_px = rect_x_px + pad_px;
        if (text_align == 1) {
            x_px = rect_x_px + rect_w_px - pad_px - text_w;
            if (x_px < rect_x_px + pad_px) {
                x_px = rect_x_px + pad_px;
            }
        } else if (text_align == 2) {
            x_px = rect_x_px + (rect_w_px - text_w) * 0.5f;
            if (x_px < rect_x_px + pad_px) {
                x_px = rect_x_px + pad_px;
            }
        }

        float x_norm = x_px / static_cast<float>(display_.width());
        float y_norm = baseline_y / static_cast<float>(display_.height());
        out = TextPosition(x_norm, y_norm, text, scale, text_color);
        return true;
    };

    for (const auto& rect : config_.static_rects) {
        TextPosition tp(0.0f, 0.0f, "", 1.0f, rect.text_color);
        if (buildText(rect.x, rect.y, rect.width, rect.height, rect.text,
                      rect.text_color, rect.text_align, rect.text_padding,
                      rect.text_scale, tp)) {
            static_rect_text_cache_.push_back(tp);
        }
    }

    if (config_.modbus.enabled) {
        for (const auto& bit_cfg : config_.status_bits) {
            TextPosition tp(0.0f, 0.0f, "", 1.0f, bit_cfg.text_color);
            if (buildText(bit_cfg.x, bit_cfg.y, bit_cfg.width, bit_cfg.height,
                          bit_cfg.text, bit_cfg.text_color, bit_cfg.text_align,
                          bit_cfg.text_padding, bit_cfg.text_scale, tp)) {
                status_bit_text_cache_.push_back(tp);
            }
        }
    }

    static_texts.insert(static_texts.end(),
                        static_rect_text_cache_.begin(), static_rect_text_cache_.end());
    static_texts.insert(static_texts.end(),
                        status_bit_text_cache_.begin(), status_bit_text_cache_.end());

    hud_.setStaticRectPositions(static_rects);
    hud_.setStaticTextPositions(static_texts);
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
    cross.h_limit_left = computeLeftEdge();
    cross.h_limit_right = computeRightEdge();
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
    hud_.clearDynamicTextPositions();
    hud_.clearDynamicRectPositions();

    bool modbus_link_ok = use_modbus_ && modbus_.isConnected();

    for (const auto& item : config_.dynamic_texts) {
        std::string text = "---";
        if (item.name == "fps") {
            text = fps_text;
        } else if (use_modbus_) {
            if (!modbus_link_ok) {
                text = "---";
            } else {
            std::lock_guard<std::mutex> lock(modbus_text_mutex_);
            auto it = modbus_text_cache_.find(item.name);
            if (it != modbus_text_cache_.end()) {
                text = it->second;
            } else {
                text = "0";
            }
            }
        }
        hud_.addTextPosition(TextPosition(item.x, item.y, text, item.scale, item.color));
    }

    if (use_modbus_) {
        for (const auto& bit_cfg : config_.status_bits) {
            uint16_t value = 0;
            bool ok = modbus_link_ok && modbus_.getVariable(bit_cfg.name, value);
            bool bit_on = false;
            if (ok) {
                bit_on = ((value >> bit_cfg.bit) & 0x1) != 0;
            }
            Color c = modbus_link_ok ? (bit_on ? bit_cfg.color_on : bit_cfg.color_off)
                                     : bit_cfg.color_unknown;
            hud_.addRectPosition(RectPosition{bit_cfg.x, bit_cfg.y,
                                              bit_cfg.width, bit_cfg.height, c});
        }
    }

}

void App::refreshModbusTextCache() {
    std::lock_guard<std::mutex> lock(modbus_text_mutex_);
    if (!modbus_.isConnected()) {
        for (const auto& item : config_.dynamic_texts) {
            if (item.name == "fps") {
                continue;
            }
            modbus_text_cache_[item.name] = "---";
        }
        return;
    }
    for (const auto& item : config_.dynamic_texts) {
        if (item.name == "fps") {
            continue;
        }
        if (item.name == "modbus_errors") {
            modbus_text_cache_[item.name] = std::to_string(modbus_.getErrorCount());
            continue;
        }
        uint16_t value = 0;
        if (modbus_.getVariable(item.name, value)) {
            int16_t signed_value = static_cast<int16_t>(value);
            auto it = config_.modbus.decimals.find(item.name);
            if (it != config_.modbus.decimals.end()) {
                modbus_text_cache_[item.name] = formatWithDecimals(signed_value, it->second);
            } else {
                modbus_text_cache_[item.name] = std::to_string(signed_value);
            }
        } else {
            modbus_text_cache_[item.name] = "0";
        }
    }
}

void App::runLoop() {
    std::cout << "Starting main loop..." << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    int current_fps = 0;
    std::string fps_text = "0";

    int hud_interval_ms = (use_modbus_ && config_.modbus.update_ms > 0)
                              ? config_.modbus.update_ms
                              : (config_.hud_update_ms > 0 ? config_.hud_update_ms : 150);
    auto last_hud_update = std::chrono::steady_clock::now()
                           - std::chrono::milliseconds(hud_interval_ms);

    while (true) {
        FrameBuffer* frame = camera_.getNextFrame();
        bool got_frame = false;
        if (frame) {
            renderer_.uploadFrame(camera_, frame);
            camera_.returnFrame(frame);
            got_frame = true;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_hud_update >= std::chrono::milliseconds(hud_interval_ms)) {
            updateCrosshair();
            updateHud(fps_text);
            last_hud_update = now;
        }

        renderer_.draw(hud_);

        if (got_frame) {
            frame_count++;
        }
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
