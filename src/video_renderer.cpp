#include "video_renderer.h"

#include <algorithm>
#include <chrono>
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

const char* kVideoFsRgb = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;

void main() {
    vec3 rgb = texture2D(uTexture, vTexCoord).rgb;
    gl_FragColor = vec4(rgb, 1.0);
}
)";

const char* kHudVs = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char* kHudFs = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";
} // namespace

VideoRenderer::~VideoRenderer() {
    shutdown();
}

bool VideoRenderer::initialize(DRMDisplay& display, const AppConfig& config,
                               float left_edge, float right_edge) {
    display_ = &display;

    use_rgb_ = false;
    use_yuv_ = false;
    if (config.video.pixel_format == "RGB888" ||
        config.video.pixel_format == "XRGB8888" ||
        config.video.pixel_format == "ARGB8888") {
        use_rgb_ = true;
    } else if (config.video.pixel_format == "YUV420") {
        use_yuv_ = true;
    }

    GLuint vs = 0;
    GLuint fs = 0;
    compileShader(kVideoVs, GL_VERTEX_SHADER, vs);
    if (use_rgb_) {
        compileShader(kVideoFsRgb, GL_FRAGMENT_SHADER, fs);
    } else {
        compileShader(kVideoFsYuv, GL_FRAGMENT_SHADER, fs);
    }

    linkProgram(vs, fs, program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    texture_count_ = use_yuv_ ? 3 : 1;
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

    if (hud_cache_enabled_) {
        GLuint hud_vs = 0;
        GLuint hud_fs = 0;
        compileShader(kHudVs, GL_VERTEX_SHADER, hud_vs);
        compileShader(kHudFs, GL_FRAGMENT_SHADER, hud_fs);
        linkProgram(hud_vs, hud_fs, hud_program_);
        glDeleteShader(hud_vs);
        glDeleteShader(hud_fs);

        hud_vertices_[0] = -1.0f; hud_vertices_[1] = -1.0f; hud_vertices_[2] = 0.0f; hud_vertices_[3] = 0.0f;
        hud_vertices_[4] =  1.0f; hud_vertices_[5] = -1.0f; hud_vertices_[6] = 1.0f; hud_vertices_[7] = 0.0f;
        hud_vertices_[8] = -1.0f; hud_vertices_[9] =  1.0f; hud_vertices_[10] = 0.0f; hud_vertices_[11] = 1.0f;
        hud_vertices_[12] = 1.0f; hud_vertices_[13] = 1.0f; hud_vertices_[14] = 1.0f; hud_vertices_[15] = 1.0f;

        glGenBuffers(1, &hud_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(hud_vertices_), hud_vertices_, GL_STATIC_DRAW);

        hud_tex_w_ = display.width();
        hud_tex_h_ = display.height();
        glGenTextures(1, &hud_tex_);
        glBindTexture(GL_TEXTURE_2D, hud_tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     static_cast<int>(hud_tex_w_), static_cast<int>(hud_tex_h_),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &hud_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, hud_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hud_tex_, 0);
        GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "HUD FBO incomplete: " << std::hex << fb_status << std::dec << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glViewport(0, 0, display.width(), display.height());
    return true;
}

void VideoRenderer::setProfileEnabled(bool enable) {
    profile_enabled_ = enable;
    upload_samples_ = 0;
    draw_samples_ = 0;
    prof_upload_total_ms_ = 0.0;
    prof_upload_get_ms_ = 0.0;
    prof_upload_planes_ms_ = 0.0;
    prof_upload_tex_y_ms_ = 0.0;
    prof_upload_tex_u_ms_ = 0.0;
    prof_upload_tex_v_ms_ = 0.0;
    prof_upload_tex_rgb_ms_ = 0.0;
    prof_draw_setup_ms_ = 0.0;
    prof_draw_video_ms_ = 0.0;
    prof_draw_hud_ms_ = 0.0;
    prof_draw_swap_ms_ = 0.0;
    profile_last_ = std::chrono::steady_clock::now();
}

void VideoRenderer::setHudCacheEnabled(bool enable) {
    hud_cache_enabled_ = enable;
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

void VideoRenderer::uploadFrame(CameraStream& camera, FrameBuffer* frame, const AppConfig& config) {
    auto t0 = std::chrono::steady_clock::now();
    uint32_t stride = 0;
    uint8_t* frame_data = camera.getFrameData(frame, stride);
    auto t1 = std::chrono::steady_clock::now();
    if (!frame_data) {
        return;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (use_yuv_) {
        CameraStream::PlaneData py, pu, pv;
        auto t2 = std::chrono::steady_clock::now();
        if (camera.getFramePlanes(frame, py, pu, pv)) {
            auto t3 = std::chrono::steady_clock::now();
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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
            auto t4 = std::chrono::steady_clock::now();

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, textures_[1]);
            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, static_cast<int>(pu.stride));
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            pu.width, pu.height,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, pu.data);
            auto t5 = std::chrono::steady_clock::now();

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, textures_[2]);
            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, static_cast<int>(pv.stride));
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            pv.width, pv.height,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, pv.data);
            auto t6 = std::chrono::steady_clock::now();

            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

            if (profile_enabled_) {
                prof_upload_get_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
                prof_upload_planes_ms_ += std::chrono::duration<double, std::milli>(t3 - t2).count();
                prof_upload_tex_y_ms_ += std::chrono::duration<double, std::milli>(t4 - t3).count();
                prof_upload_tex_u_ms_ += std::chrono::duration<double, std::milli>(t5 - t4).count();
                prof_upload_tex_v_ms_ += std::chrono::duration<double, std::milli>(t6 - t5).count();
            }
        }
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_[0]);

        GLenum format = use_rgb_ ? GL_RGB : GL_LUMINANCE;
        GLenum internal_format = use_rgb_ ? GL_RGB : GL_LUMINANCE;
        int bytes_per_pixel = 1;
        if (use_rgb_) {
            if (config.video.pixel_format == "XRGB8888" ||
                config.video.pixel_format == "ARGB8888") {
                format = GL_BGRA_EXT;
                internal_format = GL_RGBA;
                bytes_per_pixel = 4;
            } else {
                bytes_per_pixel = 3;
            }
        }

        int row_pixels = camera.getWidth();
        if (stride > 0 && bytes_per_pixel > 0) {
            row_pixels = static_cast<int>(stride / bytes_per_pixel);
        }

        auto t2 = std::chrono::steady_clock::now();
        if (!textures_initialized_ || tex_w_ != camera.getWidth() || tex_h_ != camera.getHeight()) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
                         camera.getWidth(), camera.getHeight(), 0,
                         format, GL_UNSIGNED_BYTE, nullptr);
            tex_w_ = camera.getWidth();
            tex_h_ = camera.getHeight();
            textures_initialized_ = true;
        }

        if (row_pixels == static_cast<int>(camera.getWidth())) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            camera.getWidth(), camera.getHeight(),
                            format, GL_UNSIGNED_BYTE, frame_data);
        } else {
            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, row_pixels);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            camera.getWidth(), camera.getHeight(),
                            format, GL_UNSIGNED_BYTE, frame_data);
            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
        }
        auto t3 = std::chrono::steady_clock::now();
        if (profile_enabled_) {
            prof_upload_get_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
            prof_upload_tex_rgb_ms_ += std::chrono::duration<double, std::milli>(t3 - t2).count();
        }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (profile_enabled_) {
        auto t_end = std::chrono::steady_clock::now();
        prof_upload_total_ms_ += std::chrono::duration<double, std::milli>(t_end - t0).count();
        upload_samples_++;
    }
}

