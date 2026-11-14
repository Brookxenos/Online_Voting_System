#include "data_store.h"
#include "crypto.h"

#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>

DataStore::DataStore(const std::string& dataDir) : dataDir_(dataDir) {
    // Ensure data dir exists.
    ::mkdir(dataDir_.c_str(), 0755);
    reloadAll();
    if (admins_.empty()) {
        // Bootstrap a default admin if none exists.
        Admin a;
        a.username = "admin";
        // Default password: "admin123" — bootstrapped on first run only.
        std::string salt = "b00tstr4p";
        a.passwordHash = crypto::hashPassword("admin123", salt);
        admins_.push_back(a);
        saveAdmins();
        audit("system", "BOOTSTRAP_ADMIN",
              "Created default admin 'admin' (password: admin123). Please change it.");
    }
}

// ---------------------------------------------------------------------------
// Election state
// ---------------------------------------------------------------------------

void DataStore::setElectionOpen(bool open) {
    std::lock_guard<std::mutex> lk(mtx_);
    electionOpen_ = open;
    audit("system", open ? "ELECTION_OPEN" : "ELECTION_CLOSED", "");
}

// ---------------------------------------------------------------------------
// CSV helpers
// ---------------------------------------------------------------------------

std::string DataStore::escapeCsv(const std::string& s) {
    bool needsQuote = false;
    for (char c : s) if (c == ',' || c == '"' || c == '\n' || c == '\r') { needsQuote = true; break; }
    if (!needsQuote) return s;
    std::string out; out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) { if (c == '"') out.push_back('"'); out.push_back(c); }
    out.push_back('"');
    return out;
}

std::string DataStore::unescapeCsv(const std::string& s) {
    if (s.empty() || s.front() != '"' || s.back() != '"') return s;
    std::string inner = s.substr(1, s.size() - 2);
    std::string out; out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '"' && i + 1 < inner.size() && inner[i+1] == '"') {
            out.push_back('"'); ++i;
        } else {
            out.push_back(inner[i]);
        }
    }
    return out;
}

std::vector<std::string> DataStore::splitCsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i+1] == '"') { cur.push_back('"'); ++i; }
                else inQuotes = false;
            } else cur.push_back(c);
        } else {
            if (c == '"') inQuotes = true;
            else if (c == ',') { fields.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
    }
    fields.push_back(cur);
    return fields;
}

// ---------------------------------------------------------------------------
// Voters
// ---------------------------------------------------------------------------

bool DataStore::addVoter(const Voter& v) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : voters_) if (e.id == v.id) return false;
    voters_.push_back(v);
    saveVoters();
    return true;
}

Voter* DataStore::findVoter(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& e : voters_) if (e.id == id) return &e;
    return nullptr;
}

bool DataStore::voterExists(const std::string& id) const {
    for (const auto& e : voters_) if (e.id == id) return true;
    return false;
}

void DataStore::markVoted(const std::string& id) {
    for (auto& e : voters_) if (e.id == id) { e.hasVoted = true; break; }
    saveVoters();
}

// ---------------------------------------------------------------------------
// Candidates
// ---------------------------------------------------------------------------

bool DataStore::addCandidate(const Candidate& c) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : candidates_) if (e.id == c.id) return false;
    candidates_.push_back(c);
    saveCandidates();
    return true;
}

bool DataStore::removeCandidate(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = std::remove_if(candidates_.begin(), candidates_.end(),
        [&](const Candidate& c){ return c.id == id; });
    if (it == candidates_.end()) return false;
    candidates_.erase(it, candidates_.end());
    saveCandidates();
    return true;
}

Candidate* DataStore::findCandidate(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& e : candidates_) if (e.id == id) return &e;
    return nullptr;
}

void DataStore::incrementVote(const std::string& candidateId) {
    for (auto& e : candidates_) if (e.id == candidateId) { e.votes++; break; }
    saveCandidates();
}

// ---------------------------------------------------------------------------
// Votes
// ---------------------------------------------------------------------------

void DataStore::recordVote(const Vote& v) {
    std::lock_guard<std::mutex> lk(mtx_);
    votes_.push_back(v);
    incrementVote(v.candidateId);
    markVoted(v.voterId);
    saveVotes();
}

// ---------------------------------------------------------------------------
// Admins
// ---------------------------------------------------------------------------

bool DataStore::addAdmin(const Admin& a) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& e : admins_) if (e.username == a.username) return false;
    admins_.push_back(a);
    saveAdmins();
    return true;
}

Admin* DataStore::findAdmin(const std::string& username) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& e : admins_) if (e.username == username) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Audit
// ---------------------------------------------------------------------------

static std::string nowIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

void DataStore::audit(const std::string& actor, const std::string& action,
                      const std::string& detail) {
    AuditEntry e{ nowIso(), actor, action, detail };
    auditLog_.push_back(e);
    appendAudit(e);
}

