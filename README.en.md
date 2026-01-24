# Raspberry-Camera-Graphics

C++17 application for Raspberry Pi: captures video via libcamera and renders fullscreen to a DRM/KMS display with a HUD overlay (text, panels, crosshair). Supports Modbus TCP polling and shows dynamic values (including FPS and status bits).

## Features
- Frame capture via `libcamera` (YUV) and rendering with DRM/GBM/EGL + OpenGL ES 2.0.
- HUD overlay: text, rectangles, crosshair, left/right panels.
- Dynamic values from Modbus TCP (libmodbus) and bit status indicators.
- ROI (region of interest) with auto-fit based on active panels.
- Flexible configuration via `config.ini`.

## Build
Dependencies (minimum):
- libcamera
- libdrm, gbm
- EGL, OpenGL ES 2.0
- FreeType2
- libmodbus
- pthread

Build:
```bash
make
```

Run (needs permissions for camera and DRM):
```bash
sudo ./camhud2
```

Check EGL/GLES:
```bash
make check-gles
```

## Configuration
The app reads `config.ini` from the current directory. Key sections:
- `[video]` - frame size, buffers, rotation/flip.
- `[camera]` - exposure, white balance, noise reduction, etc.
- `[hud]` - HUD refresh rate and TTF font path.
- `[crosshair]` - crosshair settings (color, size, dashed style).
- `[panel]` and `[panel.right]` - side panels.
- `[roi]` - region of interest.
- `[modbus]` + `[modbus.registers]` + `[modbus.decimals]` - Modbus TCP polling.
- `[text.static]`, `[text.dynamic]`, `[rect.static]`, `[status.bits]` - HUD elements.

## Project layout
- `src/` - implementation.
- `include/` - headers.
- `Makefile` - build.
- `config.ini` - example configuration.

## Notes
- Designed to run on Raspberry Pi with `libcamera` and DRM/KMS.
- For Modbus, set server IP/port and register map in `config.ini`.
