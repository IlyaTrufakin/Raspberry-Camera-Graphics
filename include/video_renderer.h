#pragma once

#include "camera_stream.h"
#include "config.h"
#include "drm_display.h"
#include "hud_overlay.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

class VideoRenderer {
public:
    VideoRenderer() = default;
    ~VideoRenderer();

    bool initialize(DRMDisplay& display, const AppConfig& config,
                    float left_edge, float right_edge);
    void shutdown();

    void uploadFrame(CameraStream& camera, FrameBuffer* frame, const AppConfig& config);
    void draw(HUDOverlay& hud);

private:
    void compileShader(const char* source, GLenum type, GLuint& shader);
    void linkProgram(GLuint vertex, GLuint fragment, GLuint& program);
    void buildQuadVertices(const AppConfig& config, float left_edge, float right_edge);

    DRMDisplay* display_ = nullptr;

    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLuint textures_[3] = {0, 0, 0};
    int texture_count_ = 1;
    bool use_yuv_ = false;
    bool use_rgb_ = false;
    float quad_vertices_[16]{};
};
