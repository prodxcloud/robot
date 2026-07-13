#include "vxnode/http_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;
#endif

#ifdef HAS_CURL
#  include <curl/curl.h>
#endif

namespace prodxcloud::vxnode {

namespace {

void close_socket(socket_t s) {
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

/// Winsock needs an explicit startup. Doing it once, lazily, keeps callers from
/// having to know the platform they are on.
bool init_sockets() {
#ifdef _WIN32
    static bool initialised = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialised;
#else
    return true;
#endif
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

Result<ParsedUrl> parse_url(const std::string& url) {
    ParsedUrl p;

    std::string rest = url;
    const size_t scheme_end = rest.find("://");
    if (scheme_end != std::string::npos) {
        p.scheme = to_lower(rest.substr(0, scheme_end));
        rest     = rest.substr(scheme_end + 3);
    }

    if (p.scheme != "http" && p.scheme != "https") {
        return std::unexpected(Error::bad_request("unsupported URL scheme: " + p.scheme));
    }
    p.tls  = p.scheme == "https";
    p.port = p.tls ? 443 : 80;

    const size_t path_start = rest.find('/');
    std::string  authority  = path_start == std::string::npos ? rest : rest.substr(0, path_start);
    p.path = path_start == std::string::npos ? "/" : rest.substr(path_start);

    if (authority.empty()) {
        return std::unexpected(Error::bad_request("URL has no host: " + url));
    }

    // Strip any userinfo — vxnode authenticates with a header, never in the URL.
    if (const size_t at = authority.find('@'); at != std::string::npos) {
        authority = authority.substr(at + 1);
    }

    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        p.host = authority.substr(0, colon);
        try {
            p.port = std::stoi(authority.substr(colon + 1));
        } catch (const std::exception&) {
            return std::unexpected(Error::bad_request("invalid port in URL: " + url));
        }
        if (p.port <= 0 || p.port > 65535) {
            return std::unexpected(Error::bad_request("port out of range in URL: " + url));
        }
    } else {
        p.host = authority;
    }

    if (p.host.empty()) return std::unexpected(Error::bad_request("URL has no host: " + url));
    return p;
}

#ifdef HAS_CURL

namespace {

size_t curl_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

Result<HttpResponse> http_request(const HttpRequest& req) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::unexpected(Error::internal("curl_easy_init failed"));

    HttpResponse res;
    curl_slist*  headers = nullptr;
    for (const auto& [k, v] : req.headers) {
        headers = curl_slist_append(headers, (k + ": " + v).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeout.count()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (!req.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        const std::string err = curl_easy_strerror(rc);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::unexpected(Error::internal("vxnode request failed: " + err));
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    res.status = static_cast<int>(status);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

#else  // socket implementation

Result<HttpResponse> http_request(const HttpRequest& req) {
    const auto parsed = parse_url(req.url);
    if (!parsed) return std::unexpected(parsed.error());

    if (parsed->tls) {
        // Failing loudly beats silently downgrading to plaintext and shipping an
        // API key in the clear.
        return std::unexpected(Error::bad_request(
            "https requires a TLS-capable build — rebuild with -DHAS_CURL=1, or point "
            "VXNODE_URL at the node's loopback address (http://127.0.0.1:8744)"));
    }

    if (!init_sockets()) return std::unexpected(Error::internal("socket subsystem unavailable"));

    // Resolve.
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo*         info = nullptr;
    const std::string port_s = std::to_string(parsed->port);
    if (::getaddrinfo(parsed->host.c_str(), port_s.c_str(), &hints, &info) != 0 || !info) {
        return std::unexpected(Error::not_found("cannot resolve vxnode host: " + parsed->host));
    }

    socket_t sock = kInvalidSocket;
    for (addrinfo* a = info; a != nullptr; a = a->ai_next) {
        sock = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (sock == kInvalidSocket) continue;

        if (::connect(sock, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;

        close_socket(sock);
        sock = kInvalidSocket;
    }
    ::freeaddrinfo(info);

    if (sock == kInvalidSocket) {
        return std::unexpected(Error::internal(
            "cannot reach vxnode at " + parsed->host + ":" + port_s +
            " — is the node running? (docker ps | grep vxnode)"));
    }

    // Bound the call so a hung node cannot wedge the control loop forever.
    const auto ms = static_cast<long>(req.timeout.count());
#ifdef _WIN32
    auto tv = static_cast<DWORD>(ms);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    // Build the request.
    std::ostringstream out;
    out << req.method << " " << parsed->path << " HTTP/1.1\r\n"
        << "Host: " << parsed->host << ":" << parsed->port << "\r\n"
        << "Connection: close\r\n"
        << "Accept: application/json\r\n";

    for (const auto& [k, v] : req.headers) out << k << ": " << v << "\r\n";

    if (!req.body.empty()) out << "Content-Length: " << req.body.size() << "\r\n";
    out << "\r\n" << req.body;

    const std::string raw = out.str();

    size_t sent = 0;
    while (sent < raw.size()) {
        const auto n = ::send(sock, raw.data() + sent, static_cast<int>(raw.size() - sent), 0);
        if (n <= 0) {
            close_socket(sock);
            return std::unexpected(Error::internal("vxnode connection dropped while sending"));
        }
        sent += static_cast<size_t>(n);
    }

    std::string response;
    char        buf[8192];
    for (;;) {
        const auto n = ::recv(sock, buf, sizeof(buf), 0);
        if (n < 0) {
            close_socket(sock);
            return std::unexpected(Error::timeout("vxnode did not respond within the timeout"));
        }
        if (n == 0) break;
        response.append(buf, static_cast<size_t>(n));
    }
    close_socket(sock);

    // Parse the status line and headers.
    const size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return std::unexpected(Error::internal("malformed HTTP response from vxnode"));
    }

    HttpResponse res;
    std::istringstream hs(response.substr(0, header_end));
    std::string        line;

    if (!std::getline(hs, line)) {
        return std::unexpected(Error::internal("empty HTTP response from vxnode"));
    }
    {
        std::istringstream status_line(line);
        std::string        version;
        status_line >> version >> res.status;
    }

    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = to_lower(line.substr(0, colon));
        std::string val = line.substr(colon + 1);
        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
        res.headers[key] = val;
    }

    std::string body = response.substr(header_end + 4);

    // Un-chunk if needed. `Connection: close` usually gets us a plain body, but
    // gin will chunk a streamed response and the length prefixes would otherwise
    // end up inside the JSON.
    if (const auto it = res.headers.find("transfer-encoding");
        it != res.headers.end() && to_lower(it->second).find("chunked") != std::string::npos) {
        std::string decoded;
        size_t      pos = 0;
        while (pos < body.size()) {
            const size_t eol = body.find("\r\n", pos);
            if (eol == std::string::npos) break;

            size_t chunk_size = 0;
            try {
                chunk_size = static_cast<size_t>(std::stoul(body.substr(pos, eol - pos), nullptr, 16));
            } catch (const std::exception&) {
                break;
            }
            if (chunk_size == 0) break;

            const size_t start = eol + 2;
            if (start + chunk_size > body.size()) break;

            decoded.append(body, start, chunk_size);
            pos = start + chunk_size + 2;
        }
        body = std::move(decoded);
    }

    res.body = std::move(body);
    return res;
}

#endif  // HAS_CURL

}  // namespace prodxcloud::vxnode
