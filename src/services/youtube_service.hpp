#pragma once

#include "pages/youtube/youtube_model.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <memory>
#include <optional>
#include <random>
#include <thread>

struct SearchUpdate {
    enum class Kind { Results, Thumbnail, Resolved, Error } kind = Kind::Results;
    std::vector<Video> videos;
    std::string error;
    std::string video_id;
    std::vector<std::uint8_t> pixels;
    MediaStream stream;
};

class SearchService {
public:
    virtual ~SearchService()                                     = default;
    virtual void search(const std::string &query)                = 0;
    virtual void recommend(std::size_t batch)                    = 0;
    virtual void thumbnails(const std::vector<std::string> &ids) = 0;
    virtual void resolve(const std::string &id, int height)      = 0;
    virtual void cancel()                                        = 0;
    virtual std::optional<SearchUpdate> poll()                   = 0;
};

class YoutubeService final : public SearchService {
public:
    YoutubeService();
    ~YoutubeService() override;
    void search(const std::string &query) override;
    void recommend(std::size_t batch) override;
    void thumbnails(const std::vector<std::string> &ids) override;
    void resolve(const std::string &id, int height) override;
    void cancel() override;
    std::optional<SearchUpdate> poll() override;

private:
    struct Request {
        enum class Kind { Search, Recommend, Resolve, Thumbnails } kind = Kind::Search;
        std::string text;
        int height = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
        std::vector<std::string> video_ids;
    };
    void enqueue(Request::Kind kind, const std::string &text, int height = 0,
                 const std::vector<std::string> &video_ids = {});
    void run();
    void execute(const Request &request);
    void post(SearchUpdate update, const Request &request);
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::optional<Request> pending_;
    bool stopping_ = false;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<SearchUpdate> updates_;
    std::mt19937 random_engine_;
    std::size_t last_recommendation_topic_ = 0;
    bool has_last_recommendation_topic_    = false;
};
