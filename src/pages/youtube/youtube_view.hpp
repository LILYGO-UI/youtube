#pragma once

#include "pages/youtube/youtube_view_model.hpp"
#include <lvgl.h>

class YoutubeView {
public:
    explicit YoutubeView(YoutubeViewModel &model);
    ~YoutubeView();
    lv_obj_t *create(lv_obj_t *parent);
    void destroy();

private:
    struct Row;
    void refresh();
    void layout();
    void layout_results();
    void tick();
    void layout_video();
    void rebuild_results(bool reset);
    void keyboard(bool show);
    static void event(lv_event_t *event);
    YoutubeViewModel &view_model_;
    lv_obj_t *surface_        = nullptr;
    lv_obj_t *header_         = nullptr;
    lv_obj_t *browse_         = nullptr;
    lv_obj_t *search_bar_     = nullptr;
    lv_obj_t *query_          = nullptr;
    lv_obj_t *search_button_  = nullptr;
    lv_obj_t *search_icon_    = nullptr;
    lv_obj_t *status_         = nullptr;
    lv_obj_t *feed_title_     = nullptr;
    lv_obj_t *feed_heading_   = nullptr;
    lv_obj_t *home_button_    = nullptr;
    lv_obj_t *refresh_button_ = nullptr;
    lv_obj_t *more_button_    = nullptr;
    lv_obj_t *results_        = nullptr;
    lv_obj_t *columns_[4]     = {};
    lv_obj_t *quality_        = nullptr;
    lv_obj_t *keyboard_       = nullptr;
    lv_obj_t *watch_          = nullptr;
    lv_obj_t *playing_title_  = nullptr;
    lv_obj_t *video_area_     = nullptr;
    lv_obj_t *video_          = nullptr;
    lv_obj_t *play_status_    = nullptr;
    lv_obj_t *seek_           = nullptr;
    lv_obj_t *clock_          = nullptr;
    lv_obj_t *play_button_    = nullptr;
    lv_obj_t *play_icon_      = nullptr;
    lv_obj_t *volume_         = nullptr;
    lv_timer_t *timer_        = nullptr;
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<std::uint8_t> frame_;
    lv_image_dsc_t frame_descriptor_{};
    VideoTexture texture_{};
    int results_generation_ = -1;
    bool watching_          = false;
    bool fullscreen_        = false;
    int results_width_      = 0;
};
