CXX = g++
CXXFLAGS = -std=c++17 -Wall -O3

# Библиотеки для разных версий
LIBS_GLES2 = -ldrm -lgbm -lEGL -lGLESv2 -lcamera -lcamera-base
LIBS_GLES1 = -ldrm -lgbm -lEGL -lGLES

# Цели сборки
TARGET_GLES2 = camhud
TARGET_GLES1 = camhud_gles1
TARGET_AUTO = camhud_auto

all: $(TARGET_AUTO)

# OpenGL ES 2.0 версия (рекомендуется для RPi 3)
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

# Auto-detect версия (рекомендуется для современных систем с Mesa)
$(TARGET_AUTO): camhud_auto.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_AUTO) camhud_auto.o $(LIBS_GLES2)

camhud_auto.o: camhud_auto.cpp
	$(CXX) $(CXXFLAGS) -c camhud_auto.cpp -o camhud_auto.o

clean:
	rm -f *.o $(TARGET_GLES2) $(TARGET_GLES1) $(TARGET_AUTO)

run: $(TARGET_AUTO)
	sudo ./$(TARGET_AUTO)

run-gles2: $(TARGET_GLES2)
	sudo ./$(TARGET_GLES2)

run-gles1: $(TARGET_GLES1)
	sudo ./$(TARGET_GLES1)

# Проверка поддержки OpenGL ES
check-gles:
	@echo "Checking OpenGL ES support..."
	@eglinfo 2>/dev/null || echo "eglinfo not installed. Install: sudo apt install mesa-utils"

.PHONY: all gles1 clean run run-gles2 run-gles1 check-gles
