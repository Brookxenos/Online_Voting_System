#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "data_store.h"
#include <string>
#include <thread>
#include <atomic>
#include <map>

//
// A tiny multi-threaded HTTP/1.1 server built on POSIX sockets.
// Serves static files from <webDir> and routes /api/* to handler callbacks.
//
class HttpServer {
public:
    HttpServer(int port, const std::string& webDir, DataStore& store);

    void start();      // blocks until stopped
    void stop();

private:
    int port_;
    std::string webDir_;
    DataStore& store_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};

    void acceptLoop();
    void handleClient(int clientFd);

    // HTTP message structures
    struct Request {
        std::string method;
        std::string path;          // path portion only (no query string)
        std::string query;         // raw query string (without '?')
        std::string body;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> params; // parsed query params

        std::string header(const std::string& key) const {
            auto it = headers.find(key);
            return it == headers.end() ? "" : it->second;
        }
    };

    struct Response {
        int status = 200;
        std::string statusText = "OK";
        std::string body;
        std::string contentType = "text/html; charset=utf-8";
        std::map<std::string, std::string> headers;
    };

    // Routing
    Response route(const Request& req);
    Response serveStatic(const std::string& relPath);
    Response apiResponse(int status, const std::string& statusText,
                         const std::string& jsonBody);

    // JSON helpers
    static std::string jsonError(const std::string& msg, int code = 400);
    static std::string jsonOk(const std::string& msg = "");

    // --- API handlers ---
    Response handleApiRegister(const Request& req);
    Response handleApiVoterLogin(const Request& req);
    Response handleApiAdminLogin(const Request& req);
    Response handleApiCandidates(const Request& req);
    Response handleApiVote(const Request& req);
    Response handleApiResults(const Request& req);
    Response handleApiElectionStatus(const Request& req);
    Response handleApiAdminAddCandidate(const Request& req);
    Response handleApiAdminRemoveCandidate(const Request& req);
    Response handleApiAdminAddVoter(const Request& req);
    Response handleApiAdminListVoters(const Request& req);
    Response handleApiAdminCloseElection(const Request& req);
    Response handleApiAdminAudit(const Request& req);
    Response handleApiAdminChangePassword(const Request& req);
};

#endif // HTTP_SERVER_H
