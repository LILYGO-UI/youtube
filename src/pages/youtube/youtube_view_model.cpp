#include "pages/youtube/youtube_view_model.hpp"
#include <algorithm>
#include <cmath>

YoutubeViewModel::YoutubeViewModel(std::unique_ptr<SearchService> search, std::unique_ptr<Player> player)
    : search_(std::move(search)), player_(std::move(player))
{
}
YoutubeViewModel::~YoutubeViewModel()
{
    search_->cancel();
    player_->stop();
}
void YoutubeViewModel::publish()
{
    revision_.set(revision_.value() == INT32_MAX ? 0 : revision_.value() + 1);
}

void YoutubeViewModel::recommendations()
{
    if (model_.watching) return;
    model_.query.clear();
    model_.recommending  = true;
    model_.can_load_more = true;
    model_.searching     = true;
    model_.status        = "Loading recommendations...";
    model_.videos.clear();
    appending_ = false;
    ++results_generation_;
    search_->recommend(next_batch_);
    publish();
}

void YoutubeViewModel::load_more()
{
    if (!model_.recommending || !model_.can_load_more || model_.searching || model_.watching) return;
    model_.searching = true;
    model_.status    = "Loading more...";
    appending_       = true;
    search_->recommend(next_batch_);
    publish();
}

void YoutubeViewModel::search(const std::string &query)
{
    if (model_.watching) return;
    const auto first = query.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        model_.status = "Enter a search";
        publish();
        return;
    }
    const auto trimmed = query.substr(first, query.find_last_not_of(" \t\n\r") - first + 1);
    if (trimmed.size() > 512) {
        model_.status = "Search is too long";
        publish();
        return;
    }
    // Keep a short, most-recent-first history for the search page. Repeating a
    // term moves it to the front instead of creating duplicate entries.
    model_.search_history.erase(std::remove(model_.search_history.begin(), model_.search_history.end(), trimmed),
                                model_.search_history.end());
    model_.search_history.insert(model_.search_history.begin(), trimmed);
    if (model_.search_history.size() > 12) model_.search_history.resize(12);
    model_.query         = trimmed;
    model_.recommending  = false;
    model_.can_load_more = false;
    appending_           = false;
    model_.searching     = true;
    model_.status        = "Searching...";
    model_.videos.clear();
    ++results_generation_;
    search_->search(model_.query);
    publish();
}

void YoutubeViewModel::cancel_search()
{
    search_->cancel();
    model_.searching = false;
    model_.status    = model_.recommending ? "Loading cancelled" : "Search cancelled";
    appending_       = false;
    publish();
}

void YoutubeViewModel::play(std::size_t index)
{
    if (index >= model_.videos.size()) return;
    const auto &video = model_.videos[index];
    open_video(video.id, video.title);
}

void YoutubeViewModel::open_video(const std::string &id, const std::string &title)
{
    if (!valid_video_id(id)) return;
    model_.playing_title = title.empty() ? "YouTube" : title;
    current_id_          = id;
    model_.watching      = true;
    model_.searching     = false;
    appending_           = false;
    playback_       = {};
    playback_.state = PlaybackState::Loading;
    player_->stop();
    resolving_ = true;
    search_->resolve(current_id_, model_.quality);
    publish();
}

void YoutubeViewModel::open_media(const std::string &path)
{
    search_->cancel();
    model_.searching = false;
    appending_       = false;
    current_url_     = path;
    current_id_.clear();
    resolving_           = false;
    model_.playing_title = "Local playback";
    model_.watching      = true;
    playback_            = {};
    playback_.state      = PlaybackState::Loading;
    player_->load(path, "", model_.volume);
    publish();
}

void YoutubeViewModel::back()
{
    search_->cancel();
    resolving_ = false;
    player_->stop();
    model_.watching  = false;
    model_.searching = false;
    playback_        = {};
    load_thumbnails();
    publish();
}

void YoutubeViewModel::load_thumbnails()
{
    std::vector<std::string> missing;
    for (const auto &video : model_.videos)
        if (video.thumbnail.empty() && valid_video_id(video.id)) missing.push_back(video.id);
    if (!missing.empty()) search_->thumbnails(missing);
}

void YoutubeViewModel::toggle_pause()
{
    if (playback_.state == PlaybackState::Ended || playback_.state == PlaybackState::Error) {
        if (!current_id_.empty()) {
            resolving_ = true;
            search_->resolve(current_id_, model_.quality);
        } else
            player_->load(current_url_, "", model_.volume);
        playback_       = {};
        playback_.state = PlaybackState::Loading;
        publish();
    } else if (playback_.state == PlaybackState::Playing || playback_.state == PlaybackState::Paused) {
        player_->toggle_pause();
    }
}

void YoutubeViewModel::seek(double seconds)
{
    if (playback_.duration > 0 && std::isfinite(seconds)) player_->seek(std::clamp(seconds, 0.0, playback_.duration));
}
void YoutubeViewModel::volume(int value)
{
    model_.volume = std::clamp(value, 0, 100);
    player_->volume(model_.volume);
    publish();
}
void YoutubeViewModel::quality(int height)
{
    if (height != 240 && height != 360 && height != 480 && height != 720 && height != 1080) return;
    if (model_.quality == height) return;
    model_.quality = height;
    publish();
}
void YoutubeViewModel::poll()
{
    bool changed = false;
    while (auto update = search_->poll()) {
        if (update->kind == SearchUpdate::Kind::Results) {
            if (appending_) {
                for (auto &video : update->videos) {
                    if (model_.videos.size() >= 48) break;
                    if (std::none_of(model_.videos.begin(), model_.videos.end(),
                                     [&](const Video &existing) { return existing.id == video.id; }))
                        model_.videos.push_back(std::move(video));
                }
            } else {
                model_.videos = std::move(update->videos);
                ++results_generation_;
            }
            if (model_.recommending) {
                ++next_batch_;
                model_.can_load_more = model_.videos.size() < 48;
            }
            appending_       = false;
            model_.searching = false;
            model_.status    = model_.videos.empty()
                                   ? (model_.recommending ? "No recommendations available" : "No results")
                                   : "";
            load_thumbnails();
        } else if (update->kind == SearchUpdate::Kind::Error) {
            if (resolving_) {
                resolving_      = false;
                playback_.state = PlaybackState::Error;
                playback_.error = update->error;
            } else {
                model_.searching = false;
                appending_       = false;
                model_.status    = update->error;
            }
        } else if (update->kind == SearchUpdate::Kind::Resolved) {
            resolving_ = false;
            player_->load(update->stream.url, update->stream.audio_url, model_.volume);
        } else {
            for (auto &video : model_.videos)
                if (video.id == update->video_id) video.thumbnail = std::move(update->pixels);
        }
        changed = true;
    }
    const auto &next = player_->poll();
    if (resolving_ || (playback_.state == PlaybackState::Error && next.state == PlaybackState::Idle)) {
        if (changed) publish();
        return;
    }
    if (next.state != playback_.state || next.buffering != playback_.buffering || next.error != playback_.error ||
        static_cast<int>(next.position) != static_cast<int>(playback_.position) || next.duration != playback_.duration)
        changed = true;
    playback_ = next;
    if (changed) publish();
}

bool YoutubeViewModel::render(std::vector<std::uint8_t> &pixels)
{
    return model_.watching && player_->render(pixels, 640, 360);
}

bool YoutubeViewModel::render_texture(VideoTexture &texture, int width, int height)
{
    return model_.watching && player_->render_texture(texture, width, height);
}
