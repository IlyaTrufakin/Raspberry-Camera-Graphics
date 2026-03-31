# Raspberry-Camera-Graphics

Приложение на C++17 для Raspberry Pi: захват видео с камеры через libcamera и вывод в полноэкранный DRM/KMS‑дисплей с HUD‑оверлеем (текст, панели, прицел). Поддерживается чтение значений по Modbus TCP и отображение динамических параметров (включая FPS и статусные биты).

## Возможности
- Захват кадров через `libcamera` (YUV) и вывод через DRM/GBM/EGL + OpenGL ES 2.0.
- HUD‑оверлей: текст, прямоугольники, прицел, левая/правая панели.
- Динамические значения из Modbus TCP (libmodbus) и индикация битовых статусов.
- ROI (область интереса) с авто‑подгонкой под активные панели.
- Гибкая настройка через `config.ini`.

## Сборка
Проект протестирован на Raspberry Pi 4 и Raspberry Pi 5 с ОС Raspberry Pi OS Lite (Bookworm).

Зависимости (установка в Bookworm):
```bash
sudo apt update
sudo apt install -y build-essential pkg-config libcamera-dev libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev libfreetype-dev libmodbus-dev
```

Сборка:
```bash
make
```

Запуск (нужны права для доступа к камере и DRM):
```bash
sudo ./camhud2
```

Проверка EGL/GLES:
```bash
make check-gles
```

## Конфигурация
Приложение читает `config.ini` из текущего каталога. Основные секции:
- `[video]` — размеры кадра, буферы, поворот/отражение.
- `[camera]` — параметры экспозиции, баланса, шумоподавления и т.д.
- `[hud]` — частота обновления HUD и путь к TTF‑шрифту.
- `[crosshair]` — параметры прицела (цвет, размеры, пунктир).
- `[panel]` и `[panel.right]` — боковые панели.
- `[roi]` — область интереса.
- `[modbus]` + `[modbus.registers]` + `[modbus.decimals]` — опрос Modbus TCP.
- `[text.static]`, `[text.dynamic]`, `[rect.static]`, `[status.bits]` — элементы HUD.

## Структура проекта
- `src/` — реализация.
- `include/` — заголовки.
- `Makefile` — сборка.
- `config.ini` — пример конфигурации.

## Примечания
- Приложение рассчитано на запуск на Raspberry Pi с поддержкой `libcamera` и DRM/KMS.
- Для Modbus укажите IP/порт и регистры в `config.ini`.
