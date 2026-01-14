#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "hud_overlay.h"

struct VideoConfig {
    uint32_t width = 1012;
    uint32_t height = 760;
    uint32_t buffer_count = 2;
    std::string pixel_format = "YUV420";
};

struct StaticTextConfig {
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
    std::string text;
};

struct DynamicTextConfig {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
};

struct ModbusSettings {
    bool enabled = false;
    std::string ip = "192.168.0.78";
    uint16_t port = 502;
    int update_ms = 150;
    std::map<std::string, uint16_t> registers;
};

struct AppConfig {
    VideoConfig video;
    CrosshairConfig crosshair;
    PanelConfig panel;
    int hud_update_ms = 150;
    std::vector<StaticTextConfig> static_texts;
    std::vector<DynamicTextConfig> dynamic_texts;
    ModbusSettings modbus;
};

bool loadConfig(const std::string& path, AppConfig& config);
