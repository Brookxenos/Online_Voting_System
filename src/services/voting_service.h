#ifndef ELECTRAVOTE_VOTING_SERVICE_H
#define ELECTRAVOTE_VOTING_SERVICE_H

#include "../repositories/ballot_repository.h"

// Application boundary for ballot submission. Store remains responsible for
// the existing atomic SQLite operation during this first extraction.
class VotingService {
public:
    enum class Status {
        Ok, Forbidden, InvalidRequest, ElectionNotFound, ElectionNotOpen,
        OutsideVotingWindow, NotEligible, AlreadyVoted, InvalidCandidate, StorageFailure
    };
    struct Result { Status status = Status::InvalidRequest; std::string receipt; };
    explicit VotingService(BallotRepository& repository) : repository_(repository) {}
    Result castBallot(const User& actor, long long electionId, long long candidateId);

private:
    BallotRepository& repository_;
};

#endif
