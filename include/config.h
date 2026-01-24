#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "hud_overlay.h"

struct VideoConfig {
    uint32_t width = 1012;
    uint32_t height = 760;
    uint32_t buffer_count = 2;
    bool flip_horizontal = false;
    bool flip_vertical = false;
    int rotate = 0; // 0, 90, 180, 270
};

struct StaticTextConfig {
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
    std::string text;
};

struct StaticRectConfig {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
    std::string text;
    int text_align = 0; // 0 = left, 1 = right, 2 = center
    float text_padding = 0.01f; // normalized (0..1) relative to screen width
    float text_scale = 0.0f; // <=0 => auto-fit
    Color text_color = Color(1.0f, 1.0f, 1.0f, 1.0f);
};

struct DynamicTextConfig {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
};

struct StatusBitConfig {
    std::string name;
    uint8_t bit = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    Color color_on = Color(0.0f, 1.0f, 0.0f, 1.0f);
    Color color_off = Color(1.0f, 0.0f, 0.0f, 0.5f);
    Color color_unknown = Color(0.5f, 0.5f, 0.5f, 0.5f);
    std::string text;
    int text_align = 0; // 0 = left, 1 = right, 2 = center
    float text_padding = 0.01f; // normalized (0..1) relative to screen width
    float text_scale = 0.0f; // <=0 => auto-fit
    Color text_color = Color(1.0f, 1.0f, 1.0f, 1.0f);
};

struct ModbusSettings {
    bool enabled = false;
    std::string ip = "192.168.0.78";
    uint16_t port = 502;
    uint8_t unit_id = 1;
    int update_ms = 150;
    int reconnect_ms = 1000;
    int address_offset = 0;
    bool block_read = true;
    int error_threshold = 5;
    int error_backoff_ms = 2000;
    int response_timeout_ms = 500;
    int byte_timeout_ms = 500;
    int inter_request_delay_ms = 0;
    bool log_timestamps = false;
    bool keep_last_on_error = true;
    int reset_after_errors = 3;
    std::string register_type = "holding";
    bool debug = false;
    std::map<std::string, uint16_t> registers;
    std::map<std::string, int> decimals;
};

struct CameraSettings {
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
};

struct RoiConfig {
    bool enabled = false;
    bool auto_fit = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct AppConfig {
    VideoConfig video;
    CameraSettings camera;
    CrosshairConfig crosshair;
    PanelConfig panel_left;
    PanelConfig panel_right;
    RoiConfig roi;
    int hud_update_ms = 150;
    std::string hud_font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf";
    float hud_text_vshift = 0.08f;
    std::vector<StaticTextConfig> static_texts;
    std::vector<StaticRectConfig> static_rects;
    std::vector<DynamicTextConfig> dynamic_texts;
    std::vector<StatusBitConfig> status_bits;
    ModbusSettings modbus;
};

bool loadConfig(const std::string& path, AppConfig& config);
