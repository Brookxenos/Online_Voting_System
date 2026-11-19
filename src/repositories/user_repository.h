#ifndef USER_REPOSITORY_H
#define USER_REPOSITORY_H
#include "../store.h"

class UserRepository {
public:
    explicit UserRepository(Store& store) : store_(store) {}
    Store::CreateUserResult create(User& user) { return store_.createUser(user); }
    std::optional<User> find(long long id) { return store_.findUserById(id); }
    std::vector<User> list(std::optional<long long> orgId) { return store_.listUsers(orgId); }
    void updateDisplayName(long long id, const std::string& name) { store_.updateDisplayName(id, name); }
    void updateRoleAndOrg(long long id, const std::string& role, long long orgId) { store_.updateUserRoleAndOrg(id, role, orgId); }
    void setDisabled(long long id, bool disabled) { store_.setUserDisabled(id, disabled); }
    void updatePasswordHash(long long id, const std::string& hash) { store_.updatePasswordHash(id, hash); }
    void revokeSessions(long long id) { store_.revokeAllSessions(id); }
private:
    Store& store_;
};
#endif
