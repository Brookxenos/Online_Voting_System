#include "../src/crypto.h"
#include "../src/services/voting_service.h"

#include <cstdio>
#include <iostream>
#include <unistd.h>

int main() {
    const std::string path = "/tmp/electravote-voting-service-" + std::to_string(getpid()) + ".db";
    crypto::init();
    Database db(path);
    Store store(db);
    store.initialize("/tmp/electravote-no-legacy-data");
    Organization orgA = store.createOrganization("Voting A", "voting-a", true);
    Organization orgB = store.createOrganization("Voting B", "voting-b", true);
    User voterA;
    voterA.username = "votera"; voterA.displayName = "Voter A"; voterA.role = "voter"; voterA.orgId = orgA.id;
    User voterB;
    voterB.username = "voterb"; voterB.displayName = "Voter B"; voterB.role = "voter"; voterB.orgId = orgB.id;
    if (store.createUser(voterA) != Store::CreateUserResult::Ok ||
        store.createUser(voterB) != Store::CreateUserResult::Ok) return 2;
    auto loadedA = store.findUserById(voterA.id);
    if (!loadedA) return 2;

    BallotRepository ballots(store);
    VotingService voting(ballots);
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
        if (!ok) ++failures;
    };
    auto makeDraft = [&](const Organization& org, const std::string& name, const std::string& start,
                         const std::string& end, long long& candidateId) {
        Election election;
        election.orgId = org.id; election.name = name; election.startAt = start; election.endAt = end;
        if (store.createElection(election) != Store::CreateElectionResult::Ok) return 0LL;
        Candidate candidate;
        candidate.electionId = election.id; candidate.name = "Candidate";
        if (store.addCandidate(candidate) != Store::AddCandidateResult::Ok) return 0LL;
        candidateId = candidate.id;
        return election.id;
    };
    auto open = [&](long long id, const User& voter) {
        return store.grantEligibility(id, voter.id) && store.transitionElection(id, "open", "test-admin");
    };
    auto countRows = [&](const char* table, long long electionId) {
        auto rows = db.query(std::string("SELECT COUNT(*) AS n FROM ") + table + " WHERE election_id=?",
                             {std::to_string(electionId)});
        return rows.empty() ? -1LL : std::stoll(rows[0].at("n"));
    };

    long long candidateA = 0;
    long long electionA = makeDraft(orgA, "Valid election", "", "", candidateA);
    long long candidateB = 0;
    const long long electionB = makeDraft(orgB, "Other election", "", "", candidateB);
    check(electionA > 0 && electionB > 0 && open(electionA, voterA), "eligible voter can open a valid election");
    auto success = voting.castBallot(*loadedA, electionA, candidateA);
    check(success.status == VotingService::Status::Ok && !success.receipt.empty(),
          "successful vote returns confirmation receipt");
    check(countRows("ballots", electionA) == 1 && countRows("participation", electionA) == 1,
          "successful vote records one ballot and one participation");
    auto audit = store.listAudit(electionA, 100);
    check(!audit.empty() && audit.front().action == "VOTE_CAST" && audit.front().actor == voterA.username &&
          audit.front().detail.find(std::to_string(candidateA)) == std::string::npos,
          "vote audit uses authenticated actor and omits candidate selection");
    auto duplicate = voting.castBallot(*loadedA, electionA, candidateA);
    check(duplicate.status == VotingService::Status::AlreadyVoted && countRows("ballots", electionA) == 1,
          "duplicate submission rejected without an extra ballot");
    check(voting.castBallot(*loadedA, electionA, candidateB).status == VotingService::Status::InvalidCandidate,
          "candidate from another election is rejected on retry");

    check(voting.castBallot(*store.findUserById(voterB.id), electionA, candidateA).status == VotingService::Status::NotEligible,
          "ineligible voter from another organization is rejected");

    long long timeCandidate = 0;
    long long future = makeDraft(orgA, "Future start", "", "", timeCandidate);
    if (future > 0) {
        open(future, voterA);
        db.exec("UPDATE elections SET start_at='2099-01-01T00:00:00Z' WHERE id=?", {std::to_string(future)});
        check(voting.castBallot(*store.findUserById(voterB.id), future, timeCandidate).status ==
              VotingService::Status::OutsideVotingWindow, "voting before configured start is rejected");
    } else check(false, "future-start fixture created");

    long long expiredCandidate = 0;
    long long expired = makeDraft(orgB, "Expired election", "", "2000-01-01T00:00:00Z", expiredCandidate);
    if (expired > 0 && open(expired, voterB)) {
        check(voting.castBallot(*store.findUserById(voterB.id), expired, expiredCandidate).status ==
              VotingService::Status::OutsideVotingWindow, "voting after configured end is rejected");
    } else check(false, "expired election fixture opened");

    long long rollbackCandidate = 0;
    long long rollbackElection = makeDraft(orgA, "Rollback election", "", "", rollbackCandidate);
    if (rollbackElection > 0 && open(rollbackElection, voterA)) {
        db.exec("CREATE TRIGGER fail_ballot_insert BEFORE INSERT ON ballots BEGIN "
                "SELECT RAISE(ABORT,'injected ballot write failure'); END");
        auto failed = voting.castBallot(*loadedA, rollbackElection, rollbackCandidate);
        check(failed.status == VotingService::Status::StorageFailure &&
              countRows("participation", rollbackElection) == 0 && countRows("ballots", rollbackElection) == 0,
              "ballot write failure rolls back participation and returns safe failure");
        check(store.listAudit(rollbackElection, 100).empty(),
              "failed transaction does not leave a vote audit event");
        db.exec("DROP TRIGGER fail_ballot_insert");
        auto recovered = voting.castBallot(*loadedA, rollbackElection, rollbackCandidate);
        check(recovered.status == VotingService::Status::Ok && countRows("participation", rollbackElection) == 1 &&
              countRows("ballots", rollbackElection) == 1,
              "submission succeeds after injected failure is removed");
    } else check(false, "rollback election fixture opened");

    long long closedCandidate = 0;
    long long closed = makeDraft(orgA, "Closed election", "", "", closedCandidate);
    if (closed > 0 && open(closed, voterA) && store.transitionElection(closed, "closed", "test-admin")) {
        check(voting.castBallot(*loadedA, closed, closedCandidate).status == VotingService::Status::ElectionNotOpen,
              "closed election rejects submission");
    } else check(false, "closed election fixture prepared");

    User unauthorized = *loadedA;
    unauthorized.role = "org_admin";
    check(voting.castBallot(unauthorized, electionA, candidateA).status == VotingService::Status::Forbidden,
          "non-voter role cannot submit a ballot");
    check(voting.castBallot(*loadedA, electionA, candidateB).status == VotingService::Status::InvalidCandidate,
          "candidate belonging to another election is rejected");
    check(voting.castBallot(*loadedA, electionA, 999999).status == VotingService::Status::InvalidCandidate,
          "candidate outside the election is rejected");
    check(voting.castBallot(*loadedA, 0, candidateA).status == VotingService::Status::InvalidRequest,
          "invalid election ID is rejected");

    if (failures) std::cerr << failures << " voting service checks failed\n";
    std::remove(path.c_str());
    std::remove((path + "-shm").c_str());
    std::remove((path + "-wal").c_str());
    return failures ? 1 : 0;
}