void DataStore::appendAudit(const AuditEntry& e) {
    std::ofstream ofs(dataDir_ + "/audit.log", std::ios::app);
    ofs << e.timestamp << '|' << e.actor << '|' << e.action << '|'
        << e.detail << '\n';
}

std::vector<AuditEntry> DataStore::recentAudit(size_t n) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (auditLog_.size() <= n) return auditLog_;
    return std::vector<AuditEntry>(auditLog_.end() - n, auditLog_.end());
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void DataStore::saveAll() {
    saveVoters(); saveCandidates(); saveVotes(); saveAdmins();
}

void DataStore::reloadAll() {
    loadVoters(); loadCandidates(); loadVotes(); loadAdmins(); loadAudit();
}

void DataStore::loadVoters() {
    voters_.clear();
    std::ifstream ifs(dataDir_ + "/voters.csv");
    if (!ifs) return;
    std::string line;
    std::getline(ifs, line); // header
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto f = splitCsv(line);
        if (f.size() < 5) continue;
        Voter v;
        v.id = f[0]; v.name = f[1]; v.passwordHash = f[2];
        v.nationalId = f[3]; v.hasVoted = (f[4] == "1" || f[4] == "true");
        voters_.push_back(v);
    }
}

void DataStore::saveVoters() {
    std::ofstream ofs(dataDir_ + "/voters.csv", std::ios::trunc);
    ofs << "id,name,passwordHash,nationalId,hasVoted\n";
    for (const auto& v : voters_) {
        ofs << v.id << ',' << escapeCsv(v.name) << ','
            << v.passwordHash << ',' << escapeCsv(v.nationalId) << ','
            << (v.hasVoted ? "1" : "0") << '\n';
    }
}

void DataStore::loadCandidates() {
    candidates_.clear();
    std::ifstream ifs(dataDir_ + "/candidates.csv");
    if (!ifs) return;
    std::string line;
    std::getline(ifs, line);
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto f = splitCsv(line);
        if (f.size() < 6) continue;
        Candidate c;
        c.id = f[0]; c.name = f[1]; c.party = f[2];
        c.symbol = f[3]; c.position = f[4];
        try { c.votes = std::stoi(f[5]); } catch(...) { c.votes = 0; }
        candidates_.push_back(c);
    }
}

void DataStore::saveCandidates() {
    std::ofstream ofs(dataDir_ + "/candidates.csv", std::ios::trunc);
    ofs << "id,name,party,symbol,position,votes\n";
    for (const auto& c : candidates_) {
        ofs << c.id << ',' << escapeCsv(c.name) << ',' << escapeCsv(c.party)
            << ',' << escapeCsv(c.symbol) << ',' << escapeCsv(c.position)
            << ',' << c.votes << '\n';
    }
}

void DataStore::loadVotes() {
    votes_.clear();
    std::ifstream ifs(dataDir_ + "/votes.csv");
    if (!ifs) return;
    std::string line;
    std::getline(ifs, line);
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto f = splitCsv(line);
        if (f.size() < 4) continue;
        Vote v{ f[0], f[1], f[2], f[3] };
        votes_.push_back(v);
    }
}

void DataStore::saveVotes() {
    std::ofstream ofs(dataDir_ + "/votes.csv", std::ios::trunc);
    ofs << "voterId,candidateId,timestamp,signature\n";
    for (const auto& v : votes_) {
        ofs << v.voterId << ',' << v.candidateId << ',' << v.timestamp
            << ',' << v.signature << '\n';
    }
}

void DataStore::loadAdmins() {
    admins_.clear();
    std::ifstream ifs(dataDir_ + "/admins.csv");
    if (!ifs) return;
    std::string line;
    std::getline(ifs, line);
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto f = splitCsv(line);
        if (f.size() < 2) continue;
        Admin a{ f[0], f[1] };
        admins_.push_back(a);
    }
}

void DataStore::saveAdmins() {
    std::ofstream ofs(dataDir_ + "/admins.csv", std::ios::trunc);
    ofs << "username,passwordHash\n";
    for (const auto& a : admins_) {
        ofs << a.username << ',' << a.passwordHash << '\n';
    }
}

void DataStore::loadAudit() {
    auditLog_.clear();
    std::ifstream ifs(dataDir_ + "/audit.log");
    if (!ifs) return;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        // pipe-delimited: timestamp|actor|action|detail
        std::vector<std::string> parts;
        std::string cur;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '|') { parts.push_back(cur); cur.clear(); }
            else cur.push_back(line[i]);
        }
        parts.push_back(cur);
        if (parts.size() < 4) continue;
        AuditEntry e{ parts[0], parts[1], parts[2], parts[3] };
        auditLog_.push_back(e);
    }
}
