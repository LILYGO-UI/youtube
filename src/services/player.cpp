#include "services/player.hpp"
#include "pages/youtube/youtube_model.hpp"

#include <mpv/client.h>
#include <mpv/render.h>
#if defined(YOUTUBE_ENABLE_OPENGL)
#include <mpv/render_gl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;

double node_number(const mpv_node *map, const char *key, double fallback = -1)
{
    if (!map || map->format != MPV_FORMAT_NODE_MAP || !map->u.list) return fallback;
    for (int i = 0; i < map->u.list->num; ++i) {
        if (std::strcmp(map->u.list->keys[i], key) != 0) continue;
        const auto &value = map->u.list->values[i];
        if (value.format == MPV_FORMAT_DOUBLE) return value.u.double_;
        if (value.format == MPV_FORMAT_INT64) return static_cast<double>(value.u.int64);
        if (value.format == MPV_FORMAT_FLAG) return value.u.flag;
        return fallback;
    }
    return fallback;
}

#if defined(YOUTUBE_ENABLE_OPENGL)
void *gl_proc_address(void *, const char *name)
{
    auto *address = reinterpret_cast<void *>(eglGetProcAddress(name));
    return address ? address : dlsym(RTLD_DEFAULT, name);
}

// libmpv and LVGL share the UI thread's EGL context, so neither owns its GL state.
class GlState {
public:
    GlState()
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer_);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_zero_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer_);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_);
        glGetIntegerv(GL_SCISSOR_BOX, scissor_);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment_);
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment_);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb_);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha_);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_eq_rgb_);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_eq_alpha_);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color_);
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask_);
        for (std::size_t i = 0; i < capability_count; ++i) enabled_[i] = glIsEnabled(capabilities_[i]);
        const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
        const bool es3 = version && std::strstr(version, "OpenGL ES 3.");
        unpack_subimage_ = es3 || (extensions && std::strstr(extensions, "GL_EXT_unpack_subimage"));
        if (unpack_subimage_) {
            glGetIntegerv(GL_UNPACK_ROW_LENGTH_EXT, &unpack_row_length_);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS_EXT, &unpack_skip_rows_);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS_EXT, &unpack_skip_pixels_);
            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
        }
        if (es3)
            bind_vertex_array_ = reinterpret_cast<BindVertexArray>(eglGetProcAddress("glBindVertexArray"));
        else if (extensions && std::strstr(extensions, "GL_OES_vertex_array_object"))
            bind_vertex_array_ = reinterpret_cast<BindVertexArray>(eglGetProcAddress("glBindVertexArrayOES"));
        if (bind_vertex_array_) {
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &vertex_array_);
            bind_vertex_array_(0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glUseProgram(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        for (GLenum capability : capabilities_) glDisable(capability);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }

    ~GlState()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer_);
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_zero_);
        glActiveTexture(active_texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        if (bind_vertex_array_) bind_vertex_array_(vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, array_buffer_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer_);
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glScissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment_);
        glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment_);
        if (unpack_subimage_) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, unpack_row_length_);
            glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, unpack_skip_rows_);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, unpack_skip_pixels_);
        }
        glBlendFuncSeparate(blend_src_rgb_, blend_dst_rgb_, blend_src_alpha_, blend_dst_alpha_);
        glBlendEquationSeparate(blend_eq_rgb_, blend_eq_alpha_);
        glClearColor(clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]);
        glColorMask(color_mask_[0], color_mask_[1], color_mask_[2], color_mask_[3]);
        for (std::size_t i = 0; i < capability_count; ++i) {
            if (enabled_[i]) glEnable(capabilities_[i]);
            else glDisable(capabilities_[i]);
        }
    }

    void forget_texture(GLuint texture)
    {
        if (texture_ == static_cast<GLint>(texture)) texture_ = 0;
        if (texture_zero_ == static_cast<GLint>(texture)) texture_zero_ = 0;
    }

    void forget_framebuffer(GLuint framebuffer)
    {
        if (framebuffer_ == static_cast<GLint>(framebuffer)) framebuffer_ = 0;
    }

