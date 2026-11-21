#include "user_service.h"
#include "../crypto.h"
#include <cctype>

namespace {
bool validUsername(const std::string& s) {
    if (s.size() < 3 || s.size() > 32) return false;
    for (unsigned char c : s) if (!std::isalnum(c) && c != '_' && c != '.' && c != '-') return false;
    return true;
}
bool validRole(const std::string& role) {
    return role == "platform_admin" || role == "org_admin" || role == "observer" || role == "voter";
}
bool roleRequiresOrganization(const std::string& role) {
    return role == "org_admin" || role == "observer" || role == "voter";
}
bool validPassword(const std::string& p) { return p.size() >= 8 && p.size() <= 256; }
}

bool UserService::mayManage(const User& actor, const User& target) const {
    if (actor.role == "platform_admin") return true;
    return actor.role == "org_admin" && actor.orgId > 0 && actor.orgId == target.orgId &&
           target.role != "platform_admin";
}

std::vector<User> UserService::list(const User& actor, long long orgId, Status& status) {
    status = Status::Ok;
    if (actor.role == "platform_admin") return repository_.list(orgId > 0 ? std::optional<long long>(orgId) : std::nullopt);
    if ((actor.role == "org_admin" || actor.role == "observer") && actor.orgId > 0)
        return repository_.list(actor.orgId);
    status = Status::Forbidden;
    return {};
}

UserService::Result UserService::get(const User& actor, long long id) {
    auto target = repository_.find(id);
    if (!target) return {Status::NotFound, {}};
    if (actor.id != target->id && !mayManage(actor, *target)) return {Status::Forbidden, {}};
    return {Status::Ok, *target};
}

UserService::Result UserService::create(const User& actor, User user, const std::string& password) {
    if (!validUsername(user.username) || user.displayName.empty() || user.displayName.size() > 100 ||
        !validRole(user.role) || !validPassword(password)) return {Status::Invalid, {}};
    if (actor.role == "org_admin") {
        if (!actor.orgId || user.role == "platform_admin") return {Status::Forbidden, {}};
        user.orgId = actor.orgId;
    } else if (actor.role != "platform_admin") return {Status::Forbidden, {}};
    if (roleRequiresOrganization(user.role) && user.orgId <= 0) return {Status::Invalid, {}};
    if (!roleRequiresOrganization(user.role)) user.orgId = 0;
    if (user.orgId > 0 && !repository_.organizationExists(user.orgId)) return {Status::OrgNotFound, {}};
    user.passwordHash = crypto::hashPassword(password);
    const auto r = repository_.create(user);
    if (r == Store::CreateUserResult::DuplicateUsername) return {Status::Duplicate, {}};
    if (r == Store::CreateUserResult::OrgNotFound) return {Status::OrgNotFound, {}};
    return {Status::Ok, user};
}

UserService::Result UserService::update(const User& actor, long long id, const std::string& name,
                                         const std::string& role, long long orgId) {
    auto target = repository_.find(id);
    if (!target) return {Status::NotFound, {}};
    if (!mayManage(actor, *target)) return {Status::Forbidden, {}};
    if (name.empty() || name.size() > 100 || !validRole(role)) return {Status::Invalid, {}};
    if (actor.role == "org_admin" && role == "platform_admin") return {Status::Forbidden, {}};
    const long long nextOrg = actor.role == "org_admin" ? actor.orgId
        : (roleRequiresOrganization(role) ? orgId : 0);
    if (roleRequiresOrganization(role) && nextOrg <= 0) return {Status::Invalid, {}};
    if (nextOrg > 0 && !repository_.organizationExists(nextOrg)) return {Status::OrgNotFound, {}};
    try { repository_.updateRoleAndOrg(id, role, nextOrg); }
    catch (...) { return {Status::OrgNotFound, {}}; }
    repository_.updateDisplayName(id, name);
    target->displayName = name; target->role = role; target->orgId = nextOrg;
    return {Status::Ok, *target};
}

UserService::Status UserService::setDisabled(const User& actor, long long id, bool disabled) {
    auto target = repository_.find(id);
    if (!target) return Status::NotFound;
    if (!mayManage(actor, *target) || target->id == actor.id) return Status::Forbidden;
    repository_.setDisabled(id, disabled);
    return Status::Ok;
}

UserService::Status UserService::changePassword(const User& actor, long long id, const std::string& password) {
    auto target = repository_.find(id);
    if (!target) return Status::NotFound;
    // This endpoint is for administrator-initiated resets. Self-service
    // password changes use /api/auth/password, which verifies the current
    // password before updating the hash.
    if (actor.id == target->id || !mayManage(actor, *target)) return Status::Forbidden;
    if (!validPassword(password)) return Status::Invalid;
    repository_.updatePasswordHash(id, crypto::hashPassword(password));
    repository_.revokeSessions(id);
    return Status::Ok;
}

UserService::Status UserService::remove(const User& actor, long long id) {
    auto target = repository_.find(id);
    if (!target) return Status::NotFound;
    if (target->id == actor.id || !mayManage(actor, *target)) return Status::Forbidden;
    switch (repository_.deleteSafely(id, actor.username)) {
        case Store::DeleteUserResult::Deleted: return Status::Ok;
        case Store::DeleteUserResult::NotFound: return Status::NotFound;
        case Store::DeleteUserResult::MustDisable: return Status::MustDisable;
        case Store::DeleteUserResult::Protected: return Status::Protected;
        case Store::DeleteUserResult::HasLinkedRecords: return Status::HasLinkedRecords;
    }
    return Status::Invalid;
}
