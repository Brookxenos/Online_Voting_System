#include "api.h"
#include "json.h"
#include "crypto.h"
#include "models.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <sstream>

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

namespace {

HttpServer::Response jsonResponse(int status, const json::Object& obj) {
    HttpServer::Response r;
    r.status = status;
    r.statusText = (status == 200) ? "OK" : (status == 201) ? "Created" :
                   (status == 400) ? "Bad Request" : (status == 401) ? "Unauthorized" :
                   (status == 403) ? "Forbidden" : (status == 404) ? "Not Found" :
                   (status == 409) ? "Conflict" : (status == 423) ? "Locked" :
                   (status == 429) ? "Too Many Requests" :
                   (status >= 500) ? "Internal Server Error" : "OK";
    r.body = json::Value(obj).dump();
    return r;
}

HttpServer::Response ok(json::Object obj = {}) {
    obj["ok"] = true;
    return jsonResponse(200, obj);
}

HttpServer::Response created(json::Object obj = {}) {
    obj["ok"] = true;
    return jsonResponse(201, obj);
}

HttpServer::Response fail(int status, const std::string& msg) {
    json::Object o;
    o["ok"] = false;
    o["error"] = msg;
    return jsonResponse(status, o);
}

// ---------------------------------------------------------------------------
// Input validation
// ---------------------------------------------------------------------------

bool validUsername(const std::string& s) {
    if (s.size() < 3 || s.size() > 32) return false;
    for (char c : s) {
        if (!std::isalnum((unsigned char)c) && c != '_' && c != '.' && c != '-') return false;
    }
    return true;
}

bool validPassword(const std::string& s) {
    return s.size() >= 8 && s.size() <= 256;
}

std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool validTextField(const std::string& s, size_t min, size_t max) {
    return s.size() >= min && s.size() <= max;
}

// ---------------------------------------------------------------------------
// Rate limiting (login/registration brute-force resistance)
// ---------------------------------------------------------------------------

struct RateBucket {
    std::vector<long long> failures;   // unix timestamps of recent failures
};

std::mutex g_rateMtx;
std::map<std::string, RateBucket> g_rateBuckets;

static long long unixNow() {
    return (long long)std::time(nullptr);
}

// 5 failures per key within 5 minutes blocks further attempts for 5 minutes.
bool rateLimited(const std::string& key) {
    std::lock_guard<std::mutex> lk(g_rateMtx);
    auto it = g_rateBuckets.find(key);
    if (it == g_rateBuckets.end()) return false;
    long long now = unixNow();
    auto& f = it->second.failures;
    f.erase(std::remove_if(f.begin(), f.end(),
                           [&](long long t){ return now - t > 300; }), f.end());
    if (f.size() >= 5 && now - f.back() < 300) return true;
    return false;
}

void recordFailure(const std::string& key) {
    std::lock_guard<std::mutex> lk(g_rateMtx);
    g_rateBuckets[key].failures.push_back(unixNow());
    // Keep memory bounded.
    if (g_rateBuckets.size() > 10000) g_rateBuckets.clear();
}

void clearFailures(const std::string& key) {
    std::lock_guard<std::mutex> lk(g_rateMtx);
    g_rateBuckets.erase(key);
}

// ---------------------------------------------------------------------------
// Authentication helpers
// ---------------------------------------------------------------------------

const char* kSessionCookie = "ov_session";

std::optional<User> currentUser(const HttpServer::Request& req, Store& store) {
    std::string token = req.cookie(kSessionCookie);
    if (token.empty()) return std::nullopt;
    return store.userForSessionToken(token);
}

json::Value userJson(const User& u) {
    json::Object o;
    o["id"] = (double)u.id;
    o["username"] = u.username;
    o["displayName"] = u.displayName;
    o["role"] = u.role;
    o["orgId"] = (double)u.orgId;
    return json::Value(o);
}

json::Value electionJson(const Election& e) {
    json::Object o;
    o["id"] = (double)e.id;
    o["orgId"] = (double)e.orgId;
    o["name"] = e.name;
    o["description"] = e.description;
    o["status"] = e.status;
    o["resultsPublic"] = e.resultsPublic;
    o["showLiveResults"] = e.showLiveResults;
    o["startAt"] = e.startAt;
    o["endAt"] = e.endAt;
    o["createdAt"] = e.createdAt;
    return json::Value(o);
}

json::Value candidateJson(const Candidate& c) {
    json::Object o;
    o["id"] = (double)c.id;
    o["electionId"] = (double)c.electionId;
    o["name"] = c.name;
    o["party"] = c.party;
    o["description"] = c.description;
    o["position"] = (double)c.position;
    return json::Value(o);
}

// ---------------------------------------------------------------------------
// Authorization helpers
// ---------------------------------------------------------------------------

bool isPlatformAdmin(const User& u) { return u.role == "platform_admin"; }

bool isOrgAdminFor(const User& u, long long orgId) {
    return u.role == "org_admin" && u.orgId == orgId;
}

bool canAdminElection(const User& u, const Election& e) {
    return isPlatformAdmin(u) || isOrgAdminFor(u, e.orgId);
}

bool canObserveElection(const User& u, const Election& e) {
    return canAdminElection(u, e) || (u.role == "observer" && u.orgId == e.orgId);
}

bool userCanViewElection(const User& u, Store& store, const Election& e) {
    if (canAdminElection(u, e)) return true;
    if (u.role == "observer" && u.orgId == e.orgId) return true;
    if (u.role == "voter") {
        // Eligible users can always view.
        if (store.isEligible(e.id, u.id)) return true;
        // Participants can view after voting.
        if (store.hasParticipation(e.id, u.id)) return true;
        // Non-draft elections of self-registration orgs are publicly viewable.
        if (e.status != "draft") {
            auto org = store.getOrganization(e.orgId);
            if (org && org->allowSelfRegistration) return true;
        }
    }
    return false;
}

// Parses and validates a JSON body; on failure responds 400.
bool parseBody(const HttpServer::Request& req, json::Value& out, HttpServer::Response& err) {
    if (!req.header("content-type").empty() &&
        req.header("content-type").find("application/json") == std::string::npos) {
        err = fail(415, "Content-Type must be application/json");
        return false;
    }
    if (req.body.size() > 64 * 1024) {
        err = fail(413, "Body too large");
        return false;
    }
    try {
        out = json::parse(req.body);
    } catch (...) {
        err = fail(400, "Invalid JSON body");
        return false;
    }
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void registerApiRoutes(HttpServer& server, Store& store, const ApiConfig& cfg) {

    // ======================= Authentication ================================

    server.addRoute("POST", "/api/auth/register", [&](const HttpServer::Request& req) {
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string username = trimStr(body.getString("username"));
        std::string displayName = trimStr(body.getString("displayName"));
        std::string password = body.getString("password");

        if (!validUsername(username)) return fail(400, "Username must be 3-32 characters (letters, digits, _ . -)");
        if (displayName.empty()) displayName = username;
        if (!validTextField(displayName, 1, 100)) return fail(400, "Display name too long");
        if (!validPassword(password)) return fail(400, "Password must be at least 8 characters");

        if (rateLimited("reg|" + req.clientIp)) return fail(429, "Too many attempts; try again later");

        User u;
        u.username = username;
        u.displayName = displayName;
        u.passwordHash = crypto::hashPassword(password);
        u.role = "voter";

        // Self-registration joins the first organization that allows it.
        for (const auto& org : store.listOrganizations()) {
            if (org.allowSelfRegistration) { u.orgId = org.id; break; }
        }

        auto res = store.createUser(u);
        if (res == Store::CreateUserResult::DuplicateUsername) {
            recordFailure("reg|" + req.clientIp);
            return fail(409, "Username is already taken");
        }
        if (res != Store::CreateUserResult::Ok) return fail(400, "Registration is not available");

        store.addAudit(username, "REGISTER", 0, "self-registration");
        json::Object o;
        o["user"] = userJson(u);
        return created(o);
    });

    server.addRoute("POST", "/api/auth/login", [&](const HttpServer::Request& req) {
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string username = trimStr(body.getString("username"));
        std::string password = body.getString("password");
        if (username.empty() || password.empty()) return fail(400, "username and password required");

        std::string key = "login|" + req.clientIp + "|" + username;
        if (rateLimited(key)) {
            store.addAudit(username, "LOGIN_RATE_LIMITED", 0, "");
            return fail(429, "Too many failed attempts; try again later");
        }

        auto user = store.findUserByUsername(username);
        if (!user || user->disabled || !crypto::verifyPassword(password, user->passwordHash)) {
            recordFailure(key);
            // Same message for unknown user / wrong password / disabled — no
            // account enumeration.
            store.addAudit(username, "LOGIN_FAIL", 0, "");
            return fail(401, "Invalid username or password");
        }

        // Upgrade legacy SHA-256 hashes to Argon2id on successful login.
        if (!crypto::isModernHash(user->passwordHash)) {
            store.updatePasswordHash(user->id, crypto::hashPassword(password));
            store.addAudit(username, "PASSWORD_UPGRADED", 0, "legacy hash upgraded to Argon2id");
        }

        clearFailures(key);
        std::string token = store.createSession(user->id, cfg.sessionHours);
        store.addAudit(username, "LOGIN_OK", 0, "");

        HttpServer::Response r = ok();
        r.body = json::Value([&]{
            json::Object o;
            o["ok"] = true;
            o["user"] = userJson(*user);
            return json::Value(o);
        }()).dump();
        // Secure flag is only set when explicitly configured (HTTPS deployment);
        // HttpOnly + SameSite=Lax always apply.
        r.setCookie(kSessionCookie, token, cfg.sessionHours * 3600, true, "Lax");
        if (cfg.secureCookies) {
            r.setCookies.back() += "; Secure";
        }
        return r;
    });

    server.addRoute("POST", "/api/auth/logout", [&](const HttpServer::Request& req) {
        std::string token = req.cookie(kSessionCookie);
        if (!token.empty()) store.revokeSession(token);
        HttpServer::Response r = ok();
        r.clearCookie(kSessionCookie);
        return r;
    });

    server.addRoute("GET", "/api/me", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        json::Object o;
        o["user"] = userJson(*user);
        return ok(o);
    });

    server.addRoute("POST", "/api/auth/password", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string current = body.getString("currentPassword");
        std::string next = body.getString("newPassword");
        if (!validPassword(next)) return fail(400, "New password must be at least 8 characters");
        if (!crypto::verifyPassword(current, user->passwordHash)) {
            store.addAudit(user->username, "PASSWORD_CHANGE_FAIL", 0, "");
            return fail(401, "Current password is incorrect");
        }
        store.updatePasswordHash(user->id, crypto::hashPassword(next));
        // Invalidate existing sessions after a sensitive change.
        store.revokeAllSessions(user->id);
        store.addAudit(user->username, "PASSWORD_CHANGED", 0, "");
        HttpServer::Response r = ok({{"message", json::Value(std::string("Password updated; please log in again"))}});
        r.clearCookie(kSessionCookie);
        return r;
    });

