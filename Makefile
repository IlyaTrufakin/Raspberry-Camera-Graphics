CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I/usr/include/libcamera -I/usr/include/libdrm

# Библиотеки для разных версий
LIBS_GLES2 = -ldrm -lgbm -lEGL -lGLESv2 -lcamera -lcamera-base -lpthread
LIBS_GLES1 = -ldrm -lgbm -lEGL -lGLES

# Исходные файлы для модульной версии
MODULAR_SOURCES = main.cpp camera_stream.cpp hud_overlay.cpp modbus_client.cpp
MODULAR_OBJECTS = $(MODULAR_SOURCES:.cpp=.o)

# Цели сборки
TARGET_MODULAR = camhud2
TARGET_GLES2 = camhud
TARGET_GLES1 = camhud_gles1
TARGET_AUTO = camhud_auto

# По умолчанию собираем модульную версию
all: $(TARGET_MODULAR)

# Модульная версия (новая архитектура)
$(TARGET_MODULAR): $(MODULAR_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET_MODULAR) $(MODULAR_OBJECTS) $(LIBS_GLES2)

main.o: main.cpp camera_stream.h hud_overlay.h modbus_client.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

camera_stream.o: camera_stream.cpp camera_stream.h
	$(CXX) $(CXXFLAGS) -c camera_stream.cpp -o camera_stream.o

hud_overlay.o: hud_overlay.cpp hud_overlay.h
	$(CXX) $(CXXFLAGS) -c hud_overlay.cpp -o hud_overlay.o

modbus_client.o: modbus_client.cpp modbus_client.h
	$(CXX) $(CXXFLAGS) -c modbus_client.cpp -o modbus_client.o

# OpenGL ES 2.0 версия (старая монолитная)
$(TARGET_GLES2): camhud1.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_GLES2) camhud1.o $(LIBS_GLES2)

camhud1.o: camhud1.cpp
	$(CXX) $(CXXFLAGS) -c camhud1.cpp -o camhud1.o

# OpenGL ES 1.1 версия (запасной вариант, без libcamera)
gles1: $(TARGET_GLES1)

$(TARGET_GLES1): camhud_gles1.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_GLES1) camhud_gles1.o $(LIBS_GLES1)

camhud_gles1.o: camhud_gles1.cpp
	$(CXX) $(CXXFLAGS) -c camhud_gles1.cpp -o camhud_gles1.o

# Auto-detect версия
$(TARGET_AUTO): camhud_auto.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_AUTO) camhud_auto.o $(LIBS_GLES2)

camhud_auto.o: camhud_auto.cpp
	$(CXX) $(CXXFLAGS) -c camhud_auto.cpp -o camhud_auto.o

clean:
	rm -f *.o $(TARGET_MODULAR) $(TARGET_GLES2) $(TARGET_GLES1) $(TARGET_AUTO)

run: $(TARGET_MODULAR)
	sudo ./$(TARGET_MODULAR)

run-old: $(TARGET_GLES2)
	sudo ./$(TARGET_GLES2)

run-gles2: $(TARGET_GLES2)
	sudo ./$(TARGET_GLES2)

run-gles1: $(TARGET_GLES1)
	sudo ./$(TARGET_GLES1)

# Проверка поддержки OpenGL ES
check-gles:
	@echo "Checking OpenGL ES support..."
	@eglinfo 2>/dev/null || echo "eglinfo not installed. Install: sudo apt install mesa-utils"

.PHONY: all gles1 clean run run-old run-gles2 run-gles1 check-gles
