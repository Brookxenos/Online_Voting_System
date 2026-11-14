#include "http.h"
#include "api.h"
#include "store.h"
#include "crypto.h"

#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <termios.h>
#include <unistd.h>

static HttpServer* g_server = nullptr;

static void onSignal(int) {
    if (g_server) g_server->stop();
    std::_Exit(0);
}

static std::string envOr(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : def;
}

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [port] [web_dir] [data_dir]\n\n"
              << "       " << prog << " --reset-admin-password [username]\n\n"
              << "Positional arguments are a legacy convenience; configuration is\n"
              << "normally supplied through environment variables:\n"
              << "  OV_PORT                     listen port (default 8080)\n"
              << "  OV_WEB_DIR                  static web root (default ./web)\n"
              << "  OV_DATA_DIR                 data directory (default ./data)\n"
              << "  OV_DB_FILE                  database file (default <data_dir>/openvote.db)\n"
              << "  OV_SESSION_HOURS            session lifetime in hours (default 168)\n"
              << "  OV_SECURE_COOKIES           set Secure cookie flag when on HTTPS (default 0)\n"
              << "  OV_INITIAL_ADMIN_PASSWORD   initial platform-admin password; if unset,\n"
              << "                              a random one is generated and printed once\n";
}

static bool readHiddenInput(const std::string& prompt, std::string& value) {
    if (!::isatty(STDIN_FILENO)) {
        std::cerr << "[main] password reset requires an interactive terminal\n";
        return false;
    }
    termios original{};
    if (::tcgetattr(STDIN_FILENO, &original) != 0) {
        std::cerr << "[main] could not configure terminal input\n";
        return false;
    }
    termios hidden = original;
    hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) {
        std::cerr << "[main] could not hide terminal input\n";
        return false;
    }
    std::cout << prompt << std::flush;
    bool readOk = static_cast<bool>(std::getline(std::cin, value));
    const bool restoreOk = (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == 0);
    std::cout << '\n';
    if (!restoreOk) {
        std::cerr << "[main] could not restore terminal settings\n";
        return false;
    }
    return readOk;
}

static int resetAdminPassword(const std::string& dbFile,
                              const std::string& username) {
    if (!std::filesystem::exists(dbFile)) {
        std::cerr << "[main] database not found: " << dbFile
                  << " (no files were created)\n";
        return 1;
    }

    Database db(dbFile);
    Store store(db);
    auto user = store.findUserByUsername(username);
    if (!user || user->role != "platform_admin" || user->disabled) {
        std::cerr << "[main] no enabled platform administrator with that username\n";
        return 1;
    }

    std::string password, confirmation;
    if (!readHiddenInput("New password (8-256 characters): ", password) ||
        !readHiddenInput("Confirm new password: ", confirmation)) {
        std::fill(password.begin(), password.end(), '\0');
        std::fill(confirmation.begin(), confirmation.end(), '\0');
        return 1;
    }
    if (password.size() < 8 || password.size() > 256) {
        std::fill(password.begin(), password.end(), '\0');
        std::fill(confirmation.begin(), confirmation.end(), '\0');
        std::cerr << "[main] password must be between 8 and 256 characters\n";
        return 1;
    }
    if (password != confirmation) {
        std::fill(password.begin(), password.end(), '\0');
        std::fill(confirmation.begin(), confirmation.end(), '\0');
        std::cerr << "[main] passwords do not match; no changes made\n";
        return 1;
    }

    crypto::init();
    const std::string newHash = crypto::hashPassword(password);
    std::fill(password.begin(), password.end(), '\0');
    std::fill(confirmation.begin(), confirmation.end(), '\0');
    store.updatePasswordHash(user->id, newHash);
    store.revokeAllSessions(user->id);
    store.addAudit("local-operator", "ADMIN_PASSWORD_RESET", 0, "");
    std::cout << "[main] password reset for platform administrator '"
              << username << "'; existing sessions were revoked\n";
    return 0;
}

