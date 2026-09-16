#include "services/player.hpp"
#include "services/http.hpp"
#include "pages/youtube/youtube_model.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <sys/resource.h>

int main(int argc, char **argv)
{
    bool audio_enabled = false;
    bool positional_only = false;
    std::string audio_file;
    std::vector<std::string> positional;
    const auto print_usage = [] {
        std::fprintf(stderr,
                     "Usage: youtube-player-smoke [--audio] [--audio-file PATH] <media path, URL or YouTube ID> [quality]\n");
        return 2;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (!positional_only && argument == "--") {
            positional_only = true;
        } else if (!positional_only && argument == "--audio") {
            audio_enabled = true;
        } else if (!positional_only && argument == "--audio-file") {
            if (++i == argc || !argv[i][0]) return print_usage();
            audio_file = argv[i];
        } else if (!positional_only && argument.rfind("--", 0) == 0) {
            return print_usage();
        } else {
            positional.push_back(argument);
        }
    }
    if (positional.empty() || positional.size() > 2) return print_usage();
    std::printf("Audio output: %s\n", audio_enabled ? "enabled (volume 70)" : "silent (null output)");
    MpvPlayer player(!audio_enabled);
    const int level = audio_enabled ? 70 : 0;
    const std::string &input = positional.front();
    if (valid_video_id(input)) {
        std::atomic_bool cancel{false};
        try {
            auto stream = parse_player_response(youtube_request("player", player_request(input), cancel),
                                                positional.size() == 2 ? std::atoi(positional[1].c_str()) : 360);
            player.load(stream.url, audio_file.empty() ? stream.audio_url : audio_file, level);
        } catch (const std::exception &error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    } else
        player.load(input, audio_file, level);
    std::vector<std::uint8_t> pixels;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    int frames          = 0;
    bool advancing      = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto &state = player.poll();
        if (state.state == PlaybackState::Error) {
            std::fprintf(stderr, "%s\n", state.error.c_str());
            return 1;
        }
        if (player.render(pixels, 640, 360)) {
            if (std::any_of(pixels.begin(), pixels.end(), [](auto value) { return value > 32; })) ++frames;
        }
        advancing = advancing || state.position > 1;
        if (frames >= 10 && advancing) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (frames < 10 || !advancing) {
        std::fprintf(stderr, "No moving video: %d frames\n", frames);
        return 1;
    }
    player.toggle_pause();
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (player.poll().state != PlaybackState::Paused && std::chrono::steady_clock::now() < until) {
        player.render(pixels, 640, 360);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (player.poll().state != PlaybackState::Paused) return 1;
    player.seek(0);
    player.toggle_pause();
    until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (player.poll().state != PlaybackState::Playing && std::chrono::steady_clock::now() < until) {
        player.render(pixels, 640, 360);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (player.poll().state != PlaybackState::Playing) return 1;
    player.stop();
    std::printf("PASS: %d nonblank frames, advancing clock, pause, seek and resume\n", frames);
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    std::printf("Peak RSS: %.1f MiB\n", usage.ru_maxrss / 1048576.0);
#else
    std::printf("Peak RSS: %.1f MiB\n", usage.ru_maxrss / 1024.0);
#endif
}
