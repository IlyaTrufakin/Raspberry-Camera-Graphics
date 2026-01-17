CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -Iinclude -I/usr/include/libcamera -I/usr/include/libdrm -I/usr/include/freetype2

LIBS = -ldrm -lgbm -lEGL -lGLESv2 -lcamera -lcamera-base -lpthread -lfreetype -lmodbus

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

TARGET = camhud2

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
