#pragma once

#include "components/lv_subject.hpp"
#include "pages/youtube/youtube_model.hpp"
#include "services/player.hpp"
#include "services/youtube_service.hpp"

class YoutubeViewModel {
public:
    YoutubeViewModel(std::unique_ptr<SearchService> search, std::unique_ptr<Player> player);
    ~YoutubeViewModel();
    void recommendations();
    void load_more();
    void search(const std::string &query);
    void cancel_search();
    void play(std::size_t index);
    void open_video(const std::string &id, const std::string &title = "YouTube");
    void open_media(const std::string &path);
    void back();
    void toggle_pause();
    void seek(double seconds);
    void volume(int value);
    void quality(int height);
    void poll();
    bool render(std::vector<std::uint8_t> &pixels);
    bool render_texture(VideoTexture &texture, int width, int height);
    const YoutubeModel &model() const
    {
        return model_;
    }
    const Playback &playback() const
    {
        return playback_;
    }
    int results_generation() const
    {
        return results_generation_;
    }
    lv_subject_t *revision()
    {
        return revision_.get();
    }

private:
    void publish();
    void load_thumbnails();
    YoutubeModel model_;
    Playback playback_;
    std::unique_ptr<SearchService> search_;
    std::unique_ptr<Player> player_;
    IntSubject revision_{0};
    int results_generation_ = 0;
    std::string current_url_;
    bool resolving_         = false;
    bool appending_         = false;
    std::size_t next_batch_ = 0;
    std::string current_id_;
};
