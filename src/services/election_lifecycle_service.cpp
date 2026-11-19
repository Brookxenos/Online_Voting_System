#include "election_lifecycle_service.h"

ElectionLifecycleService::Result ElectionLifecycleService::transition(
    const User& actor, long long electionId, const std::string& status) {
    if (electionId <= 0) return {Status::Invalid, {}};
    auto election = repository_.get(electionId);
    if (!election) return {Status::NotFound, {}};
    const bool authorized = actor.role == "platform_admin" ||
        (actor.role == "org_admin" && actor.orgId > 0 && actor.orgId == election->orgId);
    if (!authorized) {
        repository_.recordRejectedLifecycle(actor.username, electionId, status);
        return {Status::Forbidden, *election};
    }
    if (!repository_.transition(electionId, status, actor.username)) {
        repository_.recordRejectedLifecycle(actor.username, electionId, status);
        return {Status::Conflict, *election};
    }
    auto updated = repository_.get(electionId);
    return updated ? Result{Status::Ok, *updated} : Result{Status::NotFound, {}};
}
