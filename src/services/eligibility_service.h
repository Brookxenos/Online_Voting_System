#ifndef ELECTRAVOTE_ELIGIBILITY_SERVICE_H
#define ELECTRAVOTE_ELIGIBILITY_SERVICE_H
#include "../repositories/eligibility_repository.h"

class EligibilityService {
public:
    enum class Status { Ok, Invalid, Forbidden, NotFound, Conflict, InvalidVoter };
    struct Result { Status status = Status::Invalid; bool changed = false; std::vector<User> users; };
    explicit EligibilityService(EligibilityRepository& repository) : repository_(repository) {}
    Result list(const User& actor, long long electionId);
    Result change(const User& actor, long long electionId, long long userId, bool grant);
private:
    EligibilityRepository& repository_;
};
#endif
