#ifndef ELECTRAVOTE_ELIGIBILITY_REPOSITORY_H
#define ELECTRAVOTE_ELIGIBILITY_REPOSITORY_H
#include "../store.h"

class EligibilityRepository {
public:
    explicit EligibilityRepository(Store& store) : store_(store) {}
    std::optional<Election> election(long long id) { return store_.getElection(id); }
    std::optional<User> user(long long id) { return store_.findUserById(id); }
    std::vector<User> list(long long electionId) { return store_.listEligibleUsers(electionId); }
    bool contains(long long electionId, long long userId) { return store_.isEligible(electionId, userId); }
    bool grant(long long electionId, long long userId) { return store_.grantEligibility(electionId, userId); }
    bool revoke(long long electionId, long long userId) { return store_.revokeEligibility(electionId, userId); }
private:
    Store& store_;
};
#endif
