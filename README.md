# CamHUD - Видео HUD для Raspberry Pi с камерой IMX477

Программа для захвата видео с камеры IMX477 и вывода на HDMI с наложением графики (HUD).

## Особенности

- **Минимальная задержка**: Прямой вывод на HDMI через DRM/KMS без X Window
- **OpenGL ES2**: Аппаратное ускорение для наложения графики
- **YUV420 pipeline**: Эффективная обработка видео без лишних преобразований
- **1920x1080**: Поддержка Full HD
- **HUD overlay**: Прицел, текстовые метки, цифровые значения

## Требования

### Аппаратное обеспечение
- Raspberry Pi 3 (или новее)
- Камера IMX477 (подключена через CSI интерфейс)
- HDMI монитор

### Программное обеспечение

Установите необходимые библиотеки:

```bash
sudo apt update
sudo apt install -y \
    libdrm-dev \
    libgbm-dev \
    libegl-dev \
    libgles2-mesa-dev \
    libcamera-dev \
    libcamera0
```

## Сборка

### Проверка поддержки OpenGL ES

Сначала проверьте, какую версию OpenGL ES поддерживает ваша система:

```bash
make check-gles
```

или вручную:

```bash
glxinfo | grep "OpenGL ES"
```

### Raspberry Pi 3 с VideoCore IV

**Рекомендуется**: OpenGL ES 2.0 версия (с шейдерами):

```bash
make
```

**Альтернатива**: OpenGL ES 1.1 версия (без шейдеров, без libcamera):

```bash
make gles1
```

## Запуск

Программа требует прав root для доступа к DRM/KMS:

### GLES 2.0 версия (с камерой IMX477):
```bash
sudo ./camhud
```
или
```bash
make run
```

### GLES 1.1 версия (только overlay, без камеры):
```bash
sudo ./camhud_gles1
```
или
```bash
make run-gles1
```

## Настройка камеры

Убедитесь, что камера IMX477 подключена и распознается системой:

```bash
libcamera-hello --list-cameras
```

Если камера не обнаружена, проверьте:
1. Физическое подключение через CSI интерфейс
2. В `/boot/config.txt` должна быть строка: `dtoverlay=imx477`
3. Перезагрузите Raspberry Pi после изменения config.txt

## Версии программы

Проект включает две версии для разного оборудования:

### 1. camhud (GLES 2.0) - Рекомендуется
- **Файл**: [camhud1.cpp](camhud1.cpp)
- **OpenGL ES**: 2.0 (с шейдерами)
- **Камера**: libcamera + IMX477
- **Производительность**: Высокая (GPU шейдеры)
- **Совместимость**: Raspberry Pi 3 и новее с VideoCore IV

### 2. camhud_gles1 (GLES 1.1) - Запасной вариант
- **Файл**: [camhud_gles1.cpp](camhud_gles1.cpp)
- **OpenGL ES**: 1.1 (fixed pipeline)
- **Камера**: Без камеры (только overlay demo)
- **Производительность**: Средняя
- **Совместимость**: Старые системы, тестирование

## Структура кода (GLES 2.0 версия)

### Основные компоненты

1. **DRM/KMS**: Прямой доступ к выводу на HDMI ([camhud1.cpp:203-253](camhud1.cpp#L203-L253))
2. **libcamera**: Захват видео с IMX477 ([camhud1.cpp:288-322](camhud1.cpp#L288-L322))
3. **OpenGL ES 2.0**: Рендеринг видео и графики (полностью совместим с VideoCore IV)
4. **YUV shaders**: Конверсия YUV420 → RGB ([camhud1.cpp:38-56](camhud1.cpp#L38-L56))

### HUD элементы

Программа включает примеры наложения графики:

- `draw_crosshair()` - Прицел в центре экрана ([camhud1.cpp:135-166](camhud1.cpp#L135-L166))
- `draw_hud_value()` - Блоки с цифровыми значениями ([camhud1.cpp:169-199](camhud1.cpp#L169-L199))
- `draw_text()` - Текстовые метки (placeholder) ([camhud1.cpp:114-132](camhud1.cpp#L114-L132))

## Модификация HUD

### Добавление своих переменных

В основном цикле ([camhud1.cpp:398-449](camhud1.cpp#L398-L449)) вы можете добавить свои переменные:

```cpp
int my_speed = 120;  // Ваша переменная
float temperature = 36.6;

// В цикле рендеринга:
draw_hud_value(overlay_prog, overlay_aPos, overlay_uColor, -0.95f, 0.85f, my_speed);
```

### Изменение цветов и позиций

Цвета задаются в формате RGBA (0.0-1.0):
- Красный: `glUniform4f(uColor, 1.0f, 0.0f, 0.0f, 1.0f);`
- Зеленый: `glUniform4f(uColor, 0.0f, 1.0f, 0.0f, 1.0f);`
- Синий: `glUniform4f(uColor, 0.0f, 0.0f, 1.0f, 1.0f);`

Позиции в normalized device coordinates (-1.0 до 1.0):
- Центр экрана: `(0.0, 0.0)`
- Верхний левый: `(-1.0, 1.0)`
- Нижний правый: `(1.0, -1.0)`

### Добавление реального текста

Текущая версия использует placeholder для текста. Для отрисовки реального текста:

1. Установите FreeType: `sudo apt install libfreetype-dev`
2. Создайте текстурный атлас из шрифта
3. Используйте текстурированные quad'ы для каждого символа

Или используйте готовые библиотеки: FTGL, GLText

## Оптимизация производительности

### Текущая реализация
- Использует `usleep(16666)` для ~60 FPS
- Можно убрать для максимальной скорости

### TODO: Zero-copy видео
Текущая версия имеет placeholder для загрузки YUV данных. Для полного zero-copy:

1. Используйте DMA-BUF из libcamera
2. Создайте EGLImage из DMA-BUF handle
3. Привяжите EGLImage к OpenGL текстурам

Пример:
```cpp
// Получаем DMA-BUF fd из FrameBuffer
int fd = framebuffer->planes()[0].fd.get();

// Создаем EGLImage
EGLImage eglImage = eglCreateImage(
    egl_display, EGL_NO_CONTEXT,
    EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);

// Привязываем к GL текстуре
glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eglImage);
```

## Запуск при загрузке

Создайте systemd service:

```bash
sudo nano /etc/systemd/system/camhud.service
```

Содержимое:
```ini
[Unit]
Description=Camera HUD Display
After=graphical.target

[Service]
Type=simple
User=root
WorkingDirectory=/home/pi/camhud
ExecStart=/home/pi/camhud/camhud
Restart=always

[Install]
WantedBy=multi-user.target
```

Активируйте:
```bash
sudo systemctl enable camhud
sudo systemctl start camhud
```

## Устранение неполадок

### Камера не найдена
```
No cameras found
```
**Решение**: Проверьте подключение камеры и настройки в `/boot/config.txt`

### Ошибка DRM
```
Failed to open DRM device
```
**Решение**: Запускайте с sudo, убедитесь что пользователь в группе `video`

### Черный экран
**Решение**:
- Проверьте что HDMI подключен до запуска
- Попробуйте другое разрешение если 1920x1080 не поддерживается

### Низкий FPS
**Решение**:
- Убедитесь что используется аппаратное ускорение
- Проверьте `vcgencmd get_config int` для GPU memory (минимум 128MB)
- Уменьшите разрешение камеры

## Лицензия

MIT License - используйте свободно для своих проектов.
