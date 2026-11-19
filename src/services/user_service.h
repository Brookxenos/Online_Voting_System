#ifndef USER_SERVICE_H
#define USER_SERVICE_H
#include "../repositories/user_repository.h"

class UserService {
public:
    enum class Status { Ok, Invalid, Forbidden, NotFound, Duplicate, OrgNotFound };
    struct Result { Status status = Status::Invalid; User user; };
    explicit UserService(UserRepository& repository) : repository_(repository) {}
    std::vector<User> list(const User& actor, long long requestedOrgId, Status& status);
    Result get(const User& actor, long long id);
    Result create(const User& actor, User user, const std::string& password);
    Result update(const User& actor, long long id, const std::string& displayName,
                  const std::string& role, long long orgId);
    Status setDisabled(const User& actor, long long id, bool disabled);
    Status changePassword(const User& actor, long long id, const std::string& password);
private:
    UserRepository& repository_;
    bool mayManage(const User& actor, const User& target) const;
};
#endif
