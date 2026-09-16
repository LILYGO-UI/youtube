#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

struct ProcessResult {
    int code = -1;
    std::string output;
    std::string error;
};

ProcessResult run_process(const std::vector<std::string> &arguments, const std::atomic_bool &cancel,
                          std::chrono::seconds timeout, std::size_t output_limit = 4 * 1024 * 1024);
