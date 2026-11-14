#ifndef HTTP_H
#define HTTP_H

//
// HTTP/1.1 server on POSIX sockets.
//
// One thread per connection (existing behavior). Compared to the legacy
// implementation this adds:
//   - socket send/receive timeouts and a request deadline
//   - request body size cap
//   - cookie parsing
//   - URL decoding of the path
//   - centralized exception handling (route() never leaks exceptions)
//   - response helpers for JSON and cookies
//

#include <string>
#include <map>
#include <functional>
#include <atomic>
#include <thread>

class HttpServer {
public:
    struct Request {
        std::string method;
        std::string path;                       // decoded, no query string
        std::string query;
        std::string body;
        std::map<std::string, std::string> headers;   // lowercase keys
        std::map<std::string, std::string> params;    // query parameters (decoded)
        std::map<std::string, std::string> cookies;

        std::string header(const std::string& key) const;
        std::string cookie(const std::string& name) const;
        std::string param(const std::string& name) const;
        std::string clientIp;
    };

    struct Response {
        int status = 200;
        std::string statusText = "OK";
        std::string contentType = "application/json; charset=utf-8";
        std::string body;
        std::map<std::string, std::string> headers;
        std::vector<std::string> setCookies;   // Set-Cookie can appear multiple times

        void setCookie(const std::string& name, const std::string& value,
                       int maxAgeSeconds, bool httpOnly = true,
                       const std::string& sameSite = "Lax");
        void clearCookie(const std::string& name);
    };

    // Handler receives the request; path parameters are matched against a
    // pattern like "/api/elections/:id/candidates" and injected as params.
    using Handler = std::function<Response(const Request&)>;

    HttpServer(int port, const std::string& webDir);

    // Register a route. Exact segments match literally; segments starting
    // with ':' match one path segment and are exposed in req.params.
    void addRoute(const std::string& method, const std::string& pattern, Handler h);

    void start();   // blocks
    void stop();

private:
    int port_;
    std::string webDir_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};

    struct Route {
        std::string method;
        std::vector<std::string> segments;
        Handler handler;
    };
    std::vector<Route> routes_;

    void acceptLoop();
    void handleClient(int clientFd);
    bool recvRequest(int fd, std::string& out, std::string& clientIp);
    Response dispatch(Request& req);
    Response serveStatic(const std::string& relPath);
    static std::vector<std::string> splitPath(const std::string& path);
};

// Shared response builders for API controllers.
std::string urlDecode(const std::string& s);

#endif // HTTP_H
