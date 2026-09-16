#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class PlaybackState { Idle, Loading, Playing, Paused, Ended, Error };

struct Playback {
    PlaybackState state = PlaybackState::Idle;
    double position     = 0;
    double duration     = 0;
    bool buffering      = false;
    std::string error;
};

struct VideoTexture {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
};

class Player {
public:
    virtual ~Player()                                                                   = default;
    virtual void load(const std::string &url, const std::string &audio_url, int volume) = 0;
    virtual void stop()                                                                 = 0;
    virtual void toggle_pause()                                                         = 0;
    virtual void seek(double seconds)                                                   = 0;
    virtual void volume(int value)                                                      = 0;
    virtual const Playback &poll()                                                      = 0;
    virtual bool render(std::vector<std::uint8_t> &pixels, int width, int height)       = 0;
    virtual bool render_texture(VideoTexture &, int, int) { return false; }
};

class MpvPlayer final : public Player {
public:
    explicit MpvPlayer(bool silent = false, bool opengl = false);
    ~MpvPlayer() override;
    void load(const std::string &url, const std::string &audio_url, int volume) override;
    void stop() override;
    void toggle_pause() override;
    void seek(double seconds) override;
    void volume(int value) override;
    const Playback &poll() override;
    bool render(std::vector<std::uint8_t> &pixels, int width, int height) override;
    bool render_texture(VideoTexture &texture, int width, int height) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
