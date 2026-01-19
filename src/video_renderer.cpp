#include "video_renderer.h"

#include <iostream>

#ifndef GL_UNPACK_ROW_LENGTH_EXT
#define GL_UNPACK_ROW_LENGTH_EXT 0x0CF2
#endif

namespace {
const char* kVideoVs = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char* kVideoFsYuv = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;

void main() {
    float y = texture2D(uTexY, vTexCoord).r;
    float u = texture2D(uTexU, vTexCoord).r - 0.5;
    float v = texture2D(uTexV, vTexCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    gl_FragColor = vec4(r, g, b, 1.0);
}
)";

} // namespace

VideoRenderer::~VideoRenderer() {
    shutdown();
}

bool VideoRenderer::initialize(DRMDisplay& display, const AppConfig& config,
                               float left_edge, float right_edge) {
    display_ = &display;

    GLuint vs = 0;
    GLuint fs = 0;
    compileShader(kVideoVs, GL_VERTEX_SHADER, vs);
    compileShader(kVideoFsYuv, GL_FRAGMENT_SHADER, fs);

    linkProgram(vs, fs, program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    texture_count_ = 3;
    glGenTextures(texture_count_, textures_);
    for (int i = 0; i < texture_count_; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    buildQuadVertices(config, left_edge, right_edge);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices_), quad_vertices_, GL_STATIC_DRAW);

    glViewport(0, 0, display.width(), display.height());
    return true;
}

void VideoRenderer::compileShader(const char* source, GLenum type, GLuint& shader) {
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compilation error: " << log << std::endl;
    }
}

void VideoRenderer::linkProgram(GLuint vertex, GLuint fragment, GLuint& program) {
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program linking error: " << log << std::endl;
    }
}

void VideoRenderer::buildQuadVertices(const AppConfig& config, float left_edge, float right_edge) {
    float x0 = left_edge * 2.0f - 1.0f;
    float x1 = right_edge * 2.0f - 1.0f;

    float u0 = config.video.flip_horizontal ? 1.0f : 0.0f;
    float u1 = config.video.flip_horizontal ? 0.0f : 1.0f;
    float v0 = config.video.flip_vertical ? 1.0f : 0.0f;
    float v1 = config.video.flip_vertical ? 0.0f : 1.0f;

    float tl_u = u0, tl_v = v0;
    float tr_u = u1, tr_v = v0;
    float bl_u = u0, bl_v = v1;
    float br_u = u1, br_v = v1;

    switch (config.video.rotate) {
        case 90:
            tl_u = u0; tl_v = v1;
            tr_u = u0; tr_v = v0;
            bl_u = u1; bl_v = v1;
            br_u = u1; br_v = v0;
            break;
        case 180:
            tl_u = u1; tl_v = v1;
            tr_u = u0; tr_v = v1;
            bl_u = u1; bl_v = v0;
            br_u = u0; br_v = v0;
            break;
        case 270:
            tl_u = u1; tl_v = v0;
            tr_u = u1; tr_v = v1;
            bl_u = u0; bl_v = v0;
            br_u = u0; br_v = v1;
            break;
        default:
            break;
    }

    quad_vertices_[0] = x0; quad_vertices_[1] = -1.0f; quad_vertices_[2] = bl_u; quad_vertices_[3] = bl_v;
    quad_vertices_[4] = x1; quad_vertices_[5] = -1.0f; quad_vertices_[6] = br_u; quad_vertices_[7] = br_v;
    quad_vertices_[8] = x0; quad_vertices_[9] =  1.0f; quad_vertices_[10] = tl_u; quad_vertices_[11] = tl_v;
    quad_vertices_[12] = x1; quad_vertices_[13] = 1.0f; quad_vertices_[14] = tr_u; quad_vertices_[15] = tr_v;
}

void VideoRenderer::uploadFrame(CameraStream& camera, FrameBuffer* frame) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    CameraStream::PlaneData py, pu, pv;
    if (!camera.getFramePlanes(frame, py, pu, pv)) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return;
    }

    if (!textures_initialized_ || tex_w_ != py.width || tex_h_ != py.height) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     py.width, py.height, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textures_[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     pu.width, pu.height, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textures_[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     pv.width, pv.height, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
        tex_w_ = py.width;
        tex_h_ = py.height;
        textures_initialized_ = true;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, static_cast<int>(py.stride));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                      py.width, py.height,
                      GL_LUMINANCE, GL_UNSIGNED_BYTE, py.data);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, static_cast<int>(pu.stride));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    pu.width, pu.height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, pu.data);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textures_[2]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, static_cast<int>(pv.stride));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    pv.width, pv.height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, pv.data);

    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void VideoRenderer::draw(HUDOverlay& hud) {
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glUniform1i(glGetUniformLocation(program_, "uTexY"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glUniform1i(glGetUniformLocation(program_, "uTexU"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textures_[2]);
    glUniform1i(glGetUniformLocation(program_, "uTexV"), 2);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    GLint posLoc = glGetAttribLocation(program_, "aPosition");
    GLint texLoc = glGetAttribLocation(program_, "aTexCoord");

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);

    hud.render();
    display_->swapBuffers();
}

void VideoRenderer::shutdown() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (textures_[0] != 0) {
        glDeleteTextures(texture_count_, textures_);
        textures_[0] = textures_[1] = textures_[2] = 0;
    }
}
