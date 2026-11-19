#include "voting_service.h"
#include <ctime>
#include <exception>

namespace {
std::string currentUtcIso() {
    std::time_t value = std::time(nullptr);
    std::tm utc{};
    ::gmtime_r(&value, &utc);
    char out[32];
    std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return out;
}

VotingService::Status fromStore(Store::CastResult::Status status) {
    switch (status) {
        case Store::CastResult::Status::Ok: return VotingService::Status::Ok;
        case Store::CastResult::Status::ElectionNotFound: return VotingService::Status::ElectionNotFound;
        case Store::CastResult::Status::ElectionNotOpen: return VotingService::Status::ElectionNotOpen;
        case Store::CastResult::Status::OutsideVotingWindow: return VotingService::Status::OutsideVotingWindow;
        case Store::CastResult::Status::NotEligible: return VotingService::Status::NotEligible;
        case Store::CastResult::Status::AlreadyVoted: return VotingService::Status::AlreadyVoted;
        case Store::CastResult::Status::InvalidCandidate: return VotingService::Status::InvalidCandidate;
    }
    return VotingService::Status::StorageFailure;
}
}

VotingService::Result VotingService::castBallot(const User& actor, long long electionId,
                                                 long long candidateId) {
    if (actor.id <= 0 || electionId <= 0 || candidateId <= 0)
        return {Status::InvalidRequest, {}};
    if (actor.role != "voter") return {Status::Forbidden, {}};

    try {
    auto voter = repository_.voter(actor.id);
    if (!voter || voter->disabled || voter->role != "voter" || voter->username != actor.username)
        return {Status::Forbidden, {}};
    auto election = repository_.election(electionId);
    if (!election) return {Status::ElectionNotFound, {}};
    if (election->status != "open") return {Status::ElectionNotOpen, {}};
    const std::string now = currentUtcIso();
    if ((!election->startAt.empty() && now < election->startAt) ||
        (!election->endAt.empty() && now > election->endAt))
        return {Status::OutsideVotingWindow, {}};
    if (voter->orgId != election->orgId || !repository_.eligible(electionId, actor.id))
        return {Status::NotEligible, {}};
    if (!repository_.candidateBelongs(electionId, candidateId))
        return {Status::InvalidCandidate, {}};
    if (repository_.participated(electionId, actor.id))
        return {Status::AlreadyVoted, {}};

    auto stored = repository_.record(electionId, actor.id, candidateId, actor.username);
    return {fromStore(stored.status), stored.receipt};
    } catch (const std::exception&) {
        // Do not expose database/constraint details to the HTTP caller. Store's
        // transaction has rolled back participation and ballot together.
        return {Status::StorageFailure, {}};
    }
}
