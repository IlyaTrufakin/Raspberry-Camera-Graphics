#pragma once

#include <GLES2/gl2.h>
#include <string>
#include <vector>
#include <cstdint>
#include <map>

#include <ft2build.h>
#include FT_FREETYPE_H

// Структура для цвета RGBA
struct Color {
    float r, g, b, a;
    Color(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

// Структура для позиции текста
struct TextPosition {
    float x, y;  // Нормализованные координаты (0.0 - 1.0)
    std::string text;  // Текст для отображения
    float scale;       // Масштаб шрифта
    Color color;

    TextPosition(float x, float y, const std::string& text, float scale = 1.0f,
                 const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f))
        : x(x), y(y), text(text), scale(scale), color(color) {}
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

// Структура для хранения информации о символе
struct Character {
    GLuint texture_id;  // ID текстуры глифа
    int width;          // Ширина глифа
    int height;         // Высота глифа
    int bearing_x;      // Смещение от baseline по X
    int bearing_y;      // Смещение от baseline по Y
    int advance;        // Сдвиг для следующего символа
};

// Класс для отрисовки HUD
class HUDOverlay {
public:
    HUDOverlay();
    ~HUDOverlay();

    // Инициализация OpenGL ресурсов и шрифта
    bool initialize(uint32_t display_width, uint32_t display_height,
                   const std::string& font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");

    // Настройка прицела
    void setCrosshairConfig(const CrosshairConfig& config);

    // Добавление текстовой позиции для отображения
    void addTextPosition(const TextPosition& text_pos);

    // Очистка всех текстовых позиций
    void clearTextPositions();

    // Отрисовка HUD
    void render();

    // Отрисовка текста напрямую
    void renderTextDirect(const std::string& text, float x, float y, float scale, const Color& color);

private:
    void renderCrosshair();
    void renderText();
    void compileShader(const char* source, GLenum type, GLuint& shader);
    void linkProgram(GLuint vertex, GLuint fragment, GLuint& program);
    bool loadFont(const std::string& font_path);

    uint32_t display_width_;
    uint32_t display_height_;

    CrosshairConfig crosshair_config_;
    std::vector<TextPosition> text_positions_;

    GLuint hud_program_;
    GLuint text_program_;
    GLuint hud_vbo_;
    GLuint text_vbo_;

    // FreeType
    FT_Library ft_library_;
    FT_Face ft_face_;
    std::map<uint32_t, Character> characters_;  // Кэш символов (поддерживает Unicode)
};
