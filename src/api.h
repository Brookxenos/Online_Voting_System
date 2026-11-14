#ifndef API_H
#define API_H

//
// API controllers. Registers every /api route on the HttpServer and
// enforces authentication/authorization server-side on each endpoint.
// The frontend is never trusted: session identity comes from the
// HttpOnly session cookie, roles from the database.
//

#include "http.h"
#include "store.h"
#include <string>

struct ApiConfig {
    int sessionHours = 24 * 7;
    bool secureCookies = false;   // set true when served over HTTPS
};

void registerApiRoutes(HttpServer& server, Store& store, const ApiConfig& cfg);

#endif // API_H
