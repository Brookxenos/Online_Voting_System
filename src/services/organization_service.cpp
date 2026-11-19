#include "organization_service.h"
#include <cctype>

namespace {
bool validName(const std::string& s) { return !s.empty() && s.size() <= 100; }
bool validSlug(const std::string& s) {
    if (s.size() < 3 || s.size() > 32) return false;
    for (unsigned char c : s) if (!std::isalnum(c) && c != '_' && c != '.' && c != '-') return false;
    return true;
}
}

std::vector<Organization> OrganizationService::list(const User& actor, Status& status) {
    status = Status::Ok;
    if (actor.role == "platform_admin") return repository_.list();
    if ((actor.role == "org_admin" || actor.role == "observer" || actor.role == "voter") && actor.orgId > 0) {
        auto org = repository_.get(actor.orgId);
        return org ? std::vector<Organization>{*org} : std::vector<Organization>{};
    }
    status = Status::Forbidden;
    return {};
}

OrganizationService::Result OrganizationService::get(const User& actor, long long id) {
    if (id <= 0) return {Status::Invalid, {}};
    auto org = repository_.get(id);
    if (!org) return {Status::NotFound, {}};
    if (actor.role != "platform_admin" &&
        !((actor.role == "org_admin" || actor.role == "observer" || actor.role == "voter") && actor.orgId == id))
        return {Status::Forbidden, {}};
    return {Status::Ok, *org};
}

OrganizationService::Result OrganizationService::create(const User& actor, const std::string& name,
                                                           const std::string& slug, bool selfReg) {
    if (actor.role != "platform_admin") return {Status::Forbidden, {}};
    if (!validName(name) || !validSlug(slug)) return {Status::Invalid, {}};
    if (repository_.bySlug(slug)) return {Status::Duplicate, {}};
    try { return {Status::Ok, repository_.create(name, slug, selfReg)}; }
    catch (...) { return {Status::Duplicate, {}}; }
}

OrganizationService::Result OrganizationService::update(const User& actor, long long id,
        const std::string& name, const std::string& slug, bool selfReg) {
    if (id <= 0) return {Status::Invalid, {}};
    auto existing = repository_.get(id);
    if (!existing) return {Status::NotFound, {}};
    if (actor.role == "org_admin") {
        if (actor.orgId != id) return {Status::Forbidden, {}};
        if (name != existing->name || slug != existing->slug) return {Status::Forbidden, {}};
        try { repository_.update(id, existing->name, existing->slug, selfReg); }
        catch (...) { return {Status::Invalid, {}}; }
    } else if (actor.role == "platform_admin") {
        if (!validName(name) || !validSlug(slug)) return {Status::Invalid, {}};
        auto collision = repository_.bySlug(slug);
        if (collision && collision->id != id) return {Status::Duplicate, {}};
        try { repository_.update(id, name, slug, selfReg); }
        catch (...) { return {Status::Duplicate, {}}; }
    } else return {Status::Forbidden, {}};
    return {Status::Ok, *repository_.get(id)};
}
