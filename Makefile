CXX = g++
PKG_MODULES = libcamera libdrm gbm egl glesv2 freetype2 libmodbus
PKG_CFLAGS = $(shell pkg-config --cflags $(PKG_MODULES))
PKG_LIBS = $(shell pkg-config --libs $(PKG_MODULES))

CXXFLAGS = -std=c++17 -Wall -O2 -Iinclude $(PKG_CFLAGS)

LIBS = $(PKG_LIBS) -lcamera-base -lpthread

SRC_DIR = src
SOURCES = $(SRC_DIR)/main.cpp \
          $(SRC_DIR)/camera_stream.cpp \
          $(SRC_DIR)/drm_display.cpp \
          $(SRC_DIR)/hud_overlay.cpp \
          $(SRC_DIR)/modbus_client.cpp \
          $(SRC_DIR)/video_renderer.cpp \
          $(SRC_DIR)/app.cpp \
          $(SRC_DIR)/config.cpp
OBJECTS = $(SOURCES:.cpp=.o)

TARGET = camera

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(SRC_DIR)/*.o $(TARGET)

run: $(TARGET)
	sudo ./$(TARGET)

check-gles:
	@echo "Checking OpenGL ES support..."
	@eglinfo 2>/dev/null || echo "eglinfo not installed. Install: sudo apt install mesa-utils"

.PHONY: all clean run check-gles
