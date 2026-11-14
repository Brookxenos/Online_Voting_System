#include "store.h"
#include "crypto.h"

#include <ctime>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <map>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string nowIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

static std::string isoInHours(int hours) {
    std::time_t t = std::time(nullptr) + (std::time_t)hours * 3600;
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Audit details must not contain delimiters or control characters.
static std::string sanitizeDetail(std::string s) {
    std::string out;
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        if (c == '|' || c == '\n' || c == '\r' || u < 0x20) out.push_back(' ');
        else out.push_back(c);
    }
    if (out.size() > 512) out.resize(512);
    return out;
}

static Election electionFromRow(const Database::Row& r) {
    Election e;
    e.id = std::stoll(r.at("id"));
    e.orgId = std::stoll(r.at("org_id"));
    e.name = r.at("name");
    e.description = r.at("description");
    e.status = r.at("status");
    e.resultsPublic = r.at("results_public") == "1";
    e.showLiveResults = r.at("show_live_results") == "1";
    e.startAt = r.at("start_at");
    e.endAt = r.at("end_at");
    e.createdBy = r.at("created_by").empty() ? 0 : std::stoll(r.at("created_by"));
    e.createdAt = r.at("created_at");
    return e;
}

static User userFromRow(const Database::Row& r) {
    User u;
    u.id = std::stoll(r.at("id"));
    u.username = r.at("username");
    u.displayName = r.at("display_name");
    u.passwordHash = r.at("password_hash");
    u.role = r.at("role");
    u.orgId = r.at("org_id").empty() ? 0 : std::stoll(r.at("org_id"));
    u.nationalId = r.count("national_id") ? r.at("national_id") : "";
    u.disabled = r.at("disabled") == "1";
    u.createdAt = r.at("created_at");
    return u;
}

static Organization orgFromRow(const Database::Row& r) {
    Organization o;
    o.id = std::stoll(r.at("id"));
    o.name = r.at("name");
    o.slug = r.at("slug");
    o.allowSelfRegistration = r.at("allow_self_registration") == "1";
    o.createdAt = r.at("created_at");
    return o;
}

// ---------------------------------------------------------------------------
// Schema migrations (versioned, applied in order, tracked by user_version)
// ---------------------------------------------------------------------------

static const std::vector<std::string> kMigrations = {
    // 1 — core schema
    R"SQL(
CREATE TABLE organizations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    slug TEXT NOT NULL UNIQUE,
    allow_self_registration INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL CHECK (role IN ('platform_admin','org_admin','observer','voter')),
    org_id INTEGER REFERENCES organizations(id),
    national_id TEXT NOT NULL DEFAULT '',
    disabled INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL
);
CREATE INDEX idx_users_org ON users(org_id);
CREATE TABLE sessions (
    token_hash TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_sessions_user ON sessions(user_id);
CREATE TABLE elections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    org_id INTEGER NOT NULL REFERENCES organizations(id),
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'draft'
        CHECK (status IN ('draft','open','closed','published','archived')),
    results_public INTEGER NOT NULL DEFAULT 0,
    show_live_results INTEGER NOT NULL DEFAULT 0,
    start_at TEXT,
    end_at TEXT,
    created_by INTEGER REFERENCES users(id),
    created_at TEXT NOT NULL
);
CREATE INDEX idx_elections_org ON elections(org_id);
CREATE TABLE candidates (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    election_id INTEGER NOT NULL REFERENCES elections(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    party TEXT NOT NULL DEFAULT '',
    description TEXT NOT NULL DEFAULT '',
    position INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_candidates_election ON candidates(election_id);
CREATE TABLE eligibility (
    election_id INTEGER NOT NULL REFERENCES elections(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    PRIMARY KEY (election_id, user_id)
);
CREATE TABLE participation (
    -- Identity side of a ballot: records THAT a user voted in an election,
    -- never WHAT they voted for.
    election_id INTEGER NOT NULL REFERENCES elections(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id),
    voted_at TEXT NOT NULL,
    PRIMARY KEY (election_id, user_id)
);
CREATE TABLE ballots (
    -- Choice side of a ballot: records WHAT was chosen for an election,
    -- never WHO chose it. No user reference is stored or derivable from
    -- this table alone.
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    election_id INTEGER NOT NULL REFERENCES elections(id) ON DELETE CASCADE,
    candidate_id INTEGER NOT NULL REFERENCES candidates(id),
    cast_at TEXT NOT NULL,
    receipt_nonce TEXT NOT NULL
);
CREATE INDEX idx_ballots_election ON ballots(election_id);
CREATE TABLE tally_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    election_id INTEGER NOT NULL REFERENCES elections(id) ON DELETE CASCADE,
    total_ballots INTEGER NOT NULL,
    total_participations INTEGER NOT NULL,
    integrity_ok INTEGER NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE tally_results (
    tally_id INTEGER NOT NULL REFERENCES tally_runs(id) ON DELETE CASCADE,
    candidate_id INTEGER NOT NULL,
    votes INTEGER NOT NULL,
    PRIMARY KEY (tally_id, candidate_id)
);
CREATE TABLE audit_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL,
    actor TEXT NOT NULL,
    action TEXT NOT NULL,
    election_id INTEGER,
    detail TEXT NOT NULL DEFAULT ''
);
CREATE INDEX idx_audit_election ON audit_events(election_id);
)SQL",
    // 2 — legacy import marker (kept as a table so migrations stay append-only)
    R"SQL(
CREATE TABLE IF NOT EXISTS migration_markers (
    name TEXT PRIMARY KEY,
    applied_at TEXT NOT NULL
);
)SQL",
};

