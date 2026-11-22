#include "api.h"
#include "json.h"
#include "crypto.h"
#include "models.h"
#include "services/voting_service.h"
#include "services/auth_service.h"
#include "services/user_service.h"
#include "services/organization_service.h"
#include "services/election_service.h"
#include "services/election_lifecycle_service.h"
#include "services/results_service.h"
#include "services/candidate_service.h"
#include "services/eligibility_service.h"

#include <algorithm>
#include <iostream>
#include <memory>
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

// ---------------------------------------------------------------------------
// Authentication helpers
// ---------------------------------------------------------------------------

const char* kSessionCookie = "ov_session";

std::optional<User> currentUser(const HttpServer::Request& req, AuthService& auth) {
    std::string token = req.cookie(kSessionCookie);
    return auth.userForSession(token);
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
    o["discoverable"] = e.discoverable;
    o["showLiveResults"] = e.showLiveResults;
    o["startAt"] = e.startAt;
    o["endAt"] = e.endAt;
    o["createdAt"] = e.createdAt;
    o["publishedAt"] = e.publishedAt;
    return json::Value(o);
}

bool validDateFilter(const std::string& value) {
    if (value.empty()) return true;
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i != 4 && i != 7 && !std::isdigit((unsigned char)value[i])) return false;
    }
    int year = std::stoi(value.substr(0, 4));
    int month = std::stoi(value.substr(5, 2));
    int day = std::stoi(value.substr(8, 2));
    if (month < 1 || month > 12) return false;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int maxDay = days[month - 1];
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) maxDay = 29;
    return day >= 1 && day <= maxDay;
}

bool parsePage(const HttpServer::Request& req, int& limit, int& offset) {
    try {
        const std::string rawLimit = req.param("limit");
        const std::string rawOffset = req.param("offset");
        limit = rawLimit.empty() ? 20 : std::stoi(rawLimit);
        offset = rawOffset.empty() ? 0 : std::stoi(rawOffset);
        return limit >= 1 && limit <= 100 && offset >= 0 && offset <= 1000000 &&
               (rawLimit.empty() || std::to_string(limit) == rawLimit) &&
               (rawOffset.empty() || std::to_string(offset) == rawOffset);
    } catch (...) { return false; }
}

