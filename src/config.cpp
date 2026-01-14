#include "config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> parts;
    std::string item;
    std::istringstream iss(s);
    while (std::getline(iss, item, ',')) {
        parts.push_back(trim(item));
    }
    return parts;
}

static bool parseBool(const std::string& s, bool& out) {
    std::string v = toLower(trim(s));
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    return false;
}

static bool parseFloat(const std::string& s, float& out) {
    try {
        out = std::stof(trim(s));
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseUInt(const std::string& s, uint32_t& out) {
    try {
        out = static_cast<uint32_t>(std::stoul(trim(s)));
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseU16(const std::string& s, uint16_t& out) {
    try {
        out = static_cast<uint16_t>(std::stoul(trim(s)));
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseColor(const std::string& s, Color& color) {
    auto parts = splitComma(s);
    if (parts.size() < 4) {
        return false;
    }
    float r = 0, g = 0, b = 0, a = 1;
    if (!parseFloat(parts[0], r) ||
        !parseFloat(parts[1], g) ||
        !parseFloat(parts[2], b) ||
        !parseFloat(parts[3], a)) {
        return false;
    }
    color = Color(r, g, b, a);
    return true;
}

static void setDefaults(AppConfig& config) {
    config.video = VideoConfig();
    config.crosshair = CrosshairConfig();
    config.panel = PanelConfig();
    config.hud_update_ms = 150;

    config.modbus = ModbusSettings();
    config.modbus.registers.clear();

    config.static_texts.clear();

    config.dynamic_texts.clear();
}

bool loadConfig(const std::string& path, AppConfig& config) {
    setDefaults(config);

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    bool cleared_static = false;
    bool cleared_dynamic = false;
    bool cleared_registers = false;

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = toLower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = toLower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));

        if (section == "video") {
            if (key == "width") {
                parseUInt(value, config.video.width);
            } else if (key == "height") {
                parseUInt(value, config.video.height);
            } else if (key == "buffer_count") {
                parseUInt(value, config.video.buffer_count);
            } else if (key == "pixel_format") {
                config.video.pixel_format = value;
            }
        } else if (section == "crosshair") {
            if (key == "enabled") {
                parseBool(value, config.crosshair.enabled);
            } else if (key == "color") {
                parseColor(value, config.crosshair.color);
            } else if (key == "center") {
                auto parts = splitComma(value);
                if (parts.size() >= 2) {
                    parseFloat(parts[0], config.crosshair.center_x);
                    parseFloat(parts[1], config.crosshair.center_y);
                }
            } else if (key == "line_length") {
                parseFloat(value, config.crosshair.line_length);
            } else if (key == "line_width") {
                parseFloat(value, config.crosshair.line_width);
            } else if (key == "gap") {
                parseFloat(value, config.crosshair.gap);
            }
        } else if (section == "panel") {
            if (key == "enabled") {
                parseBool(value, config.panel.enabled);
            } else if (key == "x") {
                parseFloat(value, config.panel.x);
            } else if (key == "y") {
                parseFloat(value, config.panel.y);
            } else if (key == "width") {
                parseFloat(value, config.panel.width);
            } else if (key == "height") {
                parseFloat(value, config.panel.height);
            } else if (key == "color") {
                parseColor(value, config.panel.color);
            }
        } else if (section == "hud") {
            if (key == "update_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.hud_update_ms = static_cast<int>(tmp);
                }
            }
        } else if (section == "modbus") {
            if (key == "enabled") {
                parseBool(value, config.modbus.enabled);
            } else if (key == "ip") {
                config.modbus.ip = value;
            } else if (key == "port") {
                parseU16(value, config.modbus.port);
            } else if (key == "update_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.update_ms = static_cast<int>(tmp);
                }
            }
        } else if (section == "modbus.registers") {
            uint16_t addr = 0;
            if (parseU16(value, addr)) {
                if (!cleared_registers) {
                    config.modbus.registers.clear();
                    cleared_registers = true;
                }
                config.modbus.registers[key] = addr;
            }
        } else if (section == "text.static") {
            auto parts = splitComma(value);
            if (parts.size() >= 8) {
                if (!cleared_static) {
                    config.static_texts.clear();
                    cleared_static = true;
                }
                StaticTextConfig item;
                parseFloat(parts[0], item.x);
                parseFloat(parts[1], item.y);
                parseFloat(parts[2], item.scale);
                float r = 1, g = 1, b = 1, a = 1;
                parseFloat(parts[3], r);
                parseFloat(parts[4], g);
                parseFloat(parts[5], b);
                parseFloat(parts[6], a);
                item.color = Color(r, g, b, a);
                std::string text = parts[7];
                for (size_t i = 8; i < parts.size(); ++i) {
                    text += ",";
                    text += parts[i];
                }
                item.text = trim(text);
                config.static_texts.push_back(item);
            }
        } else if (section == "text.dynamic") {
            auto parts = splitComma(value);
            if (parts.size() >= 7) {
                if (!cleared_dynamic) {
                    config.dynamic_texts.clear();
                    cleared_dynamic = true;
                }
                DynamicTextConfig item;
                item.name = key;
                parseFloat(parts[0], item.x);
                parseFloat(parts[1], item.y);
                parseFloat(parts[2], item.scale);
                float r = 1, g = 1, b = 1, a = 1;
                parseFloat(parts[3], r);
                parseFloat(parts[4], g);
                parseFloat(parts[5], b);
                parseFloat(parts[6], a);
                item.color = Color(r, g, b, a);
                config.dynamic_texts.push_back(item);
            }
        }
    }

    return true;
}
