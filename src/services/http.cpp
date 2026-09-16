#include "services/http.hpp"
#include <curl/curl.h>
#include <memory>
#include <stdexcept>

namespace {
struct Transfer {
    const std::atomic_bool &cancel;
    std::string data;
};
std::size_t receive(char *data, std::size_t size, std::size_t count, void *opaque)
{
    auto &transfer    = *static_cast<Transfer *>(opaque);
    const auto length = size * count;
    if (transfer.cancel || length > 4 * 1024 * 1024 - transfer.data.size()) return 0;
    try {
        transfer.data.append(data, length);
    } catch (...) {
        return 0;
    }
    return length;
}
}  // namespace

std::string youtube_request(const std::string &endpoint, const std::string &body, const std::atomic_bool &cancel)
{
    static const int initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("Cannot initialize HTTPS");
    if (endpoint != "search" && endpoint != "player") throw std::runtime_error("Invalid API request");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Cannot create HTTPS request");
    Transfer transfer{cancel, {}};
    const auto url = "https://www.youtube.com/youtubei/v1/" + endpoint + "?prettyPrint=false";
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        curl_slist_append(nullptr, "Content-Type: application/json"), curl_slist_free_all);
    if (!headers) throw std::runtime_error("Cannot allocate HTTPS headers");
    char error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT,
                     "com.google.android.youtube/21.03.36 (Linux; U; Android 16; en_US) gzip");
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(
        curl.get(), CURLOPT_XFERINFOFUNCTION, +[](void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return static_cast<Transfer *>(opaque)->cancel.load() ? 1 : 0;
        });
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
    const auto result = curl_easy_perform(curl.get());
    if (cancel) throw std::runtime_error("Cancelled");
    if (result != CURLE_OK) throw std::runtime_error(error[0] ? error : curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 200) throw std::runtime_error("YouTube HTTP " + std::to_string(status));
    return transfer.data;
}