private:
    using BindVertexArray = void (*)(GLuint);
    BindVertexArray bind_vertex_array_ = nullptr;
    bool unpack_subimage_ = false;
    static constexpr GLenum capabilities_[] = {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
                                               GL_CULL_FACE};
    static constexpr std::size_t capability_count = sizeof(capabilities_) / sizeof(capabilities_[0]);
    GLboolean enabled_[capability_count]{};
    GLint framebuffer_{}, renderbuffer_{}, program_{}, active_texture_{}, texture_{}, texture_zero_{}, vertex_array_{};
    GLint array_buffer_{}, element_buffer_{}, unpack_alignment_{}, pack_alignment_{};
    GLint unpack_row_length_{}, unpack_skip_rows_{}, unpack_skip_pixels_{};
    GLint blend_src_rgb_{}, blend_dst_rgb_{}, blend_src_alpha_{}, blend_dst_alpha_{};
    GLint blend_eq_rgb_{}, blend_eq_alpha_{};
    GLint viewport_[4]{}, scissor_[4]{};
    GLfloat clear_color_[4]{};
    GLboolean color_mask_[4]{};
};
#endif
} // namespace

struct MpvPlayer::Impl {
    void *library               = nullptr;
    mpv_handle *handle          = nullptr;
    mpv_render_context *context = nullptr;
    Playback playback;
    std::atomic_bool dirty{true};
    bool silent;
    bool opengl;
    bool debug = false;
    bool paused = false;
    bool loaded = false;
    bool separate_audio = false;
    struct NetworkDiagnostics {
        double seconds = -1;
        double bytes = -1;
        double bytes_per_second = -1;
        double idle = -1;
        double underrun = -1;
        double eof = -1;
        double avsync = NAN;
        double width = -1;
        double height = -1;
        double fps = -1;
        std::int64_t percent = -1;
    } network;
    std::string hwdec;
    std::string decoder;
    std::int64_t dropped_frames = 0;
    std::int64_t decoder_dropped_frames = 0;
    std::uint64_t rendered_frames = 0;
    std::uint64_t reported_frames = 0;
    double max_render_ms = 0;
    Clock::time_point last_report = Clock::now();
#if defined(YOUTUBE_ENABLE_OPENGL)
    GLuint texture = 0;
    GLuint framebuffer = 0;
    int texture_width = 0;
    int texture_height = 0;
    GLint maximum_texture_size = 0;
    EGLContext egl_context = EGL_NO_CONTEXT;
#endif

#define MPV_FUNCTION(name) decltype(&name) name##_fn = nullptr
    MPV_FUNCTION(mpv_create);
    MPV_FUNCTION(mpv_initialize);
    MPV_FUNCTION(mpv_set_option_string);
    MPV_FUNCTION(mpv_command_async);
    MPV_FUNCTION(mpv_command_node_async);
    MPV_FUNCTION(mpv_observe_property);
    MPV_FUNCTION(mpv_wait_event);
    MPV_FUNCTION(mpv_error_string);
    MPV_FUNCTION(mpv_request_log_messages);
    MPV_FUNCTION(mpv_terminate_destroy);
    MPV_FUNCTION(mpv_render_context_create);
    MPV_FUNCTION(mpv_render_context_set_update_callback);
    MPV_FUNCTION(mpv_render_context_update);
    MPV_FUNCTION(mpv_render_context_render);
    MPV_FUNCTION(mpv_render_context_free);
#undef MPV_FUNCTION

    explicit Impl(bool quiet, bool use_opengl) : silent(quiet), opengl(use_opengl)
    {
        const char *value = std::getenv("LILYGO_YOUTUBE_DEBUG");
        debug = value && value[0] && std::strcmp(value, "0") != 0;
    }
    ~Impl()
    {
        release();
    }

