#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace webworker {

struct FetchRequest {
    std::string requestId;
    std::string url;
    std::string method;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    double timeout;      // Request timeout in milliseconds (0 = default/no timeout)
    std::string redirect; // "follow", "error", "manual"
};

struct FetchResponse {
    std::string requestId;
    int status;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    std::string error; // Non-empty if request failed
};

} // namespace webworker