// ---------------------------------------------------------------------------
// Construction / initialization
// ---------------------------------------------------------------------------

Store::Store(Database& db) : db_(db) {}

void Store::initialize(const std::string& legacyDataDir) {
    db_.migrate(kMigrations);
    importLegacyCsvIfPresent(legacyDataDir);
}

// ---------------------------------------------------------------------------
// Organizations
// ---------------------------------------------------------------------------

std::optional<Organization> Store::getOrganization(long long id) {
    auto rows = db_.query("SELECT * FROM organizations WHERE id=?", {std::to_string(id)});
    if (rows.empty()) return std::nullopt;
    return orgFromRow(rows[0]);
}

std::optional<Organization> Store::getOrganizationBySlug(const std::string& slug) {
    auto rows = db_.query("SELECT * FROM organizations WHERE slug=?", {slug});
    if (rows.empty()) return std::nullopt;
    return orgFromRow(rows[0]);
}

std::vector<Organization> Store::listOrganizations() {
    std::vector<Organization> out;
    for (const auto& r : db_.query("SELECT * FROM organizations ORDER BY id")) {
        out.push_back(orgFromRow(r));
    }
    return out;
}

Organization Store::createOrganization(const std::string& name, const std::string& slug,
                                       bool allowSelfRegistration) {
    db_.exec("INSERT INTO organizations(name,slug,allow_self_registration,created_at) VALUES(?,?,?,?)",
             {name, slug, allowSelfRegistration ? "1" : "0", nowIso()});
    Organization o;
    o.id = db_.lastInsertId();
    o.name = name; o.slug = slug;
    o.allowSelfRegistration = allowSelfRegistration;
    o.createdAt = nowIso();
    return o;
}

// ---------------------------------------------------------------------------
// Users
// ---------------------------------------------------------------------------

Store::CreateUserResult Store::createUser(User& user) {
    if (user.orgId != 0 && !getOrganization(user.orgId)) return CreateUserResult::OrgNotFound;
    try {
        db_.exec(
            "INSERT INTO users(username,display_name,password_hash,role,org_id,national_id,disabled,created_at) "
            "VALUES(?,?,?,?,?,?,0,?)",
            {user.username, user.displayName, user.passwordHash, user.role,
             user.orgId == 0 ? "" : std::to_string(user.orgId), user.nationalId, nowIso()});
    } catch (const std::runtime_error&) {
        // UNIQUE constraint on username (or another constraint failure).
        auto rows = db_.query("SELECT id FROM users WHERE username=?", {user.username});
        return rows.empty() ? CreateUserResult::OrgNotFound : CreateUserResult::DuplicateUsername;
    }
    user.id = db_.lastInsertId();
    user.createdAt = nowIso();
    return CreateUserResult::Ok;
}

std::optional<User> Store::findUserByUsername(const std::string& username) {
    auto rows = db_.query("SELECT * FROM users WHERE username=?", {username});
    if (rows.empty()) return std::nullopt;
    return userFromRow(rows[0]);
}

