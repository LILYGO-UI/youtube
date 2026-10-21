#include "fakes.hpp"
#include "pages/youtube/youtube_view.hpp"
#include <cm0/status_bar.h>
#include <cm0/typography.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>

namespace {
std::vector<lv_color32_t> framebuffer;
int display_width;

struct RenderChrome {
    lv_obj_t *status_bar = nullptr;
    lv_obj_t *content    = nullptr;
    lv_obj_t *home       = nullptr;
};

RenderChrome create_render_chrome()
{
    RenderChrome chrome;
    auto *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf2f2f7), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(screen, 36, 0);
    lv_obj_set_style_clip_corner(screen, true, 0);

    auto *system_layer = lv_layer_top();
    chrome.status_bar = cm0_status_bar_create(system_layer);
    assert(chrome.status_bar);
    lv_obj_align(chrome.status_bar, LV_ALIGN_TOP_MID, 0, 0);

    chrome.content = lv_obj_create(screen);
    lv_obj_set_size(chrome.content, LV_PCT(100), LV_PCT(100));
    lv_obj_align(chrome.content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_top(chrome.content, CM0_STATUS_BAR_HEIGHT, 0);
    lv_obj_set_style_border_width(chrome.content, 0, 0);
    lv_obj_set_style_radius(chrome.content, 0, 0);
    lv_obj_set_style_bg_color(chrome.content, lv_color_hex(0xf2f2f7), 0);
    lv_obj_set_style_bg_grad_dir(chrome.content, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(chrome.content, LV_OPA_COVER, 0);
    lv_obj_clear_flag(chrome.content, LV_OBJ_FLAG_SCROLLABLE);

    chrome.home = cm0_home_indicator_create(system_layer);
    assert(chrome.home);
    lv_obj_set_size(chrome.home, LV_PCT(100), 38);
    lv_obj_align(chrome.home, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_clear_flag(chrome.home, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                                               LV_OBJ_FLAG_CLICKABLE));
    return chrome;
}

void destroy_render_chrome(RenderChrome &chrome)
{
    if (chrome.content) lv_obj_delete(chrome.content);
    if (chrome.status_bar) lv_obj_delete(chrome.status_bar);
    if (chrome.home) lv_obj_delete(chrome.home);
    chrome = {};
}

void flush(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<lv_color32_t *>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        std::copy_n(source + (y - area->y1) * width, width, framebuffer.begin() + y * display_width + area->x1);
    }
    lv_display_flush_ready(display);
}
void capture(lv_display_t *display, const std::string &path, int width, int height)
{
    lv_tick_inc(20);
    lv_refr_now(display);
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(pixel.red);
        file.put(pixel.green);
        file.put(pixel.blue);
    }
    assert(file.good());
}
void within(lv_obj_t *object, int width, int height)
{
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < width && area.y2 < height);
}
SearchUpdate home_results(std::size_t first = 0, std::size_t count = 10)
{
    static const char *titles[] = {
        "A weekend project",
        "Inside a tiny workshop: building something useful from the parts already on your desk",
        "Open movie - animation showcase",
        "Making a portable display for an everyday electronics project",
        "A closer look",
        "From the first prototype to a finished device: the full design process",
        "New tools for creative work",
        "A quiet afternoon exploring the city with a compact camera",
        "Studio session",
        "How a small team brings a new open source hardware idea to life",
        "The next experiment",
        "An introduction to drawing, motion and interactive displays"};
    SearchUpdate update;
    for (std::size_t i = first; i < first + count; ++i) {
        const auto id = "samplevid" + std::string(i < 10 ? "0" : "") + std::to_string(i);
        update.videos.push_back(
            {id, titles[i % 12], i % 2 ? "Creative workshop" : "Open projects", 136 + static_cast<int>(i) * 47, {}});
    }
    return update;
}
void check_feed(lv_obj_t *results, std::size_t count, int width, int height)
{
    lv_obj_update_layout(results);
    within(results, width, height);
    const int available        = lv_obj_get_content_width(results);
    const int expected_columns = available >= 1160 ? 4 : available >= 840 ? 3 : available >= 460 ? 2 : 1;
    int visible_columns        = 0;
    std::size_t cards          = 0;
    assert(lv_obj_get_child_count(results) == 4);
    for (int i = 0; i < 4; ++i) {
        auto *column = lv_obj_get_child(results, i);
        if (lv_obj_has_flag(column, LV_OBJ_FLAG_HIDDEN)) {
            assert(lv_obj_get_child_count(column) == 0);
            continue;
        }
        ++visible_columns;
        for (std::uint32_t j = 0; j < lv_obj_get_child_count(column); ++j) {
            auto *card = lv_obj_get_child(column, j);
            assert(std::strcmp(lv_obj_get_name(card), "video_card") == 0);
            auto *thumbnail = lv_obj_find_by_name(card, "video_thumbnail");
            auto *title     = lv_obj_find_by_name(card, "video_title");
            assert(thumbnail && title);
            assert(lv_obj_get_width(thumbnail) == lv_obj_get_width(card));
            assert(lv_obj_get_height(thumbnail) == lv_obj_get_width(thumbnail) * 9 / 16);
            lv_area_t card_area, image_area, title_area;
            lv_obj_get_coords(card, &card_area);
            lv_obj_get_coords(thumbnail, &image_area);
            lv_obj_get_coords(title, &title_area);
            assert(card_area.x1 >= 0 && card_area.x2 < width);
            assert(title_area.x1 >= card_area.x1 && title_area.x2 <= card_area.x2);
            assert(title_area.y1 > image_area.y2 && title_area.y2 <= card_area.y2);
            ++cards;
        }
    }
    assert(visible_columns == expected_columns);
    assert(cards == count);
}
}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4 || argc == 5);
    int width = std::atoi(argv[1]), height = std::atoi(argv[2]);
    display_width = width;
    framebuffer.resize(width * height);
    std::vector<lv_color32_t> draw_buffer(std::max(width, height) * 80);
    lv_init();
    auto *display = lv_display_create(width, height);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr, draw_buffer.size() * 4,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    auto chrome = create_render_chrome();
    if (argc == 5) {
        {
            const bool recommendations = std::strcmp(argv[4], "recommendations") == 0;
            YoutubeViewModel vm(std::make_unique<YoutubeService>(), std::make_unique<MpvPlayer>(true));
            YoutubeView view(vm);
            view.create(chrome.content);
            auto *query = lv_obj_find_by_name(lv_screen_active(), "search_query");
            if (recommendations)
                vm.recommendations();
            else {
                lv_textarea_set_text(query, "LILYGO");
                vm.search("LILYGO");
            }
            auto deadline           = std::chrono::steady_clock::now() + std::chrono::seconds(65);
            auto thumbnail_deadline = deadline;
            bool thumbnail_ready    = false;
            while (std::chrono::steady_clock::now() < deadline) {
                lv_tick_inc(20);
                lv_timer_handler();
                const auto &videos = vm.model().videos;
                if (recommendations) {
                    const bool any_thumbnail = std::any_of(videos.begin(), videos.end(),
                                                           [](const Video &video) { return !video.thumbnail.empty(); });
                    if (any_thumbnail && !thumbnail_ready) {
                        thumbnail_ready    = true;
                        thumbnail_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                    }
                    const bool all_thumbnails =
                        !videos.empty() && std::all_of(videos.begin(), videos.end(),
                                                       [](const Video &video) { return !video.thumbnail.empty(); });
                    if (all_thumbnails || (thumbnail_ready && std::chrono::steady_clock::now() >= thumbnail_deadline))
                        break;
                } else if (!videos.empty() && !videos[0].thumbnail.empty()) {
                    thumbnail_ready = true;
                    break;
                }
                if (!vm.model().searching && vm.model().videos.empty()) {
                    std::fprintf(stderr, "%s\n", vm.model().status.c_str());
                    return 1;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            assert(!vm.model().videos.empty() && thumbnail_ready);
            capture(display, argv[3], width, height);
            if (recommendations) {
                check_feed(lv_obj_find_by_name(lv_screen_active(), "search_results"), vm.model().videos.size(), width,
                           height);
                std::printf("PASS: live recommendations and thumbnail rendering\n");
            } else {
                vm.play(0);
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(65);
                while (std::chrono::steady_clock::now() < deadline) {
                    lv_tick_inc(20);
                    lv_timer_handler();
                    if (vm.playback().position >= 3) break;
                    if (vm.playback().state == PlaybackState::Error) {
                        std::fprintf(stderr, "%s\n", vm.playback().error.c_str());
                        return 1;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                assert(vm.playback().position >= 3);
                capture(display, std::string(argv[3]) + "-player.ppm", width, height);
                auto *frame = lv_obj_find_by_name(lv_screen_active(), "video_frame");
                assert(!lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));
                std::printf("PASS: live search, thumbnail and embedded YouTube playback\n");
            }
        }
        destroy_render_chrome(chrome);
        lv_display_delete(display);
        lilygo_ui_fonts_deinit();
        lv_deinit();
        return 0;
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        YoutubeViewModel vm(std::move(search), std::make_unique<FakePlayer>());
        YoutubeView view(vm);
        view.create(chrome.content);
        auto find = [](const char *name) {
            auto *object = lv_obj_find_by_name(lv_screen_active(), name);
            assert(object);
            return object;
        };
        // Match the runtime's default keypad group so opening search also
        // exercises the first character typed through a hardware keyboard.
        auto *input_group = lv_group_create();
        lv_group_set_default(input_group);

        // A search must replace an in-flight recommendation request, and the
        // search page must focus the query field so its cursor is visible.
        vm.recommendations();
        lv_obj_send_event(find("search_open"), LV_EVENT_CLICKED, nullptr);
        assert(!lv_obj_has_flag(find("search_page"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_state(find("search_query"), LV_STATE_FOCUSED));
        assert(!lv_obj_has_flag(find("search_keyboard"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_group_get_focused(input_group) == find("search_query"));
        const uint32_t character = 'A';
        assert(lv_group_send_data(input_group, character) == LV_RESULT_OK);
        assert(std::string(lv_textarea_get_text(find("search_query"))) == "A");
        lv_textarea_set_text(find("search_query"), "during loading");
        lv_obj_send_event(find("search_submit"), LV_EVENT_CLICKED, nullptr);
        assert(service->query == "during loading");
        assert(vm.model().searching && !vm.model().recommending);
        view.destroy();
        lv_group_set_default(nullptr);
        lv_group_delete(input_group);
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        YoutubeViewModel vm(std::move(search), std::make_unique<FakePlayer>());
        YoutubeView view(vm);
        view.create(chrome.content);
        auto find = [](const char *name) {
            auto *object = lv_obj_find_by_name(lv_screen_active(), name);
            assert(object);
            return object;
        };

        // Search and playback are top-level pages. The search result list is
        // moved into the search page so the query bar remains visible after
        // submitting a query.
        auto *search_page = find("search_page");
        auto *player      = find("player");
        assert(lv_obj_get_parent(search_page) == lv_obj_get_parent(player));
        assert(lv_obj_has_flag(search_page, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(player, LV_OBJ_FLAG_HIDDEN));

        lv_obj_send_event(find("search_open"), LV_EVENT_CLICKED, nullptr);
        assert(!lv_obj_has_flag(search_page, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_query"), LV_OBJ_FLAG_HIDDEN));

        lv_textarea_set_text(find("search_query"), "Open movie");
        lv_obj_send_event(find("search_submit"), LV_EVENT_CLICKED, nullptr);
        assert(service->query == "Open movie");
        assert(vm.model().searching && !vm.model().recommending);

        service->updates.push_back(sample_results());
        vm.poll();
        assert(!vm.model().searching);
        assert(!lv_obj_has_flag(search_page, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_query"), LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_results"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_parent(find("search_results")) == search_page);
        assert(std::string(lv_textarea_get_text(find("search_query"))) == "Open movie");

        lv_obj_send_event(find("video_card"), LV_EVENT_CLICKED, nullptr);
        assert(vm.model().watching);
        assert(!lv_obj_has_flag(player, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(search_page, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(find("app_header"), LV_OBJ_FLAG_HIDDEN));

        lv_obj_send_event(find("player_back"), LV_EVENT_CLICKED, nullptr);
        assert(!vm.model().watching);
        assert(lv_obj_has_flag(player, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(search_page, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(find("app_header"), LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_query"), LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_results"), LV_OBJ_FLAG_HIDDEN));
        assert(std::string(lv_textarea_get_text(find("search_query"))) == "Open movie");

        // Returning from a search result must reopen the editable search page
        // so another query can be entered without going through home first.
        lv_obj_send_event(find("search_back"), LV_EVENT_CLICKED, nullptr);
        assert(!lv_obj_has_flag(search_page, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(find("search_results"), LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_keyboard"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_state(find("search_query"), LV_STATE_FOCUSED));
        assert(std::string(lv_textarea_get_text(find("search_query"))) == "Open movie");
        lv_textarea_add_text(find("search_query"), " next");
        assert(std::string(lv_textarea_get_text(find("search_query"))) == "Open movie next");
        view.destroy();
    }
    {
        auto search   = std::make_unique<FakeSearch>();
        auto *service = search.get();
        auto player   = std::make_unique<FakePlayer>();
        auto *backend = player.get();
        YoutubeViewModel vm(std::move(search), std::move(player));
        YoutubeView view(vm);
        view.create(chrome.content);
        auto find = [](const char *name) {
            auto *object = lv_obj_find_by_name(lv_screen_active(), name);
            assert(object);
            return object;
        };
        vm.recommendations();
        assert(service->recommendation_calls == 1 && vm.model().searching);
        assert(lv_obj_has_state(find("feed_more"), LV_STATE_DISABLED));
        assert(lv_obj_has_flag(find("feed_home"), LV_OBJ_FLAG_HIDDEN));
        assert(std::string(lv_label_get_text(find("search_status"))) == "Loading recommendations...");
        lv_obj_send_event(find("search_submit"), LV_EVENT_CLICKED, nullptr);
        assert(service->cancelled && !vm.model().searching);
        assert(!lv_obj_has_state(find("feed_more"), LV_STATE_DISABLED));
        lv_obj_send_event(find("feed_more"), LV_EVENT_CLICKED, nullptr);
        assert(service->recommendation_calls == 2 && vm.model().searching);
        service->updates.push_back(home_results());
        vm.poll();
        auto *results = find("search_results");
        check_feed(results, 10, width, height);
        within(find("search_query"), width, height);
        within(find("feed_more"), width, height);
        capture(display, std::string(argv[3]) + "-home.ppm", width, height);
        auto *first_card = find("video_card");
        assert(lv_obj_get_scroll_bottom(results) > 0);
        lv_obj_scroll_to_y(results, 60, LV_ANIM_OFF);
        const auto scroll = lv_obj_get_scroll_y(results);
        assert(scroll > 0);
        lv_obj_send_event(find("feed_more"), LV_EVENT_CLICKED, nullptr);
        assert(vm.model().searching && service->batch == 1);
        SearchUpdate error;
        error.kind  = SearchUpdate::Kind::Error;
        error.error = "Recommendation request failed";
        service->updates.push_back(error);
        vm.poll();
        lv_obj_update_layout(results);
        assert(find("video_card") == first_card && vm.model().videos.size() == 10);
        assert(lv_obj_get_scroll_y(results) == scroll);
        assert(std::string(lv_label_get_text(find("search_status"))) == error.error);
        lv_obj_send_event(find("feed_more"), LV_EVENT_CLICKED, nullptr);
        service->updates.push_back(home_results(10, 2));
        vm.poll();
        check_feed(results, 12, width, height);
        assert(find("video_card") == first_card && lv_obj_get_scroll_y(results) == scroll);
        lv_obj_send_event(first_card, LV_EVENT_CLICKED, nullptr);
        assert(vm.model().watching && service->video_id == "samplevid00");
        vm.poll();
        lv_obj_send_event(find("player_back"), LV_EVENT_CLICKED, nullptr);
        lv_obj_update_layout(results);
        assert(!vm.model().watching && vm.model().recommending && vm.model().videos.size() == 12);
        assert(find("video_card") == first_card && lv_obj_get_scroll_y(results) == scroll);
        std::swap(width, height);
        display_width = width;
        lv_display_set_resolution(display, width, height);
        check_feed(results, 12, width, height);
        assert(find("video_card") == first_card && vm.model().videos[0].id == "samplevid00");
        lv_obj_scroll_to_y(results, 0, LV_ANIM_OFF);
        capture(display, std::string(argv[3]) + "-home-rotated.ppm", width, height);
        std::swap(width, height);
        display_width = width;
        lv_display_set_resolution(display, width, height);
        lv_dropdown_set_selected(find("quality"), 0);
        lv_obj_send_event(find("quality"), LV_EVENT_VALUE_CHANGED, nullptr);
        assert(vm.model().quality == 1080);
        lv_textarea_set_text(find("search_query"), "Open movie");
        lv_obj_send_event(find("search_submit"), LV_EVENT_CLICKED, nullptr);
        assert(service->query == "Open movie");
        assert(!lv_obj_has_flag(find("search_page"), LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(find("search_query"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(find("feed_home"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(find("feed_more"), LV_OBJ_FLAG_HIDDEN));
        service->updates.push_back(sample_results());
        vm.poll();
        lv_obj_update_layout(lv_screen_active());
        check_feed(results, 2, width, height);
        within(find("search_query"), width, height);
        capture(display, argv[3], width, height);
        lv_obj_send_event(find("search_query"), LV_EVENT_CLICKED, nullptr);
        lv_obj_update_layout(lv_screen_active());
        assert(lv_obj_has_state(find("search_query"), LV_STATE_FOCUSED));
        within(find("search_keyboard"), width, height);
        capture(display, std::string(argv[3]) + "-keyboard.ppm", width, height);
        lv_obj_send_event(find("search_keyboard"), LV_EVENT_CANCEL, nullptr);
        lv_obj_send_event(find("video_card"), LV_EVENT_CLICKED, nullptr);
        assert(vm.model().watching);
        vm.poll();
        backend->state.state    = PlaybackState::Playing;
        backend->state.duration = 136;
        backend->state.position = 31;
        vm.poll();
        lv_obj_update_layout(lv_screen_active());
        within(find("player_back"), width, height);
        within(find("player_controls"), width, height);
        within(find("video_area"), width, height);
        assert(lv_obj_get_height(find("video_area")) > 40);
        lv_obj_send_event(find("player_pause"), LV_EVENT_CLICKED, nullptr);
        vm.poll();
        assert(vm.playback().state == PlaybackState::Paused);
        lv_slider_set_value(find("player_seek"), 500, LV_ANIM_OFF);
        lv_obj_send_event(find("player_seek"), LV_EVENT_RELEASED, nullptr);
        vm.poll();
        assert(vm.playback().position == 68);
        capture(display, std::string(argv[3]) + "-player.ppm", width, height);
        std::swap(width, height);
        display_width = width;
        lv_display_set_resolution(display, width, height);
        lv_obj_update_layout(lv_screen_active());
        within(find("player_controls"), width, height);
        assert(vm.playback().position == 68);
        capture(display, std::string(argv[3]) + "-rotated.ppm", width, height);
        lv_obj_send_event(find("player_back"), LV_EVENT_CLICKED, nullptr);
        assert(!vm.model().watching && vm.model().videos.size() == 2);
        assert(std::string(lv_textarea_get_text(find("search_query"))) == "Open movie");
        const int recommendation_calls = service->recommendation_calls;
        lv_obj_send_event(find("feed_home"), LV_EVENT_CLICKED, nullptr);
        assert(vm.model().recommending && vm.model().searching);
        assert(service->recommendation_calls == recommendation_calls + 1);
        assert(std::string(lv_textarea_get_text(find("search_query"))).empty());
        service->updates.push_back(home_results());
        vm.poll();
        check_feed(results, 10, width, height);
        lv_obj_send_event(find("feed_more"), LV_EVENT_CLICKED, nullptr);
        service->updates.push_back(home_results());
        vm.poll();
        check_feed(results, 10, width, height);
        view.destroy();
        view.destroy();
    }
    destroy_render_chrome(chrome);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
}
