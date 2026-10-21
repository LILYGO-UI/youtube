#include "pages/youtube/youtube_view.hpp"

#if LV_USE_DRAW_OPENGLES
#include <src/drivers/opengles/lv_opengles_driver.h>
#endif
#include "components/components.hpp"
#include <cm0/typography.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace {
// Brand accent for the YouTube mark and primary playback control, fully opaque.
constexpr std::uint32_t accent = 0xe32636;
// Muted metadata and the opaque video letterbox surface.
constexpr std::uint32_t muted            = 0x657078;
constexpr std::uint32_t video_background = 0x101214;
constexpr const char *search_symbol      = "\xef\x80\x82";
constexpr const char *expand_symbol      = "\xef\x81\xa5";

void font(lv_obj_t *object, int size)
{
    if (const auto *value = lilygo_ui_font_get(size)) lv_obj_set_style_text_font(object, value, 0);
}
lv_obj_t *band(lv_obj_t *parent, lv_flex_flow_t flow, int padding = 0)
{
    auto *object = lv_obj_create(parent);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, padding, 0);
    lv_obj_set_style_pad_gap(object, 12, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(object, flow);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
lv_obj_t *label(lv_obj_t *parent, const char *text, int size = 22)
{
    auto *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    font(object, size);
    lv_obj_set_style_text_color(object, lv_color_hex(components::color_text), 0);
    return object;
}
lv_obj_t *button(lv_obj_t *parent, const char *icon, const char *name, lv_event_cb_t callback, void *data)
{
    auto *object = components::create_button(parent, icon, callback, data);
    lv_obj_set_name_static(object, name);
    lv_obj_set_size(object, 52, 48);
    lv_obj_set_style_shadow_width(object, 0, 0);
    const char *hint = "Search / Cancel";
    if (std::strcmp(name, "player_back") == 0) hint = "Back";
    if (std::strcmp(name, "player_expand") == 0) hint = "Expand / Restore";
    if (std::strcmp(name, "player_pause") == 0) hint = "Play / Pause / Retry";
    if (std::strcmp(name, "player_rewind") == 0) hint = "Back 10 seconds";
    if (std::strcmp(name, "player_forward") == 0) hint = "Forward 10 seconds";
    if (std::strcmp(name, "feed_home") == 0) hint = "Recommendations";
    auto *tooltip = label(object, hint, 14);
    lv_obj_set_style_bg_color(tooltip, lv_color_hex(components::color_action), 0);
    lv_obj_set_style_bg_opa(tooltip, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(tooltip, lv_color_white(), 0);
    lv_obj_set_style_pad_all(tooltip, 6, 0);
    lv_obj_align(tooltip, LV_ALIGN_OUT_TOP_LEFT, 0, -6);
    lv_obj_add_flag(tooltip, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING));
    lv_obj_add_flag(object, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(
        object,
        [](lv_event_t *event) {
            auto *tip       = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
            const auto code = lv_event_get_code(event);
            if (code == LV_EVENT_HOVER_OVER || code == LV_EVENT_LONG_PRESSED) {
                lv_obj_remove_flag(tip, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_translate_x(tip, 0, 0);
                lv_obj_update_layout(tip);
                lv_area_t bounds;
                lv_obj_get_coords(tip, &bounds);
                const int width = lv_display_get_horizontal_resolution(lv_obj_get_display(tip));
                const int shift = bounds.x1 < 4 ? 4 - bounds.x1 : bounds.x2 >= width - 4 ? width - 5 - bounds.x2 : 0;
                lv_obj_set_style_translate_x(tip, shift, 0);
            }
            if (code == LV_EVENT_HOVER_LEAVE || code == LV_EVENT_RELEASED) lv_obj_add_flag(tip, LV_OBJ_FLAG_HIDDEN);
        },
        LV_EVENT_ALL, tooltip);
    return object;
}
void visible(lv_obj_t *object, bool value)
{
    if (value)
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}
void describe(lv_image_dsc_t &descriptor, int width, int height, const std::uint8_t *data, bool alpha)
{
    descriptor               = {};
    descriptor.header.magic  = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf     = alpha ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_XRGB8888;
    descriptor.header.w      = width;
    descriptor.header.h      = height;
    descriptor.header.stride = width * 4;
    descriptor.data_size     = width * height * 4;
    descriptor.data          = data;
}
}  // namespace

struct YoutubeView::Row {
    YoutubeView *owner;
    std::size_t index;
    lv_obj_t *object;
    lv_obj_t *image;
    lv_obj_t *placeholder;
    lv_obj_t *thumbnail;
    lv_image_dsc_t descriptor{};
    std::vector<std::uint8_t> pixels;
};

YoutubeView::YoutubeView(YoutubeViewModel &model) : view_model_(model)
{
}
YoutubeView::~YoutubeView()
{
    destroy();
}

lv_obj_t *YoutubeView::create(lv_obj_t *parent)
{
    surface_ = components::create_page(parent);
    header_  = band(surface_, LV_FLEX_FLOW_ROW, 16);
    lv_obj_set_name_static(header_, "app_header");
    lv_obj_set_style_bg_color(header_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(header_, LV_OPA_COVER, 0);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *mark = label(header_, LV_SYMBOL_VIDEO, 28);
    lv_obj_set_style_text_color(mark, lv_color_hex(accent), 0);
    auto *title = label(header_, "YouTube", 28);
    lv_obj_set_name_static(title, "app_title");
    lv_obj_set_flex_grow(title, 1);
    open_search_button_ = button(header_, search_symbol, "search_open", event, this);

    browse_ = band(surface_, LV_FLEX_FLOW_COLUMN, 16);
    lv_obj_set_flex_grow(browse_, 1);
    lv_obj_set_style_min_height(browse_, 0, 0);
    lv_obj_set_style_pad_top(browse_, 8, 0);
    lv_obj_set_style_pad_bottom(browse_, 42, 0);
    search_bar_  = band(browse_, LV_FLEX_FLOW_ROW);
    home_button_ = button(search_bar_, LV_SYMBOL_HOME, "feed_home", event, this);

    // Keep the search surface as a sibling of the home and player surfaces.
    // This makes the search flow a real page: its own bar and results remain
    // visible after submitting a query instead of being folded into home.
    search_page_ = band(surface_, LV_FLEX_FLOW_COLUMN, 16);
    lv_obj_set_name_static(search_page_, "search_page");
    lv_obj_set_flex_grow(search_page_, 1);
    lv_obj_set_style_min_height(search_page_, 0, 0);
    search_page_bar_    = band(search_page_, LV_FLEX_FLOW_ROW);
    search_back_button_ = button(search_page_bar_, LV_SYMBOL_LEFT, "search_back", event, this);
    query_              = lv_textarea_create(search_page_bar_);
    lv_obj_set_name_static(query_, "search_query");
    lv_textarea_set_one_line(query_, true);
    lv_textarea_set_max_length(query_, 128);
    lv_textarea_set_placeholder_text(query_, "Search YouTube");
    font(query_, 22);
    lv_obj_set_width(query_, 0);
    lv_obj_set_flex_grow(query_, 1);
    lv_obj_set_height(query_, 52);
    lv_obj_add_event_cb(query_, event, LV_EVENT_ALL, this);
    search_button_ = button(search_page_bar_, search_symbol, "search_submit", event, this);
    search_icon_   = lv_obj_get_child(search_button_, 0);
    history_title_ = label(search_page_, "Search history", 22);
    history_list_  = band(search_page_, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_name_static(history_list_, "search_history");
    lv_obj_set_flex_grow(history_list_, 1);
    lv_obj_set_style_min_height(history_list_, 0, 0);
    lv_obj_set_style_pad_gap(history_list_, 8, 0);
    lv_obj_set_flex_align(history_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    visible(search_page_, false);
    visible(search_page_bar_, false);
    visible(search_back_button_, false);
    visible(query_, false);
    visible(search_button_, false);
    visible(history_title_, false);
    visible(history_list_, false);
    auto *feed_heading = feed_heading_ = band(browse_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(feed_heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *feed_summary = band(feed_heading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(feed_summary, 0);
    lv_obj_set_flex_grow(feed_summary, 1);
    lv_obj_set_style_pad_gap(feed_summary, 4, 0);
    feed_title_ = label(feed_summary, "", 22);
    lv_obj_set_width(feed_title_, LV_PCT(100));
    lv_label_set_long_mode(feed_title_, LV_LABEL_LONG_MODE_DOTS);
    status_         = label(feed_summary, "", 14);
    lv_obj_set_name_static(status_, "search_status");
    lv_obj_set_width(status_, LV_PCT(100));
    lv_obj_set_style_max_height(status_, 64, 0);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(status_, lv_color_hex(muted), 0);
    results_ = band(browse_, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(results_, "search_results");
    lv_obj_set_flex_grow(results_, 1);
    lv_obj_set_style_min_height(results_, 0, 0);
    lv_obj_add_flag(results_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(results_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(results_, LV_SCROLLBAR_MODE_AUTO);
    for (auto &column : columns_) {
        column = band(results_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(column, 1);
    }
    lv_obj_add_event_cb(results_, event, LV_EVENT_SIZE_CHANGED, this);
    more_button_ = components::create_button(browse_, "Load more", event, this);
    lv_obj_set_name_static(more_button_, "feed_more");
    lv_obj_set_size(more_button_, LV_PCT(100), 48);
    lv_obj_set_style_shadow_width(more_button_, 0, 0);

    watch_ = band(surface_, LV_FLEX_FLOW_COLUMN, 16);
    lv_obj_set_name_static(watch_, "player");
    lv_obj_set_flex_grow(watch_, 1);
    lv_obj_set_style_min_height(watch_, 0, 0);
    lv_obj_set_style_pad_bottom(watch_, 42, 0);
    auto *heading = band(watch_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(heading, LV_SYMBOL_LEFT, "player_back", event, this);
    playing_title_ = label(heading, "", 22);
    lv_obj_set_width(playing_title_, 0);
    lv_obj_set_flex_grow(playing_title_, 1);
    lv_label_set_long_mode(playing_title_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_height(playing_title_, 56, 0);
    quality_ = lv_dropdown_create(heading);
    font(quality_, 22);
    lv_dropdown_set_options(quality_, "1080p\n720p\n480p\n360p\n240p");
    lv_dropdown_set_selected(quality_, 1);  // 720p is the default playback ceiling.
    lv_obj_set_width(quality_, 138);
    lv_obj_set_style_text_color(quality_, lv_color_hex(muted), 0);
    lv_obj_set_name_static(quality_, "quality");
    lv_obj_add_event_cb(quality_, event, LV_EVENT_VALUE_CHANGED, this);
    visible(quality_, false);
    button(heading, expand_symbol, "player_expand", event, this);
    video_area_ = band(watch_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(video_area_, "video_area");
    lv_obj_set_height(video_area_, 0);
    lv_obj_set_flex_grow(video_area_, 1);
    lv_obj_set_style_min_height(video_area_, 0, 0);
    lv_obj_set_style_bg_color(video_area_, lv_color_hex(video_background), 0);
    lv_obj_set_style_bg_opa(video_area_, LV_OPA_COVER, 0);
    lv_obj_set_layout(video_area_, LV_LAYOUT_NONE);
#if LV_USE_DRAW_OPENGLES && LV_USE_3DTEXTURE
    video_ = lv_3dtexture_create(video_area_);
    lv_obj_remove_style_all(video_);
#else
    video_ = lv_image_create(video_area_);
    lv_image_set_inner_align(video_, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(video_, false);
#endif
    lv_obj_set_name_static(video_, "video_frame");
    lv_obj_set_size(video_, 16, 9);
    lv_obj_center(video_);
    lv_obj_add_event_cb(video_area_, event, LV_EVENT_SIZE_CHANGED, this);
    play_status_ = label(video_area_, "Loading...", 22);
    lv_obj_set_width(play_status_, LV_PCT(90));
    lv_obj_set_style_max_height(play_status_, LV_PCT(90), 0);
    lv_label_set_long_mode(play_status_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(play_status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(play_status_, lv_color_white(), 0);
    lv_obj_center(play_status_);
    auto *timeline = band(watch_, LV_FLEX_FLOW_ROW, 4);
    lv_obj_set_flex_align(timeline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    seek_ = lv_slider_create(timeline);
    lv_obj_set_name_static(seek_, "player_seek");
    lv_obj_set_width(seek_, 0);
    lv_obj_set_flex_grow(seek_, 1);
    lv_obj_set_height(seek_, 10);
    // The default LVGL knob extends 6 px past each side of a 10 px slider.
    // Leave enough horizontal track inset for it to remain visible at both
    // 0% and 100%.
    lv_obj_set_style_pad_hor(seek_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(seek_, 10, LV_PART_MAIN);
    lv_slider_set_range(seek_, 0, 1000);
    lv_obj_add_event_cb(seek_, event, LV_EVENT_RELEASED, this);
    clock_ = label(timeline, "0:00 / 0:00", 14);
    lv_obj_set_width(clock_, 136);
    lv_obj_set_style_text_align(clock_, LV_TEXT_ALIGN_RIGHT, 0);
    auto *controls = band(watch_, LV_FLEX_FLOW_ROW);
    lv_obj_set_name_static(controls, "player_controls");
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(controls, LV_SYMBOL_PREV, "player_rewind", event, this);
    play_button_ = button(controls, LV_SYMBOL_PAUSE, "player_pause", event, this);
    lv_obj_set_style_bg_color(play_button_, lv_color_hex(accent), 0);
    play_icon_ = lv_obj_get_child(play_button_, 0);
    button(controls, LV_SYMBOL_NEXT, "player_forward", event, this);
    auto *sound = label(controls, LV_SYMBOL_VOLUME_MAX, 22);
    lv_obj_set_style_text_color(sound, lv_color_hex(muted), 0);
    volume_ = lv_slider_create(controls);
    lv_obj_set_name_static(volume_, "player_volume");
    lv_obj_set_width(volume_, 0);
    lv_obj_set_flex_grow(volume_, 1);
    lv_obj_set_height(volume_, 10);
    // Keep the knob fully inside the controls row at the maximum volume.
    lv_obj_set_style_pad_hor(volume_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(volume_, 10, LV_PART_MAIN);
    lv_slider_set_range(volume_, 0, 100);
    lv_slider_set_value(volume_, 70, LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_, event, LV_EVENT_VALUE_CHANGED, this);
    keyboard_ = lv_keyboard_create(surface_);
    font(keyboard_, 22);
    lv_obj_set_name_static(keyboard_, "search_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_keyboard_set_textarea(keyboard_, query_);
    lv_obj_add_event_cb(keyboard_, event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, event, LV_EVENT_CANCEL, this);
    visible(keyboard_, false);
    visible(watch_, false);
    lv_obj_add_event_cb(surface_, event, LV_EVENT_SIZE_CHANGED, this);
    lv_subject_add_observer_obj(
        view_model_.revision(),
        [](lv_observer_t *observer, lv_subject_t *) {
            static_cast<YoutubeView *>(lv_observer_get_user_data(observer))->refresh();
        },
        surface_, this);
    timer_ =
        lv_timer_create([](lv_timer_t *timer) { static_cast<YoutubeView *>(lv_timer_get_user_data(timer))->tick(); },
                        LV_USE_DRAW_OPENGLES ? 5 : 20, this);
    layout();
    layout_results();
    return surface_;
}

void YoutubeView::destroy()
{
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (surface_) {
        lv_obj_delete(surface_);
        surface_ = nullptr;
    }
    rows_.clear();
    header_ = browse_ = search_bar_ = open_search_button_ = search_page_ = search_page_bar_ = search_back_button_ =
        history_title_ = history_list_ = query_ = search_button_ = search_icon_ = status_ = results_ = quality_ =
            nullptr;
    keyboard_ = watch_ = playing_title_ = video_area_ = video_ = play_status_ = seek_ = clock_ = nullptr;
    play_button_ = play_icon_ = volume_ = nullptr;
    feed_title_ = feed_heading_ = home_button_ = more_button_ = nullptr;
    for (auto &column : columns_) column = nullptr;
    results_width_      = 0;
    results_generation_ = -1;
    watching_ = fullscreen_ = search_page_active_ = search_results_active_ = false;
}

void YoutubeView::attach_feed(lv_obj_t *parent)
{
    if (!feed_heading_ || !results_ || !more_button_ || !parent) return;
    if (lv_obj_get_parent(feed_heading_) != parent) lv_obj_set_parent(feed_heading_, parent);
    if (lv_obj_get_parent(results_) != parent) lv_obj_set_parent(results_, parent);
    if (lv_obj_get_parent(more_button_) != parent) lv_obj_set_parent(more_button_, parent);

    // Reparenting appends objects to the new parent's child list. Keep the
    // feed heading, waterfall, and load-more action in that order on either
    // page.
    lv_obj_move_to_index(feed_heading_, parent == browse_ ? 1 : 3);
    lv_obj_move_to_index(results_, parent == browse_ ? 2 : 4);
    lv_obj_move_to_index(more_button_, parent == browse_ ? 3 : 5);
    results_width_ = 0;
    layout_results();
}

void YoutubeView::show_search_page(bool show)
{
    search_page_active_    = show;
    search_results_active_ = false;
    attach_feed(show ? search_page_ : browse_);

    // The app header belongs to the home page. Search and playback each have
    // their own top bar and should occupy the surface independently.
    visible(header_, !show && !watching_);
    visible(browse_, !show && !watching_);
    visible(search_page_, show);
    visible(search_page_bar_, show);
    visible(search_back_button_, show);
    visible(query_, show);
    visible(search_button_, show);
    visible(history_list_, show);
    visible(history_title_, false);
    visible(search_bar_, !show && !view_model_.model().recommending);
    visible(feed_heading_, !show);
    visible(results_, !show && !watching_);
    visible(more_button_, !show && view_model_.model().recommending);
    if (show) {
        rebuild_history();
        lv_obj_scroll_to_y(history_list_, 0, LV_ANIM_OFF);
        // Opening the search page should put the insertion point in the
        // query field immediately.  This also makes the software keyboard
        // available without requiring a second tap on the field.
        keyboard(true);
    } else {
        keyboard(false);
    }
    layout();
}

void YoutubeView::show_search_results(bool show)
{
    if (!search_page_active_) search_page_active_ = true;
    search_results_active_ = show;
    attach_feed(show ? search_page_ : browse_);
    visible(header_, false);
    visible(browse_, false);
    visible(search_page_, true);
    visible(search_page_bar_, true);
    visible(search_back_button_, true);
    visible(query_, true);
    visible(search_button_, true);
    visible(history_title_, false);
    visible(history_list_, false);
    visible(feed_heading_, show);
    visible(results_, show);
    visible(more_button_, false);
    keyboard(false);
    layout();
}

void YoutubeView::rebuild_history()
{
    if (!history_list_ || !history_title_) return;
    const auto &history = view_model_.model().search_history;
    visible(history_title_, search_page_active_ && !search_results_active_ && !history.empty());
    visible(history_list_, search_page_active_ && !search_results_active_);
    lv_obj_clean(history_list_);
    for (const auto &term : history) {
        auto *item = components::create_button(history_list_, term.c_str(), event, this);
        lv_obj_set_name_static(item, "search_history_item");
        lv_obj_set_size(item, LV_SIZE_CONTENT, 40);
        lv_obj_set_style_shadow_width(item, 0, 0);
        lv_obj_set_style_pad_hor(item, 16, 0);
        auto *item_label = lv_obj_get_child(item, 0);
        font(item_label, 18);
        lv_obj_set_width(item_label, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(item_label, LV_TEXT_ALIGN_LEFT, 0);
    }
}

void YoutubeView::keyboard(bool show)
{
    visible(keyboard_, show);
    if (show) {
        // The search field is hidden until the search page opens, so the
        // runtime's focus collector may not have added it to the default
        // input group yet. Add and focus it here as well; otherwise a
        // hardware keypad keeps sending characters to the previous control
        // and tapping the search field appears to do nothing.
        if (auto *group = lv_group_get_default()) {
            if (lv_obj_get_group(query_) != group) lv_group_add_obj(group, query_);
            if (lv_group_get_focused(group) != query_) lv_group_focus_obj(query_);
        }
        lv_obj_add_state(query_, LV_STATE_FOCUSED);
        lv_textarea_set_cursor_pos(query_, LV_TEXTAREA_CURSOR_LAST);
    } else {
        lv_obj_remove_state(query_, LV_STATE_FOCUSED);
        if (lv_obj_get_group(query_)) lv_group_remove_obj(query_);
    }
    layout();
}

void YoutubeView::event(lv_event_t *event)
{
    auto *self      = static_cast<YoutubeView *>(lv_event_get_user_data(event));
    auto *target    = lv_event_get_target_obj(event);
    const auto code = lv_event_get_code(event);
    if (target == self->surface_) {
        self->layout();
        return;
    }
    if (target == self->video_area_) {
        self->layout_video();
        return;
    }
    if (target == self->results_) {
        self->layout_results();
        return;
    }
    if (target == self->open_search_button_) {
        lv_textarea_set_text(self->query_, "");
        self->show_search_page(true);
        return;
    }
    if (target == self->search_back_button_) {
        if (self->search_results_active_) {
            // A result page is still part of the search flow. Return to the
            // editable search page and keep the current query so another
            // search can be entered without leaving this flow.
            self->show_search_page(true);
        } else {
            self->show_search_page(false);
            lv_textarea_set_text(self->query_, "");
            self->view_model_.recommendations();
        }
        return;
    }
    if (target != self->search_page_ && lv_obj_get_name(target) &&
        std::strcmp(lv_obj_get_name(target), "search_history_item") == 0) {
        auto *text = lv_obj_get_child(target, 0);
        lv_textarea_set_text(self->query_, text ? lv_label_get_text(text) : "");
        self->show_search_results(true);
        self->view_model_.search(lv_textarea_get_text(self->query_));
        return;
    }
    if (target == self->home_button_) {
        self->show_search_page(false);
        self->keyboard(false);
        lv_textarea_set_text(self->query_, "");
        self->view_model_.recommendations();
        return;
    }
    if (target == self->more_button_) {
        self->view_model_.load_more();
        return;
    }
    if (target == self->quality_) {
        static constexpr int heights[] = {1080, 720, 480, 360, 240};
        const auto selected            = lv_dropdown_get_selected(target);
        if (selected < sizeof(heights) / sizeof(heights[0])) self->view_model_.quality(heights[selected]);
        return;
    }
    if (target == self->query_) {
        if (code == LV_EVENT_CLICKED) {
            if (self->search_page_active_)
                self->keyboard(true);
            else
                self->show_search_page(true);
        } else if (code == LV_EVENT_FOCUSED && !self->search_page_active_) {
            self->show_search_page(true);
        }
        // LVGL's OK key sends READY to the keyboard first and then to its
        // textarea. The keyboard handler already submitted and hid the
        // field, so ignore that second notification.
        if (code == LV_EVENT_READY && lv_obj_has_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN)) return;
        if (code != LV_EVENT_READY) return;
    }
    if (target == self->keyboard_ && code == LV_EVENT_CANCEL) {
        self->keyboard(false);
        return;
    }
    if (target == self->search_button_ || target == self->keyboard_ || target == self->query_) {
        // The search page's button cancels an in-flight request only when
        // there is no query to submit.  In particular, an entered query must
        // replace the initial recommendations request instead of merely
        // cancelling it and leaving the query untouched.
        const char *entered_text = lv_textarea_get_text(self->query_);
        const bool empty_query =
            !entered_text || std::string(entered_text).find_first_not_of(" \t\n\r") == std::string::npos;
        const bool cancel = self->view_model_.model().searching && target == self->search_button_ &&
                            (!self->view_model_.model().recommending || empty_query);
        self->keyboard(false);
        if (cancel)
            self->view_model_.cancel_search();
        else {
            // Keep the search page active after submit. The query bar stays
            // at the top while the response replaces the history list with
            // the result waterfall below it.
            self->show_search_results(true);
            self->view_model_.search(lv_textarea_get_text(self->query_));
        }
    } else if (target == self->seek_) {
        self->view_model_.seek(lv_slider_get_value(target) / 1000.0 * self->view_model_.playback().duration);
    } else if (target == self->volume_) {
        self->view_model_.volume(lv_slider_get_value(target));
    } else {
        const char *name = lv_obj_get_name(target);
        if (!name) return;
        if (std::strcmp(name, "player_back") == 0) self->view_model_.back();
        if (std::strcmp(name, "player_pause") == 0) self->view_model_.toggle_pause();
        if (std::strcmp(name, "player_rewind") == 0) self->view_model_.seek(self->view_model_.playback().position - 10);
        if (std::strcmp(name, "player_forward") == 0)
            self->view_model_.seek(self->view_model_.playback().position + 10);
        if (std::strcmp(name, "player_expand") == 0) {
            self->fullscreen_ = !self->fullscreen_;
            visible(self->header_, !self->fullscreen_);
            self->layout();
        }
    }
}

void YoutubeView::rebuild_results(bool reset)
{
    if (reset) {
        for (auto *column : columns_) lv_obj_clean(column);
        rows_.clear();
        lv_obj_scroll_to_y(results_, 0, LV_ANIM_OFF);
    }
    for (std::size_t i = rows_.size(); i < view_model_.model().videos.size(); ++i) {
        const auto &video = view_model_.model().videos[i];
        auto row          = std::make_unique<Row>();
        row->owner        = this;
        row->index        = i;
        row->object       = band(columns_[0], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_name_static(row->object, "video_card");
        lv_obj_set_style_pad_gap(row->object, 0, 0);
        lv_obj_set_style_bg_opa(row->object, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row->object, lv_color_hex(components::color_container_background), 0);
        lv_obj_set_style_radius(row->object, 8, 0);
        lv_obj_add_flag(row->object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            row->object,
            [](lv_event_t *event) {
                auto *row = static_cast<Row *>(lv_event_get_user_data(event));
                row->owner->keyboard(false);
                row->owner->view_model_.play(row->index);
            },
            LV_EVENT_CLICKED, row.get());
        auto *thumbnail = row->thumbnail = lv_obj_create(row->object);
        lv_obj_set_name_static(thumbnail, "video_thumbnail");
        lv_obj_remove_style_all(thumbnail);
        lv_obj_set_size(thumbnail, 160, 90);
        lv_obj_set_style_bg_color(thumbnail, lv_color_hex(components::color_border), 0);
        lv_obj_set_style_bg_opa(thumbnail, LV_OPA_COVER, 0);
        lv_obj_remove_flag(thumbnail, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(thumbnail, LV_OBJ_FLAG_SCROLLABLE);
        row->placeholder = label(thumbnail, LV_SYMBOL_VIDEO, 28);
        lv_obj_center(row->placeholder);
        row->image = lv_image_create(thumbnail);
        lv_obj_remove_flag(row->image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row->image, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(row->image, LV_IMAGE_ALIGN_CONTAIN);
        if (video.duration > 0) {
            auto *duration = label(thumbnail, format_duration(video.duration).c_str(), 14);
            lv_obj_set_style_bg_color(duration, lv_color_hex(video_background), 0);
            lv_obj_set_style_bg_opa(duration, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(duration, lv_color_white(), 0);
            lv_obj_set_style_pad_hor(duration, 5, 0);
            lv_obj_set_style_pad_ver(duration, 3, 0);
            lv_obj_set_style_radius(duration, 4, 0);
            lv_obj_align(duration, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
        }
        auto *text = band(row->object, LV_FLEX_FLOW_COLUMN, 12);
        lv_obj_set_style_pad_gap(text, 6, 0);
        lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
        auto *title = label(text, video.title.c_str(), 22);
        lv_obj_set_width(title, LV_PCT(100));
        lv_obj_set_name_static(title, "video_title");
        lv_obj_set_style_max_height(title, 81, 0);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        auto *metadata = label(text, video.channel.c_str(), 14);
        lv_obj_set_width(metadata, LV_PCT(100));
        lv_label_set_long_mode(metadata, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_color(metadata, lv_color_hex(muted), 0);
        rows_.push_back(std::move(row));
    }
    results_width_ = 0;
    layout_results();
}

void YoutubeView::refresh()
{
    if (!surface_) return;
    const auto &model = view_model_.model();
    if (watching_ != model.watching) {
        watching_ = model.watching;
        if (watching_) {
            // A player is a separate page. Keep the feed attached to its
            // originating page so returning from playback restores context.
            visible(search_page_, false);
            visible(browse_, false);
        }
        visible(watch_, watching_);
        visible(quality_, watching_);
        if (!watching_) {
            if (search_page_active_) {
                if (search_results_active_)
                    show_search_results(true);
                else
                    show_search_page(true);
            } else {
                attach_feed(browse_);
                visible(header_, true);
                visible(browse_, true);
                visible(search_page_, false);
                visible(search_bar_, !model.recommending);
                visible(feed_heading_, true);
                visible(results_, true);
                visible(more_button_, model.recommending);
            }
        } else {
            visible(header_, false);
            visible(open_search_button_, false);
        }
        fullscreen_ = false;
        visible(video_, false);
        keyboard(false);
    }
    lv_label_set_text(status_, model.status.c_str());
    visible(status_, !model.status.empty());
    lv_label_set_text(feed_title_, model.recommending ? "" : "Search results");
    rebuild_history();
    // Keep the player control in sync with a quality selected before opening
    // the video (for example through the launch-time quality setting).
    static constexpr int heights[] = {1080, 720, 480, 360, 240};
    const auto quality             = std::find(std::begin(heights), std::end(heights), model.quality);
    if (quality != std::end(heights))
        lv_dropdown_set_selected(quality_, static_cast<uint32_t>(quality - std::begin(heights)));
    visible(home_button_, !search_page_active_ && !model.recommending);
    visible(search_bar_, !search_page_active_ && !watching_ && !model.recommending);
    visible(more_button_, !search_page_active_ && model.recommending);
    const bool show_feed_heading = !watching_ && (!model.recommending || !model.status.empty());
    visible(feed_heading_, show_feed_heading);
    if (model.searching || !model.can_load_more)
        lv_obj_add_state(more_button_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(more_button_, LV_STATE_DISABLED);
    lv_label_set_text(lv_obj_get_child(more_button_, 0), model.searching ? "Loading..."
                                                         : model.can_load_more
                                                             ? (model.videos.empty() ? "Try again" : "Load more")
                                                             : "All caught up");
    lv_label_set_text(search_icon_, model.searching && !model.recommending ? LV_SYMBOL_CLOSE : search_symbol);
    if (results_generation_ != view_model_.results_generation()) {
        results_generation_ = view_model_.results_generation();
        rebuild_results(true);
    } else if (rows_.size() != model.videos.size())
        rebuild_results(false);
    for (auto &row : rows_) {
        const auto &pixels = model.videos[row->index].thumbnail;
        if (pixels.size() == 160 * 90 * 4 && row->pixels.empty()) {
            row->pixels = pixels;
            describe(row->descriptor, 160, 90, row->pixels.data(), true);
            lv_image_set_src(row->image, &row->descriptor);
            visible(row->placeholder, false);
        }
    }
    if (!model.watching) return;
    lv_label_set_text(playing_title_, model.playing_title.c_str());
    const auto &playback = view_model_.playback();
    const bool loading   = playback.state == PlaybackState::Loading;
    const bool failed    = playback.state == PlaybackState::Error;
    const bool ended     = playback.state == PlaybackState::Ended;
    visible(play_status_, loading || failed || ended || playback.buffering);
    const std::string message = failed    ? playback.error
                                : ended   ? "Finished"
                                : loading ? "Opening video..."
                                          : "Buffering...";
    lv_label_set_text(play_status_, message.c_str());
    lv_label_set_text(play_icon_, failed || ended                           ? LV_SYMBOL_REFRESH
                                  : playback.state == PlaybackState::Paused ? LV_SYMBOL_PLAY
                                                                            : LV_SYMBOL_PAUSE);
    if (loading)
        lv_obj_add_state(play_button_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(play_button_, LV_STATE_DISABLED);
    if (playback.duration <= 0 || loading || failed)
        lv_obj_add_state(seek_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(seek_, LV_STATE_DISABLED);
    if (!lv_slider_is_dragged(seek_))
        lv_slider_set_value(seek_,
                            playback.duration > 0
                                ? static_cast<int>(std::clamp(playback.position / playback.duration, 0.0, 1.0) * 1000)
                                : 0,
                            LV_ANIM_OFF);
    const auto time = format_duration(playback.position) + " / " + format_duration(playback.duration);
    lv_label_set_text(clock_, time.c_str());
}

void YoutubeView::layout()
{
    if (!surface_ || !keyboard_) return;
    const int width      = lv_obj_get_content_width(surface_);
    const int height     = lv_obj_get_content_height(surface_);
    const bool landscape = width > height;
    lv_obj_set_style_pad_ver(header_, landscape ? 8 : 16, 0);
    lv_obj_set_style_pad_row(watch_, landscape ? 8 : 12, 0);
    lv_obj_set_height(keyboard_, landscape ? 180 : 260);
    // Keep the load-more action after the complete waterfall in every
    // orientation. Moving it into the heading in landscape made it look like
    // a feed control instead of the continuation of the results list.
    if (more_button_) lv_obj_set_width(more_button_, LV_PCT(100));
}

void YoutubeView::layout_results()
{
    if (!results_ || !columns_[3]) return;
    const int width = lv_obj_get_content_width(results_);
    if (width <= 0 || width == results_width_) return;
    results_width_       = width;
    const int count      = width >= 1160 ? 4 : width >= 840 ? 3 : width >= 460 ? 2 : 1;
    const int card_width = std::max(1, (width - (count - 1) * 12) / count);
    for (int i = 0; i < 4; ++i) {
        visible(columns_[i], i < count);
        lv_obj_set_width(columns_[i], card_width);
    }
    for (auto &row : rows_) {
        auto *column = columns_[row->index % count];
        if (lv_obj_get_parent(row->object) != column) lv_obj_set_parent(row->object, column);
        lv_obj_move_to_index(row->object, row->index / count);
        lv_obj_set_size(row->thumbnail, card_width, card_width * 9 / 16);
    }
}

void YoutubeView::layout_video()
{
    if (!video_area_ || !video_) return;
    const int available_width  = std::max(1, lv_obj_get_content_width(video_area_));
    const int available_height = std::max(1, lv_obj_get_content_height(video_area_));
    const int width            = std::max(1, std::min(available_width, available_height * 16 / 9));
    const int height           = std::max(1, std::min(available_height, width * 9 / 16));
    lv_obj_set_size(video_, width, height);
    lv_obj_center(video_);
}

void YoutubeView::tick()
{
    view_model_.poll();
#if LV_USE_DRAW_OPENGLES && LV_USE_3DTEXTURE
    if (!view_model_.model().watching) return;
    const bool frame_ready = view_model_.render_texture(texture_, std::min(1920, lv_obj_get_width(video_)),
                                                        std::min(1080, lv_obj_get_height(video_)));
    if (frame_ready) {
        lv_opengles_reinit_state();
        lv_3dtexture_set_src(video_, texture_.id);
        lv_obj_invalidate(video_);
        visible(video_, true);
        lv_timer_ready(lv_display_get_refr_timer(lv_obj_get_display(video_)));
    }
#else
    if (view_model_.render(frame_)) {
        if (frame_descriptor_.data != frame_.data()) {
            describe(frame_descriptor_, 640, 360, frame_.data(), false);
            lv_image_set_src(video_, &frame_descriptor_);
        }
        lv_obj_invalidate(video_);
        visible(video_, true);
    }
#endif
}
