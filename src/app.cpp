#include "app.hpp"

#include "app_identity.h"
#include "pages/youtube/youtube_view.hpp"

#include <charconv>
#include <memory>
#include <cstdlib>
#include <string_view>

namespace {

struct AppState {
    std::unique_ptr<YoutubeViewModel> view_model;
    std::unique_ptr<YoutubeView> view;
};

AppState app_state;

void app_open(const cm0_app_context_t *context)
{
    app_state.view_model = std::make_unique<YoutubeViewModel>(
        std::make_unique<YoutubeService>(), std::make_unique<MpvPlayer>(false, LV_USE_DRAW_OPENGLES != 0));
    app_state.view = std::make_unique<YoutubeView>(*app_state.view_model);
    app_state.view->create(context->root);
    if (const char *media = std::getenv("LILYGO_YOUTUBE_TEST_MEDIA"))
        app_state.view_model->open_media(media);
    else if (const char *video = std::getenv("LILYGO_YOUTUBE_TEST_VIDEO")) {
        if (const char *value = std::getenv("LILYGO_YOUTUBE_TEST_QUALITY")) {
            const std::string_view quality(value);
            int height        = 0;
            const auto result = std::from_chars(quality.data(), quality.data() + quality.size(), height);
            if (result.ec == std::errc() && result.ptr == quality.data() + quality.size())
                app_state.view_model->quality(height);
        }
        app_state.view_model->open_video(video);
    } else
        app_state.view_model->recommendations();
}

void app_close()
{
    app_state.view.reset();
    app_state.view_model.reset();
}

const cm0_app_descriptor_t app_descriptor{
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_TEMPLATE_APP_ID, app_open, app_close,
};

}  // namespace

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
