#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include "../store.h"
#include <optional>
#include <string>

class AuthService {
public:
    enum class RegisterStatus { Ok, InvalidUsername, InvalidDisplayName, InvalidPassword, Unavailable, DuplicateUsername };
    struct RegisterResult { RegisterStatus status; User user; };
    struct LoginResult { bool ok = false; bool limited = false; std::string token; User user; };

    explicit AuthService(Store& store);
    RegisterResult registerVoter(const std::string& username, const std::string& displayName,
                                  const std::string& password, const std::string& clientIp);
    LoginResult login(const std::string& username, const std::string& password,
                      const std::string& clientIp, int sessionHours);
    void logout(const std::string& token);
    std::optional<User> userForSession(const std::string& token);
    bool changePassword(const User& user, const std::string& currentPassword,
                        const std::string& newPassword);
private:
    Store& store_;
};

#endif
