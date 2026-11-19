#ifndef ELECTRAVOTE_BALLOT_REPOSITORY_H
#define ELECTRAVOTE_BALLOT_REPOSITORY_H
#include "../store.h"

// Persistence boundary for voting. Store owns the SQL and the existing
// BEGIN IMMEDIATE transaction; the repository exposes only domain operations.
class BallotRepository {
public:
    explicit BallotRepository(Store& store) : store_(store) {}
    std::optional<User> voter(long long id) { return store_.findUserById(id); }
    std::optional<Election> election(long long id) { return store_.getElection(id); }
    bool eligible(long long electionId, long long userId) { return store_.isEligible(electionId, userId); }
    bool participated(long long electionId, long long userId) { return store_.hasParticipation(electionId, userId); }
    bool candidateBelongs(long long electionId, long long candidateId) {
        const auto candidates = store_.listCandidates(electionId);
        for (const auto& candidate : candidates) if (candidate.id == candidateId) return true;
        return false;
    }
    Store::CastResult record(long long electionId, long long userId, long long candidateId,
                             const std::string& actor) {
        return store_.castBallot(electionId, userId, candidateId, actor);
    }
private:
    Store& store_;
};
#endif
