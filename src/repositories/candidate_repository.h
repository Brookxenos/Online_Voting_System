#ifndef ELECTRAVOTE_CANDIDATE_REPOSITORY_H
#define ELECTRAVOTE_CANDIDATE_REPOSITORY_H
#include "../store.h"
#include <algorithm>

class CandidateRepository {
public:
    explicit CandidateRepository(Store& store) : store_(store) {}
    std::optional<Election> election(long long id) { return store_.getElection(id); }
    bool visibleTo(const User& actor, long long electionId) {
        const auto visible = store_.listElectionsVisibleTo(actor);
        return std::any_of(visible.begin(), visible.end(), [electionId](const Election& e) { return e.id == electionId; });
    }
    std::vector<Candidate> list(long long electionId) { return store_.listCandidates(electionId); }
    Store::AddCandidateResult create(Candidate& candidate) { return store_.addCandidate(candidate); }
    bool update(long long electionId, Candidate& candidate) {
        return store_.updateCandidateDraft(electionId, candidate);
    }
    bool remove(long long candidateId, long long electionId) {
        return store_.removeCandidate(candidateId, electionId);
    }
private:
    Store& store_;
};
#endif
