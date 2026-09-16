#include "fakes.hpp"
#include <cassert>

static SearchUpdate numbered_results(std::size_t first, std::size_t count)
{
    SearchUpdate update;
    for (std::size_t number = first; number < first + count; ++number) {
        const auto value = std::to_string(number);
        update.videos.push_back(
            {std::string(11 - value.size(), '0') + value, "Recommendation " + value, "Test channel", 120, {}});
    }
    return update;
}

int main()
{
    lv_init();
    {
        auto search   = std::make_unique<FakeSearch>();
        auto player   = std::make_unique<FakePlayer>();
        auto *service = search.get();
        auto *backend = player.get();
        YoutubeViewModel vm(std::move(search), std::move(player));
        assert(vm.model().quality == 720);
        vm.quality(1080);
        vm.quality(2160);
        assert(vm.model().quality == 1080);
        vm.search("   ");
        assert(!vm.model().searching);
        vm.search("  music  ");
        assert(service->query == "music" && vm.model().searching);
        service->updates.push_back(sample_results());
        vm.poll();
        assert(vm.model().videos.size() == 2 && !vm.model().searching);
        vm.play(0);
        vm.poll();
        assert(backend->url == "https://test.googlevideo.com/video" && service->quality == 1080);
        assert(service->video_id == vm.model().videos[0].id);
        assert(vm.model().playing_title == vm.model().videos[0].title);
        assert(vm.model().watching);
        backend->state.state    = PlaybackState::Playing;
        backend->state.duration = 100;
        vm.poll();
        vm.toggle_pause();
        vm.poll();
        assert(vm.playback().state == PlaybackState::Paused);
        vm.seek(1000);
        vm.poll();
        assert(vm.playback().position == 100);
        vm.volume(150);
        assert(backend->level == 100);
        backend->state.state = PlaybackState::Error;
        vm.poll();
        vm.toggle_pause();
        vm.poll();
        assert(backend->state.state == PlaybackState::Loading);
        vm.back();
        assert(backend->stopped && !vm.model().watching && vm.model().videos.size() == 2);
        vm.search("new");
        assert(vm.model().videos.empty());
        vm.cancel_search();
        assert(service->cancelled && !vm.model().searching);
        vm.play(100);
        assert(!vm.model().watching);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto player   = std::make_unique<FakePlayer>();
        auto *service = search.get();
        auto *backend = player.get();
        YoutubeViewModel vm(std::move(search), std::move(player));
        vm.open_video("invalid");
        assert(service->video_id.empty() && !backend->stopped && !vm.model().watching);
        vm.quality(480);
        vm.quality(2160);
        vm.open_video("jNQXAC9IVRw");
        assert(service->video_id == "jNQXAC9IVRw" && service->quality == 480);
        assert(vm.model().videos.empty() && vm.model().watching && vm.model().playing_title == "YouTube");
        assert(vm.playback().state == PlaybackState::Loading && backend->stopped && backend->url.empty());
        vm.poll();
        assert(backend->url == "https://test.googlevideo.com/video");
        vm.open_video("aqz-KE-bpKQ", "Remote playback test");
        assert(service->video_id == "aqz-KE-bpKQ" && service->quality == 480);
        assert(vm.model().playing_title == "Remote playback test");
        vm.open_video("invalid", "Invalid request");
        assert(service->video_id == "aqz-KE-bpKQ" && vm.model().playing_title == "Remote playback test");
        vm.poll();
        backend->state.state = PlaybackState::Ended;
        vm.poll();
        vm.toggle_pause();
        assert(service->video_id == "aqz-KE-bpKQ" && service->quality == 480);
        assert(vm.playback().state == PlaybackState::Loading);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        YoutubeViewModel vm(std::move(search), std::make_unique<FakePlayer>());
        assert(service->recommendation_calls == 0);
        vm.recommendations();
        assert(service->recommendation_calls == 1 && service->batch == 0);
        assert(vm.model().recommending && vm.model().searching && vm.model().can_load_more);
        vm.load_more();
        assert(service->recommendation_calls == 1);

        auto initial                = sample_results();
        initial.videos[0].thumbnail = {1, 2, 3};
        service->updates.push_back(std::move(initial));
        vm.poll();
        assert(vm.model().videos.size() == 2 && !vm.model().searching && vm.model().can_load_more);
        assert(service->thumbnail_calls == 1);
        assert((service->thumbnail_ids == std::vector<std::string>{"aqz-KE-bpKQ"}));
        const auto generation  = vm.results_generation();
        const auto first_title = vm.model().videos.front().title;

        vm.load_more();
        assert(service->recommendation_calls == 2 && service->batch == 1);
        assert(vm.model().searching && vm.model().videos.size() == 2);
        SearchUpdate error;
        error.kind  = SearchUpdate::Kind::Error;
        error.error = "Recommendation request failed";
        service->updates.push_back(std::move(error));
        vm.poll();
        assert(!vm.model().searching && vm.model().can_load_more && vm.model().videos.size() == 2);
        assert(vm.model().status == "Recommendation request failed");
        assert(vm.results_generation() == generation);

        vm.load_more();
        assert(service->recommendation_calls == 3 && service->batch == 1);
        auto more            = sample_results();
        more.videos[0].title = "Duplicate title";
        more.videos.push_back(numbered_results(0, 1).videos.front());
        more.videos.push_back(more.videos.back());
        service->updates.push_back(std::move(more));
        vm.poll();
        assert(vm.model().videos.size() == 3 && !vm.model().searching);
        assert(vm.model().videos.front().title == first_title);
        assert((vm.model().videos.front().thumbnail == std::vector<std::uint8_t>{1, 2, 3}));
        assert(service->thumbnail_calls == 2);
        assert((service->thumbnail_ids == std::vector<std::string>{"aqz-KE-bpKQ", "00000000000"}));

        vm.load_more();
        assert(service->recommendation_calls == 4 && service->batch == 2);
        service->updates.push_back(numbered_results(1, 2));
        vm.cancel_search();
        vm.poll();
        assert(service->cancelled && !vm.model().searching && vm.model().can_load_more);
        assert(vm.model().videos.size() == 3);
        vm.load_more();
        assert(service->recommendation_calls == 5 && service->batch == 2);
        service->updates.push_back(SearchUpdate{});
        vm.poll();
        assert(vm.model().videos.size() == 3 && vm.model().can_load_more);
        vm.load_more();
        assert(service->recommendation_calls == 6 && service->batch == 3);
        service->updates.push_back(sample_results());
        vm.poll();
        assert(vm.model().videos.size() == 3 && vm.model().can_load_more);
        vm.load_more();
        assert(service->recommendation_calls == 7 && service->batch == 4);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        YoutubeViewModel vm(std::move(search), std::make_unique<FakePlayer>());
        vm.search("music");
        service->updates.push_back(sample_results());
        vm.poll();
        vm.recommendations();
        assert(service->batch == 0 && vm.model().query.empty() && vm.model().videos.empty());
        service->updates.push_back(sample_results());
        vm.poll();
        vm.recommendations();
        assert(service->batch == 1 && vm.model().videos.empty() && vm.model().searching);
        SearchUpdate error;
        error.kind  = SearchUpdate::Kind::Error;
        error.error = "Offline";
        service->updates.push_back(std::move(error));
        vm.poll();
        assert(vm.model().videos.empty() && !vm.model().searching && vm.model().can_load_more);
        vm.recommendations();
        assert(service->batch == 1);
        vm.cancel_search();
        vm.recommendations();
        assert(service->batch == 1);
        service->updates.push_back(numbered_results(0, 3));
        vm.poll();

        vm.load_more();
        service->updates.push_back(numbered_results(3, 3));
        const auto calls = service->recommendation_calls;
        vm.search("  new search  ");
        assert(!vm.model().recommending && !vm.model().can_load_more && vm.model().searching);
        assert(vm.model().query == "new search" && vm.model().videos.empty());
        vm.load_more();
        assert(service->recommendation_calls == calls);
        service->updates.push_back(sample_results());
        vm.poll();
        assert(vm.model().videos.size() == 2 && !vm.model().recommending);
        vm.load_more();
        assert(service->recommendation_calls == calls);
        vm.recommendations();
        assert(service->batch == 2 && vm.model().query.empty() && vm.model().videos.empty());
        assert(vm.model().recommending && vm.model().can_load_more);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        YoutubeViewModel vm(std::move(search), std::make_unique<FakePlayer>());
        vm.recommendations();
        service->updates.push_back(numbered_results(0, 40));
        vm.poll();
        assert(vm.model().videos.size() == 40 && vm.model().can_load_more);
        vm.load_more();
        service->updates.push_back(numbered_results(35, 25));
        vm.poll();
        assert(vm.model().videos.size() == 48 && !vm.model().can_load_more && !vm.model().searching);
        for (std::size_t index = 0; index < vm.model().videos.size(); ++index)
            assert(vm.model().videos[index].id == numbered_results(index, 1).videos.front().id);
        const auto calls = service->recommendation_calls;
        vm.load_more();
        assert(service->recommendation_calls == calls);
        vm.recommendations();
        assert(service->batch == 2 && vm.model().videos.empty() && vm.model().can_load_more);
        service->updates.push_back(numbered_results(100, 12));
        vm.poll();
        assert(vm.model().videos.size() == 12 && vm.model().can_load_more);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto player   = std::make_unique<FakePlayer>();
        auto *service = search.get();
        auto *backend = player.get();
        YoutubeViewModel vm(std::move(search), std::move(player));
        vm.recommendations();
        auto initial                = sample_results();
        initial.videos[0].thumbnail = {1, 2, 3};
        service->updates.push_back(std::move(initial));
        vm.poll();
        assert(service->thumbnail_calls == 1);
        assert((service->thumbnail_ids == std::vector<std::string>{"aqz-KE-bpKQ"}));
        vm.load_more();
        service->updates.push_back(numbered_results(0, 2));
        vm.play(0);
        assert(vm.model().watching && !vm.model().searching && vm.model().recommending);
        assert(vm.model().videos.size() == 2);
        const auto calls = service->recommendation_calls;
        vm.load_more();
        assert(service->recommendation_calls == calls);
        vm.poll();
        assert(backend->url == "https://test.googlevideo.com/video");
        assert(service->thumbnail_calls == 1);
        vm.back();
        assert(!vm.model().watching && !vm.model().searching && vm.model().recommending);
        assert(vm.model().videos.size() == 2 && vm.model().can_load_more);
        assert(service->thumbnail_calls == 2);
        assert((service->thumbnail_ids == std::vector<std::string>{"aqz-KE-bpKQ"}));
        assert((vm.model().videos[0].thumbnail == std::vector<std::uint8_t>{1, 2, 3}));
        SearchUpdate thumbnail;
        thumbnail.kind     = SearchUpdate::Kind::Thumbnail;
        thumbnail.video_id = "aqz-KE-bpKQ";
        thumbnail.pixels   = {4, 5, 6};
        service->updates.push_back(std::move(thumbnail));
        vm.poll();
        assert((vm.model().videos[1].thumbnail == std::vector<std::uint8_t>{4, 5, 6}));
        vm.load_more();
        assert(service->recommendation_calls == calls + 1 && service->batch == 1);
        service->updates.push_back(numbered_results(0, 2));
        vm.open_media("/tmp/offline.mp4");
        vm.poll();
        assert(service->cancelled && backend->url == "/tmp/offline.mp4");
        assert(vm.model().watching && !vm.model().searching && vm.model().recommending);
        assert(vm.model().videos.size() == 2);
        vm.back();
        assert(service->thumbnail_calls == 2);
        vm.load_more();
        assert(service->batch == 1 && service->recommendation_calls == calls + 2);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        YoutubeViewModel vm(std::move(search), std::make_unique<FakePlayer>());
        vm.recommendations();
        service->updates.push_back(SearchUpdate{});
        vm.poll();
        vm.back();
        assert(service->thumbnail_calls == 0);
        vm.search("invalid response");
        auto invalid                = sample_results();
        invalid.videos[0].id        = "invalid";
        invalid.videos[1].thumbnail = {1, 2, 3};
        service->updates.push_back(std::move(invalid));
        vm.poll();
        assert(service->thumbnail_calls == 0);
    }
    lv_deinit();
}
