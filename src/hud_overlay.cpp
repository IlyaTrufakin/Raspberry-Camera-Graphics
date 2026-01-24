#include "hud_overlay.h"

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <iostream>
#include <locale>

// Vertex shader для HUD
const char* hud_vs_src = R"(
attribute vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Fragment shader для HUD
const char* hud_fs_src = R"(
precision mediump float;
uniform vec4 uColor;
void main() {
    gl_FragColor = uColor;
}
)";

// Vertex shader для текста
const char* text_vs_src = R"(
attribute vec4 aVertex;  // <vec2 pos, vec2 tex>
varying vec2 vTexCoord;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * vec4(aVertex.xy, 0.0, 1.0);
    vTexCoord = aVertex.zw;
}
)";

// Fragment shader для текста
const char* text_fs_src = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uText;
uniform vec4 uTextColor;
void main() {
    float alpha = texture2D(uText, vTexCoord).r;
    gl_FragColor = vec4(uTextColor.rgb, uTextColor.a * alpha);
}
)";

HUDOverlay::HUDOverlay()
    : display_width_(0), display_height_(0),
      hud_program_(0), text_program_(0), hud_vbo_(0), text_vbo_(0),
      ft_library_(nullptr), ft_face_(nullptr) {
}

HUDOverlay::~HUDOverlay() {
    // Освободить текстуры глифов
    for (auto& pair : characters_) {
        glDeleteTextures(1, &pair.second.texture_id);
    }

    if (ft_face_) {
        FT_Done_Face(ft_face_);
    }
    if (ft_library_) {
        FT_Done_FreeType(ft_library_);
    }

    if (hud_program_) {
        glDeleteProgram(hud_program_);
    }
    if (text_program_) {
        glDeleteProgram(text_program_);
    }
    if (hud_vbo_) {
        glDeleteBuffers(1, &hud_vbo_);
    }
    if (text_vbo_) {
        glDeleteBuffers(1, &text_vbo_);
    }
}

void HUDOverlay::compileShader(const char* source, GLenum type, GLuint& shader) {
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compilation error: " << log << std::endl;
    }
}

void HUDOverlay::linkProgram(GLuint vertex, GLuint fragment, GLuint& program) {
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program linking error: " << log << std::endl;
    }
}

