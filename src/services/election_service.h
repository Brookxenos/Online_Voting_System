#ifndef ELECTION_SERVICE_H
#define ELECTION_SERVICE_H
#include "../repositories/election_repository.h"
class ElectionService {
public:
    enum class Status { Ok, Invalid, Forbidden, NotFound, Conflict };
    enum class DeleteStatus { Deleted, Invalid, Forbidden, NotFound, Conflict };
    enum class PurgeStatus { Purged, Invalid, Forbidden, NotFound, Conflict, ConfirmationMismatch };
    struct Result { Status status = Status::Invalid; Election election; };
    struct PurgeResult {
        PurgeStatus status = PurgeStatus::Invalid;
        long long ballots = 0;
        long long participations = 0;
        long long candidates = 0;
        long long eligible = 0;
    };
    explicit ElectionService(ElectionRepository& repository) : repository_(repository) {}
    std::vector<Election> list(const User& actor);
    Result get(const User& actor, long long id);
    Result create(const User& actor, Election election);
    Result updateDraft(const User& actor, long long id, const Election& changes);
    DeleteStatus removeSafely(const User& actor, long long id);
    PurgeResult purge(const User& actor, long long id, const std::string& confirmName);
private:
    ElectionRepository& repository_;
};
#endif
