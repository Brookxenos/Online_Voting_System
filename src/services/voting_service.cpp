#include "voting_service.h"

Store::CastResult VotingService::castBallot(long long electionId, long long voterId,
                                            long long candidateId) {
    // Preserve Store's transaction and uniqueness-constraint behavior.
    return store_.castBallot(electionId, voterId, candidateId);
}