bool HUDOverlay::loadFont(const std::string& font_path) {
    // Инициализация FreeType
    if (FT_Init_FreeType(&ft_library_)) {
        std::cerr << "Failed to init FreeType" << std::endl;
        return false;
    }

    // Загрузка шрифта
    if (FT_New_Face(ft_library_, font_path.c_str(), 0, &ft_face_)) {
        std::cerr << "Failed to load font: " << font_path << std::endl;
        return false;
    }

    // Размер шрифта в пикселях
    FT_Set_Pixel_Sizes(ft_face_, 0, 48);

    // Выравнивание для текстур глифов
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // ASCII глифы
    for (uint32_t c = 32; c < 128; c++) {
        if (FT_Load_Char(ft_face_, c, FT_LOAD_RENDER)) {
            std::cerr << "Failed to load glyph for char: " << c << std::endl;
            continue;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     ft_face_->glyph->bitmap.width,
                     ft_face_->glyph->bitmap.rows,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     ft_face_->glyph->bitmap.buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            (int)ft_face_->glyph->bitmap.width,
            (int)ft_face_->glyph->bitmap.rows,
            ft_face_->glyph->bitmap_left,
            ft_face_->glyph->bitmap_top,
            (int)ft_face_->glyph->advance.x
        };

        characters_[c] = character;
    }

    // Кириллица
    for (uint32_t c = 0x0410; c <= 0x044F; c++) {
        if (FT_Load_Char(ft_face_, c, FT_LOAD_RENDER)) {
            continue;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     ft_face_->glyph->bitmap.width,
                     ft_face_->glyph->bitmap.rows,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     ft_face_->glyph->bitmap.buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            (int)ft_face_->glyph->bitmap.width,
            (int)ft_face_->glyph->bitmap.rows,
            ft_face_->glyph->bitmap_left,
            ft_face_->glyph->bitmap_top,
            (int)ft_face_->glyph->advance.x
        };

        characters_[c] = character;
    }

    // Специальные символы кириллицы
    uint32_t cyrillic_special[] = {0x0401, 0x0451};
    for (uint32_t c : cyrillic_special) {
        if (FT_Load_Char(ft_face_, c, FT_LOAD_RENDER)) {
            continue;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     ft_face_->glyph->bitmap.width,
                     ft_face_->glyph->bitmap.rows,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                     ft_face_->glyph->bitmap.buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            (int)ft_face_->glyph->bitmap.width,
            (int)ft_face_->glyph->bitmap.rows,
            ft_face_->glyph->bitmap_left,
            ft_face_->glyph->bitmap_top,
            (int)ft_face_->glyph->advance.x
        };

        characters_[c] = character;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    std::cout << "Font loaded: " << font_path << std::endl;
    std::cout << "Total glyphs cached: " << characters_.size() << std::endl;

    return true;
}

bool HUDOverlay::loadGlyph(char32_t c) {
    if (characters_.find(c) != characters_.end()) {
        return true;
    }
    if (!ft_face_) {
        return false;
    }
    if (FT_Load_Char(ft_face_, c, FT_LOAD_RENDER)) {
        return false;
    }

    static int load_count = 0;
    if (load_count <= 10) {
        std::cout << "  Glyph U+" << std::hex << c << std::dec
                  << " bitmap: w=" << ft_face_->glyph->bitmap.width
                  << " h=" << ft_face_->glyph->bitmap.rows
                  << " pitch=" << ft_face_->glyph->bitmap.pitch
                  << " buffer=" << (void*)ft_face_->glyph->bitmap.buffer << std::endl;
        ++load_count;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                 ft_face_->glyph->bitmap.width,
                 ft_face_->glyph->bitmap.rows,
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                 ft_face_->glyph->bitmap.buffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Character character = {
        texture,
        (int)ft_face_->glyph->bitmap.width,
        (int)ft_face_->glyph->bitmap.rows,
        ft_face_->glyph->bitmap_left,
        ft_face_->glyph->bitmap_top,
        (int)ft_face_->glyph->advance.x
    };

    characters_[c] = character;
    return true;
}

bool HUDOverlay::initialize(uint32_t display_width, uint32_t display_height,
                            const std::string& font_path) {
    display_width_ = display_width;
    display_height_ = display_height;

    // Шейдер для HUD
    GLuint hud_vs, hud_fs;
    compileShader(hud_vs_src, GL_VERTEX_SHADER, hud_vs);
    compileShader(hud_fs_src, GL_FRAGMENT_SHADER, hud_fs);
    linkProgram(hud_vs, hud_fs, hud_program_);
    glDeleteShader(hud_vs);
    glDeleteShader(hud_fs);

    // Шейдер для текста
    GLuint text_vs, text_fs;
    compileShader(text_vs_src, GL_VERTEX_SHADER, text_vs);
    compileShader(text_fs_src, GL_FRAGMENT_SHADER, text_fs);
    linkProgram(text_vs, text_fs, text_program_);
    glDeleteShader(text_vs);
    glDeleteShader(text_fs);

    // VBO
    glGenBuffers(1, &hud_vbo_);

    glGenBuffers(1, &text_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Загрузка шрифта
    if (!loadFont(font_path)) {
        std::cerr << "Failed to load font, text rendering disabled" << std::endl;
    }

    std::cout << "HUD overlay initialized: " << display_width_ << "x" << display_height_ << std::endl;
    return true;
}

void HUDOverlay::setCrosshairConfig(const CrosshairConfig& config) {
    crosshair_config_ = config;
}

void HUDOverlay::setPanelConfigs(const PanelConfig& left, const PanelConfig& right) {
    panel_left_ = left;
    panel_right_ = right;
}


void HUDOverlay::addTextPosition(const TextPosition& text_pos) {
    text_positions_.push_back(text_pos);
}

void HUDOverlay::addRectPosition(const RectPosition& rect_pos) {
    rect_positions_.push_back(rect_pos);
}

void HUDOverlay::clearTextPositions() {
    text_positions_.clear();
}

void HUDOverlay::clearRectPositions() {
    rect_positions_.clear();
}

void HUDOverlay::render() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (panel_left_.enabled) {
        renderPanel(panel_left_);
    }
    if (panel_right_.enabled) {
        renderPanel(panel_right_);
    }

    if (!rect_positions_.empty()) {
        renderRectangles();
    }

    if (crosshair_config_.enabled) {
        renderCrosshair();
    }

    renderText();

    glDisable(GL_BLEND);
}

void HUDOverlay::renderPanel(const PanelConfig& config) {
    glUseProgram(hud_program_);

    GLint colorLoc = glGetUniformLocation(hud_program_, "uColor");
    glUniform4f(colorLoc,
                config.color.r,
                config.color.g,
                config.color.b,
                config.color.a);

    float x0 = config.x * 2.0f - 1.0f;
    float x1 = (config.x + config.width) * 2.0f - 1.0f;
    float y0 = config.y * 2.0f - 1.0f;
    float y1 = (config.y + config.height) * 2.0f - 1.0f;

    float panel_vertices[] = {
        x0, y0,
        x1, y0,
        x0, y1,
        x1, y1
    };

    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(panel_vertices), panel_vertices, GL_DYNAMIC_DRAW);

    GLint posLoc = glGetAttribLocation(hud_program_, "aPos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HUDOverlay::renderRectangles() {
    glUseProgram(hud_program_);

    GLint colorLoc = glGetUniformLocation(hud_program_, "uColor");
    GLint posLoc = glGetAttribLocation(hud_program_, "aPos");
    glEnableVertexAttribArray(posLoc);

    for (const auto& rect : rect_positions_) {
        glUniform4f(colorLoc,
                    rect.color.r,
                    rect.color.g,
                    rect.color.b,
                    rect.color.a);

        float x0 = rect.x * 2.0f - 1.0f;
        float x1 = (rect.x + rect.width) * 2.0f - 1.0f;
        float y0 = rect.y * 2.0f - 1.0f;
        float y1 = (rect.y + rect.height) * 2.0f - 1.0f;

        float vertices[] = {
            x0, y0,
            x1, y0,
            x0, y1,
            x1, y1
        };

        glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HUDOverlay::renderCrosshair() {
    glUseProgram(hud_program_);

    GLint colorLoc = glGetUniformLocation(hud_program_, "uColor");
    glUniform4f(colorLoc,
                crosshair_config_.color.r,
                crosshair_config_.color.g,
                crosshair_config_.color.b,
                crosshair_config_.color.a);

    float cx = crosshair_config_.center_x * 2.0f - 1.0f;
    float cy = -(crosshair_config_.center_y * 2.0f - 1.0f);

    float gap = crosshair_config_.gap;
    float left = crosshair_config_.h_limit_left * 2.0f - 1.0f;
    float right = crosshair_config_.h_limit_right * 2.0f - 1.0f;
    if (left > right) {
        std::swap(left, right);
    }

    glLineWidth(crosshair_config_.line_width);

    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    GLint posLoc = glGetAttribLocation(hud_program_, "aPos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    auto appendLine = [](std::vector<float>& verts, float x0, float y0, float x1, float y1) {
        verts.push_back(x0);
        verts.push_back(y0);
        verts.push_back(x1);
        verts.push_back(y1);
    };

    auto addDashedLine = [&](std::vector<float>& verts, float x0, float y0, float x1, float y1,
                             float dash_len, float dash_gap) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) {
            return;
        }
        float ux = dx / length;
        float uy = dy / length;
        float t = 0.0f;
        while (t < length) {
            float seg = std::min(dash_len, length - t);
            float sx = x0 + ux * t;
            float sy = y0 + uy * t;
            float ex = x0 + ux * (t + seg);
            float ey = y0 + uy * (t + seg);
            appendLine(verts, sx, sy, ex, ey);
            t += dash_len + dash_gap;
        }
    };

    std::vector<float> verts;
    if (crosshair_config_.line_style == 0) {
        float left_end = cx - gap;
        float right_start = cx + gap;

        float h_line[] = {
            left, cy,
            left_end, cy,
            right_start, cy,
            right, cy
        };

        float top = 1.0f;
        float bottom = -1.0f;
        float v_line[] = {
            cx, top,
            cx, cy + gap,
            cx, cy - gap,
            cx, bottom
        };

        if (left_end > left) {
            glBufferData(GL_ARRAY_BUFFER, sizeof(h_line), h_line, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINES, 0, 2);
        }
        if (right_start < right) {
            glBufferData(GL_ARRAY_BUFFER, sizeof(h_line), h_line, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINES, 2, 2);
        }

        glBufferData(GL_ARRAY_BUFFER, sizeof(v_line), v_line, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, 2);
        glDrawArrays(GL_LINES, 2, 2);
    } else {
        float dash_len = std::max(0.001f, crosshair_config_.dash_length);
        float dash_gap = std::max(0.0f, crosshair_config_.dash_gap);
        if (cx - gap > left) {
            addDashedLine(verts, left, cy, cx - gap, cy, dash_len, dash_gap);
        }
        if (cx + gap < right) {
            addDashedLine(verts, cx + gap, cy, right, cy, dash_len, dash_gap);
        }

        float top = 1.0f;
        float bottom = -1.0f;
        if (cy + gap < top) {
            addDashedLine(verts, cx, top, cx, cy + gap, dash_len, dash_gap);
        }
        if (cy - gap > bottom) {
            addDashedLine(verts, cx, cy - gap, cx, bottom, dash_len, dash_gap);
        }

        if (!verts.empty()) {
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * verts.size(), verts.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINES, 0, static_cast<GLint>(verts.size() / 2));
        }
    }

    glDisableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HUDOverlay::renderText() {
    if (characters_.empty()) {
        return;
    }

    for (const auto& text_pos : text_positions_) {
        float x = text_pos.x * display_width_;
        float y = text_pos.y * display_height_;

        renderTextDirect(text_pos.text, x, y, text_pos.scale, text_pos.color);
    }
}

bool HUDOverlay::measureText(const std::string& text, float scale, float& out_width,
                             float& out_ascent, float& out_descent) {
    out_width = 0.0f;
    out_ascent = 0.0f;
    out_descent = 0.0f;
    if (!ft_face_) {
        return false;
    }

    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    std::u32string unicode_text;
    try {
        unicode_text = converter.from_bytes(text);
    } catch (...) {
        return false;
    }

    for (char32_t c : unicode_text) {
        if (!loadGlyph(c)) {
            continue;
        }
        const Character& ch = characters_[c];
        out_width += (ch.advance >> 6) * scale;
        out_ascent = std::max(out_ascent, ch.bearing_y * scale);
        out_descent = std::max(out_descent, (ch.height - ch.bearing_y) * scale);
    }

    return true;
}

void HUDOverlay::renderTextDirect(const std::string& text, float x, float y,
                                  float scale, const Color& color) {
    if (characters_.empty()) {
        return;
    }

    GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    if (!blend_enabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glUseProgram(text_program_);

    float projection[16] = {
        2.0f / display_width_, 0, 0, 0,
        0, 2.0f / display_height_, 0, 0,
        0, 0, -1, 0,
        -1, -1, 0, 1
    };

    GLint projLoc = glGetUniformLocation(text_program_, "uProjection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, projection);

    GLint colorLoc = glGetUniformLocation(text_program_, "uTextColor");
    glUniform4f(colorLoc, color.r, color.g, color.b, color.a);

    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);

    GLint vertexLoc = glGetAttribLocation(text_program_, "aVertex");
    glEnableVertexAttribArray(vertexLoc);
    glVertexAttribPointer(vertexLoc, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    std::u32string unicode_text;
    try {
        unicode_text = converter.from_bytes(text);
    } catch (...) {
        if (!blend_enabled) {
            glDisable(GL_BLEND);
        }
        return;
    }

    for (char32_t c : unicode_text) {
        if (!loadGlyph(c)) {
            continue;
        }
        const Character& ch = characters_[c];

        float xpos = x + ch.bearing_x * scale;
        float ypos = y - (ch.height - ch.bearing_y) * scale;

        float w = ch.width * scale;
        float h = ch.height * scale;

        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.texture_id);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.advance >> 6) * scale;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(vertexLoc);

    if (!blend_enabled) {
        glDisable(GL_BLEND);
    }
}
