#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Video {
    std::string id;
    std::string title;
    std::string channel;
    int duration = 0;
    std::vector<std::uint8_t> thumbnail;
};

struct YoutubeModel {
    std::string query;
    std::string status = "Ready";
    std::vector<Video> videos;
    bool searching     = false;
    bool recommending  = true;
    bool can_load_more = true;
    bool watching      = false;
    std::string playing_title;
    int quality = 720;
    int volume  = 70;
};

bool valid_video_id(const std::string &id);
std::vector<Video> parse_search_results(const std::string &json);
std::string format_duration(double seconds);
struct MediaStream {
    std::string url;
    std::string audio_url;
    std::string title;
    int height = 0;
};
std::string search_request(const std::string &query);
std::string player_request(const std::string &id);
MediaStream parse_player_response(const std::string &json, int height);
