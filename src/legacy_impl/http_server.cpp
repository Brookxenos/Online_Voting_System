#include "http_server.h"
#include "json.h"
#include "crypto.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <ctime>
#include <iomanip>

// A shared secret used for vote payload encryption (XOR-stream cipher).
// In a real system this would be per-session / RSA-exchanged; for this
// educational demo we keep a single server-side key.
static const std::string VOTE_KEY = "V0t!ngSys_2024_D3moKey#xyz";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HttpServer::HttpServer(int port, const std::string& webDir, DataStore& store)
    : port_(port), webDir_(webDir), store_(store) {}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

void HttpServer::start() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { perror("socket"); return; }

    int yes = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (::bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return;
    }
    if (::listen(listenFd_, 64) < 0) {
        perror("listen"); return;
    }

    running_ = true;
    std::cout << "[server] listening on http://0.0.0.0:" << port_
              << "  (web root: " << webDir_ << ")\n";
    acceptLoop();
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
        // Detach a thread per connection.
        std::thread([this, cfd]{ handleClient(cfd); }).detach();
    }
}

// ---------------------------------------------------------------------------
// Connection handling
// ---------------------------------------------------------------------------

static std::string recvRequest(int fd) {
    std::string buf;
    char tmp[4096];
    // Read headers first.
    while (true) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        size_t hdrEnd = buf.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) {
            // Parse Content-Length.
            std::string headers = buf.substr(0, hdrEnd);
            std::string lower; lower.reserve(headers.size());
            for (char c : headers) lower.push_back((char)std::tolower((unsigned char)c));
            size_t clPos = lower.find("content-length:");
            if (clPos != std::string::npos) {
                size_t eol = headers.find("\r\n", clPos);
                std::string clStr = headers.substr(clPos + 15, eol - (clPos + 15));
                int cl = 0; try { cl = std::stoi(clStr); } catch(...) {}
                size_t bodyStart = hdrEnd + 4;
                while (buf.size() - bodyStart < (size_t)cl) {
                    n = ::recv(fd, tmp, sizeof(tmp), 0);
                    if (n <= 0) break;
                    buf.append(tmp, n);
                }
            }
            break;
        }
        if (buf.size() > 65536) break; // safety cap
    }
    return buf;
}

void HttpServer::handleClient(int fd) {
    std::string raw = recvRequest(fd);
    if (raw.empty()) { ::close(fd); return; }

    size_t hdrEnd = raw.find("\r\n\r\n");
    std::string head = raw.substr(0, hdrEnd);
    std::string body = (hdrEnd != std::string::npos) ? raw.substr(hdrEnd + 4) : "";

    Request req;
    std::istringstream hs(head);
    std::string line;
    std::getline(hs, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream rl(line);
    rl >> req.method >> req.path;

    // Split path and query
    size_t q = req.path.find('?');
    if (q != std::string::npos) {
        req.query = req.path.substr(q + 1);
        req.path  = req.path.substr(0, q);
    }
    // Parse query params
    if (!req.query.empty()) {
        std::istringstream qs(req.query);
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            size_t eq = pair.find('=');
            if (eq == std::string::npos) req.params[pair] = "";
            else {
                std::string k = pair.substr(0, eq), v = pair.substr(eq + 1);
                // URL decode (simplified)
                auto dec = [](std::string s){
                    std::string out;
                    for (size_t i = 0; i < s.size(); ++i) {
                        if (s[i] == '+') out.push_back(' ');
                        else if (s[i] == '%' && i + 2 < s.size()) {
                            auto hv = [](char c){
                                if (c >= '0' && c <= '9') return c - '0';
                                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                                return 0;
                            };
                            out.push_back((char)(hv(s[i+1]) << 4 | hv(s[i+2])));
                            i += 2;
                        } else out.push_back(s[i]);
                    }
                    return out;
                };
                req.params[dec(k)] = dec(v);
            }
        }
    }

    // Remaining headers
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c);
        std::string v = line.substr(c + 1);
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        // Lowercase key
        for (char& ch : k) ch = (char)std::tolower((unsigned char)ch);
        req.headers[k] = v;
    }

    req.body = body;
    Response resp = route(req);

    // Serialize response.
    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status << " " << resp.statusText << "\r\n";
    out << "Content-Type: " << resp.contentType << "\r\n";
    out << "Content-Length: " << resp.body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    out << "Access-Control-Allow-Headers: Content-Type\r\n";
    for (const auto& h : resp.headers) out << h.first << ": " << h.second << "\r\n";
    out << "\r\n" << resp.body;

    std::string s = out.str();
    ::send(fd, s.data(), s.size(), 0);
    ::close(fd);
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

