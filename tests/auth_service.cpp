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

    // Reset password security tests:
    // 1) Temporary reset password expires after 3 minutes
    // 2) Can only be used once (one-time login)
    // 3) Flags user to change password (mustChangePassword == true)
    store.resetPassword(registered.user.id, crypto::hashPassword("temporary-pw-123"), 3);
    auto resetUserOpt = store.findUserById(registered.user.id);
    check(resetUserOpt && resetUserOpt->mustChangePassword && !resetUserOpt->passwordResetExpiresAt.empty(),
          "reset password sets mustChangePassword and expiration timestamp");

    auto resetLogin1 = auth.login("authvoter", "temporary-pw-123", "auth-test-login-3", 1);
    check(resetLogin1.ok && resetLogin1.user.mustChangePassword,
          "first login with valid temporary password succeeds and flags user to change password");

    auto resetLogin2 = auth.login("authvoter", "temporary-pw-123", "auth-test-login-4", 1);
    check(!resetLogin2.ok, "second login with temporary reset password is rejected (single-use enforced)");

    check(auth.changePassword(resetLogin1.user, "temporary-pw-123", "new-permanent-pw-456"),
          "user can update password using temporary password as current credential");

    auto updatedUserOpt = store.findUserById(registered.user.id);
    check(updatedUserOpt && !updatedUserOpt->mustChangePassword && updatedUserOpt->passwordResetExpiresAt.empty(),
          "password update clears mustChangePassword and expiration state");

    auto permLogin1 = auth.login("authvoter", "new-permanent-pw-456", "auth-test-login-5", 1);
    check(permLogin1.ok && !permLogin1.user.mustChangePassword,
          "login with new permanent password succeeds and account is unflagged");

    auto permLogin2 = auth.login("authvoter", "new-permanent-pw-456", "auth-test-login-6", 1);
    check(permLogin2.ok, "permanent password allows repeated logins");

    // Expiration test:
    store.resetPassword(registered.user.id, crypto::hashPassword("expired-temp-pw-789"), -1);
    auto expLogin = auth.login("authvoter", "expired-temp-pw-789", "auth-test-login-7", 1);
    check(!expLogin.ok, "expired temporary reset password is rejected at login");

    if (failures) std::cerr << failures << " auth service checks failed\n";
    std::remove(path.c_str());
    std::remove((path + "-shm").c_str());
    std::remove((path + "-wal").c_str());
    return failures ? 1 : 0;
}
