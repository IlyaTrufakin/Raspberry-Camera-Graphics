#include "hud_overlay.h"
#include <iostream>
#include <cmath>
#include <codecvt>
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
    // Очистка текстур символов
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

    // Установка размера шрифта
    FT_Set_Pixel_Sizes(ft_face_, 0, 48);

    // Отключаем выравнивание байтов
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Предзагрузка ASCII символов и основных кириллических
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

    // Предзагрузка кириллицы (А-Я, а-я, Ё, ё)
    for (uint32_t c = 0x0410; c <= 0x044F; c++) {  // А-Я, а-я
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

    // Ё и ё
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

    // Проверим несколько кириллических символов
    std::cout << "Checking Cyrillic glyphs:" << std::endl;
    uint32_t test_chars[] = {0x0421, 0x043A, 0x043E, 0x0440};  // С, к, о, р
    for (uint32_t c : test_chars) {
        auto it = characters_.find(c);
        if (it != characters_.end()) {
            std::cout << "  Char U+" << std::hex << c << std::dec
                      << ": w=" << it->second.width
                      << " h=" << it->second.height << std::endl;
        }
    }

    return true;
}

bool HUDOverlay::initialize(uint32_t display_width, uint32_t display_height,
                            const std::string& font_path) {
    display_width_ = display_width;
    display_height_ = display_height;

    // Компиляция шейдеров для HUD
    GLuint hud_vs, hud_fs;
    compileShader(hud_vs_src, GL_VERTEX_SHADER, hud_vs);
    compileShader(hud_fs_src, GL_FRAGMENT_SHADER, hud_fs);
    linkProgram(hud_vs, hud_fs, hud_program_);
    glDeleteShader(hud_vs);
    glDeleteShader(hud_fs);

    // Компиляция шейдеров для текста
    GLuint text_vs, text_fs;
    compileShader(text_vs_src, GL_VERTEX_SHADER, text_vs);
    compileShader(text_fs_src, GL_FRAGMENT_SHADER, text_fs);
    linkProgram(text_vs, text_fs, text_program_);
    glDeleteShader(text_vs);
    glDeleteShader(text_fs);

    // Создание VBO для HUD
    glGenBuffers(1, &hud_vbo_);

    // Создание VBO для текста
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

void HUDOverlay::addTextPosition(const TextPosition& text_pos) {
    text_positions_.push_back(text_pos);
}

void HUDOverlay::clearTextPositions() {
    text_positions_.clear();
}

void HUDOverlay::render() {
    // Включаем блендинг для прозрачности
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (crosshair_config_.enabled) {
        renderCrosshair();
    }

    renderText();

    glDisable(GL_BLEND);
}

void HUDOverlay::renderCrosshair() {
    glUseProgram(hud_program_);

    // Устанавливаем цвет прицела
    GLint colorLoc = glGetUniformLocation(hud_program_, "uColor");
    glUniform4f(colorLoc,
                crosshair_config_.color.r,
                crosshair_config_.color.g,
                crosshair_config_.color.b,
                crosshair_config_.color.a);

    // Преобразуем нормализованные координаты в координаты OpenGL (-1..1)
    float cx = crosshair_config_.center_x * 2.0f - 1.0f;
    float cy = -(crosshair_config_.center_y * 2.0f - 1.0f);  // Инвертируем Y

    float len = crosshair_config_.line_length;
    float gap = crosshair_config_.gap;

    // Горизонтальная линия (слева и справа от центра)
    float h_line[] = {
        cx - len - gap, cy,
        cx - gap, cy,
        cx + gap, cy,
        cx + len + gap, cy
    };

    // Вертикальная линия (сверху и снизу от центра)
    float v_line[] = {
        cx, cy - len - gap,
        cx, cy - gap,
        cx, cy + gap,
        cx, cy + len + gap
    };

    // Устанавливаем толщину линий
    glLineWidth(crosshair_config_.line_width);

    // Рисуем горизонтальную линию
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(h_line), h_line, GL_DYNAMIC_DRAW);

    GLint posLoc = glGetAttribLocation(hud_program_, "aPos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glDrawArrays(GL_LINES, 0, 2);
    glDrawArrays(GL_LINES, 2, 2);

    // Рисуем вертикальную линию
    glBufferData(GL_ARRAY_BUFFER, sizeof(v_line), v_line, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, 2);
    glDrawArrays(GL_LINES, 2, 2);

    glDisableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HUDOverlay::renderText() {
    if (characters_.empty()) {
        return;  // Шрифт не загружен
    }

    for (const auto& text_pos : text_positions_) {
        // Конвертируем нормализованные координаты в пиксели
        float x = text_pos.x * display_width_;
        float y = text_pos.y * display_height_;

        renderTextDirect(text_pos.text, x, y, text_pos.scale, text_pos.color);
    }
}

void HUDOverlay::renderTextDirect(const std::string& text, float x, float y,
                                  float scale, const Color& color) {
    if (characters_.empty()) {
        std::cerr << "renderTextDirect: No characters loaded!" << std::endl;
        return;
    }

    glUseProgram(text_program_);

    // Ортографическая проекция
    float projection[16] = {
        2.0f / display_width_, 0, 0, 0,
        0, -2.0f / display_height_, 0, 0,
        0, 0, -1, 0,
        -1, 1, 0, 1
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

    // Конвертируем UTF-8 в Unicode code points
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    std::u32string unicode_text;
    try {
        unicode_text = converter.from_bytes(text);
    } catch (...) {
        // Если конвертация не удалась, пропускаем
        std::cerr << "renderTextDirect: UTF-8 conversion failed for text: " << text << std::endl;
        return;
    }

    static bool first_call = true;
    if (first_call) {
        std::cout << "renderTextDirect: text='" << text << "' unicode_len=" << unicode_text.size()
                  << " pos=(" << x << "," << y << ") scale=" << scale << std::endl;
        first_call = false;
    }

    // Рисуем каждый символ
    int char_index = 0;
    for (char32_t c : unicode_text) {
        auto it = characters_.find(c);
        if (it == characters_.end()) {
            // Символ не найден в кэше, пытаемся загрузить
            static int load_count = 0;
            if (load_count < 5) {
                std::cout << "Loading glyph dynamically: U+" << std::hex << c << std::dec << std::endl;
                load_count++;
            }

            if (FT_Load_Char(ft_face_, c, FT_LOAD_RENDER)) {
                std::cerr << "Failed to load glyph U+" << std::hex << c << std::dec << std::endl;
                continue;  // Не удалось загрузить
            }

            if (load_count <= 5) {
                std::cout << "  Glyph bitmap: w=" << ft_face_->glyph->bitmap.width
                          << " h=" << ft_face_->glyph->bitmap.rows
                          << " buffer=" << (void*)ft_face_->glyph->bitmap.buffer << std::endl;
            }

            GLuint texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA,
                        ft_face_->glyph->bitmap.width,
                        ft_face_->glyph->bitmap.rows,
                        0, GL_ALPHA, GL_UNSIGNED_BYTE,
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
            it = characters_.find(c);
        }

        const Character& ch = it->second;

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

        x += (ch.advance >> 6) * scale;  // Advance в 1/64 пикселя
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(vertexLoc);
}
