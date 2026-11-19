#ifndef ELECTRAVOTE_RESULTS_SERVICE_H
#define ELECTRAVOTE_RESULTS_SERVICE_H
#include "../store.h"
class ResultsService {
public:
    explicit ResultsService(Store& store) : store_(store) {}
    std::optional<Tally> tally(long long electionId) { return store_.tallyElection(electionId); }
    std::optional<Tally> latest(long long electionId) { return store_.latestTally(electionId); }
private:
    Store& store_;
};
#endif