    void release()
    {
#if defined(YOUTUBE_ENABLE_OPENGL)
        std::unique_ptr<GlState> state;
        if (opengl && eglGetCurrentContext() != EGL_NO_CONTEXT) state = std::make_unique<GlState>();
#endif
        if (context) {
            mpv_render_context_set_update_callback_fn(context, nullptr, nullptr);
            mpv_render_context_free_fn(context);
            context = nullptr;
        }
#if defined(YOUTUBE_ENABLE_OPENGL)
        if (state) {
            state->forget_texture(texture);
            state->forget_framebuffer(framebuffer);
        }
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
        if (texture) glDeleteTextures(1, &texture);
        framebuffer = texture = 0;
        texture_width = texture_height = 0;
        egl_context = EGL_NO_CONTEXT;
#endif
        if (handle) {
            mpv_terminate_destroy_fn(handle);
            handle = nullptr;
        }
        if (library) {
            dlclose(library);
            library = nullptr;
        }
    }

    template <typename T>
    void symbol(T &pointer, const char *name)
    {
        pointer = reinterpret_cast<T>(dlsym(library, name));
        if (!pointer) throw std::runtime_error(std::string("Incompatible libmpv: ") + name);
    }

    void option(const char *key, const char *value)
    {
        const int error = mpv_set_option_string_fn(handle, key, value);
        if (error < 0) throw std::runtime_error(std::string(key) + ": " + mpv_error_string_fn(error));
    }