json::Array electionListJson(const std::vector<Election>& elections) {
    json::Array arr;
    for (const auto& e : elections) arr.push_back(electionJson(e));
    return arr;
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
    auto ballotRepository = std::make_shared<BallotRepository>(store);
    auto votingService = std::make_shared<VotingService>(*ballotRepository);
    auto authService = std::make_shared<AuthService>(store);
    auto userRepository = std::make_shared<UserRepository>(store);
    auto userService = std::make_shared<UserService>(*userRepository);
    auto organizationRepository = std::make_shared<OrganizationRepository>(store);
    auto organizationService = std::make_shared<OrganizationService>(*organizationRepository);
    auto electionRepository = std::make_shared<ElectionRepository>(store);
    auto electionService = std::make_shared<ElectionService>(*electionRepository);
    auto electionLifecycleService = std::make_shared<ElectionLifecycleService>(*electionRepository);
    auto resultsService = std::make_shared<ResultsService>(store);
    auto candidateRepository = std::make_shared<CandidateRepository>(store);
    auto candidateService = std::make_shared<CandidateService>(*candidateRepository);
    auto eligibilityRepository = std::make_shared<EligibilityRepository>(store);
    auto eligibilityService = std::make_shared<EligibilityService>(*eligibilityRepository);

    // ======================= Authentication ================================

    server.addRoute("POST", "/api/auth/register", [&, authService](const HttpServer::Request& req) {
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string username = trimStr(body.getString("username"));
        std::string displayName = trimStr(body.getString("displayName"));
        std::string password = body.getString("password");

        auto result = authService->registerVoter(username, displayName, password, req.clientIp);
        using RS = AuthService::RegisterStatus;
        if (result.status == RS::InvalidUsername) return fail(400, "Username must be 3-32 characters (letters, digits, _ . -)");
        if (result.status == RS::InvalidDisplayName) return fail(400, "Display name too long");
        if (result.status == RS::InvalidPassword) return fail(400, "Password must be at least 8 characters");
        if (result.status == RS::DuplicateUsername) return fail(409, "Username is already taken");
        if (result.status == RS::Unavailable) return fail(400, "Registration is not available");
        json::Object o;
        o["user"] = userJson(result.user);
        return created(o);
    });

    server.addRoute("POST", "/api/auth/login", [&, authService](const HttpServer::Request& req) {
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string username = trimStr(body.getString("username"));
        std::string password = body.getString("password");
        if (username.empty() || password.empty()) return fail(400, "username and password required");

        auto result = authService->login(username, password, req.clientIp, cfg.sessionHours);
        if (result.limited) return fail(429, "Too many failed attempts; try again later");
        if (!result.ok) return fail(401, "Invalid username or password");

        HttpServer::Response r = ok();
        r.body = json::Value([&]{
            json::Object o;
            o["ok"] = true;
            o["user"] = userJson(result.user);
            return json::Value(o);
        }()).dump();
        // Secure flag is only set when explicitly configured (HTTPS deployment);
        // HttpOnly + SameSite=Lax always apply.
        r.setCookie(kSessionCookie, result.token, cfg.sessionHours * 3600, true, "Lax");
        if (cfg.secureCookies) {
            r.setCookies.back() += "; Secure";
        }
        return r;
    });

    server.addRoute("POST", "/api/auth/logout", [&, authService](const HttpServer::Request& req) {
        std::string token = req.cookie(kSessionCookie);
        authService->logout(token);
        HttpServer::Response r = ok();
        r.clearCookie(kSessionCookie);
        if (cfg.secureCookies) r.setCookies.back() += "; Secure";
        return r;
    });

    server.addRoute("GET", "/api/me", [&, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Object o;
        o["user"] = userJson(*user);
        return ok(o);
    });

    server.addRoute("GET", "/api/me/dashboard", [&, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        if (user->role != "voter") return fail(403, "Voter access required");
        json::Array elections, participation;
        for (const auto& e : store.listElectionsVisibleTo(*user)) {
            json::Object jo = electionJson(e).asObject();
            bool eligible = store.isEligible(e.id, user->id);
            bool hasVoted = store.hasParticipation(e.id, user->id);
            jo["eligible"] = eligible;
            jo["hasVoted"] = hasVoted;
            if (hasVoted) {
                const std::string votedAt = store.participationAt(e.id, user->id);
                jo["votedAt"] = votedAt;
                json::Object item;
                item["electionId"] = (double)e.id;
                item["electionName"] = e.name;
                item["status"] = e.status;
                item["votedAt"] = votedAt;
                participation.push_back(json::Value(item));
            }
            elections.push_back(json::Value(jo));
        }
        return ok({{"elections", json::Value(elections)},
                   {"participation", json::Value(participation)}});
    });

    server.addRoute("POST", "/api/me/profile", [&, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        const std::string displayName = trimStr(body.getString("displayName"));
        if (!validTextField(displayName, 1, 100)) {
            return fail(400, "Display name must be between 1 and 100 characters");
        }
        store.updateDisplayName(user->id, displayName);
        user->displayName = displayName;
        return ok({{"user", userJson(*user)}});
    });

    server.addRoute("POST", "/api/auth/password", [&, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string current = body.getString("currentPassword");
        std::string next = body.getString("newPassword");
        if (next.size() < 8 || next.size() > 256) return fail(400, "New password must be at least 8 characters");
        if (!authService->changePassword(*user, current, next)) return fail(401, "Current password is incorrect");
        HttpServer::Response r = ok({{"message", json::Value(std::string("Password updated; please log in again"))}});
        r.clearCookie(kSessionCookie);
        if (cfg.secureCookies) r.setCookies.back() += "; Secure";
        return r;
    });

    // ======================= Organizations =================================

    server.addRoute("GET", "/api/orgs", [&, authService, organizationService, organizationRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Array arr;
        OrganizationService::Status status;
        for (const auto& o : organizationService->list(*user, status)) {
            json::Object jo;
            jo["id"] = (double)o.id; jo["name"] = o.name; jo["slug"] = o.slug;
            jo["allowSelfRegistration"] = o.allowSelfRegistration; jo["createdAt"] = o.createdAt;
            arr.push_back(json::Value(jo));
        }
        json::Object root;
        root["ok"] = true;
        root["orgs"] = arr;
        return ok(root);
    });

    server.addRoute("POST", "/api/orgs", [&, authService, organizationService, organizationRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user || !isPlatformAdmin(*user)) return fail(403, "Platform administrator access required");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string name = trimStr(body.getString("name"));
        std::string slug = trimStr(body.getString("slug"));
        bool selfReg = body.has("allowSelfRegistration") ? body.at("allowSelfRegistration").asBool() : true;
        auto result = organizationService->create(*user, name, slug, selfReg);
        if (result.status == OrganizationService::Status::Invalid) return fail(400, "Organization name required (1-100 chars) and slug must be 3-32 URL-safe characters");
        if (result.status == OrganizationService::Status::Duplicate) return fail(409, "Slug already exists");
        if (result.status != OrganizationService::Status::Ok) return fail(403, "Platform administrator access required");
        Organization o = result.organization;
        store.addAudit(user->username, "ORG_CREATED", 0, "slug " + slug);

        json::Object jo;
        jo["id"] = (double)o.id;
        jo["name"] = o.name;
        jo["slug"] = o.slug;
        return created({{"org", json::Value(jo)}});
    });

    server.addRoute("GET", "/api/orgs/:id", [&, authService, organizationService, organizationRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id; try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid organization id"); }
        auto result = organizationService->get(*user, id);
        if (result.status == OrganizationService::Status::NotFound) return fail(404, "Organization not found");
        if (result.status != OrganizationService::Status::Ok) return fail(403, "Access denied");
        const auto& o = result.organization;
        json::Object jo{{"id", (double)o.id}, {"name", o.name}, {"slug", o.slug},
                        {"allowSelfRegistration", o.allowSelfRegistration}, {"createdAt", o.createdAt}};
        return ok({{"org", json::Value(jo)}});
    });

    server.addRoute("POST", "/api/orgs/:id", [&, authService, organizationService, organizationRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id; try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid organization id"); }
        json::Value body; HttpServer::Response err; if (!parseBody(req, body, err)) return err;
        const auto current = store.getOrganization(id);
        const std::string name = trimStr(body.getString("name", current ? current->name : ""));
        const std::string slug = trimStr(body.getString("slug", current ? current->slug : ""));
        const bool selfReg = body.has("allowSelfRegistration") ? body.at("allowSelfRegistration").asBool() : (current && current->allowSelfRegistration);
        auto result = organizationService->update(*user, id, name, slug, selfReg);
        if (result.status == OrganizationService::Status::NotFound) return fail(404, "Organization not found");
        if (result.status == OrganizationService::Status::Forbidden) return fail(403, "Access denied");
        if (result.status == OrganizationService::Status::Duplicate) return fail(409, "Slug already exists");
        if (result.status != OrganizationService::Status::Ok) return fail(400, "Invalid organization information");
        store.addAudit(user->username, "ORG_UPDATED", 0, "slug " + result.organization.slug);
        const auto& o = result.organization;
        json::Object jo{{"id", (double)o.id}, {"name", o.name}, {"slug", o.slug},
                        {"allowSelfRegistration", o.allowSelfRegistration}, {"createdAt", o.createdAt}};
        return ok({{"org", json::Value(jo)}});
    });

    // ======================= Elections =====================================

    server.addRoute("GET", "/api/elections", [&, authService, electionService, electionRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Array arr;
        for (const auto& e : electionService->list(*user)) {
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

    // Anonymous discovery is opt-in per election. Existing elections remain
    // hidden until an administrator enables discoverable while it is a draft.
    server.addRoute("GET", "/api/public/elections", [&, authService](const HttpServer::Request& req) {
        int limit, offset;
        if (!parsePage(req, limit, offset)) return fail(400, "Invalid limit or offset");
        std::string status = req.param("status");
        if (!status.empty() && status != "open" && status != "closed" &&
            status != "published" && status != "archived") return fail(400, "Invalid election status");
        std::string from = req.param("from"), to = req.param("to");
        if (!validDateFilter(from) || !validDateFilter(to) || (!from.empty() && !to.empty() && from > to))
            return fail(400, "Dates must use YYYY-MM-DD and form a valid range");
        std::string sort = req.param("sort");
        if (sort.empty()) sort = "start-asc";
        if (sort != "start-asc" && sort != "start-desc" && sort != "title" && sort != "newest")
            return fail(400, "Invalid sort order");
        std::string q = req.param("q");
        if (q.size() > 100) return fail(400, "Search query is too long");
        ElectionPage page = store.listPublicElections(q, status, from, to, sort, limit, offset);
        return ok({{"elections", json::Value(electionListJson(page.elections))},
                   {"total", json::Value((double)page.total)},
                   {"limit", json::Value((double)limit)},
                   {"offset", json::Value((double)offset)}});
    });

    server.addRoute("GET", "/api/public/elections/:id", [&, authService](const HttpServer::Request& req) {
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e || !e->discoverable || e->status == "draft") return fail(404, "Election not found");
        json::Object o = electionJson(*e).asObject();
        json::Array candidates;
        for (const auto& c : store.listCandidates(id)) candidates.push_back(candidateJson(c));
        o["candidates"] = candidates;
        return ok(o);
    });

    server.addRoute("GET", "/api/public/results", [&, authService](const HttpServer::Request& req) {
        int limit, offset;
        if (!parsePage(req, limit, offset)) return fail(400, "Invalid limit or offset");
        std::string from = req.param("from"), to = req.param("to");
        if (!validDateFilter(from) || !validDateFilter(to) || (!from.empty() && !to.empty() && from > to))
            return fail(400, "Dates must use YYYY-MM-DD and form a valid range");
        std::string sort = req.param("sort");
        if (sort.empty()) sort = "newest";
        if (sort != "start-asc" && sort != "start-desc" && sort != "title" && sort != "newest")
            return fail(400, "Invalid sort order");
        std::string q = req.param("q");
        if (q.size() > 100) return fail(400, "Search query is too long");
        ElectionPage page = store.listPublicResults(q, from, to, sort, limit, offset);
        json::Array arr;
        for (const auto& e : page.elections) {
            json::Object jo;
            jo["id"] = (double)e.id;
            jo["name"] = e.name;
            jo["status"] = e.status;
            jo["publishedAt"] = e.publishedAt;
            arr.push_back(json::Value(jo));
        }
        return ok({{"elections", json::Value(arr)}, {"total", json::Value((double)page.total)},
                   {"limit", json::Value((double)limit)}, {"offset", json::Value((double)offset)}});
    });

    server.addRoute("POST", "/api/elections", [&, authService, electionService, electionRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        long long orgId = (long long)body.getInt("orgId", 0);
        std::string name = trimStr(body.getString("name"));
        std::string description = body.getString("description");
        Election e;
        e.orgId = orgId;
        e.name = name;
        e.description = description;
        e.discoverable = body.has("discoverable") ? body.at("discoverable").asBool() : false;
        e.showLiveResults = body.has("showLiveResults") ? body.at("showLiveResults").asBool() : false;
        e.startAt = body.getString("startAt");
        e.endAt = body.getString("endAt");
        e.createdBy = user->id;
        auto result = electionService->create(*user,e);
        if (result.status == ElectionService::Status::Forbidden) return fail(403, "You do not administer this organization");
        if (result.status == ElectionService::Status::NotFound) return fail(404, "Organization not found");
        if (result.status != ElectionService::Status::Ok) {
            if (orgId == 0) return fail(400, "orgId required");
            if (!validTextField(name, 1, 200)) return fail(400, "Election name required (1-200 chars)");
            if (description.size() > 2000) return fail(400, "Description too long");
            return fail(400, "Dates must be empty or ISO 8601 UTC (YYYY-MM-DDTHH:MM:SSZ)");
        }
        e = result.election;
        store.addAudit(user->username, "ELECTION_CREATED", e.id, name);

        return created({{"election", electionJson(e)}});
    });

    server.addRoute("GET", "/api/elections/:id", [&, authService, electionService, electionRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto result = electionService->get(*user,id);
        if (result.status != ElectionService::Status::Ok) return fail(404, "Election not found");
        auto e = std::optional<Election>(result.election);

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

    server.addRoute("POST", "/api/elections/:id", [&, authService, electionService, electionRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
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
        if (body.has("discoverable")) changes.discoverable = body.at("discoverable").asBool();
        if (body.has("showLiveResults")) changes.showLiveResults = body.at("showLiveResults").asBool();
        if (body.has("startAt")) changes.startAt = body.getString("startAt");
        if (body.has("endAt")) changes.endAt = body.getString("endAt");
        auto result = electionService->updateDraft(*user,id,changes);
        if (result.status == ElectionService::Status::Invalid) return fail(400, "Dates must be valid ISO 8601 UTC timestamps and end after start");
        if (result.status == ElectionService::Status::Conflict) return fail(409, "Only draft elections can be edited");
        if (result.status != ElectionService::Status::Ok) return fail(403, "Administrator access required for this election");
        store.addAudit(user->username, "ELECTION_CONFIGURED", id, "draft updated");
        return ok({{"election", electionJson(result.election)}});
    });

    server.addRoute("POST", "/api/elections/:id/status", [&, authService, electionLifecycleService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        std::string newStatus = body.getString("status");
        if (newStatus != "open" && newStatus != "closed" && newStatus != "published" && newStatus != "archived") {
            return fail(400, "status must be open, closed, published, or archived");
        }
        auto result = electionLifecycleService->transition(*user, id, newStatus);
        if (result.status == ElectionLifecycleService::Status::Forbidden || result.status == ElectionLifecycleService::Status::NotFound)
            return fail(403, "Administrator access required for this election");
        if (result.status == ElectionLifecycleService::Status::Invalid) return fail(400, "Invalid election id");
        if (result.status == ElectionLifecycleService::Status::Conflict)
            return fail(409, "Invalid transition from '" + result.election.status + "' to '" + newStatus + "'");
        return ok({{"election", electionJson(result.election)}});
    });

    server.addRoute("DELETE", "/api/elections/:id", [&, authService, electionService](const HttpServer::Request& req) {
        auto actor = currentUser(req, *authService);
        if (!actor) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        const auto status = electionService->removeSafely(*actor, id);
        if (status == ElectionService::DeleteStatus::Invalid) return fail(400, "Invalid election id");
        if (status == ElectionService::DeleteStatus::NotFound) return fail(404, "Election not found");
        if (status == ElectionService::DeleteStatus::Forbidden) return fail(403, "Administrator access required for this election");
        if (status == ElectionService::DeleteStatus::Conflict)
            return fail(409, "Only draft elections with no ballots, participation, published results, or tally records can be deleted; archive other elections to retain their records");
        return ok({{"message", json::Value(std::string("Draft election deleted; audit history retained"))}});
    });

    // Permanent deletion of a posted election. This is deliberately separate
    // from DELETE /api/elections/:id (draft-only) and requires both the exact
    // election name and an explicit irreversible-action acknowledgement.
    server.addRoute("POST", "/api/elections/:id/purge", [&, authService, electionService](const HttpServer::Request& req) {
        auto actor = currentUser(req, *authService);
        if (!actor) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        if (!body.has("acknowledgeIrreversible") || !body.at("acknowledgeIrreversible").asBool()) {
            return fail(400, "Explicit acknowledgement of permanent deletion is required");
        }
        const std::string confirmName = trimStr(body.getString("confirmName"));
        if (confirmName.empty()) {
            return fail(400, "Type the exact election name to confirm permanent deletion");
        }
        auto result = electionService->purge(*actor, id, confirmName);
        if (result.status == ElectionService::PurgeStatus::Invalid) return fail(400, "Invalid election id");
        if (result.status == ElectionService::PurgeStatus::NotFound) return fail(404, "Election not found");
        if (result.status == ElectionService::PurgeStatus::Forbidden)
            return fail(403, "Administrator access required for this election");
        if (result.status == ElectionService::PurgeStatus::Conflict)
            return fail(409, "Only published or archived elections can be permanently deleted; archive the election first");
        if (result.status == ElectionService::PurgeStatus::ConfirmationMismatch)
            return fail(400, "The election name does not match; type the exact name to confirm permanent deletion");
        json::Object deleted;
        deleted["ballots"] = (double)result.ballots;
        deleted["participations"] = (double)result.participations;
        deleted["candidates"] = (double)result.candidates;
        deleted["eligible"] = (double)result.eligible;
        json::Object response;
        response["message"] = json::Value(std::string(
            "Election permanently deleted; a sanitized audit snapshot was retained"));
        response["auditRetained"] = json::Value(true);
        response["deleted"] = json::Value(deleted);
        return ok(response);
    });

    // ======================= Candidates ====================================

    server.addRoute("GET", "/api/elections/:id/candidates", [&, authService, candidateService, candidateRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto election = store.getElection(id);
        if (!election || !userCanViewElection(*user, store, *election)) return fail(404, "Election not found");
        auto result = candidateService->list(*user, id);
        if (result.status != CandidateService::Status::Ok) return fail(404, "Election not found");
        json::Array arr;
        for (const auto& c : result.candidates) arr.push_back(candidateJson(c));
        return ok({{"candidates", json::Value(arr)}, {"status", json::Value(election->status)}});
    });

    server.addRoute("POST", "/api/elections/:id/candidates", [&, authService, candidateService, candidateRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        Candidate c;
        c.electionId = id;
        c.name = trimStr(body.getString("name"));
        c.party = trimStr(body.getString("party"));
        c.description = body.getString("description");
        auto existing = candidateService->list(*user, id);
        c.position = body.getInt("position", static_cast<int>(existing.candidates.size()) + 1);
        auto result = candidateService->create(*user, c);
        if (result.status == CandidateService::Status::Invalid) {
            if (c.name.empty() || c.name.size() > 200) return fail(400, "Candidate name required (1-200 chars)");
            return fail(400, "Field too long or position invalid");
        }
        if (result.status == CandidateService::Status::Forbidden || result.status == CandidateService::Status::NotFound)
            return fail(result.status == CandidateService::Status::NotFound ? 404 : 403,
                        result.status == CandidateService::Status::NotFound ? "Election not found" : "Administrator access required for this election");
        if (result.status == CandidateService::Status::Conflict)
            return fail(409, "Candidates can only be changed while the election is a draft");
        store.addAudit(user->username, "CANDIDATE_ADDED", id, result.candidate.name);
        return created({{"candidate", candidateJson(result.candidate)}});
    });

    // Register the fixed /remove suffix before the parameterized candidate
    // route so the router does not treat "remove" as a candidate ID.
    server.addRoute("POST", "/api/elections/:id/candidates/remove", [&, authService, candidateService, candidateRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        long long candidateId;
        try { candidateId = static_cast<long long>(body.getInt("candidateId", 0)); }
        catch (...) { return fail(400, "Invalid candidateId"); }
        if (candidateId <= 0) return fail(400, "candidateId required");
        auto result = candidateService->remove(*user, id, candidateId);
        if (result.status == CandidateService::Status::Forbidden)
            return fail(403, "Administrator access required for this election");
        if (result.status == CandidateService::Status::Conflict)
            return fail(409, "Candidate could not be removed (draft election only)");
        if (result.status != CandidateService::Status::Ok)
            return fail(409, "Candidate could not be removed (draft election only)");
        store.addAudit(user->username, "CANDIDATE_REMOVED", id, std::to_string(candidateId));
        return ok();
    });

    server.addRoute("POST", "/api/elections/:id/candidates/:candidateId", [&, authService, candidateService, candidateRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long electionId, candidateId;
        try {
            electionId = std::stoll(req.param("id"));
            candidateId = std::stoll(req.param("candidateId"));
        } catch (...) { return fail(400, "Invalid election or candidate id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        auto current = candidateService->get(*user, electionId, candidateId);
        if (current.status == CandidateService::Status::Forbidden) return fail(403, "Administrator access required for this election");
        if (current.status != CandidateService::Status::Ok) return fail(404, "Candidate not found");
        Candidate changes = current.candidate;
        if (body.has("name")) changes.name = trimStr(body.getString("name"));
        if (body.has("party")) changes.party = trimStr(body.getString("party"));
        if (body.has("description")) changes.description = body.getString("description");
        if (body.has("position")) changes.position = body.getInt("position", (int)changes.position);
        auto result = candidateService->update(*user, electionId, changes);
        if (result.status == CandidateService::Status::Invalid) return fail(400, "Candidate fields are invalid or too long");
        if (result.status == CandidateService::Status::Forbidden) return fail(403, "Administrator access required for this election");
        if (result.status == CandidateService::Status::Conflict) return fail(409, "Candidates can only be edited in a draft election");
        if (result.status != CandidateService::Status::Ok) return fail(404, "Candidate not found");
        store.addAudit(user->username, "CANDIDATE_UPDATED", electionId, result.candidate.name);
        return ok({{"candidate", candidateJson(result.candidate)}});
    });

    // ======================= Eligibility ===================================

    server.addRoute("GET", "/api/elections/:id/eligibility", [&, authService, eligibilityService, eligibilityRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto result = eligibilityService->list(*user, id);
        if (result.status != EligibilityService::Status::Ok) return fail(403, "Administrator or observer access required");
        json::Array arr;
        for (const auto& u : result.users) {
            json::Object jo;
            jo["id"] = (double)u.id;
            jo["username"] = u.username;
            jo["displayName"] = u.displayName;
            arr.push_back(json::Value(jo));
        }
        return ok({{"eligible", json::Value(arr)}});
    });

    server.addRoute("POST", "/api/elections/:id/eligibility", [&, authService, eligibilityService, eligibilityRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
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

        if (action != "grant" && action != "revoke") return fail(400, "action must be grant or revoke");
        auto result = eligibilityService->change(*user, id, userId, action == "grant");
        if (result.status == EligibilityService::Status::Forbidden) return fail(403, "Administrator access required for this election");
        if (result.status == EligibilityService::Status::Conflict) return fail(409, "Eligibility can only be changed while the election is a draft");
        if (result.status == EligibilityService::Status::InvalidVoter)
            return fail(400, "Eligibility can only be granted to an active voter in this election's organization");
        if (result.status == EligibilityService::Status::NotFound) return fail(404, "User or election not found");
        if (result.status != EligibilityService::Status::Ok) return fail(400, "Invalid eligibility request");
        if (result.changed) store.addAudit(user->username,
                       action == "grant" ? "ELIGIBILITY_GRANTED" : "ELIGIBILITY_REVOKED",
                       id, "user id " + std::to_string(userId));
        return ok();
    });

    // ======================= Voting ========================================

    server.addRoute("POST", "/api/elections/:id/vote", [&, votingService, ballotRepository, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        long long candidateId = body.getInt("candidateId", 0);
        if (candidateId <= 0) return fail(400, "candidateId required");

        auto res = votingService->castBallot(*user, id, candidateId);
        switch (res.status) {
            case VotingService::Status::Ok: {
                json::Object o;
                o["ok"] = true;
                o["receipt"] = res.receipt;
                o["message"] = json::Value(std::string("Your vote has been recorded. Save this receipt code."));
                return ok(o);
            }
            case VotingService::Status::Forbidden:               return fail(403, "Only voters can cast ballots");
            case VotingService::Status::InvalidRequest:           return fail(400, "Invalid election or candidate id");
            case VotingService::Status::ElectionNotFound:         return fail(404, "Election not found");
            case VotingService::Status::ElectionNotOpen:          return fail(409, "This election is not open for voting");
            case VotingService::Status::OutsideVotingWindow:      return fail(409, "Voting is not open at this time");
            case VotingService::Status::NotEligible:              return fail(403, "You are not eligible to vote in this election");
            case VotingService::Status::AlreadyVoted:             return fail(409, "You have already voted in this election");
            case VotingService::Status::InvalidCandidate:         return fail(400, "Unknown candidate for this election");
            case VotingService::Status::StorageFailure:           return fail(500, "Vote could not be recorded");
        }
        return fail(500, "Unexpected state");
    });

    // ======================= Results =======================================

    server.addRoute("GET", "/api/elections/:id/results", [&, authService, resultsService](const HttpServer::Request& req) {
        auto userOpt = currentUser(req, *authService);
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

        // Result retrieval must never calculate or persist a tally. Publication
        // owns tally creation; live access can only inspect an existing run.
        auto tally = resultsService->latest(id);
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
            jo["percentage"] = tally->totalBallots > 0
                ? (double)tr.votes * 100.0 / (double)tally->totalBallots : 0.0;
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

    server.addRoute("GET", "/api/elections/:id/results.csv", [&, authService, resultsService](const HttpServer::Request& req) {
        auto userOpt = currentUser(req, *authService);
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid election id"); }
        auto e = store.getElection(id);
        if (!e) return fail(404, "Election not found");

        bool published = e->resultsPublic && (e->status == "published" || e->status == "archived");
        bool liveAllowed = e->showLiveResults && (e->status == "open" || e->status == "closed");
        if (published) {
            // Public publication permits unauthenticated reads.
        } else if (liveAllowed) {
            if (!userOpt) return fail(401, "Not authenticated");
            if (!userCanViewElection(*userOpt, store, *e)) return fail(403, "Access denied");
        } else {
            if (!userOpt) return fail(401, "Not authenticated");
            if (!canObserveElection(*userOpt, *e)) return fail(403, "Results are not published yet");
        }

        auto tally = resultsService->latest(id);
        if (!tally) return fail(404, "No tally has been run for this election yet");

        std::ostringstream out;
        out << "election_id,election_name,election_status,tally_created_at,candidate_id,candidate_name,candidate_party,votes,percentage\n";
        for (const auto& tr : tally->candidates) {
            auto esc = [](const std::string& s){
                std::string r;
                size_t first = 0;
                while (first < s.size() && std::isspace((unsigned char)s[first])) ++first;
                if (first < s.size() && (s[first] == '=' || s[first] == '+' || s[first] == '-' || s[first] == '@')) r.push_back('\'');
                for (char c : s) {
                    if (c == ',' || c == '"' || c == '\n' || c == '\r') { r += '"'; r += c; if (c=='"') r += '"'; }
                    else r += c;
                }
                return r;
            };
            out << id << ',' << esc(e->name) << ',' << e->status << ',' << tally->createdAt
                << ',' << tr.candidateId << ',' << esc(tr.name) << ',' << esc(tr.party)
                << ',' << tr.votes << ',' << (tally->totalBallots > 0
                    ? (double)tr.votes * 100.0 / (double)tally->totalBallots : 0.0) << '\n';
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

    server.addRoute("GET", "/api/users", [&, authService, userService, userRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long orgId = 0;
        auto it = req.params.find("orgId");
        if (it != req.params.end()) {
            try { orgId = std::stoll(it->second); } catch (...) { return fail(400, "Invalid orgId"); }
        }
        UserService::Status status;
        auto users = userService->list(*user, orgId, status);
        if (status != UserService::Status::Ok) return fail(403, "Administrator access required");
        json::Array arr;
        for (const auto& u : users) {
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

    server.addRoute("GET", "/api/admin/summary", [&, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        std::optional<long long> scope;
        if (isPlatformAdmin(*user)) scope = std::nullopt;
        else if (user->role == "org_admin" && user->orgId > 0) scope = user->orgId;
        else return fail(403, "Administrator access required");
        AdminSummary s = store.adminSummary(scope);
        json::Object summary;
        summary["totalElections"] = (double)s.elections;
        summary["activeElections"] = (double)s.activeElections;
        summary["upcomingElections"] = (double)s.upcomingElections;
        summary["publishedElections"] = (double)s.publishedElections;
        summary["totalCandidates"] = (double)s.candidates;
        summary["activeVoters"] = (double)s.activeVoters;
        summary["eligibleVoters"] = (double)s.eligibleVoters;
        summary["participations"] = (double)s.participations;
        summary["acceptedBallots"] = (double)s.acceptedBallots;
        return ok({{"summary", json::Value(summary)}});
    });

    server.addRoute("GET", "/api/users/:id", [&, authService, userService, userRepository](const HttpServer::Request& req) {
        auto actor = currentUser(req, *authService);
        if (!actor) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid user id"); }
        auto result = userService->get(*actor, id);
        if (result.status == UserService::Status::NotFound) return fail(404, "User not found");
        if (result.status != UserService::Status::Ok) return fail(403, "Access denied");
        return ok({{"user", userJson(result.user)}});
    });

    server.addRoute("POST", "/api/users", [&, authService, userService, userRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;

        User u;
        u.username = trimStr(body.getString("username"));
        u.displayName = trimStr(body.getString("displayName"));
        u.role = body.getString("role", "voter");
        std::string password = body.getString("password");

        if (u.displayName.empty()) u.displayName = u.username;
        u.orgId = body.getInt("orgId", 0);
        auto result = userService->create(*user, u, password);
        if (result.status == UserService::Status::Forbidden) return fail(403, "Organization administrators cannot create platform administrators");
        if (result.status == UserService::Status::Duplicate) return fail(409, "Username is already taken");
        if (result.status == UserService::Status::OrgNotFound) return fail(404, "Organization not found");
        if (result.status != UserService::Status::Ok) return fail(400, "Invalid user information");
        u = result.user;
        store.addAudit(user->username, "USER_CREATED", 0, u.username + " role " + u.role);
        return created({{"user", userJson(u)}});
    });

    server.addRoute("POST", "/api/users/:id", [&, authService, userService, userRepository](const HttpServer::Request& req) {
        auto actor = currentUser(req, *authService);
        if (!actor) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid user id"); }
        json::Value body; HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        auto result = userService->update(*actor, id, trimStr(body.getString("displayName")),
                                          body.getString("role"), body.getInt("orgId", actor->orgId));
        if (result.status == UserService::Status::NotFound) return fail(404, "User not found");
        if (result.status == UserService::Status::Forbidden) return fail(403, "Access denied");
        if (result.status == UserService::Status::OrgNotFound) return fail(404, "Organization not found");
        if (result.status != UserService::Status::Ok) return fail(400, "Invalid user information");
        store.addAudit(actor->username, "USER_UPDATED", 0, result.user.username);
        return ok({{"user", userJson(result.user)}});
    });

    server.addRoute("POST", "/api/users/:id/disable", [&, authService, userService, userRepository](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid user id"); }
        json::Value body;
        HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        bool disabled = body.has("disabled") ? body.at("disabled").asBool() : true;
        auto status = userService->setDisabled(*user, id, disabled);
        if (status == UserService::Status::NotFound) return fail(404, "User not found");
        if (status != UserService::Status::Ok) return fail(403, "Access denied");
        auto target = store.findUserById(id);
        store.addAudit(user->username, disabled ? "USER_DISABLED" : "USER_ENABLED", 0, target->username);
        return ok();
    });

    server.addRoute("DELETE", "/api/users/:id", [&, authService, userService](const HttpServer::Request& req) {
        auto actor = currentUser(req, *authService);
        if (!actor) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid user id"); }
        const auto status = userService->remove(*actor, id);
        if (status == UserService::Status::NotFound) return fail(404, "User not found");
        if (status == UserService::Status::Forbidden) return fail(403, "Access denied");
        if (status == UserService::Status::Protected) return fail(403, "Platform administrator accounts are protected");
        if (status == UserService::Status::NotDisabled)
            return fail(409, "Disable the account before deleting it; this prevents accidental removal of active accounts");
        if (status == UserService::Status::Retained)
            return ok({{"message", json::Value(std::string("Account removed; linked election records retained and sessions revoked"))},
                       {"recordsRetained", json::Value(true)}});
        if (status != UserService::Status::Ok) return fail(400, "User could not be deleted");
        return ok({{"message", json::Value(std::string("User deleted; sessions and audit history handled"))},
                   {"recordsRetained", json::Value(false)}});
    });

    server.addRoute("POST", "/api/users/:id/password", [&, authService, userService, userRepository](const HttpServer::Request& req) {
        auto actor = currentUser(req, *authService);
        if (!actor) return fail(401, "Not authenticated");
        long long id;
        try { id = std::stoll(req.param("id")); } catch (...) { return fail(400, "Invalid user id"); }
        json::Value body; HttpServer::Response err;
        if (!parseBody(req, body, err)) return err;
        auto status = userService->changePassword(*actor, id, body.getString("password"));
        if (status == UserService::Status::NotFound) return fail(404, "User not found");
        if (status == UserService::Status::Forbidden) return fail(403, "Access denied");
        if (status != UserService::Status::Ok) return fail(400, "Password must be between 8 and 256 characters");
        store.addAudit(actor->username, "USER_PASSWORD_CHANGED", 0, std::to_string(id));
        return ok();
    });

    // ======================= Audit =========================================

    server.addRoute("GET", "/api/audit", [&, authService](const HttpServer::Request& req) {
        auto user = currentUser(req, *authService);
        if (!user) return fail(401, "Not authenticated");

        long long electionId = 0;
        auto it = req.params.find("electionId");
        if (it != req.params.end()) {
            try { electionId = std::stoll(it->second); } catch (...) { return fail(400, "Invalid electionId"); }
        }

        if (!isPlatformAdmin(*user) && user->role != "org_admin" && user->role != "observer")
            return fail(403, "Administrator or observer access required");
        if (electionId < 0) return fail(400, "Invalid electionId");
        if (!isPlatformAdmin(*user) && electionId != 0) {
            auto e = store.getElection(electionId);
            if (!e || e->orgId != user->orgId) return fail(403, "Access denied");
        }

        json::Array arr;
        const auto events = isPlatformAdmin(*user)
            ? store.listAudit(electionId, 500)
            : store.listAuditForOrganization(user->orgId, electionId, 500);
        for (const auto& ev : events) {
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

    server.addRoute("POST", "/api/bootstrap/setup", [&, authService](const HttpServer::Request& req) {
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