std::optional<User> Store::findUserById(long long id) {
    auto rows = db_.query("SELECT * FROM users WHERE id=?", {std::to_string(id)});
    if (rows.empty()) return std::nullopt;
    return userFromRow(rows[0]);
}

std::vector<User> Store::listUsers(std::optional<long long> orgId) {
    std::vector<User> out;
    if (orgId) {
        for (const auto& r : db_.query("SELECT * FROM users WHERE org_id=? ORDER BY id",
                                       {std::to_string(*orgId)})) out.push_back(userFromRow(r));
    } else {
        for (const auto& r : db_.query("SELECT * FROM users ORDER BY id")) out.push_back(userFromRow(r));
    }
    return out;
}

void Store::updatePasswordHash(long long userId, const std::string& hash) {
    db_.exec("UPDATE users SET password_hash=? WHERE id=?", {hash, std::to_string(userId)});
}

void Store::setUserDisabled(long long userId, bool disabled) {
    db_.exec("UPDATE users SET disabled=? WHERE id=?", {disabled ? "1" : "0", std::to_string(userId)});
    if (disabled) revokeAllSessions(userId);
}

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

std::string Store::createSession(long long userId, int hoursValid) {
    std::string token = crypto::randomHex(32);
    std::string hash = crypto::sha256Hex(token);
    db_.exec("INSERT INTO sessions(token_hash,user_id,created_at,expires_at,revoked) VALUES(?,?,?,?,0)",
             {hash, std::to_string(userId), nowIso(), isoInHours(hoursValid)});
    return token;
}

std::optional<User> Store::userForSessionToken(const std::string& token) {
    if (token.empty()) return std::nullopt;
    std::string hash = crypto::sha256Hex(token);
    auto rows = db_.query(
        "SELECT u.* FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token_hash=? AND s.revoked=0 AND s.expires_at > ?",
        {hash, nowIso()});
    if (rows.empty()) return std::nullopt;
    User u = userFromRow(rows[0]);
    if (u.disabled) return std::nullopt;
    return u;
}

void Store::revokeSession(const std::string& token) {
    std::string hash = crypto::sha256Hex(token);
    db_.exec("UPDATE sessions SET revoked=1 WHERE token_hash=?", {hash});
}

void Store::revokeAllSessions(long long userId) {
    db_.exec("UPDATE sessions SET revoked=1 WHERE user_id=?", {std::to_string(userId)});
}

// ---------------------------------------------------------------------------
// Elections
// ---------------------------------------------------------------------------

Store::CreateElectionResult Store::createElection(Election& e) {
    if (!getOrganization(e.orgId)) return CreateElectionResult::OrgNotFound;
    db_.exec(
        "INSERT INTO elections(org_id,name,description,status,results_public,show_live_results,"
        "start_at,end_at,created_by,created_at) VALUES(?,?,?,?,?,?,COALESCE(NULLIF(?,''),NULL),"
        "COALESCE(NULLIF(?,''),NULL),?,?)",
        {std::to_string(e.orgId), e.name, e.description, "draft",
         e.resultsPublic ? "1" : "0", e.showLiveResults ? "1" : "0",
         e.startAt, e.endAt,
         e.createdBy == 0 ? "" : std::to_string(e.createdBy), nowIso()});
    e.id = db_.lastInsertId();
    e.status = "draft";
    e.createdAt = nowIso();
    return CreateElectionResult::Ok;
}

std::optional<Election> Store::getElection(long long id) {
    auto rows = db_.query("SELECT * FROM elections WHERE id=?", {std::to_string(id)});
    if (rows.empty()) return std::nullopt;
    return electionFromRow(rows[0]);
}

std::vector<Election> Store::listElectionsForOrg(long long orgId) {
    std::vector<Election> out;
    for (const auto& r : db_.query("SELECT * FROM elections WHERE org_id=? ORDER BY id",
                                   {std::to_string(orgId)})) {
        out.push_back(electionFromRow(r));
    }
    return out;
}

