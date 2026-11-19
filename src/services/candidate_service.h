#ifndef ELECTRAVOTE_CANDIDATE_SERVICE_H
#define ELECTRAVOTE_CANDIDATE_SERVICE_H
#include "../repositories/candidate_repository.h"

class CandidateService {
public:
    enum class Status { Ok, Invalid, Forbidden, NotFound, Conflict };
    struct Result { Status status = Status::Invalid; Candidate candidate; std::vector<Candidate> candidates; };
    explicit CandidateService(CandidateRepository& repository) : repository_(repository) {}
    Result list(const User& actor, long long electionId);
    Result get(const User& actor, long long electionId, long long candidateId);
    Result create(const User& actor, Candidate candidate);
    Result update(const User& actor, long long electionId, Candidate candidate);
    Result remove(const User& actor, long long electionId, long long candidateId);
private:
    CandidateRepository& repository_;
};
#endif
