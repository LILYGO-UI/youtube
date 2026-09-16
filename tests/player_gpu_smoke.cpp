#include "services/player.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>

namespace {
using Clock = std::chrono::steady_clock;
using Pixels = std::vector<std::uint8_t>;

void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string egl_error(const char *operation)
{
    char message[160];
    std::snprintf(message, sizeof(message), "%s failed (EGL 0x%04x)", operation, eglGetError());
    return message;
}

void require_egl(bool condition, const char *operation)
{
    if (!condition) throw std::runtime_error(egl_error(operation));
}

class EglSession {
public:
    void initialize()
    {
        auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        display_ = get_platform_display
                       ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                       : eglGetDisplay(EGL_DEFAULT_DISPLAY);
        require_egl(display_ != EGL_NO_DISPLAY, "eglGetPlatformDisplayEXT");
        EGLint major = 0, minor = 0;
        require_egl(eglInitialize(display_, &major, &minor), "eglInitialize");
        require_egl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
        const EGLint config_attributes[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config{};
        EGLint count = 0;
        require_egl(eglChooseConfig(display_, config_attributes, &config, 1, &count), "eglChooseConfig");
        require(count == 1, "No EGL pbuffer configuration supports OpenGL ES 2 and RGBA8");
        const EGLint surface_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, surface_attributes);
        require_egl(surface_ != EGL_NO_SURFACE, "eglCreatePbufferSurface");
        const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        require_egl(context_ != EGL_NO_CONTEXT, "eglCreateContext");
        require_egl(eglMakeCurrent(display_, surface_, surface_, context_), "eglMakeCurrent");
        const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        require(renderer && version, "No current OpenGL ES renderer");
        const std::string driver(renderer);
        require(driver.find("llvmpipe") == std::string::npos && driver.find("softpipe") == std::string::npos &&
                    driver.find("swrast") == std::string::npos,
                "EGL selected a software renderer: " + driver);
        std::printf("EGL %d.%d; renderer=%s; version=%s\n", major, minor, renderer, version);
    }

    ~EglSession()
    {
        if (display_ == EGL_NO_DISPLAY) return;
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
};

class ReadbackTarget {
public:
    explicit ReadbackTarget(const VideoTexture &texture)
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_);
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment_);
        glGenFramebuffers(1, &framebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
    }

    ~ReadbackTarget()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, previous_);
        glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment_);
        glDeleteFramebuffers(1, &framebuffer_);
    }

private:
    GLuint framebuffer_ = 0;
    GLint previous_ = 0;
    GLint pack_alignment_ = 4;
};

Pixels read_texture(const VideoTexture &texture)
{
    require(texture.id && texture.width > 0 && texture.height > 0, "Video texture has invalid dimensions or ID");
    require(glIsTexture(texture.id), "Video texture is not a live OpenGL texture");
    require(glGetError() == GL_NO_ERROR, "OpenGL error before texture readback");
    ReadbackTarget target(texture);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "Video texture cannot be attached to a readback framebuffer");
    Pixels pixels(static_cast<std::size_t>(texture.width) * texture.height * 4);
    glReadPixels(0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "glReadPixels failed for the video texture");
    return pixels;
}

bool nonblank(const Pixels &pixels)
{
    std::size_t lit = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        if (pixels[i] > 16 || pixels[i + 1] > 16 || pixels[i + 2] > 16) ++lit;
    return lit >= std::max<std::size_t>(1, pixels.size() / 4000);
}

std::uint64_t checksum(const Pixels &pixels)
{
    std::uint64_t value = 14695981039346656037ULL;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            value ^= pixels[i + channel];
            value *= 1099511628211ULL;
        }
    }
    return value;
}

