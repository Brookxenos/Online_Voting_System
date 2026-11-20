#ifndef ELECTRAVOTE_RESULTS_SERVICE_H
#define ELECTRAVOTE_RESULTS_SERVICE_H
#include "../store.h"
class ResultsService {
public:
    explicit ResultsService(Store& store) : store_(store) {}
    // Reads are deliberately side-effect free. Tally creation belongs to the
    // closed -> published lifecycle transaction in Store.
    std::optional<Tally> latest(long long electionId) { return store_.latestTally(electionId); }
    bool mayView(const User& user, const Election& election) const {
        if (user.role == "platform_admin") return true;
        if ((user.role == "org_admin" || user.role == "observer") &&
            user.orgId > 0 && user.orgId == election.orgId) return true;
        return false;
    }
private:
    Store& store_;
};
#endif
