#include "hud_overlay.h"
#include <iostream>
#include <cmath>

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

HUDOverlay::HUDOverlay()
    : display_width_(0), display_height_(0),
      hud_program_(0), hud_vao_(0), hud_vbo_(0) {
}

HUDOverlay::~HUDOverlay() {
    if (hud_program_) {
        glDeleteProgram(hud_program_);
    }
    if (hud_vbo_) {
        glDeleteBuffers(1, &hud_vbo_);
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

bool HUDOverlay::initialize(uint32_t display_width, uint32_t display_height) {
    display_width_ = display_width;
    display_height_ = display_height;

    // Компиляция шейдеров для HUD
    GLuint hud_vs, hud_fs;
    compileShader(hud_vs_src, GL_VERTEX_SHADER, hud_vs);
    compileShader(hud_fs_src, GL_FRAGMENT_SHADER, hud_fs);
    linkProgram(hud_vs, hud_fs, hud_program_);
    glDeleteShader(hud_vs);
    glDeleteShader(hud_fs);

    // Создание VBO для HUD
    glGenBuffers(1, &hud_vbo_);

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
    // Примечание: Для полноценной поддержки текста (особенно кириллицы)
    // потребуется интеграция библиотеки FreeType и создание текстурного атласа.
    // Это базовая заглушка для будущей реализации.

    // TODO: Реализовать рендеринг текста с поддержкой кириллицы
    // Требуется:
    // 1. Загрузка шрифта через FreeType
    // 2. Генерация текстурного атласа для символов
    // 3. Рендеринг каждого символа как текстурированного квада

    // Пока выводим предупреждение только один раз
    static bool warning_shown = false;
    if (!text_positions_.empty() && !warning_shown) {
        std::cout << "Text rendering not yet implemented (requires FreeType)" << std::endl;
        warning_shown = true;
    }
}
