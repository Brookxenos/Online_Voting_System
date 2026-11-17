#ifndef MODELS_H
#define MODELS_H

//
// OpenVote domain models. All records are persisted in the SQL database
// (see store.h / db.h); the structs below mirror the schema.
//

#include <string>
#include <vector>

struct Organization {
    long long id = 0;
    std::string name;
    std::string slug;
    bool allowSelfRegistration = true;
    std::string createdAt;
};

// Roles (least privilege):
//   platform_admin — manages organizations and platform users
//   org_admin      — manages one organization's elections and members
//   observer       — read-only visibility into that organization's elections
//   voter          — votes in elections of organizations that declare them eligible
struct User {
    long long id = 0;
    std::string username;
    std::string displayName;
    std::string passwordHash;
    std::string role;       // platform_admin | org_admin | observer | voter
    long long orgId = 0;    // 0 = no organization (platform-level only)
    std::string nationalId; // legacy data only; not collected from new registrations
    bool disabled = false;
    std::string createdAt;
};

struct Session {
    std::string tokenHash;  // sha256 of the session token; raw token never stored
    long long userId = 0;
    std::string createdAt;
    std::string expiresAt;
    bool revoked = false;
};

// Election lifecycle: draft -> open -> closed -> published -> archived;
// a cancelled draft may also be archived without deleting its record.
// (transitions enforced server-side in store.cpp / api.cpp).
struct Election {
    long long id = 0;
    long long orgId = 0;
    std::string name;
    std::string description;
    std::string status;
    bool resultsPublic = false;    // results readable without authentication once published
    bool discoverable = false;     // public election listing/details opt-in
    bool showLiveResults = false;  // live totals visible while open (must be configured deliberately)
    std::string startAt;           // ISO 8601 UTC, empty = no restriction
    std::string endAt;             // ISO 8601 UTC, empty = no restriction
    long long createdBy = 0;
    std::string createdAt;
    std::string publishedAt;
};

struct ElectionPage {
    std::vector<Election> elections;
    long long total = 0;
};

struct AdminSummary {
    long long elections = 0;
    long long activeElections = 0;
    long long upcomingElections = 0;
    long long publishedElections = 0;
    long long candidates = 0;
    long long activeVoters = 0;
    long long eligibleVoters = 0;
    long long participations = 0;
    long long acceptedBallots = 0;
};

struct Candidate {
    long long id = 0;
    long long electionId = 0;
    std::string name;
    std::string party;
    std::string description;
    long long position = 0;   // neutral display ordering
};

struct AuditEvent {
    long long id = 0;
    std::string ts;
    std::string actor;
    std::string action;
    long long electionId = 0;
    std::string detail;
};

struct TallyResult {
    long long candidateId = 0;
    std::string name;
    std::string party;
    long long votes = 0;
};

struct Tally {
    long long id = 0;
    long long electionId = 0;
    long long totalBallots = 0;
    long long totalParticipations = 0;
    bool integrityOk = false;
    std::vector<TallyResult> candidates;
    std::string createdAt;
};

#endif // MODELS_H
