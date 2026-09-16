#include "services/youtube_service.hpp"
#include "services/http.hpp"
#include "services/process.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

namespace {
// Public topic searches provide discovery without an account or personalized history.
constexpr std::array<const char *, 12> recommendation_topics{
    "technology innovation", "science explained",   "nature documentary", "live music performance",
    "travel destinations",   "cooking recipes",     "space exploration",  "DIY electronics projects",
    "art and design",        "history documentary", "architecture tour",  "short films"};
}  // namespace

YoutubeService::YoutubeService()
    : random_engine_([] {
          std::random_device device;
          const auto now    = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
          const auto thread = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
          std::seed_seq seed{device(),
                             device(),
                             static_cast<unsigned int>(now),
                             static_cast<unsigned int>(now >> 32),
                             static_cast<unsigned int>(thread),
                             static_cast<unsigned int>(thread >> 32)};
          return std::mt19937(seed);
      }())
{
    worker_ = std::thread([this] { run(); });
}
YoutubeService::~YoutubeService()
{
    cancel();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_one();
    worker_.join();
}

void YoutubeService::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancellation_) *cancellation_ = true;
    pending_.reset();
    updates_.clear();
}

void YoutubeService::enqueue(Request::Kind kind, const std::string &text, int height,
                             const std::vector<std::string> &video_ids)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancellation_) *cancellation_ = true;
        cancellation_ = std::make_shared<std::atomic_bool>(false);
        pending_      = Request{kind, text, height, cancellation_, video_ids};
        updates_.clear();
    }
    ready_.notify_one();
}

void YoutubeService::search(const std::string &query)
{
    enqueue(Request::Kind::Search, query);
}
void YoutubeService::recommend(std::size_t)
{
    std::size_t topic = 0;
    {
        // Keep the generator safe if refresh requests arrive from different threads.
        std::lock_guard<std::mutex> lock(mutex_);
        if (has_last_recommendation_topic_ && recommendation_topics.size() > 1) {
            std::uniform_int_distribution<std::size_t> distribution(0, recommendation_topics.size() - 2);
            topic = distribution(random_engine_);
            if (topic >= last_recommendation_topic_) ++topic;
        } else {
            std::uniform_int_distribution<std::size_t> distribution(0, recommendation_topics.size() - 1);
            topic = distribution(random_engine_);
        }
        last_recommendation_topic_     = topic;
        has_last_recommendation_topic_ = true;
    }
    enqueue(Request::Kind::Recommend, recommendation_topics[topic]);
}
void YoutubeService::thumbnails(const std::vector<std::string> &ids)
{
    enqueue(Request::Kind::Thumbnails, "", 0, ids);
}
void YoutubeService::resolve(const std::string &id, int height)
{
    enqueue(Request::Kind::Resolve, id, height);
}

void YoutubeService::post(SearchUpdate update, const Request &request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!*request.cancelled && request.cancelled == cancellation_) updates_.push_back(std::move(update));
}

std::optional<SearchUpdate> YoutubeService::poll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (updates_.empty()) return std::nullopt;
    auto result = std::move(updates_.front());
    updates_.pop_front();
    return result;
}

void YoutubeService::run()
{
    while (true) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_) return;
            request = std::move(*pending_);
            pending_.reset();
        }
        try {
            execute(request);
        } catch (const std::exception &error) {
            SearchUpdate failure;
            failure.kind  = SearchUpdate::Kind::Error;
            failure.error = error.what();
            post(std::move(failure), request);
        }
    }
}

void YoutubeService::execute(const Request &request)
{
    const auto &cancelled = *request.cancelled;
    if (request.kind == Request::Kind::Resolve) {
        auto json = youtube_request("player", player_request(request.text), cancelled);
        SearchUpdate resolved;
        resolved.kind   = SearchUpdate::Kind::Resolved;
        resolved.stream = parse_player_response(json, request.height);
        post(std::move(resolved), request);
        return;
    }
    if (request.kind != Request::Kind::Thumbnails) {
        auto json = youtube_request("search", search_request(request.text), cancelled);
        SearchUpdate found;
        found.videos = parse_search_results(json);
        post(std::move(found), request);
        return;
    }
    for (std::size_t index = 0; index < request.video_ids.size() && index < 48; ++index) {
        if (cancelled) return;
        const auto &id = request.video_ids[index];
        if (!valid_video_id(id)) continue;
        auto image = run_process({"ffmpeg",    "-nostdin",     "-hide_banner",
                                  "-loglevel", "error",        "-rw_timeout",
                                  "8000000",   "-i",           "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg",
                                  "-vf",       "scale=160:90", "-frames:v",
                                  "1",         "-threads",     "1",
                                  "-f",        "rawvideo",     "-pix_fmt",
                                  "bgra",      "pipe:1"},
                                 cancelled, std::chrono::seconds(12), 160 * 90 * 4);
        if (image.code == 0 && image.output.size() == 160 * 90 * 4) {
            SearchUpdate thumbnail;
            thumbnail.kind     = SearchUpdate::Kind::Thumbnail;
            thumbnail.video_id = id;
            thumbnail.pixels.assign(image.output.begin(), image.output.end());
            post(std::move(thumbnail), request);
        }
    }
}
