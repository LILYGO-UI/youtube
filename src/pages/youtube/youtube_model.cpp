#include "pages/youtube/youtube_model.hpp"
#include <json-c/json.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
json_object *get(json_object *object, const char *key)
{
    json_object *value = nullptr;
    if (object && json_object_is_type(object, json_type_object)) json_object_object_get_ex(object, key, &value);
    return value;
}
std::string string(json_object *value)
{
    return value && json_object_is_type(value, json_type_string) ? json_object_get_string(value) : "";
}
std::string field(json_object *object, const char *key)
{
    return string(get(object, key));
}
std::string text(json_object *value)
{
    auto simple = field(value, "simpleText");
    if (!simple.empty()) return simple;
    auto *runs = get(value, "runs");
    if (!json_object_is_type(runs, json_type_array)) return "";
    for (std::size_t i = 0; i < json_object_array_length(runs); ++i)
        simple += field(json_object_array_get_idx(runs, i), "text");
    return simple;
}
Json parse(const std::string &json)
{
    std::unique_ptr<json_tokener, decltype(&json_tokener_free)> parser(json_tokener_new_ex(128), json_tokener_free);
    if (!parser) throw std::runtime_error("Cannot allocate JSON parser");
    Json root(json_tokener_parse_ex(parser.get(), json.data(), static_cast<int>(json.size())), json_object_put);
    if (!root || !json_object_is_type(root.get(), json_type_object))
        throw std::runtime_error("Invalid YouTube response");
    if (auto *error = get(root.get(), "error")) throw std::runtime_error(field(error, "message"));
    return root;
}
int duration(const std::string &value)
{
    int total           = 0;
    const char *current = value.data(), *end = current + value.size();
    while (current < end) {
        int part    = 0;
        auto result = std::from_chars(current, end, part);
        if (result.ec != std::errc() || part < 0 || part > 100000 || total > 1000000) return 0;
        total = total * 60 + part;
        if (result.ptr == end) return total;
        if (*result.ptr != ':') return 0;
        current = result.ptr + 1;
    }
    return 0;
}
void collect(json_object *node, std::vector<Video> &videos, int depth)
{
    if (!node || depth > 32 || videos.size() >= 12) return;
    if (auto *renderer = get(node, "videoRenderer")) {
        Video video;
        video.id      = field(renderer, "videoId");
        video.title   = text(get(renderer, "title"));
        video.channel = text(get(renderer, "ownerText"));
        if (video.channel.empty()) video.channel = text(get(renderer, "longBylineText"));
        video.duration = duration(text(get(renderer, "lengthText")));
        if (valid_video_id(video.id) && !video.title.empty() &&
            std::none_of(videos.begin(), videos.end(), [&](const Video &other) { return video.id == other.id; }))
            videos.push_back(std::move(video));
        return;
    }
    if (json_object_is_type(node, json_type_array)) {
        for (std::size_t i = 0; i < json_object_array_length(node); ++i)
            collect(json_object_array_get_idx(node, i), videos, depth + 1);
    } else if (json_object_is_type(node, json_type_object)) {
        json_object_object_foreach(node, key, value)
        {
            (void)key;
            collect(value, videos, depth + 1);
        }
    }
}
Json request(bool android)
{
    auto root = parse(
        android
            ? R"({"context":{"client":{"clientName":"ANDROID","clientVersion":"21.03.36","androidSdkVersion":36,"hl":"en","gl":"US"}},"contentCheckOk":true,"racyCheckOk":true})"
            : R"({"context":{"client":{"clientName":"WEB","clientVersion":"2.20260623.01.00","hl":"en","gl":"US"}},"params":"EgIQAQ=="})");
    return root;
}
bool media_url(const std::string &url)
{
    if (url.rfind("https://", 0) != 0 || url.find_first_of("\r\n") != std::string::npos) return false;
    const auto end           = url.find('/', 8);
    const auto host          = url.substr(8, end == std::string::npos ? end : end - 8);
    const std::string suffix = ".googlevideo.com";
    return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

bool valid_video_id(const std::string &id)
{
    return id.size() == 11 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                      c == '_';
           });
}

std::string search_request(const std::string &query)
{
    auto root = request(false);
    json_object_object_add(root.get(), "query", json_object_new_string_len(query.data(), query.size()));
    return json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
}

std::string player_request(const std::string &id)
{
    if (!valid_video_id(id)) throw std::runtime_error("Invalid video ID");
    auto root = request(true);
    json_object_object_add(root.get(), "videoId", json_object_new_string(id.c_str()));
    return json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
}

std::vector<Video> parse_search_results(const std::string &json)
{
    auto root      = parse(json);
    auto *contents = get(root.get(), "contents");
    if (!contents) throw std::runtime_error("Search response has no contents");
    std::vector<Video> videos;
    collect(contents, videos, 0);
    return videos;
}

MediaStream parse_player_response(const std::string &json, int height)
{
    if (height != 240 && height != 360 && height != 480 && height != 720 && height != 1080) height = 720;
    auto root    = parse(json);
    auto *status = get(root.get(), "playabilityStatus");
    if (field(status, "status") != "OK") {
        auto reason = field(status, "reason");
        throw std::runtime_error(reason.empty() ? "This video is unavailable" : reason);
    }
    auto *data          = get(root.get(), "streamingData");
    json_object *chosen = nullptr, *audio = nullptr;
    int best_height = 0;
    bool combined   = false;
    for (const char *key : {"formats", "adaptiveFormats"}) {
        auto *formats = get(data, key);
        if (!json_object_is_type(formats, json_type_array)) continue;
        for (std::size_t i = 0; i < json_object_array_length(formats); ++i) {
            auto *format = json_object_array_get_idx(formats, i);
            if (!media_url(field(format, "url"))) continue;
            const auto mime = field(format, "mimeType");
            auto *width     = get(format, "width");
            const int w     = json_object_get_int(width);
            const int h     = json_object_get_int(get(format, "height"));
            const int fps   = json_object_get_int(get(format, "fps"));
            const bool av   = mime.find("mp4a") != std::string::npos;
            if (mime.find("avc1") != std::string::npos && h > 0 && h <= height && fps <= 30 &&
                (!width || (w > 0 && w <= 1920)) &&
                (h > best_height || (h == best_height && av && !combined))) {
                chosen      = format;
                best_height = h;
                combined    = av;
            }
            if (mime.rfind("audio/mp4", 0) == 0 &&
                (!audio || json_object_get_int(get(format, "bitrate")) < json_object_get_int(get(audio, "bitrate"))))
                audio = format;
        }
    }
    if (!chosen)
        throw std::runtime_error(
            "No direct H.264 stream at this quality. The video may require additional signature verification.");
    if (!combined && !audio) throw std::runtime_error("No compatible audio stream");
    return {field(chosen, "url"), combined ? "" : field(audio, "url"), field(get(root.get(), "videoDetails"), "title"),
            best_height};
}

std::string format_duration(double seconds)
{
    const int total = std::isfinite(seconds) ? static_cast<int>(std::clamp(seconds, 0.0, 2147483646.0)) : 0;
    char output[40];
    if (total >= 3600)
        std::snprintf(output, sizeof(output), "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    else
        std::snprintf(output, sizeof(output), "%d:%02d", total / 60, total % 60);
    return output;
}
