#include "http.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Request / Response helpers
// ---------------------------------------------------------------------------

std::string HttpServer::Request::header(const std::string& key) const {
    auto it = headers.find(key);
    return it == headers.end() ? "" : it->second;
}

std::string HttpServer::Request::cookie(const std::string& name) const {
    auto it = cookies.find(name);
    return it == cookies.end() ? "" : it->second;
}

std::string HttpServer::Request::param(const std::string& name) const {
    auto it = params.find(name);
    return it == params.end() ? "" : it->second;
}

void HttpServer::Response::setCookie(const std::string& name, const std::string& value,
                                     int maxAgeSeconds, bool httpOnly,
                                     const std::string& sameSite) {
    std::string c = name + "=" + value + "; Max-Age=" + std::to_string(maxAgeSeconds) +
                    "; Path=/; SameSite=" + sameSite;
    if (httpOnly) c += "; HttpOnly";
    setCookies.push_back(c);
}

void HttpServer::Response::clearCookie(const std::string& name) {
    setCookies.push_back(name + "=; Max-Age=0; Path=/; SameSite=Lax; HttpOnly");
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out.push_back(' ');
        else if (s[i] == '%' && i + 2 < s.size()) {
            auto hv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hv(s[i+1]), lo = hv(s[i+2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        } else out.push_back(s[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

HttpServer::HttpServer(int port, const std::string& webDir,
                       const std::string& bindAddress)
    : port_(port), webDir_(webDir), bindAddress_(bindAddress) {}

void HttpServer::addRoute(const std::string& method, const std::string& pattern, Handler h) {
    Route r;
    r.method = method;
    r.segments = splitPath(pattern);
    r.handler = std::move(h);
    routes_.push_back(std::move(r));
}

bool HttpServer::start() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { perror("socket"); return false; }

    int yes = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    if (::inet_pton(AF_INET, bindAddress_.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[server] invalid IPv4 bind address: " << bindAddress_ << "\n";
        ::close(listenFd_); listenFd_ = -1; return false;
    }
    addr.sin_port = htons((uint16_t)port_);

    if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); ::close(listenFd_); listenFd_ = -1; return false;
    }
    if (::listen(listenFd_, 128) < 0) {
        perror("listen"); ::close(listenFd_); listenFd_ = -1; return false;
    }

    running_ = true;
    std::cout << "[server] listening on http://" << bindAddress_ << ":" << port_
              << "  (web root: " << webDir_ << ")\n";
    acceptLoop();
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
}

void HttpServer::acceptLoop() {
    while (running_) {
        sockaddr_in client{};
        socklen_t len = sizeof(client);
        int cfd = ::accept(listenFd_, (sockaddr*)&client, &len);
        if (cfd < 0) {
            if (!running_) break;
            continue;
        }
        std::thread([this, cfd, client]{ handleClient(cfd); }).detach();
    }
}

// ---------------------------------------------------------------------------
// Connection handling
// ---------------------------------------------------------------------------

static constexpr size_t kMaxHeaderBytes = 32 * 1024;   // request line + headers
static constexpr size_t kMaxBodyBytes   = 1024 * 1024; // 1 MiB body cap
static constexpr int     kSocketTimeoutSec = 30;

bool HttpServer::recvRequest(int fd, std::string& out, std::string& clientIp) {
    sockaddr_in peer{};
    socklen_t plen = sizeof(peer);
    if (::getpeername(fd, (sockaddr*)&peer, &plen) == 0) {
        char ip[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip))) clientIp = ip;
    }

    timeval tv{};
    tv.tv_sec = kSocketTimeoutSec;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string buf;
    char tmp[8192];
    bool gotHeader = false;
    while (!gotHeader) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
        if (buf.find("\r\n\r\n") != std::string::npos) gotHeader = true;
        if (buf.size() > kMaxHeaderBytes) return false;
    }

    // Parse Content-Length and read the body.
    size_t hdrEnd = buf.find("\r\n\r\n");
    std::string headers = buf.substr(0, hdrEnd);
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    size_t clPos = lower.find("content-length:");
    size_t contentLength = 0;
    if (clPos != std::string::npos) {
        size_t eol = headers.find("\r\n", clPos);
        std::string clStr = headers.substr(clPos + 15, eol - (clPos + 15));
        try { contentLength = (size_t)std::stoull(clStr); } catch (...) {}
        if (contentLength > kMaxBodyBytes) return false;
    }
    size_t bodyStart = hdrEnd + 4;
    while (buf.size() - bodyStart < contentLength) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
    }
    out = buf.substr(0, bodyStart + contentLength);
    return true;
}