HttpServer::Response HttpServer::route(const Request& req) {
    // CORS preflight
    if (req.method == "OPTIONS") {
        Response r; r.status = 204; r.statusText = "No Content"; return r;
    }

    const std::string& p = req.path;

    if (p.rfind("/api/", 0) == 0) {
        std::string endpoint = p.substr(5);
        try {
            if (endpoint == "register"           && req.method == "POST") return handleApiRegister(req);
            if (endpoint == "voter/login"        && req.method == "POST") return handleApiVoterLogin(req);
            if (endpoint == "admin/login"        && req.method == "POST") return handleApiAdminLogin(req);
            if (endpoint == "candidates"         && req.method == "GET")  return handleApiCandidates(req);
            if (endpoint == "vote"               && req.method == "POST") return handleApiVote(req);
            if (endpoint == "results"            && req.method == "GET")  return handleApiResults(req);
            if (endpoint == "election/status"    && (req.method == "GET" || req.method == "POST")) return handleApiElectionStatus(req);
            if (endpoint == "admin/candidates"        && req.method == "POST")   return handleApiAdminAddCandidate(req);
            if (endpoint == "admin/candidates/remove" && req.method == "POST")   return handleApiAdminRemoveCandidate(req);
            if (endpoint == "admin/voters"            && req.method == "POST")   return handleApiAdminAddVoter(req);
            if (endpoint == "admin/voters"            && req.method == "GET")    return handleApiAdminListVoters(req);
            if (endpoint == "admin/election/close"    && req.method == "POST")   return handleApiAdminCloseElection(req);
            if (endpoint == "admin/audit"             && req.method == "GET")    return handleApiAdminAudit(req);
            if (endpoint == "admin/password"          && req.method == "POST")   return handleApiAdminChangePassword(req);
        } catch (const std::exception& e) {
            return apiResponse(500, "Internal Error", jsonError(std::string("Server error: ") + e.what()));
        }
        return apiResponse(404, "Not Found", jsonError("Unknown endpoint: " + endpoint));
    }

    // Static files
    std::string rel = p;
    if (rel == "/" || rel.empty()) rel = "/index.html";
    // Strip leading '/'
    if (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    return serveStatic(rel);
}

HttpServer::Response HttpServer::serveStatic(const std::string& relPath) {
    // Prevent path traversal
    if (relPath.find("..") != std::string::npos) {
        return apiResponse(403, "Forbidden", jsonError("Forbidden"));
    }
    std::string full = webDir_ + "/" + relPath;
    std::ifstream ifs(full, std::ios::binary);
    if (!ifs) {
        // Fall back to index.html for SPA-style routing
        std::ifstream idx(webDir_ + "/index.html", std::ios::binary);
        if (idx) {
            std::string content((std::istreambuf_iterator<char>(idx)), {});
            Response r; r.body = content; r.contentType = "text/html; charset=utf-8";
            return r;
        }
        return apiResponse(404, "Not Found", jsonError("Not found"));
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    Response r;
    r.body = content;
    if (relPath.size() >= 5 && relPath.substr(relPath.size() - 5) == ".html")  r.contentType = "text/html; charset=utf-8";
    else if (relPath.size() >= 4 && relPath.substr(relPath.size() - 4) == ".css")  r.contentType = "text/css; charset=utf-8";
    else if (relPath.size() >= 3 && relPath.substr(relPath.size() - 3) == ".js")   r.contentType = "application/javascript; charset=utf-8";
    else if (relPath.size() >= 5 && relPath.substr(relPath.size() - 5) == ".json") r.contentType = "application/json; charset=utf-8";
    else if (relPath.size() >= 4 && relPath.substr(relPath.size() - 4) == ".svg")  r.contentType = "image/svg+xml";
    else if (relPath.size() >= 4 && relPath.substr(relPath.size() - 4) == ".png")  r.contentType = "image/png";
    else r.contentType = "application/octet-stream";
    return r;
}

HttpServer::Response HttpServer::apiResponse(int status, const std::string& statusText,
                                             const std::string& jsonBody) {
    Response r;
    r.status = status; r.statusText = statusText;
    r.body = jsonBody; r.contentType = "application/json; charset=utf-8";
    return r;
}

std::string HttpServer::jsonError(const std::string& msg, int) {
    json::Object o;
    o["ok"] = false;
    o["error"] = msg;
    return json::Value(o).dump();
}

std::string HttpServer::jsonOk(const std::string& msg) {
    json::Object o;
    o["ok"] = true;
    if (!msg.empty()) o["message"] = msg;
    return json::Value(o).dump();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string nextVoterId(DataStore& s) {
    auto all = s.allVoters();
    int max = 0;
    for (const auto& v : all) {
        if (v.id.size() > 1 && v.id[0] == 'V') {
            try { int n = std::stoi(v.id.substr(1)); if (n > max) max = n; } catch(...) {}
        }
    }
    char buf[16]; std::snprintf(buf, sizeof(buf), "V%03d", max + 1);
    return buf;
}

static std::string nextCandidateId(DataStore& s) {
    auto all = s.allCandidates();
    int max = 0;
    for (const auto& c : all) {
        if (c.id.size() > 1 && c.id[0] == 'C') {
            try { int n = std::stoi(c.id.substr(1)); if (n > max) max = n; } catch(...) {}
        }
    }
    char buf[16]; std::snprintf(buf, sizeof(buf), "C%03d", max + 1);
    return buf;
}

static std::string isoNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{}; ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ---------------------------------------------------------------------------
// API handlers
// ---------------------------------------------------------------------------

HttpServer::Response HttpServer::handleApiRegister(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (const std::exception& e) {
        return apiResponse(400, "Bad Request", jsonError("Invalid JSON body"));
    }
    std::string name = body.getString("name");
    std::string password = body.getString("password");
    std::string nationalId = body.getString("nationalId", "");

    if (name.empty() || password.empty()) {
        return apiResponse(400, "Bad Request", jsonError("name and password are required"));
    }
    if (password.size() < 6) {
        return apiResponse(400, "Bad Request", jsonError("password must be at least 6 chars"));
    }

    Voter v;
    v.id = nextVoterId(store_);
    v.name = name;
    v.nationalId = nationalId;
    v.passwordHash = crypto::hashPassword(password, crypto::generateSalt());
    v.hasVoted = false;

    if (!store_.addVoter(v)) {
        return apiResponse(409, "Conflict", jsonError("Voter ID collision; try again"));
    }
    store_.audit(v.id, "REGISTER", "Voter '" + name + "' registered");

    json::Object o; o["ok"] = true;
    o["voterId"] = v.id;
    o["message"] = "Registration successful. Save your voter ID.";
    return apiResponse(201, "Created", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiVoterLogin(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }

    std::string id = body.getString("voterId");
    std::string password = body.getString("password");
    if (id.empty() || password.empty())
        return apiResponse(400, "Bad Request", jsonError("voterId and password required"));

    Voter* v = store_.findVoter(id);
    if (!v || !crypto::verifyPassword(password, v->passwordHash)) {
        store_.audit(id, "LOGIN_FAIL", "voter login failed");
        return apiResponse(401, "Unauthorized", jsonError("Invalid credentials"));
    }
    store_.audit(id, "LOGIN_OK", "voter logged in");
    json::Object o; o["ok"] = true;
    o["voterId"] = v->id;
    o["name"] = v->name;
    o["hasVoted"] = v->hasVoted;
    return apiResponse(200, "OK", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiAdminLogin(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }

    std::string username = body.getString("username");
    std::string password = body.getString("password");
    if (username.empty() || password.empty())
        return apiResponse(400, "Bad Request", jsonError("username and password required"));

    Admin* a = store_.findAdmin(username);
    if (!a || !crypto::verifyPassword(password, a->passwordHash)) {
        store_.audit(username, "ADMIN_LOGIN_FAIL", "admin login failed");
        return apiResponse(401, "Unauthorized", jsonError("Invalid admin credentials"));
    }
    store_.audit(username, "ADMIN_LOGIN_OK", "admin logged in");
    json::Object o; o["ok"] = true;
    o["username"] = a->username;
    o["role"] = "admin";
    return apiResponse(200, "OK", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiCandidates(const Request&) {
    auto list = store_.allCandidates();
    json::Array arr;
    for (const auto& c : list) {
        json::Object o;
        o["id"] = c.id;
        o["name"] = c.name;
        o["party"] = c.party;
        o["symbol"] = c.symbol;
        o["position"] = c.position;
        o["votes"] = c.votes;
        arr.push_back(json::Value(o));
    }
    json::Object root;
    root["ok"] = true;
    root["candidates"] = arr;
    root["electionOpen"] = store_.electionOpen();
    return apiResponse(200, "OK", json::Value(root).dump());
}

HttpServer::Response HttpServer::handleApiVote(const Request& req) {
    if (!store_.electionOpen()) {
        return apiResponse(403, "Forbidden", jsonError("Election is closed"));
    }
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }

    std::string voterId = body.getString("voterId");
    std::string encVote = body.getString("ballot");
    std::string password = body.getString("password");

    if (voterId.empty() || encVote.empty() || password.empty())
        return apiResponse(400, "Bad Request", jsonError("voterId, ballot, and password required"));

    // Authenticate the voter first.
    Voter* v = store_.findVoter(voterId);
    if (!v || !crypto::verifyPassword(password, v->passwordHash)) {
        store_.audit(voterId, "VOTE_AUTH_FAIL", "vote rejected: bad credentials");
        return apiResponse(401, "Unauthorized", jsonError("Invalid credentials"));
    }
    if (v->hasVoted) {
        store_.audit(voterId, "VOTE_DOUBLE", "vote rejected: already voted");
        return apiResponse(409, "Conflict", jsonError("You have already voted"));
    }

    // Decrypt the ballot: it is base64(xorCrypt("candidateId|voterId", VOTE_KEY)).
    std::string dec = crypto::xorCrypt(crypto::base64Decode(encVote), VOTE_KEY);
    size_t sep = dec.find('|');
    if (sep == std::string::npos) {
        store_.audit(voterId, "VOTE_BAD_BALLOT", "malformed decrypted ballot");
        return apiResponse(400, "Bad Request", jsonError("Malformed ballot"));
    }
    std::string candidateId = dec.substr(0, sep);
    std::string voterInBallot = dec.substr(sep + 1);
    if (voterInBallot != voterId) {
        store_.audit(voterId, "VOTE_TAMPER", "ballot voter id mismatch");
        return apiResponse(400, "Bad Request", jsonError("Ballot tamper check failed"));
    }

    Candidate* c = store_.findCandidate(candidateId);
    if (!c) {
        store_.audit(voterId, "VOTE_BAD_CAND", "unknown candidate " + candidateId);
        return apiResponse(400, "Bad Request", jsonError("Unknown candidate"));
    }

    std::string ts = isoNow();
    std::string sig = crypto::sha256(voterId + "|" + candidateId + "|" + ts);
    Vote vote{ voterId, candidateId, ts, sig };
    store_.recordVote(vote);
    store_.audit(voterId, "VOTE", "voted for " + candidateId + " (" + c->name + ")");

    json::Object o; o["ok"] = true;
    o["message"] = "Vote recorded successfully";
    o["timestamp"] = ts;
    o["receipt"] = sig.substr(0, 16); // short receipt code
    return apiResponse(200, "OK", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiResults(const Request&) {
    auto list = store_.allCandidates();
    int total = 0;
    for (const auto& c : list) total += c.votes;

    json::Array arr;
    for (const auto& c : list) {
        json::Object o;
        o["id"] = c.id;
        o["name"] = c.name;
        o["party"] = c.party;
        o["symbol"] = c.symbol;
        o["position"] = c.position;
        o["votes"] = c.votes;
        o["percent"] = total > 0 ? (c.votes * 100.0 / total) : 0.0;
        arr.push_back(json::Value(o));
    }
    // Sort by votes desc (already in-memory copy)
    std::sort(arr.begin(), arr.end(), [](const json::Value& a, const json::Value& b){
        return a.asObject().at("votes").asInt() > b.asObject().at("votes").asInt();
    });

    json::Object root;
    root["ok"] = true;
    root["totalVotes"] = total;
    root["results"] = arr;
    root["electionOpen"] = store_.electionOpen();
    if (!arr.empty() && total > 0) {
        const json::Object& lead = arr[0].asObject();
        root["leader"] = lead.at("name").asString();
    }
    return apiResponse(200, "OK", json::Value(root).dump());
}

HttpServer::Response HttpServer::handleApiElectionStatus(const Request& req) {
    json::Object o;
    o["ok"] = true;
    o["electionOpen"] = store_.electionOpen();
    if (req.method == "POST") {
        json::Value body;
        try { body = json::parse(req.body); } catch(...) {}
        if (body.has("open")) {
            store_.setElectionOpen(body.at("open").asBool());
            o["electionOpen"] = body.at("open").asBool();
            o["message"] = body.at("open").asBool() ? "Election opened" : "Election closed";
        }
    }
    return apiResponse(200, "OK", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiAdminAddCandidate(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }

    Candidate c;
    c.name = body.getString("name");
    c.party = body.getString("party");
    c.symbol = body.getString("symbol");
    c.position = body.getString("position", "President");
    if (c.name.empty() || c.party.empty())
        return apiResponse(400, "Bad Request", jsonError("name and party required"));
    c.id = nextCandidateId(store_);
    c.votes = 0;

    if (!store_.addCandidate(c))
        return apiResponse(409, "Conflict", jsonError("Candidate ID collision"));
    store_.audit("admin", "ADD_CANDIDATE", c.id + " " + c.name + " (" + c.party + ")");

    json::Object o; o["ok"] = true;
    o["candidateId"] = c.id;
    o["message"] = "Candidate added";
    return apiResponse(201, "Created", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiAdminRemoveCandidate(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }
    std::string id = body.getString("candidateId");
    if (id.empty()) return apiResponse(400, "Bad Request", jsonError("candidateId required"));
    if (!store_.removeCandidate(id))
        return apiResponse(404, "Not Found", jsonError("Candidate not found"));
    store_.audit("admin", "REMOVE_CANDIDATE", id);
    return apiResponse(200, "OK", jsonOk("Candidate removed"));
}

HttpServer::Response HttpServer::handleApiAdminAddVoter(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }
    std::string name = body.getString("name");
    std::string password = body.getString("password");
    std::string nationalId = body.getString("nationalId", "");
    if (name.empty() || password.empty())
        return apiResponse(400, "Bad Request", jsonError("name and password required"));
    if (password.size() < 6)
        return apiResponse(400, "Bad Request", jsonError("password must be >= 6 chars"));

    Voter v;
    v.id = nextVoterId(store_);
    v.name = name;
    v.nationalId = nationalId;
    v.passwordHash = crypto::hashPassword(password, crypto::generateSalt());
    if (!store_.addVoter(v))
        return apiResponse(409, "Conflict", jsonError("Voter ID collision"));
    store_.audit("admin", "ADD_VOTER", v.id + " " + name);

    json::Object o; o["ok"] = true;
    o["voterId"] = v.id;
    o["message"] = "Voter added by admin";
    return apiResponse(201, "Created", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiAdminListVoters(const Request&) {
    auto all = store_.allVoters();
    json::Array arr;
    for (const auto& v : all) {
        json::Object o;
        o["id"] = v.id;
        o["name"] = v.name;
        o["nationalId"] = v.nationalId;
        o["hasVoted"] = v.hasVoted;
        arr.push_back(json::Value(o));
    }
    json::Object root;
    root["ok"] = true;
    root["voters"] = arr;
    return apiResponse(200, "OK", json::Value(root).dump());
}

HttpServer::Response HttpServer::handleApiAdminCloseElection(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); } catch(...) {}
    bool open = body.has("open") ? body.at("open").asBool() : false;
    store_.setElectionOpen(open);
    store_.audit("admin", open ? "ELECTION_OPEN" : "ELECTION_CLOSED", "");
    json::Object o; o["ok"] = true;
    o["electionOpen"] = open;
    return apiResponse(200, "OK", json::Value(o).dump());
}

HttpServer::Response HttpServer::handleApiAdminAudit(const Request&) {
    auto entries = store_.recentAudit(200);
    json::Array arr;
    for (const auto& e : entries) {
        json::Object o;
        o["timestamp"] = e.timestamp;
        o["actor"] = e.actor;
        o["action"] = e.action;
        o["detail"] = e.detail;
        arr.push_back(json::Value(o));
    }
    json::Object root; root["ok"] = true; root["entries"] = arr;
    return apiResponse(200, "OK", json::Value(root).dump());
}

HttpServer::Response HttpServer::handleApiAdminChangePassword(const Request& req) {
    json::Value body;
    try { body = json::parse(req.body); }
    catch (...) { return apiResponse(400, "Bad Request", jsonError("Invalid JSON body")); }
    std::string username = body.getString("username");
    std::string oldPwd = body.getString("oldPassword");
    std::string newPwd = body.getString("newPassword");
    if (username.empty() || oldPwd.empty() || newPwd.empty())
        return apiResponse(400, "Bad Request", jsonError("username, oldPassword, newPassword required"));
    if (newPwd.size() < 8)
        return apiResponse(400, "Bad Request", jsonError("new password must be >= 8 chars"));

    Admin* a = store_.findAdmin(username);
    if (!a || !crypto::verifyPassword(oldPwd, a->passwordHash))
        return apiResponse(401, "Unauthorized", jsonError("Old password incorrect"));

    a->passwordHash = crypto::hashPassword(newPwd, crypto::generateSalt());
    store_.saveAdmins();
    store_.audit(username, "PASSWORD_CHANGE", "admin password changed");
    return apiResponse(200, "OK", jsonOk("Password updated"));
}
