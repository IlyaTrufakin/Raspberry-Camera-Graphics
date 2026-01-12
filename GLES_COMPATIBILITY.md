# OpenGL ES совместимость с Raspberry Pi

## Поддержка по моделям

| Модель | GPU | OpenGL ES 1.1 | OpenGL ES 2.0 | OpenGL ES 3.0 |
|--------|-----|---------------|---------------|---------------|
| Raspberry Pi 1 | VideoCore IV | ✅ Да | ✅ Да | ❌ Нет |
| Raspberry Pi 2 | VideoCore IV | ✅ Да | ✅ Да | ❌ Нет |
| **Raspberry Pi 3** | **VideoCore IV** | ✅ **Да** | ✅ **Да** | ❌ **Нет** |
| Raspberry Pi 4 | VideoCore VI | ✅ Да | ✅ Да | ✅ Да |
| Raspberry Pi 5 | VideoCore VII | ✅ Да | ✅ Да | ✅ Да |

## Для вашего Raspberry Pi 3

### ✅ Поддерживается: OpenGL ES 2.0

VideoCore IV GPU в RPi 3 **полностью поддерживает** OpenGL ES 2.0:
- ✅ Vertex и Fragment шейдеры (GLSL ES 1.00)
- ✅ Программируемый pipeline
- ✅ Текстуры и сэмплеры
- ✅ Framebuffer objects (FBO)
- ✅ Hardware acceleration

### 📝 Наш код использует GLES 2.0

Файл [camhud1.cpp](camhud1.cpp) использует **OpenGL ES 2.0**:

```cpp
// EGL контекст для GLES 2.0
EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

// Шейдеры совместимы с GLSL ES 1.00 (без #version директивы = 1.00)
const char* video_fs_src = R"(
precision mediump float;        // GLES 2.0
varying vec2 vTexCoord;         // GLES 2.0 (не 'in')
uniform sampler2D uTextureY;    // GLES 2.0
void main() {
    gl_FragColor = ...;         // GLES 2.0 (не 'out vec4')
}
)";
```

## Ошибки в оригинальном коде (исправлены)

❌ **Было** в строке 26:
```glsl
#version 300 es  // Это OpenGL ES 3.0 - НЕ РАБОТАЕТ на RPi 3!
```

✅ **Стало**:
```glsl
// Без #version = автоматически GLSL ES 1.00 (GLES 2.0)
precision mediump float;
```

## Проверка на вашей системе

### 1. Проверить драйвер:
```bash
vcgencmd version
```

### 2. Проверить EGL/GLES:
```bash
sudo apt install mesa-utils
eglinfo | grep "OpenGL ES"
```

Вы должны увидеть:
```
OpenGL ES profile version: 2.0
```

### 3. Запустить тестовую программу:
```bash
make gles1          # Простая версия без шейдеров (для теста)
sudo ./camhud_gles1 # Должна показать HUD overlay
```

Если работает, значит DRM/KMS настроены правильно. Затем:

```bash
make                # GLES 2.0 версия с камерой
sudo ./camhud       # Полная версия
```

## Различия версий в коде

### GLES 2.0 (camhud1.cpp)
- **Шейдеры**: Да, программируемые
- **YUV конверсия**: В GPU (быстро)
- **HUD overlay**: Через fragment shader
- **Производительность**: ⚡ Высокая

```cpp
// Компиляция шейдеров
GLuint prog = make_program(vs_src, fs_src);
glUseProgram(prog);
```

### GLES 1.1 (camhud_gles1.cpp)
- **Шейдеры**: Нет, fixed pipeline
- **YUV конверсия**: Нет (для демо)
- **HUD overlay**: Через glColor + glVertex
- **Производительность**: 🐌 Средняя

```cpp
// Fixed pipeline
glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
glVertexPointer(...);
glDrawArrays(...);
```

## Рекомендация для RPi 3

🎯 **Используйте GLES 2.0 версию** ([camhud1.cpp](camhud1.cpp)):
- Оптимально для VideoCore IV
- Минимальная задержка
- YUV конверсия на GPU
- Все возможности камеры

GLES 1.1 версия ([camhud_gles1.cpp](camhud_gles1.cpp)) нужна только для:
- Отладки без камеры
- Проверки DRM/KMS
- Очень старого оборудования (не ваш случай)

## Возможные проблемы

### Ошибка: "Failed to create EGL context"
**Причина**: Неправильная версия в ctx_attr
**Решение**: Убедитесь что `EGL_CONTEXT_CLIENT_VERSION = 2`

### Ошибка: "Shader compile error"
**Причина**: Использование GLSL 3.00 синтаксиса
**Решение**: Наш код уже исправлен, использует GLSL 1.00

### Черный экран
**Причина**: Шейдеры не загружают текстуры
**Решение**: В коде есть TODO для DMA-BUF интеграции (см. README)

## Ссылки

- [VideoCore IV спецификация](https://docs.broadcom.com/doc/12358545)
- [OpenGL ES 2.0 Reference](https://www.khronos.org/opengles/sdk/docs/reference_cards/OpenGL-ES-2_0-Reference-card.pdf)
- [Raspberry Pi Graphics](https://www.raspberrypi.com/documentation/computers/legacy_config_txt.html#gpu_mem)
