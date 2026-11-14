#include "http_server.h"
#include "data_store.h"

#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>

static HttpServer* g_server = nullptr;

static void onSignal(int sig) {
    std::cerr << "\n[main] received signal " << sig << ", shutting down...\n";
    if (g_server) g_server->stop();
    std::_Exit(0);
}

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [port] [web_dir] [data_dir]\n"
              << "  port     — TCP port to listen on (default 8080)\n"
              << "  web_dir  — directory with static web assets (default ../web)\n"
              << "  data_dir — directory for CSV data files (default ../data)\n";
}

int main(int argc, char** argv) {
    int port = 8080;
    std::string webDir = "../web";
    std::string dataDir = "../data";

    if (argc >= 2) {
        try { port = std::stoi(argv[1]); }
        catch (...) { usage(argv[0]); return 1; }
    }
    if (argc >= 3) webDir = argv[2];
    if (argc >= 4) dataDir = argv[3];

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    DataStore store(dataDir);
    HttpServer server(port, webDir, store);
    g_server = &server;

    std::cout << "[main] Online Voting System starting...\n"
              << "[main]   port     = " << port << "\n"
              << "[main]   web dir  = " << webDir << "\n"
              << "[main]   data dir = " << dataDir << "\n"
              << "[main]   default admin: username='admin' password='admin123'\n\n";

    server.start();
    return 0;
}
