#include "../src/services/auth_service.h"
#include "../src/crypto.h"
#include <cstdio>
#include <iostream>
#include <unistd.h>

int main() {
    const std::string path = "/tmp/electravote-auth-service-test-" + std::to_string(getpid()) + ".db";
    crypto::init();
    Database db(path);
    Store store(db);
    store.initialize("/tmp/electravote-no-legacy-data");
    store.createOrganization("Auth Test", "auth-test", true);
    AuthService auth(store);
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
        if (!ok) ++failures;
    };
    auto registered = auth.registerVoter("authvoter", "Auth Voter", "password123", "auth-test-ip");
    check(registered.status == AuthService::RegisterStatus::Ok && registered.user.role == "voter",
          "registration creates voter");
    auto duplicate = auth.registerVoter("authvoter", "", "password123", "auth-test-ip-2");
    check(duplicate.status == AuthService::RegisterStatus::DuplicateUsername, "duplicate registration rejected");
    auto login = auth.login("authvoter", "password123", "auth-test-login", 1);
    check(login.ok && !login.token.empty(), "login creates session token");
    check(auth.userForSession(login.token).has_value(), "session token validates");
    auth.logout(login.token);
    check(!auth.userForSession(login.token).has_value(), "logout revokes session");
    auto login2 = auth.login("authvoter", "password123", "auth-test-login-2", 1);
    check(login2.ok, "login succeeds after logout");
    auth.logout(login2.token);
    if (failures) std::cerr << failures << " auth service checks failed\n";
    std::remove(path.c_str());
    std::remove((path + "-shm").c_str());
    std::remove((path + "-wal").c_str());
    return failures ? 1 : 0;
}
