#ifndef ELECTRAVOTE_ELECTION_LIFECYCLE_SERVICE_H
#define ELECTRAVOTE_ELECTION_LIFECYCLE_SERVICE_H
#include "../repositories/election_repository.h"

class ElectionLifecycleService {
public:
    enum class Status { Ok, Invalid, Forbidden, NotFound, Conflict };
    struct Result { Status status = Status::Invalid; Election election; };
    explicit ElectionLifecycleService(ElectionRepository& repository) : repository_(repository) {}
    Result transition(const User& actor, long long electionId, const std::string& status);
private:
    ElectionRepository& repository_;
};
#endif
