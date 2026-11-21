#ifndef ELECTION_REPOSITORY_H
#define ELECTION_REPOSITORY_H
#include "../store.h"
#include "../services/audit_service.h"
class ElectionRepository {
public:
    explicit ElectionRepository(Store& store) : store_(store), audit_(store) {}
    Store::CreateElectionResult create(Election& e) { return store_.createElection(e); }
    Store::DeleteElectionResult deleteDraftSafely(long long id, const std::string& actor) {
        return store_.deleteDraftElectionSafely(id, actor);
    }
    std::optional<Election> get(long long id) { return store_.getElection(id); }
    std::vector<Election> listVisible(const User& user) { return store_.listElectionsVisibleTo(user); }
    bool updateDraft(long long id, const Election& e) { return store_.updateElectionDraft(id, e); }
    bool transition(long long id, const std::string& status, const std::string& actor) {
        return store_.transitionElection(id, status, actor);
    }
    void recordRejectedLifecycle(const std::string& actor, long long id, const std::string& requested) {
        audit_.lifecycleRejected(actor, id, requested);
    }
private:
    Store& store_;
    AuditService audit_;
};
#endif
