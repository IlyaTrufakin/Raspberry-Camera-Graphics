#include "config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    // Ignore UTF-8 BOM on the first logical token (common on Windows-edited INI files).
    if (start + 2 < s.size() &&
        static_cast<unsigned char>(s[start]) == 0xEF &&
        static_cast<unsigned char>(s[start + 1]) == 0xBB &&
        static_cast<unsigned char>(s[start + 2]) == 0xBF) {
        start += 3;
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

static std::vector<std::string> splitByAny(const std::string& s, const std::string& delims) {
    std::vector<std::string> parts;
    std::string item;
    for (char ch : s) {
        if (delims.find(ch) != std::string::npos) {
            if (!item.empty()) {
                parts.push_back(trim(item));
                item.clear();
            }
        } else {
            item.push_back(ch);
        }
    }
    if (!item.empty()) {
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

static bool parseInt(const std::string& s, int& out) {
    try {
        out = std::stoi(trim(s));
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseLineStyle(const std::string& s, int& out) {
    std::string v = toLower(trim(s));
    if (v == "solid") {
        out = 0;
        return true;
    }
    if (v == "dashed" || v == "dash") {
        out = 1;
        return true;
    }
    return parseInt(s, out);
}

static bool parseAlign(const std::string& s, int& out) {
    std::string v = toLower(trim(s));
    if (v == "left" || v == "l") {
        out = 0;
        return true;
    }
    if (v == "right" || v == "r") {
        out = 1;
        return true;
    }
    if (v == "center" || v == "centre" || v == "c" || v == "middle" || v == "m") {
        out = 2;
        return true;
    }
    return parseInt(s, out);
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
    config.camera = CameraSettings();
    config.crosshair = CrosshairConfig();
    config.panel_left = PanelConfig();
    config.panel_right = PanelConfig();
    config.roi = RoiConfig();
    config.hud_update_ms = 150;
    config.hud_font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf";
    config.hud_text_vshift = 0.08f;

    config.modbus = ModbusSettings();
    config.modbus.registers.clear();
    config.modbus.decimals.clear();

    config.static_texts.clear();

    config.static_rects.clear();

    config.dynamic_texts.clear();

    config.status_bits.clear();
}

bool loadConfig(const std::string& path, AppConfig& config) {
    setDefaults(config);

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    bool cleared_static = false;
    bool cleared_static_rects = false;
    bool cleared_dynamic = false;
    bool cleared_registers = false;
    bool cleared_status = false;
    std::map<std::string, size_t> status_index;
    std::vector<std::string> status_ids;
    std::map<std::string, size_t> rect_index;

    std::vector<std::string> logical_lines;
    {
        std::string raw;
        std::string current_section;
        std::string pending;
        while (std::getline(file, raw)) {
            std::string t = trim(raw);
            if (t.empty() || t[0] == '#' || t[0] == ';') {
                continue;
            }
            if (t.front() == '[' && t.back() == ']') {
                if (!pending.empty()) {
                    logical_lines.push_back(pending);
                    pending.clear();
                }
                logical_lines.push_back(t);
                current_section = toLower(trim(t.substr(1, t.size() - 2)));
                continue;
            }
            bool allow_continuation = (current_section == "status.bits" || current_section == "rect.static");
            size_t eq_pos = t.find('=');
            if (eq_pos == std::string::npos) {
                if (allow_continuation && !pending.empty()) {
                    pending += "; ";
                    pending += t;
                }
                continue;
            }
            if (!pending.empty()) {
                logical_lines.push_back(pending);
                pending.clear();
            }
            std::string key = toLower(trim(t.substr(0, eq_pos)));
            bool is_compact = allow_continuation &&
                              t.find(':', eq_pos + 1) != std::string::npos &&
                              key.find('.') == std::string::npos;
            if (is_compact) {
                pending = t;
            } else {
                logical_lines.push_back(t);
            }
        }
        if (!pending.empty()) {
            logical_lines.push_back(pending);
        }
    }

    std::string section;
    std::string line;
    for (const auto& raw_line : logical_lines) {
        line = raw_line;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = toLower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t hash_pos = line.find('#');
        size_t semi_pos = (section == "status.bits" || section == "rect.static")
                              ? std::string::npos
                              : line.find(';');
        size_t cut_pos = std::string::npos;
        if (hash_pos != std::string::npos) {
            cut_pos = hash_pos;
        }
        if (semi_pos != std::string::npos) {
            cut_pos = (cut_pos == std::string::npos) ? semi_pos : std::min(cut_pos, semi_pos);
        }
        if (cut_pos != std::string::npos) {
            line = trim(line.substr(0, cut_pos));
            if (line.empty()) {
                continue;
            }
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
            } else if (key == "flip_horizontal") {
                parseBool(value, config.video.flip_horizontal);
            } else if (key == "flip_vertical") {
                parseBool(value, config.video.flip_vertical);
            } else if (key == "rotate") {
                int tmp = 0;
                if (parseInt(value, tmp)) {
                    if (tmp == 0 || tmp == 90 || tmp == 180 || tmp == 270) {
                        config.video.rotate = tmp;
                    }
                }
            }
        } else if (section == "camera") {
            if (key == "ae_enable") {
                parseBool(value, config.camera.ae_enable);
            } else if (key == "awb_enable") {
                parseBool(value, config.camera.awb_enable);
            } else if (key == "controls_dump") {
                parseBool(value, config.camera.controls_dump);
            } else if (key == "exposure_time_us") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.camera.exposure_time_us = static_cast<int>(tmp);
                }
            } else if (key == "analogue_gain") {
                parseFloat(value, config.camera.analogue_gain);
            } else if (key == "brightness") {
                parseFloat(value, config.camera.brightness);
            } else if (key == "contrast") {
                parseFloat(value, config.camera.contrast);
            } else if (key == "saturation") {
                parseFloat(value, config.camera.saturation);
            } else if (key == "sharpness") {
                parseFloat(value, config.camera.sharpness);
            } else if (key == "exposure_compensation") {
                parseFloat(value, config.camera.exposure_compensation);
            } else if (key == "frame_duration_us") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.camera.frame_duration_us = static_cast<int>(tmp);
                }
            } else if (key == "ae_metering") {
                parseInt(value, config.camera.ae_metering);
            } else if (key == "ae_constraint") {
                parseInt(value, config.camera.ae_constraint);
            } else if (key == "ae_exposure_mode") {
                parseInt(value, config.camera.ae_exposure_mode);
            } else if (key == "awb_mode") {
                parseInt(value, config.camera.awb_mode);
            } else if (key == "ae_flicker_mode") {
                parseInt(value, config.camera.ae_flicker_mode);
            } else if (key == "exposure_time_mode") {
                parseInt(value, config.camera.exposure_time_mode);
            } else if (key == "analogue_gain_mode") {
                parseInt(value, config.camera.analogue_gain_mode);
            } else if (key == "noise_reduction_mode") {
                parseInt(value, config.camera.noise_reduction_mode);
            } else if (key == "hdr_mode") {
                parseInt(value, config.camera.hdr_mode);
            } else if (key == "sync_mode") {
                parseInt(value, config.camera.sync_mode);
            } else if (key == "sync_frames") {
                parseInt(value, config.camera.sync_frames);
            } else if (key == "colour_temperature") {
                parseInt(value, config.camera.colour_temperature);
            } else if (key == "ae_flicker_period") {
                parseInt(value, config.camera.ae_flicker_period);
            } else if (key == "stats_output_enable") {
                bool tmp = false;
                if (parseBool(value, tmp)) {
                    config.camera.stats_output_enable = tmp ? 1 : 0;
                }
            } else if (key == "cnn_enable_input_tensor") {
                bool tmp = false;
                if (parseBool(value, tmp)) {
                    config.camera.cnn_enable_input_tensor = tmp ? 1 : 0;
                }
            } else if (key == "colour_gains") {
                auto parts = splitComma(value);
                if (parts.size() >= 2) {
                    float r = 0.0f;
                    float b = 0.0f;
                    if (parseFloat(parts[0], r) && parseFloat(parts[1], b)) {
                        config.camera.colour_gains = {r, b};
                        config.camera.colour_gains_set = true;
                    }
                }
            } else if (key == "colour_correction_matrix") {
                auto parts = splitComma(value);
                if (parts.size() >= 9) {
                    std::array<float, 9> m{};
                    bool ok = true;
                    for (size_t i = 0; i < 9; ++i) {
                        if (!parseFloat(parts[i], m[i])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        config.camera.colour_correction_matrix = m;
                        config.camera.colour_correction_matrix_set = true;
                    }
                }
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
            } else if (key == "line_style") {
                parseLineStyle(value, config.crosshair.line_style);
            } else if (key == "dash_length") {
                parseFloat(value, config.crosshair.dash_length);
            } else if (key == "dash_gap") {
                parseFloat(value, config.crosshair.dash_gap);
            } else if (key == "modbus_override") {
                parseBool(value, config.crosshair.modbus_override);
            }
        } else if (section == "panel") {
            if (key == "enabled") {
                parseBool(value, config.panel_left.enabled);
            } else if (key == "x") {
                parseFloat(value, config.panel_left.x);
            } else if (key == "y") {
                parseFloat(value, config.panel_left.y);
            } else if (key == "width") {
                parseFloat(value, config.panel_left.width);
            } else if (key == "height") {
                parseFloat(value, config.panel_left.height);
            } else if (key == "color") {
                parseColor(value, config.panel_left.color);
            }
        } else if (section == "panel.right") {
            if (key == "enabled") {
                parseBool(value, config.panel_right.enabled);
            } else if (key == "x") {
                parseFloat(value, config.panel_right.x);
            } else if (key == "y") {
                parseFloat(value, config.panel_right.y);
            } else if (key == "width") {
                parseFloat(value, config.panel_right.width);
            } else if (key == "height") {
                parseFloat(value, config.panel_right.height);
            } else if (key == "color") {
                parseColor(value, config.panel_right.color);
            }
        } else if (section == "roi") {
            if (key == "enabled") {
                parseBool(value, config.roi.enabled);
            } else if (key == "auto_fit") {
                parseBool(value, config.roi.auto_fit);
            } else if (key == "x") {
                parseFloat(value, config.roi.x);
            } else if (key == "y") {
                parseFloat(value, config.roi.y);
            } else if (key == "width") {
                parseFloat(value, config.roi.width);
            } else if (key == "height") {
                parseFloat(value, config.roi.height);
            }
        } else if (section == "hud") {
            if (key == "update_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.hud_update_ms = static_cast<int>(tmp);
                }
            } else if (key == "font_path") {
                config.hud_font_path = value;
            } else if (key == "text_vshift") {
                parseFloat(value, config.hud_text_vshift);
            }
        } else if (section == "modbus") {
            if (key == "enabled") {
                parseBool(value, config.modbus.enabled);
            } else if (key == "ip") {
                config.modbus.ip = value;
            } else if (key == "port") {
                parseU16(value, config.modbus.port);
            } else if (key == "unit_id") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp) && tmp <= 255) {
                    config.modbus.unit_id = static_cast<uint8_t>(tmp);
                }
            } else if (key == "update_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.update_ms = static_cast<int>(tmp);
                }
            } else if (key == "reconnect_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.reconnect_ms = static_cast<int>(tmp);
                }
            } else if (key == "address_offset") {
                int tmp = 0;
                if (parseInt(value, tmp)) {
                    config.modbus.address_offset = tmp;
                }
            } else if (key == "block_read") {
                parseBool(value, config.modbus.block_read);
            } else if (key == "error_threshold") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.error_threshold = static_cast<int>(tmp);
                }
            } else if (key == "error_backoff_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.error_backoff_ms = static_cast<int>(tmp);
                }
            } else if (key == "response_timeout_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.response_timeout_ms = static_cast<int>(tmp);
                }
            } else if (key == "byte_timeout_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.byte_timeout_ms = static_cast<int>(tmp);
                }
            } else if (key == "inter_request_delay_ms") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.inter_request_delay_ms = static_cast<int>(tmp);
                }
            } else if (key == "log_timestamps") {
                parseBool(value, config.modbus.log_timestamps);
            } else if (key == "keep_last_on_error") {
                parseBool(value, config.modbus.keep_last_on_error);
            } else if (key == "reset_after_errors") {
                uint32_t tmp = 0;
                if (parseUInt(value, tmp)) {
                    config.modbus.reset_after_errors = static_cast<int>(tmp);
                }
            } else if (key == "register_type") {
                std::string v = toLower(trim(value));
                if (v == "holding" || v == "input") {
                    config.modbus.register_type = v;
                }
            } else if (key == "debug") {
                parseBool(value, config.modbus.debug);
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
        } else if (section == "modbus.decimals") {
            int decimals = 0;
            if (parseInt(value, decimals)) {
                config.modbus.decimals[key] = decimals;
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
        } else if (section == "rect.static") {
            size_t dot = key.find('.');
            if (dot != std::string::npos) {
                std::string id = key.substr(0, dot);
                std::string field = key.substr(dot + 1);
                if (!cleared_static_rects) {
                    config.static_rects.clear();
                    cleared_static_rects = true;
                    rect_index.clear();
                }
                size_t idx = 0;
                auto it = rect_index.find(id);
                if (it == rect_index.end()) {
                    config.static_rects.push_back(StaticRectConfig());
                    idx = config.static_rects.size() - 1;
                    rect_index[id] = idx;
                } else {
                    idx = it->second;
                }
                StaticRectConfig& item = config.static_rects[idx];
                if (field == "x") {
                    parseFloat(value, item.x);
                } else if (field == "y") {
                    parseFloat(value, item.y);
                } else if (field == "width" || field == "w") {
                    parseFloat(value, item.width);
                } else if (field == "height" || field == "h") {
                    parseFloat(value, item.height);
                } else if (field == "color") {
                    parseColor(value, item.color);
                } else if (field == "text") {
                    item.text = value;
                } else if (field == "text_align" || field == "align") {
                    parseAlign(value, item.text_align);
                } else if (field == "text_padding" || field == "text_pad" || field == "pad") {
                    parseFloat(value, item.text_padding);
                } else if (field == "text_scale") {
                    parseFloat(value, item.text_scale);
                } else if (field == "text_color" || field == "text.color") {
                    parseColor(value, item.text_color);
                }
            } else if (value.find(':') != std::string::npos) {
                std::string id = key;
                if (!cleared_static_rects) {
                    config.static_rects.clear();
                    cleared_static_rects = true;
                    rect_index.clear();
                }
                size_t idx = 0;
                auto it = rect_index.find(id);
                if (it == rect_index.end()) {
                    config.static_rects.push_back(StaticRectConfig());
                    idx = config.static_rects.size() - 1;
                    rect_index[id] = idx;
                } else {
                    idx = it->second;
                }
                StaticRectConfig& item = config.static_rects[idx];
                auto fields = splitByAny(value, ";|");
                for (const auto& entry : fields) {
                    if (entry.empty()) {
                        continue;
                    }
                    size_t colon = entry.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    std::string field = toLower(trim(entry.substr(0, colon)));
                    std::string field_value = trim(entry.substr(colon + 1));
                    if (field == "x") {
                        parseFloat(field_value, item.x);
                    } else if (field == "y") {
                        parseFloat(field_value, item.y);
                    } else if (field == "width" || field == "w") {
                        parseFloat(field_value, item.width);
                    } else if (field == "height" || field == "h") {
                        parseFloat(field_value, item.height);
                    } else if (field == "color") {
                        parseColor(field_value, item.color);
                    } else if (field == "text") {
                        item.text = field_value;
                    } else if (field == "text_align" || field == "align") {
                        parseAlign(field_value, item.text_align);
                    } else if (field == "text_padding" || field == "text_pad" || field == "pad") {
                        parseFloat(field_value, item.text_padding);
                    } else if (field == "text_scale") {
                        parseFloat(field_value, item.text_scale);
                    } else if (field == "text_color" || field == "text.color") {
                        parseColor(field_value, item.text_color);
                    }
                }
            } else {
                auto parts = splitComma(value);
                if (parts.size() >= 8) {
                    if (!cleared_static_rects) {
                        config.static_rects.clear();
                        cleared_static_rects = true;
                        rect_index.clear();
                    }
                    StaticRectConfig item;
                    parseFloat(parts[0], item.x);
                    parseFloat(parts[1], item.y);
                    parseFloat(parts[2], item.width);
                    parseFloat(parts[3], item.height);
                    float r = 1, g = 1, b = 1, a = 1;
                    parseFloat(parts[4], r);
                    parseFloat(parts[5], g);
                    parseFloat(parts[6], b);
                    parseFloat(parts[7], a);
                    item.color = Color(r, g, b, a);
                    config.static_rects.push_back(item);
                }
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
        } else if (section == "status.bits") {
            size_t dot = key.find('.');
            if (dot != std::string::npos) {
                std::string id = key.substr(0, dot);
                std::string field = key.substr(dot + 1);
                if (!cleared_status) {
                    config.status_bits.clear();
                    cleared_status = true;
                    status_index.clear();
                    status_ids.clear();
                }
                size_t idx = 0;
                auto it = status_index.find(id);
                if (it == status_index.end()) {
                    config.status_bits.push_back(StatusBitConfig());
                    status_ids.push_back(id);
                    idx = config.status_bits.size() - 1;
                    status_index[id] = idx;
                } else {
                    idx = it->second;
                }
                StatusBitConfig& item = config.status_bits[idx];
                if (field == "name" || field == "source" || field == "register") {
                    item.name = value;
                } else if (field == "bit") {
                    uint32_t bit_tmp = 0;
                    if (parseUInt(value, bit_tmp) && bit_tmp <= 31) {
                        item.bit = static_cast<uint8_t>(bit_tmp);
                    }
                } else if (field == "x") {
                    parseFloat(value, item.x);
                } else if (field == "y") {
                    parseFloat(value, item.y);
                } else if (field == "width") {
                    parseFloat(value, item.width);
                } else if (field == "height") {
                    parseFloat(value, item.height);
                } else if (field == "color_on" || field == "on_color" || field == "color.on") {
                    parseColor(value, item.color_on);
                } else if (field == "color_off" || field == "off_color" || field == "color.off") {
                    parseColor(value, item.color_off);
                } else if (field == "color_unknown" || field == "color_lost" || field == "color_offline" ||
                           field == "color.unknown" || field == "color.no_link" || field == "unknown_color") {
                    parseColor(value, item.color_unknown);
                } else if (field == "text") {
                    item.text = value;
                } else if (field == "text_align" || field == "align") {
                    parseAlign(value, item.text_align);
                } else if (field == "text_padding" || field == "text_pad" || field == "pad") {
                    parseFloat(value, item.text_padding);
                } else if (field == "text_scale") {
                    parseFloat(value, item.text_scale);
                } else if (field == "text_color" || field == "text.color") {
                    parseColor(value, item.text_color);
                }
            } else if (value.find(':') != std::string::npos) {
                std::string id = key;
                if (!cleared_status) {
                    config.status_bits.clear();
                    cleared_status = true;
                    status_index.clear();
                    status_ids.clear();
                }
                size_t idx = 0;
                auto it = status_index.find(id);
                if (it == status_index.end()) {
                    config.status_bits.push_back(StatusBitConfig());
                    status_ids.push_back(id);
                    idx = config.status_bits.size() - 1;
                    status_index[id] = idx;
                } else {
                    idx = it->second;
                }
                StatusBitConfig& item = config.status_bits[idx];
                auto fields = splitByAny(value, ";|");
                for (const auto& entry : fields) {
                    if (entry.empty()) {
                        continue;
                    }
                    size_t colon = entry.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    std::string field = toLower(trim(entry.substr(0, colon)));
                    std::string field_value = trim(entry.substr(colon + 1));
                    if (field == "name" || field == "source" || field == "register") {
                        item.name = field_value;
                    } else if (field == "bit") {
                        uint32_t bit_tmp = 0;
                        if (parseUInt(field_value, bit_tmp) && bit_tmp <= 31) {
                            item.bit = static_cast<uint8_t>(bit_tmp);
                        }
                    } else if (field == "x") {
                        parseFloat(field_value, item.x);
                    } else if (field == "y") {
                        parseFloat(field_value, item.y);
                    } else if (field == "width" || field == "w") {
                        parseFloat(field_value, item.width);
                    } else if (field == "height" || field == "h") {
                        parseFloat(field_value, item.height);
                    } else if (field == "color_on" || field == "on_color" || field == "color.on" || field == "on") {
                        parseColor(field_value, item.color_on);
                    } else if (field == "color_off" || field == "off_color" || field == "color.off" || field == "off") {
                        parseColor(field_value, item.color_off);
                    } else if (field == "color_unknown" || field == "color_lost" || field == "color_offline" ||
                               field == "color.unknown" || field == "color.no_link" || field == "unknown" || field == "na") {
                        parseColor(field_value, item.color_unknown);
                    } else if (field == "text") {
                        item.text = field_value;
                    } else if (field == "text_align" || field == "align") {
                        parseAlign(field_value, item.text_align);
                    } else if (field == "text_padding" || field == "text_pad" || field == "pad") {
                        parseFloat(field_value, item.text_padding);
                    } else if (field == "text_scale") {
                        parseFloat(field_value, item.text_scale);
                    } else if (field == "text_color" || field == "text.color") {
                        parseColor(field_value, item.text_color);
                    }
                }
            } else {
            auto parts = splitComma(value);
            if (parts.size() >= 14) {
                if (!cleared_status) {
                    config.status_bits.clear();
                    cleared_status = true;
                    status_index.clear();
                    status_ids.clear();
                }
                StatusBitConfig item;
                item.name = parts[0];
                uint32_t bit_tmp = 0;
                if (parseUInt(parts[1], bit_tmp) && bit_tmp <= 31) {
                    item.bit = static_cast<uint8_t>(bit_tmp);
                }
                parseFloat(parts[2], item.x);
                parseFloat(parts[3], item.y);
                parseFloat(parts[4], item.width);
                parseFloat(parts[5], item.height);
                float r_on = 0, g_on = 1, b_on = 0, a_on = 1;
                float r_off = 1, g_off = 0, b_off = 0, a_off = 0.5f;
                parseFloat(parts[6], r_on);
                parseFloat(parts[7], g_on);
                parseFloat(parts[8], b_on);
                parseFloat(parts[9], a_on);
                parseFloat(parts[10], r_off);
                parseFloat(parts[11], g_off);
                parseFloat(parts[12], b_off);
                parseFloat(parts[13], a_off);
                item.color_on = Color(r_on, g_on, b_on, a_on);
                item.color_off = Color(r_off, g_off, b_off, a_off);
                if (parts.size() >= 18) {
                    float r_u = 0.5f, g_u = 0.5f, b_u = 0.5f, a_u = 0.5f;
                    parseFloat(parts[14], r_u);
                    parseFloat(parts[15], g_u);
                    parseFloat(parts[16], b_u);
                    parseFloat(parts[17], a_u);
                    item.color_unknown = Color(r_u, g_u, b_u, a_u);
                }
                config.status_bits.push_back(item);
                status_index[key] = config.status_bits.size() - 1;
                status_ids.push_back(std::string());
            }
            }
        }
    }

    for (size_t i = 0; i < config.status_bits.size() && i < status_ids.size(); ++i) {
        if (config.status_bits[i].name.empty() && !status_ids[i].empty()) {
            config.status_bits[i].name = status_ids[i];
        }
    }

    return true;
}
