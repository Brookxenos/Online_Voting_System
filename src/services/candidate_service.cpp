#include "candidate_service.h"
#include <algorithm>
#include <cctype>

namespace {
bool manages(const User& actor, const Election& election) {
    return actor.role == "platform_admin" ||
        (actor.role == "org_admin" && actor.orgId > 0 && actor.orgId == election.orgId);
}
bool valid(Candidate& c) {
    auto trim = [](std::string& s) {
        size_t first = 0, last = s.size();
        while (first < last && std::isspace(static_cast<unsigned char>(s[first]))) ++first;
        while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) --last;
        s = s.substr(first, last - first);
    };
    trim(c.name);
    trim(c.party);
    return !c.name.empty() && c.name.size() <= 200 && c.party.size() <= 200 &&
        c.description.size() <= 2000 && c.position >= 0;
}
}

CandidateService::Result CandidateService::list(const User& actor, long long id) {
    if (id <= 0) return {Status::Invalid, {}, {}};
    auto e = repository_.election(id);
    if (!e) return {Status::NotFound, {}, {}};
    if (!repository_.visibleTo(actor, id))
        return {Status::Forbidden, {}, {}};
    return {Status::Ok, {}, repository_.list(id)};
}

CandidateService::Result CandidateService::get(const User& actor, long long id, long long candidateId) {
    if (id <= 0 || candidateId <= 0) return {Status::Invalid, {}, {}};
    auto result = list(actor, id);
    if (result.status != Status::Ok) return result;
    auto found = std::find_if(result.candidates.begin(), result.candidates.end(),
        [candidateId](const Candidate& c) { return c.id == candidateId; });
    if (found == result.candidates.end()) return {Status::NotFound, {}, {}};
    return {Status::Ok, *found, {}};
}

CandidateService::Result CandidateService::create(const User& actor, Candidate c) {
    if (c.electionId <= 0 || !valid(c)) return {Status::Invalid, {}, {}};
    auto e = repository_.election(c.electionId);
    if (!e) return {Status::NotFound, {}, {}};
    if (!manages(actor, *e)) return {Status::Forbidden, {}, {}};
    if (e->status != "draft") return {Status::Conflict, {}, {}};
    auto result = repository_.create(c);
    if (result == Store::AddCandidateResult::NotEditable) return {Status::Conflict, {}, {}};
    if (result != Store::AddCandidateResult::Ok) return {Status::NotFound, {}, {}};
    return {Status::Ok, c, {}};
}

CandidateService::Result CandidateService::update(const User& actor, long long id, Candidate c) {
    if (id <= 0 || c.id <= 0 || !valid(c)) return {Status::Invalid, {}, {}};
    auto e = repository_.election(id);
    if (!e) return {Status::NotFound, {}, {}};
    if (!manages(actor, *e)) return {Status::Forbidden, {}, {}};
    if (e->status != "draft") return {Status::Conflict, {}, {}};
    c.electionId = id;
    if (!repository_.update(id, c)) return {Status::NotFound, {}, {}};
    return {Status::Ok, c, {}};
}

CandidateService::Result CandidateService::remove(const User& actor, long long id, long long candidateId) {
    if (id <= 0 || candidateId <= 0) return {Status::Invalid, {}, {}};
    auto e = repository_.election(id);
    if (!e) return {Status::NotFound, {}, {}};
    if (!manages(actor, *e)) return {Status::Forbidden, {}, {}};
    if (e->status != "draft") return {Status::Conflict, {}, {}};
    if (!repository_.remove(candidateId, id)) return {Status::NotFound, {}, {}};
    return {Status::Ok, {}, {}};
}
