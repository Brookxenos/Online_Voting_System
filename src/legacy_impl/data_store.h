#ifndef DATA_STORE_H
#define DATA_STORE_H

#include "models.h"
#include <string>
#include <vector>
#include <mutex>

//
// CSV-backed data store. Thread-safe via a single mutex (server is
// multi-threaded, one worker thread per client connection).
//
// Files live in <dataDir>/:
//   voters.csv     — id,name,passwordHash,nationalId,hasVoted
//   candidates.csv — id,name,party,symbol,position,votes
//   votes.csv      — voterId,candidateId,timestamp,signature
//   admins.csv     — username,passwordHash
//   audit.log      — timestamp|actor|action|detail   (pipe-delimited, append-only)
//
class DataStore {
public:
    explicit DataStore(const std::string& dataDir);

    // --- Election state -----------------------------------------------------
    bool electionOpen() const { return electionOpen_; }
    void setElectionOpen(bool open);

    // --- Voters -------------------------------------------------------------
    bool     addVoter(const Voter& v);
    Voter*   findVoter(const std::string& id);
    bool     voterExists(const std::string& id) const;
    std::vector<Voter> allVoters() const { return voters_; }
    void     markVoted(const std::string& id);

    // --- Candidates ---------------------------------------------------------
    bool        addCandidate(const Candidate& c);
    bool        removeCandidate(const std::string& id);
    Candidate*  findCandidate(const std::string& id);
    std::vector<Candidate> allCandidates() const { return candidates_; }
    void        incrementVote(const std::string& candidateId);

    // --- Votes --------------------------------------------------------------
    void     recordVote(const Vote& v);
    std::vector<Vote> allVotes() const { return votes_; }

    // --- Admins -------------------------------------------------------------
    bool      addAdmin(const Admin& a);
    Admin*    findAdmin(const std::string& username);
    std::vector<Admin> allAdmins() const { return admins_; }
    void      saveAdmins();

    // --- Audit --------------------------------------------------------------
    void     audit(const std::string& actor, const std::string& action,
                   const std::string& detail);
    std::vector<AuditEntry> recentAudit(size_t n) const;

    // --- Persistence --------------------------------------------------------
    void     saveAll();
    void     reloadAll();

private:
    std::string dataDir_;
    mutable std::mutex mtx_;
    bool electionOpen_ = true;

    std::vector<Voter>     voters_;
    std::vector<Candidate> candidates_;
    std::vector<Vote>      votes_;
    std::vector<Admin>     admins_;
    std::vector<AuditEntry> auditLog_;

    // helpers
    static std::string escapeCsv(const std::string& s);
    static std::string unescapeCsv(const std::string& s);
    static std::vector<std::string> splitCsv(const std::string& line);

    void loadVoters();
    void loadCandidates();
    void loadVotes();
    void loadAdmins();
    void loadAudit();
    void saveVoters();
    void saveCandidates();
    void saveVotes();
    void appendAudit(const AuditEntry& e);
};

#endif // DATA_STORE_H