    void report()
    {
        if (!debug || !loaded) return;
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_report).count();
        if (elapsed < 5.0) return;
        std::fprintf(stderr,
                     "[youtube-player] renderer=%s hwdec=%s fps=%.1f dropped=%lld decoder-dropped=%lld render-max-ms=%.2f position=%.1f paused=%d buffering=%d\n",
                     opengl ? "opengl-es" : "software", hwdec.empty() ? "unknown" : hwdec.c_str(),
                     (rendered_frames - reported_frames) / elapsed,
                     static_cast<long long>(dropped_frames), static_cast<long long>(decoder_dropped_frames),
                     max_render_ms, playback.position, paused, playback.buffering);
        std::fprintf(stderr,
                     "[youtube-cache] main-seconds=%.2f main-KiB=%.1f input-KiB/s=%.1f idle=%.0f underrun=%.0f eof=%.0f buffered-percent=%lld avsync-ms=%.1f video=%.0fx%.0f@%.2f separate-audio=%d\n",
                     network.seconds, network.bytes < 0 ? -1 : network.bytes / 1024,
                     network.bytes_per_second < 0 ? -1 : network.bytes_per_second / 1024,
                     network.idle, network.underrun, network.eof, static_cast<long long>(network.percent),
                     network.avsync * 1000, network.width, network.height, network.fps, separate_audio);
        reported_frames = rendered_frames;
        max_render_ms = 0;
        last_report = now;
    }

    void rendered(Clock::time_point started)
    {
        ++rendered_frames;
        max_render_ms = std::max(max_render_ms, std::chrono::duration<double, std::milli>(Clock::now() - started).count());
    }

    bool initialize()
    {
        if (handle) return true;
        try {
            for (const char *name : {"libmpv.so.2", "libmpv.2.dylib", "/opt/homebrew/lib/libmpv.2.dylib",
                                     "/usr/local/lib/libmpv.2.dylib"}) {
                library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
                if (library) break;
            }
            if (!library) throw std::runtime_error("libmpv is unavailable. Install libmpv2 on the device.");
#define LOAD(name) symbol(name##_fn, #name)
            LOAD(mpv_create);
            LOAD(mpv_initialize);
            LOAD(mpv_set_option_string);
            LOAD(mpv_command_async);
            LOAD(mpv_command_node_async);
            LOAD(mpv_observe_property);
            LOAD(mpv_wait_event);
            LOAD(mpv_error_string);
            LOAD(mpv_request_log_messages);
            LOAD(mpv_terminate_destroy);
            LOAD(mpv_render_context_create);
            LOAD(mpv_render_context_set_update_callback);
            LOAD(mpv_render_context_update);
            LOAD(mpv_render_context_render);
            LOAD(mpv_render_context_free);
#undef LOAD
            handle = mpv_create_fn();
            if (!handle) throw std::runtime_error("Cannot create player");
            option("config", "no");
            option("load-scripts", "no");
            option("vo", "libmpv");
            option("terminal", "no");
            option("input-default-bindings", "no");
            option("input-vo-keyboard", "no");
            option("osc", "no");
            option("osd-level", "0");
            // LVGL must remain responsive: render calls do not wait for presentation.
            option("video-timing-offset", "0");
#if defined(YOUTUBE_ENABLE_OPENGL)
            option("hwdec", opengl ? "v4l2m2m" : "v4l2m2m-copy");
#else
            option("hwdec", "auto-copy");
#endif
            if (opengl) {
                option("scale", "bilinear");
                option("dscale", "bilinear");
                option("cscale", "bilinear");
                option("dither-depth", "no");
                option("correct-downscaling", "no");
                option("linear-downscaling", "no");
                option("sigmoid-upscaling", "no");
                option("gpu-dumb-mode", "yes");
            }
            option("vd-lavc-threads", "2");
            option("demuxer-max-bytes", "16MiB");
            option("demuxer-max-back-bytes", "4MiB");
            option("cache-secs", "30");
            option("cache-pause", "yes");
            option("cache-pause-initial", "yes");
            option("cache-pause-wait", "3");
            option("network-timeout", "15");
            option("ytdl", "no");
            option("user-agent", "com.google.android.youtube/21.03.36 (Linux; U; Android 16; en_US) gzip");
            if (silent) option("ao", "null");
#if defined(YOUTUBE_DEVICE_ALSA)
            else {
                option("ao", "alsa");
                option("audio-device", "alsa/default");
                // Match the CM0 audio apps: ES8389's 19.2 MHz clock does not
                // support 44.1 kHz, so convert source audio before ALSA output.
                option("audio-samplerate", "48000");
                option("audio-format", "s16");
                option("audio-channels", "stereo");
            }
#endif
            int error = mpv_initialize_fn(handle);
            if (error < 0) throw std::runtime_error(mpv_error_string_fn(error));
            mpv_request_log_messages_fn(handle, "error");
            mpv_observe_property_fn(handle, 1, "time-pos", MPV_FORMAT_DOUBLE);
            mpv_observe_property_fn(handle, 2, "duration", MPV_FORMAT_DOUBLE);
            mpv_observe_property_fn(handle, 3, "pause", MPV_FORMAT_FLAG);
            mpv_observe_property_fn(handle, 4, "paused-for-cache", MPV_FORMAT_FLAG);
            mpv_observe_property_fn(handle, 5, "hwdec-current", MPV_FORMAT_STRING);
            mpv_observe_property_fn(handle, 6, "video-codec", MPV_FORMAT_STRING);
            mpv_observe_property_fn(handle, 7, "frame-drop-count", MPV_FORMAT_INT64);
            mpv_observe_property_fn(handle, 8, "decoder-frame-drop-count", MPV_FORMAT_INT64);
            if (debug) {
                mpv_observe_property_fn(handle, 9, "demuxer-cache-state", MPV_FORMAT_NODE);
                mpv_observe_property_fn(handle, 10, "avsync", MPV_FORMAT_DOUBLE);
                mpv_observe_property_fn(handle, 11, "video-params", MPV_FORMAT_NODE);
                mpv_observe_property_fn(handle, 12, "container-fps", MPV_FORMAT_DOUBLE);
                mpv_observe_property_fn(handle, 13, "cache-buffering-state", MPV_FORMAT_INT64);
                mpv_observe_property_fn(handle, 14, "current-ao", MPV_FORMAT_STRING);
                mpv_observe_property_fn(handle, 15, "audio-params/samplerate", MPV_FORMAT_INT64);
                mpv_observe_property_fn(handle, 16, "audio-out-params/samplerate", MPV_FORMAT_INT64);
                mpv_observe_property_fn(handle, 17, "audio-out-params/channel-count", MPV_FORMAT_INT64);
                mpv_observe_property_fn(handle, 18, "audio-out-params/format", MPV_FORMAT_STRING);
            }
            if (opengl) {
#if defined(YOUTUBE_ENABLE_OPENGL)
                egl_context = eglGetCurrentContext();
                if (egl_context == EGL_NO_CONTEXT) throw std::runtime_error("OpenGL video requires an active EGL display");
                GlState state;
                glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
                mpv_opengl_init_params init{gl_proc_address, nullptr};
                mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init},
                    {MPV_RENDER_PARAM_INVALID, nullptr}};
                error = mpv_render_context_create_fn(&context, handle, params);
                const auto *driver = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
                if (debug) std::fprintf(stderr, "[youtube-player] renderer=opengl-es driver=%.120s\n", driver ? driver : "unknown");
#else
                throw std::runtime_error("This build has no OpenGL video renderer");
#endif
            } else {
                mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
                                             {MPV_RENDER_PARAM_INVALID, nullptr}};
                error = mpv_render_context_create_fn(&context, handle, params);
                if (debug) std::fprintf(stderr, "[youtube-player] renderer=software\n");
            }
            if (error < 0) throw std::runtime_error(std::string("Video renderer: ") + mpv_error_string_fn(error));
            mpv_render_context_set_update_callback_fn(
                context, [](void *data) { static_cast<Impl *>(data)->dirty = true; }, this);
            return true;
        } catch (const std::exception &error) {
            playback.state = PlaybackState::Error;
            playback.error = error.what();
            release();
            return false;
        }
    }

    void command(std::initializer_list<std::string> args)
    {
        if (!handle) return;
        std::vector<const char *> argv;
        for (const auto &arg : args) argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        const int error = mpv_command_async_fn(handle, 0, argv.data());
        if (error < 0) {
            playback.state = PlaybackState::Error;
            playback.error = mpv_error_string_fn(error);
        }
    }
};