void capture(const std::string &prefix, const char *name, const VideoTexture &texture, const Pixels &pixels)
{
    if (prefix.empty()) return;
    const std::string path = prefix + "-" + name + ".ppm";
    std::ofstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Cannot open screenshot: " + path);
    file << "P6\n" << texture.width << ' ' << texture.height << "\n255\n";
    // GL readback starts at the bottom; PPM rows start at the top.
    std::vector<char> row(static_cast<std::size_t>(texture.width) * 3);
    for (int y = texture.height - 1; y >= 0; --y) {
        for (int x = 0; x < texture.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * texture.width + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                row[static_cast<std::size_t>(x) * 3 + channel] = static_cast<char>(pixels[offset + channel]);
        }
        file.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    require(static_cast<bool>(file), "Cannot write screenshot: " + path);
    std::printf("Screenshot: %s\n", path.c_str());
}

const Playback &checked_state(MpvPlayer &player)
{
    const auto &state = player.poll();
    require(state.state != PlaybackState::Error, "Player error: " + state.error);
    return state;
}

bool pump(MpvPlayer &player, VideoTexture &texture, int width, int height)
{
    checked_state(player);
    const bool rendered = player.render_texture(texture, width, height);
    checked_state(player);
    return rendered;
}

void tick_wait()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

double process_cpu_seconds()
{
    rusage usage{};
    require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
           usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}

Pixels wait_for_picture(MpvPlayer &player, VideoTexture &texture, int width, int height)
{
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    while (Clock::now() < deadline) {
        if (pump(player, texture, width, height)) {
            auto pixels = read_texture(texture);
            if (nonblank(pixels)) return pixels;
        }
        tick_wait();
    }
    throw std::runtime_error("No nonblack GPU video frame within 8 seconds");
}

void wait_for_pause(MpvPlayer &player, VideoTexture &texture, int width, int height)
{
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline) {
        pump(player, texture, width, height);
        if (checked_state(player).state == PlaybackState::Paused) return;
        tick_wait();
    }
    throw std::runtime_error("Pause was not acknowledged within 3 seconds");
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "Usage: youtube-player-gpu-smoke <local moving video longer than 15s> [screenshot prefix]\n");
        return 2;
    }
    try {
        const std::string media = argv[1];
        const std::string prefix = argc == 3 ? argv[2] : "";
        require(static_cast<bool>(std::ifstream(media, std::ios::binary)), "Cannot read the local video file");
        EglSession egl;
        egl.initialize();
        GLuint last_texture = 0;
        {
            MpvPlayer player(true, true);
            VideoTexture texture;
            const auto loading_started = Clock::now();
            player.load(media, "", 0);
            auto first_pixels = wait_for_picture(player, texture, 960, 540);
            const double first_picture_seconds = std::chrono::duration<double>(Clock::now() - loading_started).count();
            const auto first_checksum = checksum(first_pixels);
            capture(prefix, "first", texture, first_pixels);
            std::printf("First GPU picture: %.3fs, texture=%u, size=%dx%d\n",
                        first_picture_seconds, texture.id, texture.width, texture.height);
            const auto playback_started = Clock::now();
            const double cpu_started = process_cpu_seconds();
            const double position_started = checked_state(player).position;
            const auto playback_deadline = playback_started + std::chrono::seconds(10);
            auto next_pixel_check = playback_started + std::chrono::milliseconds(1300);
            bool pixels_changed = false;
            int frames = 0;
            while (Clock::now() < playback_deadline) {
                if (pump(player, texture, 960, 540)) ++frames;
                require(checked_state(player).state != PlaybackState::Ended,
                        "Video ended during the 10-second playback sample; use a clip longer than 15 seconds");
                if (Clock::now() >= next_pixel_check) {
                    pixels_changed = pixels_changed || checksum(read_texture(texture)) != first_checksum;
                    next_pixel_check += std::chrono::milliseconds(1300);
                }
                tick_wait();
            }
            const double elapsed = std::chrono::duration<double>(Clock::now() - playback_started).count();
            const double cpu_elapsed = process_cpu_seconds() - cpu_started;
            const double position_delta = checked_state(player).position - position_started;
            const auto moving_pixels = read_texture(texture);
            require(frames >= 20 && position_delta > 5.0, "Video frames or playback clock did not advance");
            require(nonblank(moving_pixels), "Video texture became entirely black during playback");
            require(pixels_changed || checksum(moving_pixels) != first_checksum,
                    "Video texture pixels did not change during playback");
            capture(prefix, "playing", texture, moving_pixels);
            std::printf("Playback: frames=%d fps=%.2f cpu=%.1f%% wall=%.3fs clock-delta=%.3fs\n",
                        frames, frames / elapsed, cpu_elapsed / elapsed * 100.0, elapsed, position_delta);

            player.toggle_pause();
            wait_for_pause(player, texture, 960, 540);
            const auto settle = Clock::now() + std::chrono::milliseconds(200);
            while (Clock::now() < settle) {
                pump(player, texture, 960, 540);
                tick_wait();
            }
            const double paused_position = checked_state(player).position;
            const GLuint paused_texture = texture.id;
            const auto paused_pixels = read_texture(texture);
            const auto paused_checksum = checksum(paused_pixels);
            const auto pause_deadline = Clock::now() + std::chrono::milliseconds(700);
            while (Clock::now() < pause_deadline) {
                pump(player, texture, 960, 540);
                require(checked_state(player).state == PlaybackState::Paused, "Player left the paused state unexpectedly");
                tick_wait();
            }
            require(std::abs(checked_state(player).position - paused_position) < 0.05,
                    "Playback clock advanced while paused");
            require(texture.id == paused_texture && checksum(read_texture(texture)) == paused_checksum,
                    "GPU texture changed while paused");
            capture(prefix, "paused", texture, paused_pixels);
            require(pump(player, texture, 640, 360), "Paused resize did not redraw the latest video frame");
            require(texture.width == 640 && texture.height == 360 && texture.id != paused_texture,
                    "Paused resize did not replace the video render target");
            const auto resized_pixels = read_texture(texture);
            require(nonblank(resized_pixels), "Paused resize produced an empty video texture");
            require(checked_state(player).state == PlaybackState::Paused &&
                        std::abs(checked_state(player).position - paused_position) < 0.05,
                    "Paused resize changed the playback state or clock");
            capture(prefix, "paused-resized", texture, resized_pixels);
            std::printf("Pause: stable clock and pixels; resize=%dx%d texture=%u\n",
                        texture.width, texture.height, texture.id);

            player.seek(0);
            const auto seek_deadline = Clock::now() + std::chrono::seconds(4);
            bool sought = false;
            while (Clock::now() < seek_deadline) {
                pump(player, texture, 640, 360);
                const auto &state = checked_state(player);
                if (state.state == PlaybackState::Paused && state.position < 1.0) {
                    sought = true;
                    break;
                }
                tick_wait();
            }
            require(sought, "Seeking to the beginning while paused failed");
            const double resume_position = checked_state(player).position;
            player.toggle_pause();
            const auto resume_deadline = Clock::now() + std::chrono::seconds(4);
            int resumed_frames = 0;
            bool resumed = false;
            while (Clock::now() < resume_deadline) {
                if (pump(player, texture, 640, 360)) ++resumed_frames;
                const auto &state = checked_state(player);
                if (state.state == PlaybackState::Playing && state.position > resume_position + 1.0 && resumed_frames >= 10) {
                    resumed = true;
                    break;
                }
                tick_wait();
            }
            require(resumed, "Playback did not resume with advancing frames and clock after seek");
            const auto resumed_pixels = read_texture(texture);
            require(nonblank(resumed_pixels), "Resumed video texture is empty");
            capture(prefix, "resumed", texture, resumed_pixels);
            std::printf("Seek/resume: frames=%d position=%.3fs\n", resumed_frames, checked_state(player).position);
            last_texture = texture.id;
        }
        require(!glIsTexture(last_texture), "Destroying the first player leaked its video texture");
        require(glGetError() == GL_NO_ERROR, "OpenGL error after destroying the first player");
        {
            MpvPlayer player(true, true);
            VideoTexture texture;
            player.load(media, "", 0);
            const auto pixels = wait_for_picture(player, texture, 640, 360);
            capture(prefix, "recreated", texture, pixels);
            last_texture = texture.id;
            std::printf("Recreated player: nonblack texture=%u\n", texture.id);
        }
        require(!glIsTexture(last_texture), "Destroying the recreated player leaked its video texture");
        require(glGetError() == GL_NO_ERROR, "OpenGL error after destroying the recreated player");
        rusage usage{};
        require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
        std::printf("PASS: GPU pixels, playback, pause, resize, seek/resume, destroy/recreate; peak-RSS=%.1f MiB\n",
                    usage.ru_maxrss / 1024.0);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
