#ifndef ELECTRAVOTE_VOTING_SERVICE_H
#define ELECTRAVOTE_VOTING_SERVICE_H

#include "../store.h"

// Application boundary for ballot submission. Store remains responsible for
// the existing atomic SQLite operation during this first extraction.
class VotingService {
public:
    explicit VotingService(Store& store) : store_(store) {}
    Store::CastResult castBallot(long long electionId, long long voterId,
                                 long long candidateId);

private:
    Store& store_;
};

#endif
