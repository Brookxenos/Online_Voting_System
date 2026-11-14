#ifndef MODELS_H
#define MODELS_H

#include <string>
#include <vector>

// All data records persisted to CSV files in the data/ directory.
// Field order matches the CSV column order for easy load/save.

struct Voter {
    std::string id;          // e.g. "V001"
    std::string name;
    std::string passwordHash;// "salt$sha256hex"
    std::string nationalId;  // optional unique citizen id
    bool        hasVoted = false;
};

struct Candidate {
    std::string id;          // e.g. "C001"
    std::string name;
    std::string party;
    std::string symbol;      // emoji or short token
    std::string position;    // e.g. "President"
    int         votes = 0;
};

struct Admin {
    std::string username;
    std::string passwordHash;
};

struct Vote {
    std::string voterId;
    std::string candidateId;
    std::string timestamp;   // ISO 8601
    std::string signature;   // hash of (voterId|candidateId|timestamp) for tamper check
};

struct AuditEntry {
    std::string timestamp;
    std::string actor;       // voter id / admin username / "system"
    std::string action;      // e.g. "REGISTER", "LOGIN_OK", "VOTE", "ADD_CANDIDATE"
    std::string detail;
};

#endif // MODELS_H