MpvPlayer::MpvPlayer(bool silent, bool opengl) : impl_(std::make_unique<Impl>(silent, opengl))
{
}
MpvPlayer::~MpvPlayer() = default;

void MpvPlayer::load(const std::string &url, const std::string &audio_url, int volume_value)
{
    if (!impl_->initialize()) return;
    impl_->playback       = {};
    impl_->playback.state = PlaybackState::Loading;
    impl_->loaded         = false;
    impl_->separate_audio = !audio_url.empty();
    impl_->network        = {};
    impl_->dirty          = true;
    impl_->rendered_frames = impl_->reported_frames = 0;
    impl_->dropped_frames = impl_->decoder_dropped_frames = 0;
    impl_->max_render_ms = 0;
    impl_->last_report = Clock::now();
    impl_->command({"set", "pause", "no"});
    volume(volume_value);
    // Pass external audio as a typed map so commas and URL query strings remain literal.
    mpv_node option{};
    option.format   = MPV_FORMAT_STRING;
    option.u.string = const_cast<char *>(audio_url.c_str());
    char *keys[]    = {const_cast<char *>("audio-file")};
    mpv_node_list options{audio_url.empty() ? 0 : 1, &option, keys};
    mpv_node args[5]{};
    const char *strings[] = {"loadfile", url.c_str(), "replace"};
    for (int i = 0; i < 3; ++i) {
        args[i].format   = MPV_FORMAT_STRING;
        args[i].u.string = const_cast<char *>(strings[i]);
    }
    args[3].format  = MPV_FORMAT_INT64;
    args[3].u.int64 = -1;
    args[4].format  = MPV_FORMAT_NODE_MAP;
    args[4].u.list  = &options;
    mpv_node_list arguments{5, args, nullptr};
    mpv_node command{};
    command.format  = MPV_FORMAT_NODE_ARRAY;
    command.u.list  = &arguments;
    const int error = impl_->mpv_command_node_async_fn(impl_->handle, 0, &command);
    if (error < 0) {
        impl_->playback.state = PlaybackState::Error;
        impl_->playback.error = impl_->mpv_error_string_fn(error);
    }
}

void MpvPlayer::stop()
{
    impl_->command({"stop"});
    impl_->loaded   = false;
    impl_->playback = {};
}

void MpvPlayer::toggle_pause()
{
    impl_->command({"cycle", "pause"});
}
void MpvPlayer::seek(double seconds)
{
    if (!std::isfinite(seconds)) return;
    impl_->command({"seek", std::to_string(std::max(0.0, seconds)), "absolute"});
}
void MpvPlayer::volume(int value)
{
    impl_->command({"set", "volume", std::to_string(std::clamp(value, 0, 100))});
}