void HttpServer::handleClient(int fd) {
    std::string raw, clientIp;
    if (!recvRequest(fd, raw, clientIp) || raw.empty()) {
        ::close(fd);
        return;
    }

    size_t hdrEnd = raw.find("\r\n\r\n");
    std::string head = raw.substr(0, hdrEnd);
    std::string body = (hdrEnd != std::string::npos) ? raw.substr(hdrEnd + 4) : "";

    Request req;
    req.clientIp = clientIp;

    std::istringstream hs(head);
    std::string line;
    std::getline(hs, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream rl(line);
    std::string rawPath;
    rl >> req.method >> rawPath;

    size_t q = rawPath.find('?');
    if (q != std::string::npos) {
        req.query = rawPath.substr(q + 1);
        rawPath = rawPath.substr(0, q);
    }
    req.path = urlDecode(rawPath);

    // Query parameters
    if (!req.query.empty()) {
        std::istringstream qs(req.query);
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            size_t eq = pair.find('=');
            if (eq == std::string::npos) req.params[urlDecode(pair)] = "";
            else req.params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
    }

    // Headers (lowercase keys)
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c);
        std::string v = line.substr(c + 1);
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        std::transform(k.begin(), k.end(), k.begin(),
                       [](unsigned char ch){ return (char)std::tolower(ch); });
        req.headers[k] = v;
    }

    // Cookies
    std::string cookieHeader = req.header("cookie");
    size_t pos = 0;
    while (pos < cookieHeader.size()) {
        size_t semi = cookieHeader.find(';', pos);
        std::string pair = cookieHeader.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string name = pair.substr(0, eq);
            std::string value = pair.substr(eq + 1);
            auto trim = [](std::string s){
                while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
                return s;
            };
            req.cookies[trim(name)] = trim(value);
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }

    req.body = body;

    Response resp = dispatch(req);

    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status << " " << resp.statusText << "\r\n";
    out << "Content-Type: " << resp.contentType << "\r\n";
    out << "Content-Length: " << resp.body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Cache-Control: no-store\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    for (const auto& sc : resp.setCookies) out << "Set-Cookie: " << sc << "\r\n";
    for (const auto& h : resp.headers) out << h.first << ": " << h.second << "\r\n";
    out << "\r\n" << resp.body;

    std::string s = out.str();
    ::send(fd, s.data(), s.size(), 0);
    ::close(fd);
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

std::vector<std::string> HttpServer::splitPath(const std::string& path) {
    std::vector<std::string> segs;
    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (!seg.empty()) segs.push_back(seg);
    }
    return segs;
}

HttpServer::Response HttpServer::dispatch(Request& req) {
    // Path-parameter matching needs a non-const params map.
    for (const auto& r : routes_) {
        if (r.method != req.method) continue;
        auto segs = splitPath(req.path);
        if (segs.size() != r.segments.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < segs.size() && ok; ++i) {
            const std::string& pat = r.segments[i];
            if (pat.rfind(':', 0) == 0) req.params[pat.substr(1)] = segs[i];
            else if (pat != segs[i]) ok = false;
        }
        if (!ok) continue;
        try {
            return r.handler(req);
        } catch (const std::exception& e) {
            std::cerr << "[server] 500 on " << req.method << " " << req.path
                      << ": " << e.what() << "\n";
            Response r2;
            r2.status = 500; r2.statusText = "Internal Server Error";
            r2.body = "{\"ok\":false,\"error\":\"Internal server error\"}";
            return r2;
        }
    }

    // No matching API route — try static files.
    if (req.path.rfind("/api/", 0) != 0) {
        std::string rel = req.path;
        if (rel == "/" || rel.empty()) rel = "/index.html";
        if (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
        return serveStatic(rel);
    }

    Response r;
    r.status = 404; r.statusText = "Not Found";
    r.body = "{\"ok\":false,\"error\":\"Unknown endpoint\"}";
    return r;
}

HttpServer::Response HttpServer::serveStatic(const std::string& relPath) {
    // Prevent path traversal.
    if (relPath.find("..") != std::string::npos) {
        Response r; r.status = 403; r.statusText = "Forbidden";
        r.contentType = "text/plain; charset=utf-8";
        r.body = "Forbidden";
        return r;
    }
    std::string full = webDir_ + "/" + relPath;
    std::ifstream ifs(full, std::ios::binary);
    if (!ifs) {
        // SPA fallback.
        std::ifstream idx(webDir_ + "/index.html", std::ios::binary);
        if (idx) {
            std::string content((std::istreambuf_iterator<char>(idx)), {});
            Response r; r.body = content;
            r.contentType = "text/html; charset=utf-8";
            return r;
        }
        Response r; r.status = 404; r.statusText = "Not Found";
        r.contentType = "text/plain; charset=utf-8";
        r.body = "Not found";
        return r;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    Response r;
    r.body = content;
    auto endsWith = [](const std::string& s, const std::string& suf){
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };
    if (endsWith(relPath, ".html")) r.contentType = "text/html; charset=utf-8";
    else if (endsWith(relPath, ".css")) r.contentType = "text/css; charset=utf-8";
    else if (endsWith(relPath, ".js")) r.contentType = "application/javascript; charset=utf-8";
    else if (endsWith(relPath, ".json")) r.contentType = "application/json; charset=utf-8";
    else if (endsWith(relPath, ".svg")) r.contentType = "image/svg+xml";
    else if (endsWith(relPath, ".png")) r.contentType = "image/png";
    else if (endsWith(relPath, ".ico")) r.contentType = "image/x-icon";
    else r.contentType = "application/octet-stream";
    return r;
}
