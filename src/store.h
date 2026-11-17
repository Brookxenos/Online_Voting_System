#ifndef STORE_H
#define STORE_H

//
// Repository layer on top of Database. Contains all SQL for the
// application. Services/HTTP handlers never write SQL directly.
//
// Privacy design note (see also AGENTS.md rule 8 — do not over-claim):
// The ballot submission transaction writes a "participation" record
// (who voted, for which election) and a "ballots" record (which choice,
// for which election) in the same transaction, but the two rows share no
// cross-reference. Identity and choice are separated at the schema level.
// This is an educational separation of trust domains, NOT a guarantee of
// ballot anonymity: timing correlation, server logs, backups and privileged
// database access could still link a voter to a choice.
//

#include "db.h"
#include "models.h"
#include <optional>
#include <vector>

class Store {
public:
    explicit Store(Database& db);

    // Apply schema migrations and, if the database is fresh and legacy CSV
    // files exist, import them (never deleting the CSV files themselves).
    void initialize(const std::string& legacyDataDir);

    // --- Organizations ------------------------------------------------------
    std::optional<Organization> getOrganization(long long id);
    std::optional<Organization> getOrganizationBySlug(const std::string& slug);
    std::vector<Organization> listOrganizations();
    Organization createOrganization(const std::string& name, const std::string& slug,
                                    bool allowSelfRegistration);

    // --- Users --------------------------------------------------------------
    enum class CreateUserResult { Ok, DuplicateUsername, OrgNotFound };
    CreateUserResult createUser(User& user); // fills id/createdAt
    std::optional<User> findUserByUsername(const std::string& username);
    std::optional<User> findUserById(long long id);
    std::vector<User> listUsers(std::optional<long long> orgId = std::nullopt);
    void updateDisplayName(long long userId, const std::string& displayName);
    void updatePasswordHash(long long userId, const std::string& hash);
    void setUserDisabled(long long userId, bool disabled);
    std::string participationAt(long long electionId, long long userId);
    AdminSummary adminSummary(std::optional<long long> orgId = std::nullopt);

    // --- Sessions -----------------------------------------------------------
    // Creates a session for the user and returns the raw token (sent to the
    // client as a cookie). Only the SHA-256 of the token is stored.
    std::string createSession(long long userId, int hoursValid);
    std::optional<User> userForSessionToken(const std::string& token);
    void revokeSession(const std::string& token);
    void revokeAllSessions(long long userId);

    // --- Elections ----------------------------------------------------------
    enum class CreateElectionResult { Ok, OrgNotFound };
    CreateElectionResult createElection(Election& e); // fills id/createdAt
    std::optional<Election> getElection(long long id);
    // Elections the user may see:
    //   platform_admin: all elections
    //   org_admin/observer: their organization's elections
    //   voter: elections of self-registration orgs (open/closed/published)
    //          plus elections where they hold eligibility or participation
    std::vector<Election> listElectionsVisibleTo(const User& user);
    std::vector<Election> listElectionsForOrg(long long orgId);
    ElectionPage listPublicElections(const std::string& query, const std::string& status,
                                    const std::string& from, const std::string& to,
                                    const std::string& sort, int limit, int offset);
    ElectionPage listPublicResults(const std::string& query, const std::string& from,
                                   const std::string& to, const std::string& sort,
                                   int limit, int offset);
    bool updateElectionDraft(long long electionId, const Election& changes); // draft-only edits
    // Enforced lifecycle transition. Returns false on invalid transition.
    // Running the tally when moving closed -> published.
    bool transitionElection(long long electionId, const std::string& newStatus,
                            const std::string& actor);

    // --- Candidates ---------------------------------------------------------
    enum class AddCandidateResult { Ok, ElectionNotFound, NotEditable };
    AddCandidateResult addCandidate(Candidate& c); // fills id
    std::vector<Candidate> listCandidates(long long electionId);
    bool removeCandidate(long long candidateId, long long electionId);

    // --- Eligibility --------------------------------------------------------
    bool grantEligibility(long long electionId, long long userId); // false if user unknown
    bool revokeEligibility(long long electionId, long long userId);
    std::vector<User> listEligibleUsers(long long electionId);
    bool isEligible(long long electionId, long long userId);
    bool hasParticipation(long long electionId, long long userId);

    // --- Voting -------------------------------------------------------------
    struct CastResult {
        enum class Status {
            Ok,
            ElectionNotFound,
            ElectionNotOpen,
            OutsideVotingWindow,
            NotEligible,
            AlreadyVoted,
            InvalidCandidate,
        } status = Status::Ok;
        std::string receipt;    // non-revealing confirmation code when Ok
    };
    // Transaction-safe ballot submission. Duplicate votes are rejected via
    // the primary-key constraint on (election_id, user_id) participation.
    CastResult castBallot(long long electionId, long long userId, long long candidateId);

    // --- Tally and results --------------------------------------------------
    std::optional<Tally> tallyElection(long long electionId);
    std::optional<Tally> latestTally(long long electionId);
    long long ballotCount(long long electionId);
    long long participationCount(long long electionId);

    // --- Audit --------------------------------------------------------------
    // Detail strings are sanitized (no pipes/control chars/newlines) and must
    // never contain passwords, tokens, ballot selections or identity-choice
    // pairs. Enforced here, not just by convention at call sites.
    void addAudit(const std::string& actor, const std::string& action,
                  long long electionId, const std::string& detail);
    std::vector<AuditEvent> listAudit(long long electionId /* 0 = all */, size_t limit);

private:
    Database& db_;

    void importLegacyCsvIfPresent(const std::string& dataDir);
    long long importOrg(const std::string& name, const std::string& slug);
};

#endif // STORE_H