std::vector<Election> Store::listElectionsVisibleTo(const User& user) {
    std::vector<Election> out;
    if (user.role == "platform_admin") {
        for (const auto& r : db_.query("SELECT * FROM elections ORDER BY id")) {
            out.push_back(electionFromRow(r));
        }
        return out;
    }
    if ((user.role == "org_admin" || user.role == "observer") && user.orgId != 0) {
        return listElectionsForOrg(user.orgId);
    }
    // Voters: elections of organizations that allow self-registration
    // (any non-draft status) plus elections where they are eligible or
    // have already participated.
    for (const auto& r : db_.query(
             "SELECT DISTINCT e.* FROM elections e "
             "JOIN organizations o ON o.id = e.org_id "
             "WHERE e.status != 'draft' AND o.allow_self_registration = 1 "
             "UNION "
             "SELECT e.* FROM elections e JOIN eligibility el ON el.election_id = e.id "
             "WHERE el.user_id = ? "
             "UNION "
             "SELECT e.* FROM elections e JOIN participation p ON p.election_id = e.id "
             "WHERE p.user_id = ? ORDER BY id",
             {std::to_string(user.id), std::to_string(user.id)})) {
        out.push_back(electionFromRow(r));
    }
    return out;
}

bool Store::updateElectionDraft(long long electionId, const Election& changes) {
    auto eOpt = getElection(electionId);
    if (!eOpt || eOpt->status != "draft") return false;
    db_.exec(
        "UPDATE elections SET name=?, description=?, show_live_results=?, "
        "start_at=?, end_at=? WHERE id=? AND status='draft'",
        {changes.name, changes.description, changes.showLiveResults ? "1" : "0",
         changes.startAt, changes.endAt, std::to_string(electionId)});
    return true;
}

bool Store::transitionElection(long long electionId, const std::string& newStatus,
                               const std::string& actor) {
    auto eOpt = getElection(electionId);
    if (!eOpt) return false;
    const std::string cur = eOpt->status;

    // Valid lifecycle transitions:
    //   draft -> open            (election starts)
    //   open -> closed           (voting ends; no more ballots)
    //   closed -> published      (tally run + results publication)
    //   published -> archived    (record keeping)
    static const std::map<std::string, std::vector<std::string>> valid = {
        {"draft",     {"open"}},
        {"open",      {"closed"}},
        {"closed",    {"published"}},
        {"published", {"archived"}},
    };

    auto it = valid.find(cur);
    if (it == valid.end()) return false;
    if (std::find(it->second.begin(), it->second.end(), newStatus) == it->second.end()) {
        return false;
    }

    if (newStatus == "open") {
        // Stamp actual open time if no start time was configured.
        db_.exec("UPDATE elections SET status='open', "
                 "start_at=COALESCE(NULLIF(start_at,''), ?) WHERE id=? AND status='draft'",
                 {nowIso(), std::to_string(electionId)});
        addAudit(actor, "ELECTION_OPEN", electionId, "");
        return true;
    }

    if (newStatus == "published") {
        // A tally must exist and be internally consistent before publication.
        auto tally = tallyElection(electionId);
        if (!tally || !tally->integrityOk) {
            addAudit(actor, "PUBLISH_BLOCKED", electionId,
                     "tally integrity check failed; results not published");
            return false;
        }
        db_.exec("UPDATE elections SET status='published', results_public=1 WHERE id=?",
                 {std::to_string(electionId)});
        addAudit(actor, "ELECTION_PUBLISHED", electionId, "tally id " + std::to_string(tally->id));
        return true;
    }

    db_.exec("UPDATE elections SET status=? WHERE id=?",
             {newStatus, std::to_string(electionId)});
    addAudit(actor, newStatus == "closed" ? "ELECTION_CLOSED" : "ELECTION_ARCHIVED",
             electionId, "");
    return true;
}

// ---------------------------------------------------------------------------
// Candidates
// ---------------------------------------------------------------------------

Store::AddCandidateResult Store::addCandidate(Candidate& c) {
    auto eOpt = getElection(c.electionId);
    if (!eOpt) return AddCandidateResult::ElectionNotFound;
    if (eOpt->status != "draft") return AddCandidateResult::NotEditable;
    db_.exec(
        "INSERT INTO candidates(election_id,name,party,description,position) VALUES(?,?,?,?,?)",
        {std::to_string(c.electionId), c.name, c.party, c.description,
         std::to_string(c.position)});
    c.id = db_.lastInsertId();
    return AddCandidateResult::Ok;
}

