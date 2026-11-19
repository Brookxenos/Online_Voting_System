#ifndef ELECTION_REPOSITORY_H
#define ELECTION_REPOSITORY_H
#include "../store.h"
class ElectionRepository {
public:
    explicit ElectionRepository(Store& store) : store_(store) {}
    Store::CreateElectionResult create(Election& e) { return store_.createElection(e); }
    std::optional<Election> get(long long id) { return store_.getElection(id); }
    std::vector<Election> listVisible(const User& user) { return store_.listElectionsVisibleTo(user); }
    bool updateDraft(long long id, const Election& e) { return store_.updateElectionDraft(id, e); }
private:
    Store& store_;
};
#endif
