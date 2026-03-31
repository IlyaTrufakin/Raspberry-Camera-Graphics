#include "app.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
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

static bool parseLiteralOffsetPx(const std::string& token, int16_t& out) {
    if (token.empty()) {
        return false;
    }
    try {
        size_t pos = 0;
        int value = std::stoi(token, &pos, 10);
        while (pos < token.size() && std::isspace(static_cast<unsigned char>(token[pos]))) {
            ++pos;
        }
        if (pos != token.size()) {
            return false;
        }
        if (value < static_cast<int>(std::numeric_limits<int16_t>::min())) {
            value = static_cast<int>(std::numeric_limits<int16_t>::min());
        }
        if (value > static_cast<int>(std::numeric_limits<int16_t>::max())) {
            value = static_cast<int>(std::numeric_limits<int16_t>::max());
        }
        out = static_cast<int16_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

CameraConfig App::buildCameraConfig() const {
    CameraConfig cam{};
    cam.width = config_.video.width;
    cam.height = config_.video.height;
    cam.buffer_count = config_.video.buffer_count;

    // Adaptive exposure controller requires manual exposure mode.
    cam.ae_enable = config_.camera.ae_enable && !config_.exposure_control.enabled;
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

    if (config_.exposure_control.enabled) {
        cam.ae_enable = false;
        if (cam.exposure_time_mode < 0) {
            cam.exposure_time_mode = 1;
        }
        if (cam.analogue_gain_mode < 0) {
            cam.analogue_gain_mode = 1;
        }
    }

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
    initializeExposureControl();

    if (!hud_.initialize(display_.width(), display_.height(), config_.hud_font_path)) {
        std::cerr << "Failed to initialize HUD" << std::endl;
        return false;
    }
    updateLineObjects();
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

void App::initializeExposureControl() {
    exposure_control_active_ = config_.exposure_control.enabled;
    exposure_recovery_boost_frames_ = 0;
    exposure_frame_counter_ = 0;
    exposure_last_debug_ = std::chrono::steady_clock::now();

    if (!exposure_control_active_) {
        camera_.setRuntimeExposureControl(false, 0, 0.0f);
        return;
    }

    int min_exp = std::max(1, config_.exposure_control.min_exposure_us);
    int max_exp = std::max(min_exp, config_.exposure_control.max_exposure_us);
    float min_gain = std::max(0.1f, config_.exposure_control.min_gain);
    float max_gain = std::max(min_gain, config_.exposure_control.max_gain);

    int initial_exp = config_.camera.exposure_time_us;
    if (initial_exp <= 0) {
        initial_exp = (config_.camera.frame_duration_us > 0)
                          ? (config_.camera.frame_duration_us / 2)
                          : 4000;
    }
    if (initial_exp < min_exp) initial_exp = min_exp;
    if (initial_exp > max_exp) initial_exp = max_exp;

    float initial_gain = config_.camera.analogue_gain > 0.0f
                             ? config_.camera.analogue_gain
                             : min_gain;
    if (initial_gain < min_gain) initial_gain = min_gain;
    if (initial_gain > max_gain) initial_gain = max_gain;

    exposure_time_us_ = initial_exp;
    exposure_gain_ = initial_gain;

    camera_.setRuntimeExposureControl(true, exposure_time_us_, exposure_gain_);

    if (config_.exposure_control.debug) {
        std::cout << "Exposure control enabled: exp_us=" << exposure_time_us_
                  << " gain=" << exposure_gain_
                  << " roi=(" << config_.exposure_control.roi_x << ","
                  << config_.exposure_control.roi_y << ","
                  << config_.exposure_control.roi_width << ","
                  << config_.exposure_control.roi_height << ")"
                  << std::endl;
    }
}

void App::updateAdaptiveExposure(FrameBuffer* frame) {
    if (!exposure_control_active_ || !frame) {
        return;
    }

    const auto& cfg = config_.exposure_control;
    int update_every = std::max(1, cfg.update_every_frames);
    exposure_frame_counter_++;
    if ((exposure_frame_counter_ % static_cast<uint64_t>(update_every)) != 0) {
        return;
    }

    CameraStream::PlaneData y, u, v;
    if (!camera_.getFramePlanes(frame, y, u, v)) {
        return;
    }
    (void)u;
    (void)v;
    if (!y.data || y.width == 0 || y.height == 0 || y.stride == 0) {
        return;
    }

    float rx = std::max(0.0f, std::min(1.0f, cfg.roi_x));
    float ry = std::max(0.0f, std::min(1.0f, cfg.roi_y));
    float rw = std::max(0.0f, std::min(1.0f, cfg.roi_width));
    float rh = std::max(0.0f, std::min(1.0f, cfg.roi_height));
    if (rw <= 0.0f || rh <= 0.0f) {
        return;
    }

    uint32_t x0 = static_cast<uint32_t>(rx * static_cast<float>(y.width));
    uint32_t y0 = static_cast<uint32_t>(ry * static_cast<float>(y.height));
    uint32_t roi_w = std::max<uint32_t>(1, static_cast<uint32_t>(rw * static_cast<float>(y.width)));
    uint32_t roi_h = std::max<uint32_t>(1, static_cast<uint32_t>(rh * static_cast<float>(y.height)));
    uint32_t x1 = std::min<uint32_t>(y.width, x0 + roi_w);
    uint32_t y1 = std::min<uint32_t>(y.height, y0 + roi_h);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    int step = std::max(1, cfg.sample_step);
    std::array<uint32_t, 256> hist{};
    uint64_t sum = 0;
    uint64_t samples = 0;

    for (uint32_t yy = y0; yy < y1; yy += static_cast<uint32_t>(step)) {
        const uint8_t* row = y.data + static_cast<size_t>(yy) * y.stride;
        for (uint32_t xx = x0; xx < x1; xx += static_cast<uint32_t>(step)) {
            uint8_t lum = row[xx];
            hist[lum]++;
            sum += lum;
            samples++;
        }
    }

    if (samples == 0) {
        return;
    }

    auto percentile = [&](float p) -> int {
        uint64_t target = static_cast<uint64_t>(p * static_cast<float>(samples));
        if (target == 0) {
            target = 1;
        }
        uint64_t accum = 0;
        for (int i = 0; i < 256; ++i) {
            accum += hist[static_cast<size_t>(i)];
            if (accum >= target) {
                return i;
            }
        }
        return 255;
    };

    float mean = static_cast<float>(sum) / static_cast<float>(samples);
    int p99 = percentile(0.99f);

    int min_exp = std::max(1, cfg.min_exposure_us);
    int max_exp = std::max(min_exp, cfg.max_exposure_us);
    float min_gain = std::max(0.1f, cfg.min_gain);
    float max_gain = std::max(min_gain, cfg.max_gain);

    int next_exp = exposure_time_us_;
    float next_gain = exposure_gain_;

    uint64_t over_count = 0;
    int high_bin = std::max(0, std::min(255, cfg.high_luma));
    for (int i = high_bin; i < 256; ++i) {
        over_count += hist[static_cast<size_t>(i)];
    }
    float over_ratio = static_cast<float>(over_count) / static_cast<float>(samples);

    float target = std::max(1.0f, static_cast<float>(cfg.target_luma));
    bool hard_highlight = (p99 > cfg.high_luma + 28) || (over_ratio > 0.10f);
    bool soft_highlight = (p99 > cfg.high_luma) && (over_ratio > 0.02f) && (mean > target * 1.05f);
    bool highlight_active = hard_highlight || soft_highlight;

    bool recovery_boost = false;
    if (exposure_recovery_boost_frames_ > 0) {
        exposure_recovery_boost_frames_--;
    }

    if (hard_highlight) {
        // Strong clipping: reduce exposure quickly, but limit step to avoid ping-pong.
        float down = std::max(0.10f, std::min(0.98f, cfg.exposure_step_down));
        float severity = 1.0f + static_cast<float>(std::max(0, p99 - cfg.high_luma)) / 64.0f;
        if (severity > 1.8f) {
            severity = 1.8f;
        }
        next_exp = std::max(min_exp, static_cast<int>(static_cast<float>(next_exp) * std::pow(down, severity)));
        if (cfg.use_gain && next_gain > min_gain) {
            float gain_drop = cfg.gain_step_down * (0.5f + 0.7f * severity);
            next_gain = std::max(min_gain, next_gain - gain_drop);
        }
        exposure_recovery_boost_frames_ = 12;
    } else if (soft_highlight) {
        // Mild overexposure: gentle decay only.
        next_exp = std::max(min_exp, static_cast<int>(static_cast<float>(next_exp) * 0.93f));
        if (cfg.use_gain && next_gain > min_gain) {
            next_gain = std::max(min_gain, next_gain - cfg.gain_step_down * 0.22f);
        }
        if (exposure_recovery_boost_frames_ < 6) {
            exposure_recovery_boost_frames_ = 6;
        }
    } else if (mean < target - 1.0f) {
        // Accelerate recovery only after highlight has fully cleared.
        if (exposure_recovery_boost_frames_ > 0 &&
            over_ratio < 0.01f &&
            p99 < (cfg.high_luma - 12)) {
            recovery_boost = true;
        }
        float deficit = (target - mean) / target;
        if (deficit < 0.0f) {
            deficit = 0.0f;
        }
        if (deficit > 1.0f) {
            deficit = 1.0f;
        }
        float up_severity = 1.0f + deficit * 2.5f + (recovery_boost ? 0.9f : 0.0f);
        float step_up = std::max(1.001f, cfg.exposure_step_up);
        if (next_exp < max_exp) {
            int grown = static_cast<int>(static_cast<float>(next_exp) * std::pow(step_up, up_severity));
            int min_delta = 1 + static_cast<int>(std::round(deficit * (recovery_boost ? 10.0f : 5.0f)));
            next_exp = std::min(max_exp, std::max(next_exp + min_delta, grown));
        }
        if (cfg.use_gain && next_gain < max_gain) {
            float gain_up = cfg.gain_step_up * (1.0f + deficit * 2.5f + (recovery_boost ? 1.5f : 0.0f));
            next_gain = std::min(max_gain, next_gain + gain_up);
        }
    } else if (mean > target + 6.0f) {
        if (cfg.use_gain && next_gain > min_gain) {
            next_gain = std::max(min_gain, next_gain - cfg.gain_step_down * 0.2f);
        } else {
            next_exp = std::max(min_exp, static_cast<int>(static_cast<float>(next_exp) * 0.97f));
        }
    }

    // Slew-rate limits to suppress oscillations caused by control latency.
    float up_cap = recovery_boost ? 1.55f : 1.30f;
    float down_cap = hard_highlight ? 0.60f : 0.82f;
    int exp_low_cap = std::max(min_exp, static_cast<int>(static_cast<float>(exposure_time_us_) * down_cap));
    int exp_high_cap = std::min(max_exp, static_cast<int>(static_cast<float>(exposure_time_us_) * up_cap));
    if (exp_high_cap < exp_low_cap) {
        exp_high_cap = exp_low_cap;
    }
    next_exp = std::max(exp_low_cap, std::min(exp_high_cap, next_exp));

    float gain_down_cap = hard_highlight ? 0.80f : 0.55f;
    float gain_up_cap = recovery_boost ? 0.70f : 0.35f;
    float gain_low_cap = std::max(min_gain, exposure_gain_ - gain_down_cap);
    float gain_high_cap = std::min(max_gain, exposure_gain_ + gain_up_cap);
    if (gain_high_cap < gain_low_cap) {
        gain_high_cap = gain_low_cap;
    }
    next_gain = std::max(gain_low_cap, std::min(gain_high_cap, next_gain));

    // Optional floor during highlight to prevent excessive dimming.
    if (highlight_active) {
        if (cfg.highlight_min_exposure_us > 0) {
            int floor_exp = std::max(min_exp, cfg.highlight_min_exposure_us);
            floor_exp = std::min(max_exp, floor_exp);
            next_exp = std::max(next_exp, floor_exp);
        }
        if (cfg.use_gain && cfg.highlight_min_gain > 0.0f) {
            float floor_gain = std::max(min_gain, cfg.highlight_min_gain);
            floor_gain = std::min(max_gain, floor_gain);
            next_gain = std::max(next_gain, floor_gain);
        }
    }

    if (next_exp < min_exp) next_exp = min_exp;
    if (next_exp > max_exp) next_exp = max_exp;
    if (next_gain < min_gain) next_gain = min_gain;
    if (next_gain > max_gain) next_gain = max_gain;

    bool changed = (next_exp != exposure_time_us_) ||
                   (std::fabs(next_gain - exposure_gain_) > 0.0001f);
    if (changed) {
        exposure_time_us_ = next_exp;
        exposure_gain_ = next_gain;
        camera_.setRuntimeExposureControl(true, exposure_time_us_, exposure_gain_);
    }

    if (cfg.debug) {
        auto now = std::chrono::steady_clock::now();
        if (now - exposure_last_debug_ >= std::chrono::milliseconds(500)) {
            exposure_last_debug_ = now;
            CameraStream::CaptureStats stats{};
            bool has_stats = camera_.getFrameCaptureStats(frame, stats);
            std::cout << "EXP ctrl: mean=" << mean
                      << " p99=" << p99
                      << " over=" << over_ratio
                      << " hl=" << (highlight_active ? 1 : 0)
                      << " samples=" << samples
                      << " exp_target_us=" << exposure_time_us_
                      << " gain_target=" << exposure_gain_;
            if (has_stats) {
                if (stats.has_exposure_time) {
                    std::cout << " exp_actual_us=" << stats.exposure_time_us;
                }
                if (stats.has_analogue_gain) {
                    std::cout << " gain_actual=" << stats.analogue_gain;
                }
                if (stats.has_frame_duration) {
                    std::cout << " frame_us=" << stats.frame_duration_us;
                }
            }
            std::cout
                      << std::endl;
        }
    }
}

void App::updateLineObjects() {
    std::vector<LineObjectRender> lines;
    if (display_.width() <= 0 || display_.height() <= 0) {
        hud_.setLineObjects(lines);
        return;
    }

    const float width = static_cast<float>(display_.width());
    const float height = static_cast<float>(display_.height());
    const float center_x_px = width * 0.5f;
    const float center_y_px = height * 0.5f;

    float left = computeLeftEdge() * 2.0f - 1.0f;
    float right = computeRightEdge() * 2.0f - 1.0f;
    if (right <= left) {
        left = -1.0f;
        right = 1.0f;
    }

    lines.reserve(config_.vertical_lines.size() + config_.horizontal_lines.size());

    for (const auto& cfg : config_.vertical_lines) {
        if (!cfg.enabled) {
            continue;
        }

        int16_t offset_px = 0;
        uint16_t raw = 0;
        if (!cfg.offset_var.empty()) {
            if (use_modbus_ && modbus_.getVariable(cfg.offset_var, raw)) {
                offset_px = static_cast<int16_t>(raw);
            } else if (parseLiteralOffsetPx(cfg.offset_var, offset_px)) {
            }
        }

        float x_px = center_x_px + static_cast<float>(offset_px);
        if (x_px < 0.0f) x_px = 0.0f;
        if (x_px > width) x_px = width;
        float x_ndc = 2.0f * (x_px / width) - 1.0f;

        LineObjectRender line;
        line.x0_ndc = x_ndc;
        line.y0_ndc = 1.0f;
        line.x1_ndc = x_ndc;
        line.y1_ndc = -1.0f;
        line.color = cfg.color;
        line.line_width = cfg.line_width;
        line.line_style = cfg.line_style;
        line.dash_length = 2.0f * (std::max(1.0f, cfg.dash_length_px) / height);
        line.dash_gap = 2.0f * (std::max(0.0f, cfg.dash_gap_px) / height);
        lines.push_back(line);
    }

    for (const auto& cfg : config_.horizontal_lines) {
        if (!cfg.enabled) {
            continue;
        }

        int16_t offset_px = 0;
        uint16_t raw = 0;
        if (!cfg.offset_var.empty()) {
            if (use_modbus_ && modbus_.getVariable(cfg.offset_var, raw)) {
                offset_px = static_cast<int16_t>(raw);
            } else if (parseLiteralOffsetPx(cfg.offset_var, offset_px)) {
            }
        }

        float y_px = center_y_px + static_cast<float>(offset_px);
        if (y_px < 0.0f) y_px = 0.0f;
        if (y_px > height) y_px = height;
        float y_ndc = -(2.0f * (y_px / height) - 1.0f);

        LineObjectRender line;
        line.x0_ndc = left;
        line.y0_ndc = y_ndc;
        line.x1_ndc = right;
        line.y1_ndc = y_ndc;
        line.color = cfg.color;
        line.line_width = cfg.line_width;
        line.line_style = cfg.line_style;
        line.dash_length = 2.0f * (std::max(1.0f, cfg.dash_length_px) / width);
        line.dash_gap = 2.0f * (std::max(0.0f, cfg.dash_gap_px) / width);
        lines.push_back(line);
    }

    hud_.setLineObjects(lines);
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

void App::updateHud(const std::string& fps_text) {
    hud_.clearDynamicTextPositions();
    hud_.clearDynamicRectPositions();
    updateLineObjects();

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
            updateAdaptiveExposure(frame);
            camera_.returnFrame(frame);
            got_frame = true;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_hud_update >= std::chrono::milliseconds(hud_interval_ms)) {
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
