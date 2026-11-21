#ifndef ELECTION_SERVICE_H
#define ELECTION_SERVICE_H
#include "../repositories/election_repository.h"
class ElectionService {
public:
    enum class Status { Ok, Invalid, Forbidden, NotFound, Conflict };
    enum class DeleteStatus { Deleted, Invalid, Forbidden, NotFound, Conflict };
    struct Result { Status status = Status::Invalid; Election election; };
    explicit ElectionService(ElectionRepository& repository) : repository_(repository) {}
    std::vector<Election> list(const User& actor);
    Result get(const User& actor, long long id);
    Result create(const User& actor, Election election);
    Result updateDraft(const User& actor, long long id, const Election& changes);
    DeleteStatus removeSafely(const User& actor, long long id);
private:
    ElectionRepository& repository_;
};
#endif
