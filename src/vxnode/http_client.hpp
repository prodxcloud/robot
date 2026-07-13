#pragma once

/// @file http_client.hpp
/// @brief A minimal, dependency-free HTTP/1.1 client.
///
/// vxnode binds to 127.0.0.1:8744 on the host and is fronted by nginx, which
/// terminates TLS. The robot talks to its node over the loopback interface, so
/// plain HTTP over a socket is all that is required and pulling in libcurl +
/// OpenSSL to reach localhost would be a poor trade.
///
/// If the node is remote and TLS-fronted, configure the client with a
/// `https://` base URL and build with -DHAS_CURL=1 to route through libcurl.

#include <chrono>
#include <string>
#include <unordered_map>

#include "common/types.hpp"

namespace prodxcloud::vxnode {

struct HttpResponse {
    int                                          status = 0;
    std::string                                  body;
    std::unordered_map<std::string, std::string> headers;

    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
};

struct HttpRequest {
    std::string                                  method = "GET";
    std::string                                  url;
    std::string                                  body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::milliseconds                    timeout{15000};
};

/// Parsed pieces of a URL.
struct ParsedUrl {
    std::string scheme = "http";
    std::string host   = "127.0.0.1";
    int         port   = 80;
    std::string path   = "/";
    bool        tls    = false;
};

/// Parse an absolute URL. Exposed so the client's URL handling is directly testable.
Result<ParsedUrl> parse_url(const std::string& url);

/// Perform a blocking HTTP request.
Result<HttpResponse> http_request(const HttpRequest& req);

}  // namespace prodxcloud::vxnode