std::vector<Candidate> Store::listCandidates(long long electionId) {
    std::vector<Candidate> out;
    for (const auto& r : db_.query(
             "SELECT * FROM candidates WHERE election_id=? ORDER BY position, id",
             {std::to_string(electionId)})) {
        Candidate c;
        c.id = std::stoll(r.at("id"));
        c.electionId = std::stoll(r.at("election_id"));
        c.name = r.at("name");
        c.party = r.at("party");
        c.description = r.at("description");
        c.position = std::stoll(r.at("position"));
        out.push_back(c);
    }
    return out;
}

bool Store::removeCandidate(long long candidateId, long long electionId) {
    auto eOpt = getElection(electionId);
    if (!eOpt || eOpt->status != "draft") return false;
    db_.exec("DELETE FROM candidates WHERE id=? AND election_id=?",
             {std::to_string(candidateId), std::to_string(electionId)});
    return true;
}

// ---------------------------------------------------------------------------
// Eligibility
// ---------------------------------------------------------------------------

bool Store::grantEligibility(long long electionId, long long userId) {
    if (!findUserById(userId)) return false;
    db_.exec("INSERT OR IGNORE INTO eligibility(election_id,user_id) VALUES(?,?)",
             {std::to_string(electionId), std::to_string(userId)});
    return true;
}

bool Store::revokeEligibility(long long electionId, long long userId) {
    db_.exec("DELETE FROM eligibility WHERE election_id=? AND user_id=?",
             {std::to_string(electionId), std::to_string(userId)});
    return true;
}

std::vector<User> Store::listEligibleUsers(long long electionId) {
    std::vector<User> out;
    for (const auto& r : db_.query(
             "SELECT u.* FROM eligibility el JOIN users u ON u.id = el.user_id "
             "WHERE el.election_id=? ORDER BY u.id",
             {std::to_string(electionId)})) {
        out.push_back(userFromRow(r));
    }
    return out;
}

bool Store::isEligible(long long electionId, long long userId) {
    auto rows = db_.query(
        "SELECT 1 AS one FROM eligibility WHERE election_id=? AND user_id=?",
        {std::to_string(electionId), std::to_string(userId)});
    return !rows.empty();
}

// ---------------------------------------------------------------------------
// Voting — the transactional core
// ---------------------------------------------------------------------------

Store::CastResult Store::castBallot(long long electionId, long long userId, long long candidateId) {
    CastResult res;
    res.status = CastResult::Status::ElectionNotFound;

    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    db_.tx([&]{
        auto eOpt = getElection(electionId);
        if (!eOpt) return;
        const Election& e = *eOpt;

        if (e.status != "open") { res.status = CastResult::Status::ElectionNotOpen; return; }

        std::string now = nowIso();
        if (!e.startAt.empty() && now < e.startAt) { res.status = CastResult::Status::OutsideVotingWindow; return; }
        if (!e.endAt.empty() && now > e.endAt)     { res.status = CastResult::Status::OutsideVotingWindow; return; }

        if (!isEligible(electionId, userId)) { res.status = CastResult::Status::NotEligible; return; }

        auto cand = db_.query("SELECT id FROM candidates WHERE id=? AND election_id=?",
                              {std::to_string(candidateId), std::to_string(electionId)});
        if (cand.empty()) { res.status = CastResult::Status::InvalidCandidate; return; }

        // Duplicate-vote prevention: the participation primary key
        // (election_id, user_id) makes a second insert impossible inside
        // this transaction.
        auto already = db_.query(
            "SELECT 1 AS one FROM participation WHERE election_id=? AND user_id=?",
            {std::to_string(electionId), std::to_string(userId)});
        if (!already.empty()) { res.status = CastResult::Status::AlreadyVoted; return; }

        // Write the two separated records. No cross-reference exists
        // between them.
        std::string nonce = crypto::randomHex(16);
        db_.exec("INSERT INTO participation(election_id,user_id,voted_at) VALUES(?,?,?)",
                 {std::to_string(electionId), std::to_string(userId), now});
        db_.exec("INSERT INTO ballots(election_id,candidate_id,cast_at,receipt_nonce) VALUES(?,?,?,?)",
                 {std::to_string(electionId), std::to_string(candidateId), now, nonce});

        long long ballotId = db_.lastInsertId();
        // Non-revealing receipt: binds ballot row, choice and nonce, but
        // carries no user identity and is not published on any endpoint.
        res.receipt = crypto::sha256Hex(
            "ov-receipt|" + std::to_string(ballotId) + "|" +
            std::to_string(candidateId) + "|" + nonce).substr(0, 24);
        res.status = CastResult::Status::Ok;
    });

    if (res.status == CastResult::Status::Ok) {
        addAudit("voter", "VOTE_CAST", electionId, "");
    }
    return res;
}