const Playback &MpvPlayer::poll()
{
    auto &p = *impl_;
    if (!p.handle) return p.playback;
    for (int i = 0; i < 100; ++i) {
        const auto *event = p.mpv_wait_event_fn(p.handle, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_START_FILE) {
            p.loaded            = false;
            p.playback.state    = PlaybackState::Loading;
            p.playback.position = 0;
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            p.loaded = true;
            p.playback.error.clear();
            p.playback.state = p.paused ? PlaybackState::Paused : PlaybackState::Playing;
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                p.loaded         = false;
                p.playback.state = PlaybackState::Error;
                if (p.playback.error.empty()) p.playback.error = p.mpv_error_string_fn(end->error);
            } else if (end->reason == MPV_END_FILE_REASON_EOF) {
                p.loaded         = false;
                p.playback.state = PlaybackState::Ended;
            }
        } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *property = static_cast<mpv_event_property *>(event->data);
            if (p.debug && event->reply_userdata >= 14 && event->reply_userdata <= 18) {
                if (!property->data)
                    std::fprintf(stderr, "[youtube-audio] %s=unavailable\n", property->name);
                else if (property->format == MPV_FORMAT_STRING)
                    std::fprintf(stderr, "[youtube-audio] %s=%.100s\n", property->name,
                                 *static_cast<char **>(property->data));
                else if (property->format == MPV_FORMAT_INT64)
                    std::fprintf(stderr, "[youtube-audio] %s=%lld\n", property->name,
                                 static_cast<long long>(*static_cast<std::int64_t *>(property->data)));
            }
            if (!property->data) {
                if (event->reply_userdata == 9) {
                    p.network.seconds = p.network.bytes = p.network.bytes_per_second = -1;
                    p.network.idle = p.network.underrun = p.network.eof = -1;
                }
                if (event->reply_userdata == 10) p.network.avsync = NAN;
                if (event->reply_userdata == 11) p.network.width = p.network.height = -1;
                if (event->reply_userdata == 12) p.network.fps = -1;
                if (event->reply_userdata == 13) p.network.percent = -1;
                continue;
            }
            if (event->reply_userdata == 1) p.playback.position = *static_cast<double *>(property->data);
            if (event->reply_userdata == 2) p.playback.duration = *static_cast<double *>(property->data);
            if (event->reply_userdata == 3) {
                p.paused = *static_cast<int *>(property->data) != 0;
                if (p.loaded) p.playback.state = p.paused ? PlaybackState::Paused : PlaybackState::Playing;
            }
            if (event->reply_userdata == 4) {
                const bool buffering = *static_cast<int *>(property->data) != 0;
                if (p.debug && buffering != p.playback.buffering)
                    std::fprintf(stderr, "[youtube-cache] buffering=%d position=%.2f\n", buffering, p.playback.position);
                p.playback.buffering = buffering;
            }
            if (event->reply_userdata == 5 || event->reply_userdata == 6) {
                const char *value = *static_cast<char **>(property->data);
                auto &previous = event->reply_userdata == 5 ? p.hwdec : p.decoder;
                const std::string current = value ? value : "unknown";
                if (current != previous) {
                    previous = current;
                    if (p.debug)
                        std::fprintf(stderr, "[youtube-player] %s=%.200s\n",
                                     event->reply_userdata == 5 ? "hwdec" : "decoder", current.c_str());
                }
            }
            if (event->reply_userdata == 7) p.dropped_frames = *static_cast<std::int64_t *>(property->data);
            if (event->reply_userdata == 8) p.decoder_dropped_frames = *static_cast<std::int64_t *>(property->data);
            if (event->reply_userdata == 9) {
                const auto *state = static_cast<mpv_node *>(property->data);
                p.network.seconds = node_number(state, "cache-duration");
                p.network.bytes = node_number(state, "fw-bytes");
                p.network.bytes_per_second = node_number(state, "raw-input-rate");
                p.network.idle = node_number(state, "idle");
                p.network.underrun = node_number(state, "underrun");
                p.network.eof = node_number(state, "eof");
            }
            if (event->reply_userdata == 10) p.network.avsync = *static_cast<double *>(property->data);
            if (event->reply_userdata == 11) {
                const auto *state = static_cast<mpv_node *>(property->data);
                p.network.width = node_number(state, "w");
                p.network.height = node_number(state, "h");
            }
            if (event->reply_userdata == 12) p.network.fps = *static_cast<double *>(property->data);
            if (event->reply_userdata == 13) p.network.percent = *static_cast<std::int64_t *>(property->data);
        } else if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto *log = static_cast<mpv_event_log_message *>(event->data);
            if (log && log->text) p.playback.error = std::string(log->text).substr(0, 500);
        } else if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0) {
            p.playback.error = p.mpv_error_string_fn(event->error);
        }
    }
    p.report();
    return p.playback;
}

