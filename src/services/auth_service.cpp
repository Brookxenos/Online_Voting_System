#include "auth_service.h"
#include "../crypto.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <mutex>

namespace {
struct Bucket { std::vector<long long> failures; };
std::mutex rateMutex;
std::map<std::string, Bucket> buckets;
long long nowSeconds() { return static_cast<long long>(std::time(nullptr)); }
std::string nowIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}
bool limited(const std::string& key) {
    std::lock_guard<std::mutex> lock(rateMutex);
    auto it = buckets.find(key);
    if (it == buckets.end()) return false;
    auto& f = it->second.failures;
    const auto now = nowSeconds();
    f.erase(std::remove_if(f.begin(), f.end(), [now](long long t) { return now - t > 300; }), f.end());
    return f.size() >= 5 && now - f.back() < 300;
}
void failure(const std::string& key) {
    std::lock_guard<std::mutex> lock(rateMutex);
    buckets[key].failures.push_back(nowSeconds());
    if (buckets.size() > 10000) buckets.clear();
}
void clear(const std::string& key) {
    std::lock_guard<std::mutex> lock(rateMutex);
    buckets.erase(key);
}
bool validUsername(const std::string& s) {
    if (s.size() < 3 || s.size() > 32) return false;
    for (unsigned char c : s) if (!std::isalnum(c) && c != '_' && c != '.' && c != '-') return false;
    return true;
}
bool validPassword(const std::string& s) { return s.size() >= 8 && s.size() <= 256; }
}

AuthService::AuthService(Store& store) : store_(store) {}

AuthService::RegisterResult AuthService::registerVoter(const std::string& username,
        const std::string& displayName, const std::string& password, const std::string& clientIp) {
    if (!validUsername(username)) return {RegisterStatus::InvalidUsername, {}};
    const std::string name = displayName.empty() ? username : displayName;
    if (name.empty() || name.size() > 100) return {RegisterStatus::InvalidDisplayName, {}};
    if (!validPassword(password)) return {RegisterStatus::InvalidPassword, {}};
    const auto key = "reg|" + clientIp;
    if (limited(key)) return {RegisterStatus::Unavailable, {}};
    User user;
    user.username = username; user.displayName = name; user.role = "voter";
    for (const auto& org : store_.listOrganizations()) {
        if (org.allowSelfRegistration) { user.orgId = org.id; break; }
    }
    if (!user.orgId) return {RegisterStatus::Unavailable, {}};
    user.passwordHash = crypto::hashPassword(password);
    auto result = store_.createUser(user);
    if (result == Store::CreateUserResult::DuplicateUsername) {
        failure(key); return {RegisterStatus::DuplicateUsername, {}};
    }
    if (result != Store::CreateUserResult::Ok) return {RegisterStatus::Unavailable, {}};
    store_.addAudit(username, "REGISTER", 0, "self-registration");
    return {RegisterStatus::Ok, user};
}

AuthService::LoginResult AuthService::login(const std::string& username, const std::string& password,
        const std::string& clientIp, int sessionHours) {
    const auto key = "login|" + clientIp + "|" + username;
    if (limited(key)) {
        store_.addAudit(username, "LOGIN_RATE_LIMITED", 0, "");
        return {false, true, {}, {}};
    }
    auto user = store_.findUserByUsername(username);
    if (!user || user->disabled || !crypto::verifyPassword(password, user->passwordHash)) {
        failure(key); store_.addAudit(username, "LOGIN_FAIL", 0, "");
        return {};
    }
    if (user->mustChangePassword) {
        std::string now = nowIso();
        if (!user->passwordResetExpiresAt.empty() && now > user->passwordResetExpiresAt) {
            failure(key);
            store_.addAudit(username, "LOGIN_FAIL", 0, "credential expired");
            return {};
        }
        if (user->passwordResetUsed) {
            failure(key);
            store_.addAudit(username, "LOGIN_FAIL", 0, "credential already used");
            return {};
        }
        store_.markResetPasswordUsed(user->id);
        user->passwordResetUsed = true;
        store_.addAudit(username, "RESET_LOGIN", 0, "one-time credential consumed");
    }
    if (!crypto::isModernHash(user->passwordHash)) {
        store_.updatePasswordHash(user->id, crypto::hashPassword(password));
        store_.addAudit(username, "PASSWORD_UPGRADED", 0, "legacy hash upgraded to Argon2id");
    }
    clear(key);
    auto token = store_.createSession(user->id, sessionHours);
    store_.addAudit(username, "LOGIN_OK", 0, "");
    return {true, false, token, *user};
}

void AuthService::logout(const std::string& token) { if (!token.empty()) store_.revokeSession(token); }
std::optional<User> AuthService::userForSession(const std::string& token) {
    return token.empty() ? std::nullopt : store_.userForSessionToken(token);
}
bool AuthService::changePassword(const User& user, const std::string& current, const std::string& next) {
    if (!validPassword(next)) return false;
    if (!crypto::verifyPassword(current, user.passwordHash)) {
        store_.addAudit(user.username, "PASSWORD_CHANGE_FAIL", 0, ""); return false;
    }
    store_.updatePasswordHash(user.id, crypto::hashPassword(next));
    store_.revokeAllSessions(user.id);
    store_.addAudit(user.username, "PASSWORD_CHANGED", 0, "");
    return true;
}