// ---------------------------------------------------------------------------
// Tally and results
// ---------------------------------------------------------------------------

std::optional<Tally> Store::tallyElection(long long electionId) {
    auto eOpt = getElection(electionId);
    if (!eOpt) return std::nullopt;

    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    db_.tx([&]{
        long long totalBallots = 0, totalParts = 0;
        auto b = db_.query("SELECT COUNT(*) AS n FROM ballots WHERE election_id=?",
                           {std::to_string(electionId)});
        auto p = db_.query("SELECT COUNT(*) AS n FROM participation WHERE election_id=?",
                           {std::to_string(electionId)});
        if (!b.empty()) totalBallots = std::stoll(b[0].at("n"));
        if (!p.empty()) totalParts = std::stoll(p[0].at("n"));

        bool integrityOk = (totalBallots == totalParts);

        db_.exec("INSERT INTO tally_runs(election_id,total_ballots,total_participations,"
                 "integrity_ok,created_at) VALUES(?,?,?,?,?)",
                 {std::to_string(electionId), std::to_string(totalBallots),
                  std::to_string(totalParts), integrityOk ? "1" : "0", nowIso()});
        long long tallyId = db_.lastInsertId();

        for (const auto& c : listCandidates(electionId)) {
            auto cnt = db_.query("SELECT COUNT(*) AS n FROM ballots WHERE election_id=? AND candidate_id=?",
                                 {std::to_string(electionId), std::to_string(c.id)});
            long long votes = cnt.empty() ? 0 : std::stoll(cnt[0].at("n"));
            db_.exec("INSERT INTO tally_results(tally_id,candidate_id,votes) VALUES(?,?,?)",
                     {std::to_string(tallyId), std::to_string(c.id), std::to_string(votes)});
        }
    });

    return latestTally(electionId);
}

std::optional<Tally> Store::latestTally(long long electionId) {
    auto runs = db_.query(
        "SELECT * FROM tally_runs WHERE election_id=? ORDER BY id DESC LIMIT 1",
        {std::to_string(electionId)});
    if (runs.empty()) return std::nullopt;
    Tally t;
    t.id = std::stoll(runs[0].at("id"));
    t.electionId = std::stoll(runs[0].at("election_id"));
    t.totalBallots = std::stoll(runs[0].at("total_ballots"));
    t.totalParticipations = std::stoll(runs[0].at("total_participations"));
    t.integrityOk = runs[0].at("integrity_ok") == "1";
    t.createdAt = runs[0].at("created_at");

    for (const auto& c : listCandidates(electionId)) {
        TallyResult tr;
        tr.candidateId = c.id;
        tr.name = c.name;
        tr.party = c.party;
        tr.votes = 0;
        auto cnt = db_.query("SELECT votes FROM tally_results WHERE tally_id=? AND candidate_id=?",
                             {std::to_string(t.id), std::to_string(c.id)});
        if (!cnt.empty()) tr.votes = std::stoll(cnt[0].at("votes"));
        t.candidates.push_back(tr);
    }
    return t;
}

long long Store::ballotCount(long long electionId) {
    auto b = db_.query("SELECT COUNT(*) AS n FROM ballots WHERE election_id=?",
                       {std::to_string(electionId)});
    return b.empty() ? 0 : std::stoll(b[0].at("n"));
}

long long Store::participationCount(long long electionId) {
    auto p = db_.query("SELECT COUNT(*) AS n FROM participation WHERE election_id=?",
                       {std::to_string(electionId)});
    return p.empty() ? 0 : std::stoll(p[0].at("n"));
}

// ---------------------------------------------------------------------------
// Audit
// ---------------------------------------------------------------------------

void Store::addAudit(const std::string& actor, const std::string& action,
                     long long electionId, const std::string& detail) {
    db_.exec("INSERT INTO audit_events(ts,actor,action,election_id,detail) VALUES(?,?,?,?,?)",
             {nowIso(), actor.empty() ? "system" : actor, sanitizeDetail(action),
              electionId == 0 ? "" : std::to_string(electionId), sanitizeDetail(detail)});
}

