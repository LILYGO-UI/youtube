#pragma once
#include <atomic>
#include <string>

std::string youtube_request(const std::string &endpoint, const std::string &body, const std::atomic_bool &cancel);