    // ======================= Organizations =================================

    server.addRoute("GET", "/api/orgs", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        json::Array arr;
        if (isPlatformAdmin(*user)) {
            for (const auto& o : store.listOrganizations()) {
                json::Object jo;
                jo["id"] = (double)o.id;
                jo["name"] = o.name;
                jo["slug"] = o.slug;
                jo["allowSelfRegistration"] = o.allowSelfRegistration;
                jo["createdAt"] = o.createdAt;
                arr.push_back(json::Value(jo));
            }
        } else if (user->orgId != 0) {
            auto o = store.getOrganization(user->orgId);
            if (o) {
                json::Object jo;
                jo["id"] = (double)o->id;
                jo["name"] = o->name;
                jo["slug"] = o->slug;
                jo["allowSelfRegistration"] = o->allowSelfRegistration;
                jo["createdAt"] = o->createdAt;
                arr.push_back(json::Value(jo));
            }
        }
        json::Object root;
        root["ok"] = true;
        root["orgs"] = arr;
        return ok(root);
    });

    server.addRoute("POST", "/api/orgs", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user || !isPlatformAdmin(*user)) return fail(403, "Platform administrator access required");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string name = trimStr(body.getString("name"));
        std::string slug = trimStr(body.getString("slug"));
        bool selfReg = body.has("allowSelfRegistration") ? body.at("allowSelfRegistration").asBool() : true;
        if (!validTextField(name, 1, 100)) return fail(400, "Organization name required (1-100 chars)");
        if (!validUsername(slug)) return fail(400, "Slug must be 3-32 URL-safe characters");

        if (store.getOrganizationBySlug(slug)) return fail(409, "Slug already exists");
        Organization o = store.createOrganization(name, slug, selfReg);
        store.addAudit(user->username, "ORG_CREATED", 0, "slug " + slug);

        json::Object jo;
        jo["id"] = (double)o.id;
        jo["name"] = o.name;
        jo["slug"] = o.slug;
        return created({{"org", json::Value(jo)}});
    });

    // ======================= Elections =====================================

    server.addRoute("GET", "/api/elections", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        json::Array arr;
        for (const auto& e : store.listElectionsVisibleTo(*user)) {
            json::Object jo = electionJson(e).asObject();
            // Per-voter participation status (identity side only — never the choice).
            if (user->role == "voter") {
                jo["eligible"] = store.isEligible(e.id, user->id);
                jo["hasVoted"] = store.hasParticipation(e.id, user->id);
            }
            arr.push_back(json::Value(jo));
        }
        json::Object root;
        root["ok"] = true;
        root["elections"] = arr;
        return ok(root);
    });

    server.addRoute("POST", "/api/elections", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        long long orgId = (long long)body.getInt("orgId", 0);
        if (orgId == 0) return fail(400, "orgId required");
        if (!isPlatformAdmin(*user) && !isOrgAdminFor(*user, orgId)) {
            return fail(403, "You do not administer this organization");
        }

        std::string name = trimStr(body.getString("name"));
        std::string description = body.getString("description");
        if (!validTextField(name, 1, 200)) return fail(400, "Election name required (1-200 chars)");
        if (description.size() > 2000) return fail(400, "Description too long");

        Election e;
        e.orgId = orgId;
        e.name = name;
        e.description = description;
        e.showLiveResults = body.has("showLiveResults") ? body.at("showLiveResults").asBool() : false;
        e.startAt = body.getString("startAt");
        e.endAt = body.getString("endAt");
        e.createdBy = user->id;
        // Basic ISO-timestamp shape validation when provided.
        auto validIso = [](const std::string& s){
            return s.empty() || (s.size() == 20 && s[4] == '-' && s[10] == 'T' && s.back() == 'Z');
        };
        if (!validIso(e.startAt) || !validIso(e.endAt)) {
            return fail(400, "Dates must be empty or ISO 8601 UTC (YYYY-MM-DDTHH:MM:SSZ)");
        }

        auto res = store.createElection(e);
        if (res != Store::CreateElectionResult::Ok) return fail(404, "Organization not found");
        store.addAudit(user->username, "ELECTION_CREATED", e.id, name);

        return created({{"election", electionJson(e)}});
    });

    server.addRoute("GET", "/api/elections/:id", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !userCanViewElection(*user, store, *e)) return fail(404, "Election not found");

        json::Object o = electionJson(*e).asObject();
        json::Array cands;
        for (const auto& c : store.listCandidates(id)) cands.push_back(candidateJson(c));
        o["candidates"] = cands;

        // Vote counts are only exposed through the results endpoint rules.
        bool canSeeLive = canObserveElection(*user, *e) ||
                          (e->showLiveResults && e->status != "draft");
        o["participationCount"] = canSeeLive ? (double)store.participationCount(id) : -1.0;

        if (user->role == "voter") {
            o["eligible"] = store.isEligible(id, user->id);
            o["hasVoted"] = store.hasParticipation(id, user->id);
        }
        return ok(o);
    });

    server.addRoute("POST", "/api/elections/:id", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !canAdminElection(*user, *e)) return fail(403, "Administrator access required for this election");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        Election changes = *e;
        if (body.has("name")) {
            changes.name = trimStr(body.getString("name"));
            if (!validTextField(changes.name, 1, 200)) return fail(400, "Election name required");
        }
        if (body.has("description")) changes.description = body.getString("description");
        if (body.has("showLiveResults")) changes.showLiveResults = body.at("showLiveResults").asBool();
        if (body.has("startAt")) changes.startAt = body.getString("startAt");
        if (body.has("endAt")) changes.endAt = body.getString("endAt");

        // Published/open ballot configuration must not be silently changed:
        // draft-only edits are enforced by the store.
        if (!store.updateElectionDraft(id, changes)) {
            return fail(409, "Only draft elections can be edited");
        }
        store.addAudit(user->username, "ELECTION_CONFIGURED", id, "draft updated");
        e = store.getElection(id);
        return ok({{"election", electionJson(*e)}});
    });

    server.addRoute("POST", "/api/elections/:id/status", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !canAdminElection(*user, *e)) return fail(403, "Administrator access required for this election");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string newStatus = body.getString("status");
        if (newStatus != "open" && newStatus != "closed" && newStatus != "published" && newStatus != "archived") {
            return fail(400, "status must be open, closed, published, or archived");
        }
        if (!store.transitionElection(id, newStatus, user->username)) {
            return fail(409, "Invalid transition from '" + e->status + "' to '" + newStatus + "'");
        }
        e = store.getElection(id);
        return ok({{"election", electionJson(*e)}});
    });

    // ======================= Candidates ====================================

    server.addRoute("GET", "/api/elections/:id/candidates", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !userCanViewElection(*user, store, *e)) return fail(404, "Election not found");
        json::Array arr;
        for (const auto& c : store.listCandidates(id)) arr.push_back(candidateJson(c));
        return ok({{"candidates", json::Value(arr)}, {"status", json::Value(e->status)}});
    });

    server.addRoute("POST", "/api/elections/:id/candidates", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !canAdminElection(*user, *e)) return fail(403, "Administrator access required for this election");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        Candidate c;
        c.electionId = id;
        c.name = trimStr(body.getString("name"));
        c.party = trimStr(body.getString("party"));
        c.description = body.getString("description");
        c.position = body.getInt("position", (int)store.listCandidates(id).size() + 1);
        if (!validTextField(c.name, 1, 200)) return fail(400, "Candidate name required (1-200 chars)");
        if (c.party.size() > 200 || c.description.size() > 2000) return fail(400, "Field too long");

        auto res = store.addCandidate(c);
        if (res == Store::AddCandidateResult::ElectionNotFound) return fail(404, "Election not found");
        if (res == Store::AddCandidateResult::NotEditable) {
            return fail(409, "Candidates can only be changed while the election is a draft");
        }
        store.addAudit(user->username, "CANDIDATE_ADDED", id, c.name);
        return created({{"candidate", candidateJson(c)}});
    });

    server.addRoute("POST", "/api/elections/:id/candidates/remove", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !canAdminElection(*user, *e)) return fail(403, "Administrator access required for this election");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        long long candidateId;
        try { candidateId = (long long)body.getInt("candidateId", 0); } catch (...) { return fail(400, "Invalid candidateId"); }
        if (candidateId <= 0) return fail(400, "candidateId required");

        if (!store.removeCandidate(candidateId, id)) {
            return fail(409, "Candidate could not be removed (draft election only)");
        }
        store.addAudit(user->username, "CANDIDATE_REMOVED", id, std::to_string(candidateId));
        return ok();
    });

    // ======================= Eligibility ===================================

    server.addRoute("GET", "/api/elections/:id/eligibility", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !canObserveElection(*user, *e)) return fail(403, "Administrator or observer access required");
        json::Array arr;
        for (const auto& u : store.listEligibleUsers(id)) {
            json::Object jo;
            jo["id"] = (double)u.id;
            jo["username"] = u.username;
            jo["displayName"] = u.displayName;
            arr.push_back(json::Value(jo));
        }
        return ok({{"eligible", json::Value(arr)}});
    });

    server.addRoute("POST", "/api/elections/:id/eligibility", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !canAdminElection(*user, *e)) return fail(403, "Administrator access required for this election");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string action = body.getString("action", "grant");
        long long userId = body.getInt("userId", 0);
        std::string username = trimStr(body.getString("username"));
        if (userId == 0 && !username.empty()) {
            auto u = store.findUserByUsername(username);
            if (!u) return fail(404, "User not found");
            userId = u->id;
        }
        if (userId <= 0) return fail(400, "userId or username required");

        bool okRes;
        if (action == "grant") okRes = store.grantEligibility(id, userId);
        else if (action == "revoke") okRes = store.revokeEligibility(id, userId);
        else return fail(400, "action must be grant or revoke");
        if (!okRes) return fail(404, "User not found");

        store.addAudit(user->username,
                       action == "grant" ? "ELIGIBILITY_GRANTED" : "ELIGIBILITY_REVOKED",
                       id, "user id " + std::to_string(userId));
        return ok();
    });

    // ======================= Voting ========================================

    server.addRoute("POST", "/api/elections/:id/vote", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        if (user->role != "voter") return fail(403, "Only voters can cast ballots");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        long long candidateId = body.getInt("candidateId", 0);
        if (candidateId <= 0) return fail(400, "candidateId required");

        auto res = store.castBallot(id, user->id, candidateId);
        switch (res.status) {
            case Store::CastResult::Status::Ok: {
                json::Object o;
                o["ok"] = true;
                o["receipt"] = res.receipt;
                o["message"] = json::Value(std::string("Your vote has been recorded. Save this receipt code."));
                return ok(o);
            }
            case Store::CastResult::Status::ElectionNotFound:   return fail(404, "Election not found");
            case Store::CastResult::Status::ElectionNotOpen:    return fail(409, "This election is not open for voting");
            case Store::CastResult::Status::OutsideVotingWindow:return fail(409, "Voting is not open at this time");
            case Store::CastResult::Status::NotEligible:        return fail(403, "You are not eligible to vote in this election");
            case Store::CastResult::Status::AlreadyVoted:       return fail(409, "You have already voted in this election");
            case Store::CastResult::Status::InvalidCandidate:   return fail(400, "Unknown candidate for this election");
        }
        return fail(500, "Unexpected state");
    });

    // ======================= Results =======================================

    server.addRoute("GET", "/api/elections/:id/results", [&](const HttpServer::Request& req) {
        auto userOpt = currentUser(req, store);
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e) return fail(404, "Election not found");

        bool published = e->resultsPublic && (e->status == "published" || e->status == "archived");
        bool liveAllowed = e->showLiveResults && (e->status == "open" || e->status == "closed");

        if (published) {
            // Public read; authentication optional.
        } else if (liveAllowed) {
            // Config explicitly permits live totals; still requires auth to
            // avoid unauthenticated scraping.
            if (!userOpt) return fail(401, "Not authenticated");
            if (!userCanViewElection(*userOpt, store, *e)) return fail(403, "Access denied");
        } else {
            // Admins/observers may inspect configured data; everyone else gets
            // a uniform denial that reveals nothing.
            if (!userOpt) return fail(401, "Not authenticated");
            if (!canObserveElection(*userOpt, *e)) {
                return fail(403, "Results are not published yet");
            }
        }

        auto tally = store.latestTally(id);
        if (!tally) {
            // A permitted reader may trigger the (idempotent) tally
            // computation; it is only run when someone is actually allowed
            // to see the results.
            bool maySee = published || liveAllowed ||
                          (userOpt && canObserveElection(*userOpt, *e));
            if (maySee) tally = store.tallyElection(id);
        }
        if (!tally) {
            return fail(404, "No tally has been run for this election yet");
        }

        json::Array arr;
        for (const auto& tr : tally->candidates) {
            json::Object jo;
            jo["candidateId"] = (double)tr.candidateId;
            jo["name"] = tr.name;
            jo["party"] = tr.party;
            jo["votes"] = (double)tr.votes;
            arr.push_back(json::Value(jo));
        }

        json::Object o;
        o["ok"] = true;
        o["electionId"] = (double)id;
        o["electionName"] = e->name;
        o["status"] = e->status;
        o["published"] = published;
        o["totalBallots"] = (double)tally->totalBallots;
        o["totalParticipations"] = (double)tally->totalParticipations;
        o["integrityOk"] = tally->integrityOk;
        o["results"] = arr;
        // No leader announcement, no ranking beyond raw counts — publication
        // order stays neutral (candidate position order).
        return ok(o);
    });

    server.addRoute("GET", "/api/elections/:id/results.csv", [&](const HttpServer::Request& req) {
        auto userOpt = currentUser(req, store);
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e) return fail(404, "Election not found");

        bool published = e->resultsPublic && (e->status == "published" || e->status == "archived");
        bool liveAllowed = e->showLiveResults && (e->status == "open" || e->status == "closed");
        if (!published) {
            if (!liveAllowed) {
                if (!userOpt) return fail(401, "Not authenticated");
                if (!canObserveElection(*userOpt, *e)) return fail(403, "Results are not published yet");
            } else if (!userOpt) {
                return fail(401, "Not authenticated");
            }
        }

        auto tally = store.latestTally(id);
        if (!tally) {
            bool published = e->resultsPublic && (e->status == "published" || e->status == "archived");
            bool liveAllowed = e->showLiveResults && (e->status == "open" || e->status == "closed");
            bool maySee = published || liveAllowed ||
                          (userOpt && canObserveElection(*userOpt, *e));
            if (maySee) tally = store.tallyElection(id);
        }
        if (!tally) return fail(404, "No tally has been run for this election yet");

        std::ostringstream out;
        out << "election_id,election_name,election_status,tally_created_at,candidate_id,candidate_name,candidate_party,votes\n";
        for (const auto& tr : tally->candidates) {
            auto esc = [](const std::string& s){
                std::string r;
                for (char c : s) {
                    if (c == ',' || c == '"' || c == '\n') { r += '"'; r += c; if (c=='"') r += '"'; }
                    else r += c;
                }
                return r;
            };
            out << id << ',' << esc(e->name) << ',' << e->status << ',' << tally->createdAt
                << ',' << tr.candidateId << ',' << esc(tr.name) << ',' << esc(tr.party)
                << ',' << tr.votes << '\n';
        }

        HttpServer::Response r;
        r.status = 200;
        r.contentType = "text/csv; charset=utf-8";
        r.headers["Content-Disposition"] = "attachment; filename=\"election-" +
                                           std::to_string(id) + "-results.csv\"";
        r.body = out.str();
        return r;
    });

    // ======================= Users administration ==========================

    server.addRoute("GET", "/api/users", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long orgId = 0;
        auto it = req.params.find("orgId");
        if (it != req.params.end()) {
            try { orgId = std::stoll(it->second); } catch (...) { return fail(400, "Invalid orgId"); }
        }
        std::optional<long long> scope;
        if (isPlatformAdmin(*user)) {
            scope = orgId != 0 ? std::optional<long long>(orgId) : std::nullopt;
        } else if (user->role == "org_admin") {
            scope = user->orgId;
        } else {
            return fail(403, "Administrator access required");
        }
        json::Array arr;
        for (const auto& u : store.listUsers(scope)) {
            json::Object jo;
            jo["id"] = (double)u.id;
            jo["username"] = u.username;
            jo["displayName"] = u.displayName;
            jo["role"] = u.role;
            jo["orgId"] = (double)u.orgId;
            jo["disabled"] = u.disabled;
            arr.push_back(json::Value(jo));
        }
        return ok({{"users", json::Value(arr)}});
    });

    server.addRoute("POST", "/api/users", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        if (!isPlatformAdmin(*user) && user->role != "org_admin") {
            return fail(403, "Administrator access required");
        }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        User u;
        u.username = trimStr(body.getString("username"));
        u.displayName = trimStr(body.getString("displayName"));
        u.role = body.getString("role", "voter");
        std::string password = body.getString("password");

        if (!validUsername(u.username)) return fail(400, "Username must be 3-32 characters (letters, digits, _ . -)");
        if (u.displayName.empty()) u.displayName = u.username;
        if (!validTextField(u.displayName, 1, 100)) return fail(400, "Display name too long");
        if (!validPassword(password)) return fail(400, "Password must be at least 8 characters");

        long long orgId = body.getInt("orgId", 0);
        if (isPlatformAdmin(*user)) {
            // Platform admins may place users anywhere; default no org.
            u.orgId = orgId;
            if (u.role != "voter" && u.role != "org_admin" && u.role != "observer" &&
                u.role != "platform_admin") {
                return fail(400, "Invalid role");
            }
        } else {
            // Org admins manage only their own organization and only its roles.
            u.orgId = user->orgId;
            if (u.role != "voter" && u.role != "org_admin" && u.role != "observer") {
                return fail(403, "Organization administrators cannot create platform administrators");
            }
        }
        if (u.role == "voter" && u.orgId == 0) return fail(400, "Voters must belong to an organization");

        u.passwordHash = crypto::hashPassword(password);
        auto res = store.createUser(u);
        if (res == Store::CreateUserResult::DuplicateUsername) return fail(409, "Username is already taken");
        if (res == Store::CreateUserResult::OrgNotFound) return fail(404, "Organization not found");
        store.addAudit(user->username, "USER_CREATED", 0, u.username + " role " + u.role);
        return created({{"user", userJson(u)}});
    });

    server.addRoute("POST", "/api/users/:id/disable", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid user id"); }
        auto target = store.findUserById(id);
        if (!target) return fail(404, "User not found");
        if (isPlatformAdmin(*user)) {
            // ok
        } else if (user->role == "org_admin") {
            if (target->orgId != user->orgId) return fail(403, "Not in your organization");
            if (target->role == "platform_admin") return fail(403, "Cannot disable platform administrators");
        } else {
            return fail(403, "Administrator access required");
        }
        if (target->id == user->id) return fail(400, "Cannot disable your own account");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        bool disabled = body.has("disabled") ? body.at("disabled").asBool() : true;
        store.setUserDisabled(target->id, disabled);
        store.addAudit(user->username, disabled ? "USER_DISABLED" : "USER_ENABLED", 0, target->username);
        return ok();
    });

    // ======================= Audit =========================================

    server.addRoute("GET", "/api/audit", [&](const HttpServer::Request& req) {
        auto user = currentUser(req, store);
        if (!user) return fail(401, "Not authenticated");

        long long electionId = 0;
        auto it = req.params.find("electionId");
        if (it != req.params.end()) {
            try { electionId = std::stoll(it->second); } catch (...) { return fail(400, "Invalid electionId"); }
        }

        if (!isPlatformAdmin(*user)) {
            if (user->role != "org_admin" && user->role != "observer") {
                return fail(403, "Administrator or observer access required");
            }
            // Scoped: only events of elections in the user's organization.
            if (electionId != 0) {
                auto e = store.getElection(electionId);
                if (!e || e->orgId != user->orgId) return fail(403, "Access denied");
            }
        }

        json::Array arr;
        for (const auto& ev : store.listAudit(electionId, 500)) {
            json::Object jo;
            jo["ts"] = ev.ts;
            jo["actor"] = ev.actor;
            jo["action"] = ev.action;
            jo["electionId"] = (double)ev.electionId;
            jo["detail"] = ev.detail;
            arr.push_back(json::Value(jo));
        }
        return ok({{"entries", json::Value(arr)}});
    });

    // ======================= Platform bootstrap ============================

    server.addRoute("GET", "/api/bootstrap/status", [&](const HttpServer::Request&) {
        bool hasAdmin = false;
        for (const auto& u : store.listUsers()) {
            if (u.role == "platform_admin" && !u.disabled) { hasAdmin = true; break; }
        }
        return ok({{"needsSetup", json::Value(!hasAdmin)}});
    });

    server.addRoute("POST", "/api/bootstrap/setup", [&](const HttpServer::Request& req) {
        // One-time platform-admin creation. Allowed only while no enabled
        // platform administrator exists. The password is supplied by the
        // operator here (or via OV_INITIAL_ADMIN_PASSWORD at startup) —
        // there is no default credential in the code.
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        bool hasAdmin = false;
        for (const auto& u : store.listUsers()) {
            if (u.role == "platform_admin" && !u.disabled) { hasAdmin = true; break; }
        }
        if (hasAdmin) return fail(409, "A platform administrator already exists");

        std::string username = trimStr(body.getString("username"));
        std::string password = body.getString("password");
        if (!validUsername(username)) return fail(400, "Username must be 3-32 characters (letters, digits, _ . -)");
        if (!validPassword(password)) return fail(400, "Password must be at least 8 characters");

        User u;
        u.username = username;
        u.displayName = "Platform Administrator";
        u.role = "platform_admin";
        u.passwordHash = crypto::hashPassword(password);
        if (store.createUser(u) != Store::CreateUserResult::Ok) return fail(409, "Username is already taken");
        store.addAudit(username, "BOOTSTRAP_ADMIN", 0, "platform administrator created via setup");
        json::Object o;
        o["user"] = userJson(u);
        return created(o);
    });
}