int main(int argc, char** argv) {
    try {
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        if (argc >= 2 && std::string(argv[1]) == "--reset-admin-password") {
            if (argc > 3) { usage(argv[0]); return 1; }
            const std::string dataDir = envOr("OV_DATA_DIR", "data");
            const std::string dbFile = envOr("OV_DB_FILE", dataDir + "/openvote.db");
            const std::string username = argc == 3 ? argv[2] : "admin";
            return resetAdminPassword(dbFile, username);
        }

        // ---- Configuration (environment overrides, legacy argv kept working) ----
        std::string webDir = envOr("OV_WEB_DIR", "web");
        std::string dataDir = envOr("OV_DATA_DIR", "data");
        std::string dbFile  = envOr("OV_DB_FILE", "");
        std::string portStr = envOr("OV_PORT", "8080");

        if (argc >= 2) portStr = argv[1];
        if (argc >= 3) webDir = argv[2];
        if (argc >= 4) dataDir = argv[3];
        if (dbFile.empty()) dbFile = dataDir + "/openvote.db";

        int port = 0;
        try { port = std::stoi(portStr); }
        catch (...) { usage(argv[0]); return 1; }
        if (port <= 0 || port > 65535) {
            std::cerr << "[main] invalid port: " << portStr << "\n";
            return 1;
        }

        std::filesystem::create_directories(dataDir);

        int sessionHours = 24 * 7;
        try { sessionHours = std::stoi(envOr("OV_SESSION_HOURS", "168")); } catch (...) {}
        if (sessionHours < 1) sessionHours = 1;

        bool secureCookies = envOr("OV_SECURE_COOKIES", "0") == "1";

        // ---- Crypto + storage ----
        crypto::init();

        Database db(dbFile);
        Store store(db);
        store.initialize(dataDir);   // migrations + one-time legacy CSV import

        // ---- One-time platform-admin provisioning (no default password) ----
        bool hasAdmin = false;
        for (const auto& u : store.listUsers()) {
            if (u.role == "platform_admin" && !u.disabled) { hasAdmin = true; break; }
        }
        if (!hasAdmin) {
            std::string pw = envOr("OV_INITIAL_ADMIN_PASSWORD", "");
            if (pw.empty()) {
                pw = crypto::randomHex(12);
                std::cout
                    << "\n"
                    << "=============================================================\n"
                    << "  INITIAL PLATFORM ADMINISTRATOR (shown once — store it now)\n"
                    << "     username: admin\n"
                    << "     password: " << pw << "\n"
                    << "  This password is randomly generated, not stored in code, and\n"
                    << "  cannot be recovered. It is not written to any file.\n"
                    << "  Set OV_INITIAL_ADMIN_PASSWORD to choose it explicitly.\n"
                    << "=============================================================\n\n";
            } else {
                std::cout << "[main] initial platform administrator 'admin' created "
                             "(password from OV_INITIAL_ADMIN_PASSWORD)\n";
            }
            User admin;
            admin.username = "admin";
            admin.displayName = "Platform Administrator";
            admin.role = "platform_admin";
            admin.passwordHash = crypto::hashPassword(pw);
            if (store.createUser(admin) != Store::CreateUserResult::Ok) {
                std::cerr << "[main] failed to create initial admin\n";
                return 1;
            }
            store.addAudit("system", "BOOTSTRAP_ADMIN", 0,
                           "initial platform administrator provisioned");
        }

        // ---- HTTP server + routes ----
        HttpServer server(port, webDir);
        ApiConfig cfg;
        cfg.sessionHours = sessionHours;
        cfg.secureCookies = secureCookies;
        registerApiRoutes(server, store, cfg);
        g_server = &server;

        std::cout << "[main] OpenVote starting\n"
                  << "[main]   port     = " << port << "\n"
                  << "[main]   web dir  = " << webDir << "\n"
                  << "[main]   data dir = " << dataDir << "\n"
                  << "[main]   database = " << dbFile << "\n"
                  << "[main]   session lifetime = " << sessionHours << "h\n";

        server.start();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[main] fatal: " << e.what() << "\n";
        return 1;
    }
}
