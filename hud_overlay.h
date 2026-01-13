#pragma once

#include <GLES2/gl2.h>
#include <string>
#include <vector>
#include <cstdint>

// Структура для цвета RGBA
struct Color {
    float r, g, b, a;
    Color(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

// Структура для позиции текста
struct TextPosition {
    float x, y;  // Нормализованные координаты (0.0 - 1.0)
    std::string label;  // Метка (например, "Скорость:")
    std::string* value_ptr;  // Указатель на строковое значение
    Color color;

    TextPosition(float x, float y, const std::string& label, std::string* value_ptr,
                 const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f))
        : x(x), y(y), label(label), value_ptr(value_ptr), color(color) {}
};

// Конфигурация прицела
struct CrosshairConfig {
    bool enabled = true;
    Color color = Color(0.0f, 1.0f, 0.0f, 0.8f);  // Зеленый по умолчанию
    float center_x = 0.5f;  // Центр экрана по X
    float center_y = 0.5f;  // Центр экрана по Y
    float line_length = 0.05f;  // Длина линий
    float line_width = 2.0f;  // Толщина линий в пикселях
    float gap = 0.01f;  // Зазор от центра
};

// Класс для отрисовки HUD
class HUDOverlay {
public:
    HUDOverlay();
    ~HUDOverlay();

    // Инициализация OpenGL ресурсов
    bool initialize(uint32_t display_width, uint32_t display_height);

    // Настройка прицела
    void setCrosshairConfig(const CrosshairConfig& config);

    // Добавление текстовой позиции для отображения
    void addTextPosition(const TextPosition& text_pos);

    // Очистка всех текстовых позиций
    void clearTextPositions();

    // Отрисовка HUD
    void render();

private:
    void renderCrosshair();
    void renderText();
    void compileShader(const char* source, GLenum type, GLuint& shader);
    void linkProgram(GLuint vertex, GLuint fragment, GLuint& program);

    uint32_t display_width_;
    uint32_t display_height_;

    CrosshairConfig crosshair_config_;
    std::vector<TextPosition> text_positions_;

    GLuint hud_program_;
    GLuint hud_vao_;
    GLuint hud_vbo_;
};