std::vector<AuditEvent> Store::listAudit(long long electionId, size_t limit) {
    std::vector<AuditEvent> out;
    std::string sql = "SELECT * FROM audit_events";
    std::vector<std::string> binds;
    if (electionId != 0) {
        sql += " WHERE election_id=?";
        binds.push_back(std::to_string(electionId));
    }
    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit);
    for (const auto& r : db_.query(sql, binds)) {
        AuditEvent e;
        e.id = std::stoll(r.at("id"));
        e.ts = r.at("ts");
        e.actor = r.at("actor");
        e.action = r.at("action");
        e.electionId = r.at("election_id").empty() ? 0 : std::stoll(r.at("election_id"));
        e.detail = r.at("detail");
        out.push_back(e);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Legacy CSV import (never deletes; CSV files are left untouched)
// ---------------------------------------------------------------------------

static std::vector<std::string> splitCsv(const std::string& line) {
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

static std::string unescapeCsv(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

long long Store::importOrg(const std::string& name, const std::string& slug) {
    db_.exec("INSERT INTO organizations(name,slug,allow_self_registration,created_at) VALUES(?,?,1,?)",
             {name, slug, nowIso()});
    return db_.lastInsertId();
}

void Store::importLegacyCsvIfPresent(const std::string& dataDir) {
    // Import runs once, only into a fresh database, and only if the legacy
    // files exist. Nothing is ever deleted; the CSV files stay in place.
    auto markers = db_.query("SELECT 1 AS one FROM migration_markers WHERE name='legacy_csv'");
    auto userCount = db_.query("SELECT COUNT(*) AS n FROM users");
    bool dbHasUsers = !userCount.empty() && std::stoll(userCount[0].at("n")) > 0;
    if (!markers.empty() || dbHasUsers) return;

    std::ifstream votersFile(dataDir + "/voters.csv");
    std::ifstream adminsFile(dataDir + "/admins.csv");
    std::ifstream candFile(dataDir + "/candidates.csv");
    std::ifstream votesFile(dataDir + "/votes.csv");
    if (!votersFile && !adminsFile && !candFile) return; // nothing to import

    addAudit("system", "LEGACY_IMPORT_BEGIN", 0,
             "importing historical CSV data; historical records are identity-linked and not anonymous");

    long long orgId = importOrg("Imported (legacy CSV data)", "legacy-import");

    // Admins -> platform_admin with legacy-format password hashes.
    // (Verification of the old SHA-256 scheme happens on first login, after
    // which the hash is upgraded to Argon2id.)
    if (adminsFile) {
        std::string line;
        std::getline(adminsFile, line); // header
        while (std::getline(adminsFile, line)) {
            if (line.empty()) continue;
            auto f = splitCsv(line);
            if (f.size() < 2) continue;
            User u;
            u.username = unescapeCsv(f[0]);
            u.displayName = u.username;
            u.role = "platform_admin";
            // Old format "<salt>$<hash>" -> tagged "sha256$<salt>$<hash>".
            u.passwordHash = "sha256$" + unescapeCsv(f[1]);
            if (createUser(u) != CreateUserResult::Ok) {
                addAudit("system", "LEGACY_IMPORT_SKIP_ADMIN", 0, "username " + u.username);
            }
        }
    }

    // Voters -> voter role in the import organization.
    std::map<std::string, long long> voterIds;      // legacy "V001" -> new user id
    std::map<std::string, bool> voterHasVoted;
    if (votersFile) {
        std::string line;
        std::getline(votersFile, line); // header
        while (std::getline(votersFile, line)) {
            if (line.empty()) continue;
            auto f = splitCsv(line);
            if (f.size() < 5) continue;
            User u;
            u.username = unescapeCsv(f[0]);               // legacy voter ID as username
            u.displayName = unescapeCsv(f[1]);
            u.role = "voter";
            u.orgId = orgId;
            u.passwordHash = "sha256$" + unescapeCsv(f[2]);
            u.nationalId = unescapeCsv(f[3]);
            voterHasVoted[u.username] = (unescapeCsv(f[4]) == "1" || unescapeCsv(f[4]) == "true");
            if (createUser(u) == CreateUserResult::Ok) {
                voterIds[u.username] = u.id;
            } else {
                addAudit("system", "LEGACY_IMPORT_SKIP_VOTER", 0, "username " + u.username);
            }
        }
    }

    // Candidates + votes -> one closed "imported" election, so the historical
    // records are preserved and consistent, and can never be mixed with new
    // ballots. Candidate vote counts from the CSV are NOT trusted; the actual
    // votes.csv rows are imported as ballots instead (discrepancies audited).
    if (candFile) {
        struct LegacyCandidate { std::string legacyId; std::string name; std::string party;
                                 std::string symbol; std::string position; };
        std::vector<LegacyCandidate> imported;
        std::string line;
        std::getline(candFile, line); // header
        while (std::getline(candFile, line)) {
            if (line.empty()) continue;
            auto f = splitCsv(line);
            if (f.size() < 6) continue;
            imported.push_back({
                unescapeCsv(f[0]), unescapeCsv(f[1]), unescapeCsv(f[2]),
                unescapeCsv(f[3]), unescapeCsv(f[4])
            });
        }

        Election e;
        e.orgId = orgId;
        e.name = "Imported legacy election";
        e.description =
            "Historical election data migrated from the legacy CSV system. "
            "These records are identity-linked (voter and choice were stored "
            "together by the old system) and are NOT anonymous.";
        e.createdBy = 0;
        if (createElection(e) == CreateElectionResult::Ok) {
            long long legacyElectionId = e.id;
            std::map<std::string, long long> candIds; // legacy candidate id -> new id
            long long pos = 0;
            for (const auto& lc : imported) {
                Candidate c;
                c.electionId = legacyElectionId;
                c.name = lc.name;
                c.party = lc.party;
                c.description = "legacy symbol: " + lc.symbol + ", position: " + lc.position;
                c.position = ++pos;
                if (addCandidate(c) == AddCandidateResult::Ok) {
                    candIds[lc.legacyId] = c.id;
                }
            }
            addAudit("system", "LEGACY_IMPORT_ELECTION", legacyElectionId, "imported candidates in CSV order");

            long long votesImported = 0, votesSkipped = 0;
            if (votesFile) {
                std::string vline;
                std::getline(votesFile, vline); // header
                while (std::getline(votesFile, vline)) {
                    if (vline.empty()) continue;
                    auto f = splitCsv(vline);
                    if (f.size() < 4) continue;
                    std::string legacyVoter = unescapeCsv(f[0]);
                    std::string legacyCand = unescapeCsv(f[1]);
                    auto vi = voterIds.find(legacyVoter);
                    auto ci = candIds.find(legacyCand);
                    if (vi == voterIds.end() || ci == candIds.end()) { votesSkipped++; continue; }
                    // Historical record: identity and choice are imported
                    // together because they were linked in the source data.
                    // Marked as legacy participation; NOT anonymous.
                    db_.exec("INSERT OR IGNORE INTO participation(election_id,user_id,voted_at) VALUES(?,?,?)",
                             {std::to_string(legacyElectionId), std::to_string(vi->second),
                              unescapeCsv(f[2])});
                    db_.exec("INSERT INTO ballots(election_id,candidate_id,cast_at,receipt_nonce) VALUES(?,?,?,?)",
                             {std::to_string(legacyElectionId), std::to_string(ci->second),
                              unescapeCsv(f[2]), "legacy"});
                    votesImported++;
                }
            }
            if (votesImported != (long long)voterIds.size() &&
                std::any_of(voterHasVoted.begin(), voterHasVoted.end(),
                            [](auto& kv){ return kv.second; })) {
                addAudit("system", "LEGACY_IMPORT_MISMATCH", legacyElectionId,
                         "votes.csv rows do not cover every voter marked as voted");
            }
            db_.exec("UPDATE elections SET status='closed', results_public=0 WHERE id=?",
                     {std::to_string(legacyElectionId)});
            addAudit("system", "LEGACY_IMPORT_DONE", legacyElectionId,
                     "votes imported: " + std::to_string(votesImported) +
                     ", skipped: " + std::to_string(votesSkipped));
        }
    }

    db_.exec("INSERT INTO migration_markers(name,applied_at) VALUES('legacy_csv',?)", {nowIso()});
    addAudit("system", "LEGACY_IMPORT_COMPLETE", 0, "");
}