bool MpvPlayer::render(std::vector<std::uint8_t> &pixels, int width, int height)
{
    auto &p = *impl_;
    if (!p.context || p.opengl || width < 1 || height < 1 || width > 640 || height > 360 || !p.dirty.exchange(false))
        return false;
    if (!(p.mpv_render_context_update_fn(p.context) & MPV_RENDER_UPDATE_FRAME)) return false;
    const auto started = Clock::now();
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    int size[]                = {width, height};
    std::size_t stride        = static_cast<std::size_t>(width) * 4;
    int block                 = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, size},
                                 {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>("bgr0")},
                                 {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                                 {MPV_RENDER_PARAM_SW_POINTER, pixels.data()},
                                 {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int error           = p.mpv_render_context_render_fn(p.context, params);
    if (error < 0) {
        p.playback.state = PlaybackState::Error;
        p.playback.error = p.mpv_error_string_fn(error);
        return false;
    }
    p.rendered(started);
    return true;
}

bool MpvPlayer::render_texture(VideoTexture &output, int width, int height)
{
#if defined(YOUTUBE_ENABLE_OPENGL)
    auto &p = *impl_;
    if (!p.context || !p.opengl || width < 1 || height < 1 || width > 1920 || height > 1080 ||
        width > p.maximum_texture_size || height > p.maximum_texture_size)
        return false;
    if (eglGetCurrentContext() != p.egl_context) {
        p.playback.state = PlaybackState::Error;
        p.playback.error = "The video OpenGL context is no longer current";
        return false;
    }
    const bool resize = width != p.texture_width || height != p.texture_height;
    const bool updated = p.dirty.exchange(false);
    if (!updated && !resize) return false;
    const auto started = Clock::now();
    GlState state;
    const bool frame = p.mpv_render_context_update_fn(p.context) & MPV_RENDER_UPDATE_FRAME;
    if (!frame && !resize) return false;
    if (resize) {
        GLuint texture = 0, framebuffer = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (!texture || !framebuffer || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
            if (texture) glDeleteTextures(1, &texture);
            p.playback.state = PlaybackState::Error;
            p.playback.error = "Cannot allocate the video OpenGL framebuffer";
            return false;
        }
        state.forget_framebuffer(p.framebuffer);
        state.forget_texture(p.texture);
        if (p.framebuffer) glDeleteFramebuffers(1, &p.framebuffer);
        if (p.texture) glDeleteTextures(1, &p.texture);
        p.texture = texture;
        p.framebuffer = framebuffer;
        p.texture_width = width;
        p.texture_height = height;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (p.debug) std::fprintf(stderr, "[youtube-player] target=%dx%d\n", width, height);
    }
    mpv_opengl_fbo target{static_cast<int>(p.framebuffer), width, height, 0};
    int flip = 0, block = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &target},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                 {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int error = p.mpv_render_context_render_fn(p.context, params);
    if (error < 0) {
        p.playback.state = PlaybackState::Error;
        p.playback.error = p.mpv_error_string_fn(error);
        return false;
    }
    output = {p.texture, width, height};
    p.rendered(started);
    return true;
#else
    (void)output;
    (void)width;
    (void)height;
    return false;
#endif
}
