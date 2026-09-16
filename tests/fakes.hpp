#pragma once
#include "pages/youtube/youtube_view_model.hpp"

class FakeSearch : public SearchService {
public:
    std::string query;
    bool cancelled = false;
    std::string video_id;
    int quality              = 0;
    std::size_t batch        = 0;
    int recommendation_calls = 0;
    std::vector<std::string> thumbnail_ids;
    int thumbnail_calls = 0;
    std::deque<SearchUpdate> updates;
    void search(const std::string &value) override
    {
        query     = value;
        cancelled = false;
        updates.clear();
    }
    void recommend(std::size_t value) override
    {
        batch = value;
        ++recommendation_calls;
        cancelled = false;
        updates.clear();
    }
    void thumbnails(const std::vector<std::string> &ids) override
    {
        thumbnail_ids = ids;
        ++thumbnail_calls;
        cancelled = false;
    }
    void cancel() override
    {
        cancelled = true;
        updates.clear();
    }
    void resolve(const std::string &id, int height) override
    {
        video_id = id;
        quality  = height;
        SearchUpdate update;
        update.kind   = SearchUpdate::Kind::Resolved;
        update.stream = {"https://test.googlevideo.com/video", "", "Test", height};
        updates.clear();
        updates.push_back(std::move(update));
    }
    std::optional<SearchUpdate> poll() override
    {
        if (updates.empty()) return {};
        auto update = std::move(updates.front());
        updates.pop_front();
        return update;
    }
};

class FakePlayer : public Player {
public:
    Playback state;
    std::string url;
    int level    = 70;
    bool stopped = false;
    void load(const std::string &value, const std::string &, int volume) override
    {
        url         = value;
        level       = volume;
        state       = {};
        state.state = PlaybackState::Loading;
    }
    void stop() override
    {
        stopped = true;
        state   = {};
    }
    void toggle_pause() override
    {
        state.state = state.state == PlaybackState::Paused ? PlaybackState::Playing : PlaybackState::Paused;
    }
    void seek(double seconds) override
    {
        state.position = seconds;
    }
    void volume(int value) override
    {
        level = value;
    }
    const Playback &poll() override
    {
        return state;
    }
    bool render(std::vector<std::uint8_t> &, int, int) override
    {
        return false;
    }
};

inline SearchUpdate sample_results()
{
    SearchUpdate update;
    update.videos = {
        {"jNQXAC9IVRw", "A longer video title that wraps across the search result", "Example channel", 136, {}},
        {"aqz-KE-bpKQ", "Open movie - playback sample", "Blender Foundation", 596, {}}};
    return update;
}