void VideoRenderer::draw(HUDOverlay& hud, bool hud_dirty) {
    auto t0 = std::chrono::steady_clock::now();
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program_);

    if (use_yuv_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glUniform1i(glGetUniformLocation(program_, "uTexY"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textures_[1]);
        glUniform1i(glGetUniformLocation(program_, "uTexU"), 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textures_[2]);
        glUniform1i(glGetUniformLocation(program_, "uTexV"), 2);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glUniform1i(glGetUniformLocation(program_, use_rgb_ ? "uTexture" : "uTexY"), 0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    GLint posLoc = glGetAttribLocation(program_, "aPosition");
    GLint texLoc = glGetAttribLocation(program_, "aTexCoord");

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    auto t1 = std::chrono::steady_clock::now();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    auto t2 = std::chrono::steady_clock::now();

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);

    if (hud_cache_enabled_) {
        if (hud_dirty) {
            glBindFramebuffer(GL_FRAMEBUFFER, hud_fbo_);
            glViewport(0, 0, display_->width(), display_->height());
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            hud.render();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, display_->width(), display_->height());
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(hud_program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hud_tex_);
        glUniform1i(glGetUniformLocation(hud_program_, "uTexture"), 0);

        glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
        GLint hpos = glGetAttribLocation(hud_program_, "aPosition");
        GLint htex = glGetAttribLocation(hud_program_, "aTexCoord");
        glEnableVertexAttribArray(hpos);
        glEnableVertexAttribArray(htex);
        glVertexAttribPointer(hpos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(htex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(hpos);
        glDisableVertexAttribArray(htex);
        glDisable(GL_BLEND);
    } else {
        hud.render();
    }
    auto t3 = std::chrono::steady_clock::now();
    display_->swapBuffers();
    auto t4 = std::chrono::steady_clock::now();

    if (profile_enabled_) {
        prof_draw_setup_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
        prof_draw_video_ms_ += std::chrono::duration<double, std::milli>(t2 - t1).count();
        prof_draw_hud_ms_ += std::chrono::duration<double, std::milli>(t3 - t2).count();
        prof_draw_swap_ms_ += std::chrono::duration<double, std::milli>(t4 - t3).count();
        draw_samples_++;
        if (t4 - profile_last_ >= std::chrono::seconds(1)) {
            double u_div = upload_samples_ > 0 ? static_cast<double>(upload_samples_) : 1.0;
            double d_div = draw_samples_ > 0 ? static_cast<double>(draw_samples_) : 1.0;
            std::cout << "VR upload avg ms: total=" << (prof_upload_total_ms_ / u_div)
                      << " get=" << (prof_upload_get_ms_ / u_div)
                      << " planes=" << (prof_upload_planes_ms_ / u_div)
                      << " y=" << (prof_upload_tex_y_ms_ / u_div)
                      << " u=" << (prof_upload_tex_u_ms_ / u_div)
                      << " v=" << (prof_upload_tex_v_ms_ / u_div)
                      << " rgb=" << (prof_upload_tex_rgb_ms_ / u_div)
                      << " | draw avg ms: setup=" << (prof_draw_setup_ms_ / d_div)
                      << " draw=" << (prof_draw_video_ms_ / d_div)
                      << " hud=" << (prof_draw_hud_ms_ / d_div)
                      << " swap=" << (prof_draw_swap_ms_ / d_div)
                      << std::endl;
            upload_samples_ = 0;
            draw_samples_ = 0;
            prof_upload_total_ms_ = 0.0;
            prof_upload_get_ms_ = 0.0;
            prof_upload_planes_ms_ = 0.0;
            prof_upload_tex_y_ms_ = 0.0;
            prof_upload_tex_u_ms_ = 0.0;
            prof_upload_tex_v_ms_ = 0.0;
            prof_upload_tex_rgb_ms_ = 0.0;
            prof_draw_setup_ms_ = 0.0;
            prof_draw_video_ms_ = 0.0;
            prof_draw_hud_ms_ = 0.0;
            prof_draw_swap_ms_ = 0.0;
            profile_last_ = t4;
        }
    }
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
    if (hud_program_) {
        glDeleteProgram(hud_program_);
        hud_program_ = 0;
    }
    if (hud_vbo_) {
        glDeleteBuffers(1, &hud_vbo_);
        hud_vbo_ = 0;
    }
    if (hud_fbo_) {
        glDeleteFramebuffers(1, &hud_fbo_);
        hud_fbo_ = 0;
    }
    if (hud_tex_) {
        glDeleteTextures(1, &hud_tex_);
        hud_tex_ = 0;
    }
    if (textures_[0] != 0) {
        glDeleteTextures(texture_count_, textures_);
        textures_[0] = textures_[1] = textures_[2] = 0;
    }
}
