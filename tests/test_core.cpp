#include "pages/youtube/youtube_model.hpp"
#include "services/process.hpp"
#include <cassert>
#include <future>
#include <thread>

int main()
{
    assert(valid_video_id("jNQXAC9IVRw"));
    assert(!valid_video_id("../badvalue"));
    assert(!valid_video_id("jNQXAC9IVR;"));
    const auto videos = parse_search_results(
        R"({"contents":[null,{"videoRenderer":{"videoId":"invalid"}},{"videoRenderer":{"videoId":"jNQXAC9IVRw","title":{"runs":[{"text":"A & B"}]},"ownerText":{"runs":[{"text":"Channel"}]},"lengthText":{"simpleText":"1:31"}}}]})");
    assert(videos.size() == 1 && videos[0].duration == 91 && videos[0].channel == "Channel");
    assert(parse_search_results(R"({"contents":[]})").empty());
    std::string nested = R"({"contents":[],"metadata":)";
    nested.append(48, '[');
    nested += "0";
    nested.append(48, ']');
    nested += "}";
    assert(parse_search_results(nested).empty());
    bool failed = false;
    try {
        parse_search_results("{}");
    } catch (...) {
        failed = true;
    }
    assert(failed);
    assert(format_duration(-1) == "0:00");
    assert(format_duration(3661) == "1:01:01");
    const auto stream = parse_player_response(
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"formats":[{"height":720,"mimeType":"video/mp4; codecs=avc1,mp4a","url":"https://test.googlevideo.com/720"},{"height":360,"mimeType":"video/mp4; codecs=avc1,mp4a","url":"https://test.googlevideo.com/360"}]}})",
        360);
    assert(stream.height == 360 && stream.audio_url.empty());
    const auto separate = parse_player_response(
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"adaptiveFormats":[{"height":240,"mimeType":"video/mp4; codecs=avc1","url":"https://test.googlevideo.com/video"},{"mimeType":"audio/mp4; codecs=mp4a","url":"https://test.googlevideo.com/audio","bitrate":48000}]}})",
        240);
    assert(separate.height == 240 && separate.audio_url == "https://test.googlevideo.com/audio");
    const std::string hd_response =
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"adaptiveFormats":[{"height":2160,"fps":30,"mimeType":"video/mp4; codecs=avc1","url":"https://test.googlevideo.com/4k"},{"height":1080,"fps":60,"mimeType":"video/mp4; codecs=avc1","url":"https://test.googlevideo.com/60fps"},{"height":1080,"fps":30,"mimeType":"video/webm; codecs=vp9","url":"https://test.googlevideo.com/vp9"},{"height":1080,"fps":30,"mimeType":"video/mp4; codecs=avc1","url":"https://test.googlevideo.com/1080"},{"height":720,"fps":30,"mimeType":"video/mp4; codecs=avc1","url":"https://test.googlevideo.com/720"},{"mimeType":"audio/mp4; codecs=mp4a","url":"https://test.googlevideo.com/audio","bitrate":128000}]}})";
    const auto full_hd = parse_player_response(hd_response, 1080);
    assert(full_hd.height == 1080 && full_hd.url == "https://test.googlevideo.com/1080");
    assert(full_hd.audio_url == "https://test.googlevideo.com/audio");
    const auto hd = parse_player_response(hd_response, 720);
    assert(hd.height == 720 && hd.url == "https://test.googlevideo.com/720");
    const auto bounded_width = parse_player_response(
        R"({"playabilityStatus":{"status":"OK"},"streamingData":{"formats":[{"width":2560,"height":1080,"fps":30,"mimeType":"video/mp4; codecs=avc1,mp4a","url":"https://test.googlevideo.com/ultrawide"},{"width":1920,"height":1080,"fps":30,"mimeType":"video/mp4; codecs=avc1,mp4a","url":"https://test.googlevideo.com/full-hd"}]}})",
        1080);
    assert(bounded_width.height == 1080 && bounded_width.url == "https://test.googlevideo.com/full-hd");
    failed = false;
    try {
        parse_player_response(
            R"({"playabilityStatus":{"status":"OK"},"streamingData":{"formats":[{"height":360,"mimeType":"video/mp4; codecs=avc1,mp4a","signatureCipher":"s=secret"}]}})",
            360);
    } catch (...) {
        failed = true;
    }
    assert(failed);
    std::atomic_bool cancel{false};
    auto result = run_process({"/usr/bin/printf", "%s", "$(touch /tmp/should-not-exist); 'quoted'"}, cancel,
                              std::chrono::seconds(2));
    assert(result.code == 0 && result.output == "$(touch /tmp/should-not-exist); 'quoted'");
    result = run_process({"/not/a/real/program"}, cancel, std::chrono::seconds(1));
    assert(result.code != 0 && !result.error.empty());
    result = run_process({"/usr/bin/printf", "123456"}, cancel, std::chrono::seconds(2), 3);
    assert(result.code != 0 && result.output.size() == 3);
    result = run_process({"/bin/sleep", "10"}, cancel, std::chrono::seconds(1));
    assert(result.code != 0 && result.error == "Request timed out");
    auto future = std::async(std::launch::async,
                             [&] { return run_process({"/bin/sleep", "10"}, cancel, std::chrono::seconds(15)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel = true;
    assert(future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(future.get().code != 0);
}
