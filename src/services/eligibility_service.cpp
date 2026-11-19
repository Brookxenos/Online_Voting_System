#include "eligibility_service.h"

namespace {
bool manages(const User& actor, const Election& election) {
    return actor.role == "platform_admin" ||
        (actor.role == "org_admin" && actor.orgId > 0 && actor.orgId == election.orgId);
}
}

EligibilityService::Result EligibilityService::list(const User& actor, long long id) {
    if (id <= 0) return {Status::Invalid, false, {}};
    auto e = repository_.election(id);
    if (!e) return {Status::NotFound, false, {}};
    if (!manages(actor, *e) && !(actor.role == "observer" && actor.orgId == e->orgId))
        return {Status::Forbidden, false, {}};
    return {Status::Ok, false, repository_.list(id)};
}

EligibilityService::Result EligibilityService::change(const User& actor, long long id,
                                                       long long userId, bool grant) {
    if (id <= 0 || userId <= 0) return {Status::Invalid, false, {}};
    auto e = repository_.election(id);
    if (!e) return {Status::NotFound, false, {}};
    if (!manages(actor, *e)) return {Status::Forbidden, false, {}};
    if (e->status != "draft") return {Status::Conflict, false, {}};
    auto target = repository_.user(userId);
    if (!target) return {Status::NotFound, false, {}};
    if (grant && (target->role != "voter" || target->disabled || target->orgId != e->orgId))
        return {Status::InvalidVoter, false, {}};
    const bool existed = repository_.contains(id, userId);
    if (grant) {
        if (!repository_.grant(id, userId)) {
            auto current = repository_.election(id);
            return {current && current->status != "draft" ? Status::Conflict : Status::NotFound, false, {}};
        }
        return {Status::Ok, !existed, {}};
    }
    if (!repository_.revoke(id, userId)) {
        auto current = repository_.election(id);
        return {current && current->status != "draft" ? Status::Conflict : Status::NotFound, false, {}};
    }
    return {Status::Ok, existed, {}};
}
