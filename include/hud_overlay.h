#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

// Цвет RGBA
struct Color {
    float r, g, b, a;
    Color(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

// Позиция и параметры текста (нормализованные координаты 0..1)
struct TextPosition {
    float x, y;  // Позиция в долях экрана (0.0 - 1.0)
    std::string text;  // Текстовая строка
    float scale;       // Масштаб шрифта
    Color color;

    TextPosition(float x, float y, const std::string& text, float scale = 1.0f,
                 const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f))
        : x(x), y(y), text(text), scale(scale), color(color) {}
};

// Параметры прицела/центра
struct CrosshairConfig {
    bool enabled = true;
    Color color = Color(0.0f, 1.0f, 0.0f, 0.8f);
    float center_x = 0.5f;
    float center_y = 0.5f;
    float line_length = 0.05f;
    float line_width = 2.0f;
    float gap = 0.01f;
    int line_style = 0;
    float dash_length = 0.05f;
    float dash_gap = 0.03f;
    bool modbus_override = true;
    float h_limit_left = 0.0f;
    float h_limit_right = 1.0f;
};

// Фоновая панель для текста (нормализованные координаты)
struct PanelConfig {
    bool enabled = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.25f;
    float height = 1.0f;
    Color color = Color(0.0f, 0.0f, 0.0f, 0.4f);
};

struct RectPosition {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
};

// Символ шрифта (текстурированный глиф)
struct Character {
    GLuint texture_id;  // ID текстуры глифа
    int width;          // Ширина глифа
    int height;         // Высота глифа
    int bearing_x;      // Смещение от базовой линии по X
    int bearing_y;      // Смещение от базовой линии по Y
    int advance;        // Горизонтальный шаг (1/64 px)
};

// Отрисовка HUD (линии и текст)
class HUDOverlay {
public:
    HUDOverlay();
    ~HUDOverlay();

    // Инициализация OpenGL и шрифта
    bool initialize(uint32_t display_width, uint32_t display_height,
                   const std::string& font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");

    // Параметры перекрестия
    void setCrosshairConfig(const CrosshairConfig& config);

    void setPanelConfigs(const PanelConfig& left, const PanelConfig& right);

    // Добавить строку текста
    void addTextPosition(const TextPosition& text_pos);
    void addRectPosition(const RectPosition& rect_pos);
    void setStaticTextPositions(const std::vector<TextPosition>& text_positions);
    void setStaticRectPositions(const std::vector<RectPosition>& rect_positions);

    // Очистить список текстовых строк
    void clearTextPositions();
    void clearRectPositions();
    void clearDynamicTextPositions();
    void clearDynamicRectPositions();

    // Отрисовка HUD
    void render();

    // Отрисовка текста в пиксельных координатах
    void renderTextDirect(const std::string& text, float x, float y, float scale, const Color& color);
    bool measureText(const std::string& text, float scale, float& out_width,
                     float& out_ascent, float& out_descent);

private:
    void renderCrosshair();
    void renderPanel(const PanelConfig& config);
    void renderRectangles();
    void renderText();
    void compileShader(const char* source, GLenum type, GLuint& shader);
    void linkProgram(GLuint vertex, GLuint fragment, GLuint& program);
    bool loadFont(const std::string& font_path);
    bool loadGlyph(char32_t codepoint);

    uint32_t display_width_;
    uint32_t display_height_;

    CrosshairConfig crosshair_config_;
    PanelConfig panel_left_;
    PanelConfig panel_right_;
    std::vector<TextPosition> static_text_positions_;
    std::vector<RectPosition> static_rect_positions_;
    std::vector<TextPosition> dynamic_text_positions_;
    std::vector<RectPosition> dynamic_rect_positions_;

    GLuint hud_program_;
    GLuint text_program_;
    GLuint hud_vbo_;
    GLuint text_vbo_;

    // FreeType
    FT_Library ft_library_;
    FT_Face ft_face_;
    std::map<uint32_t, Character> characters_;  // Кеш глифов (Unicode)
};

