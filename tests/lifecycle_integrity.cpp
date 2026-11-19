#include "../src/crypto.h"
#include "../src/store.h"

#include <cstdio>
#include <iostream>
#include <unistd.h>

int main() {
    const std::string path = "/tmp/electravote-lifecycle-integrity-" + std::to_string(getpid()) + ".db";
    crypto::init();
    Database db(path);
    Store store(db);
    store.initialize("/tmp/electravote-no-legacy-data");
    Organization org = store.createOrganization("Integrity Test", "integrity-test", true);
    User voter;
    voter.username = "integrityvoter";
    voter.displayName = "Integrity Voter";
    voter.role = "voter";
    voter.orgId = org.id;
    if (store.createUser(voter) != Store::CreateUserResult::Ok) return 2;

    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::cout << (ok ? "PASS: " : "FAIL: ") << name << '\n';
        if (!ok) ++failures;
    };
    auto makeClosed = [&](const std::string& name, long long& candidateId) {
        Election election;
        election.orgId = org.id;
        election.name = name;
        if (store.createElection(election) != Store::CreateElectionResult::Ok) return 0LL;
        Candidate candidate;
        candidate.electionId = election.id;
        candidate.name = "Candidate";
        if (store.addCandidate(candidate) != Store::AddCandidateResult::Ok) return 0LL;
        candidateId = candidate.id;
        if (!store.transitionElection(election.id, "open", "test-admin") ||
            !store.transitionElection(election.id, "closed", "test-admin")) return 0LL;
        return election.id;
    };
    auto addBallot = [&](long long electionId, long long candidateId) {
        db.exec("INSERT INTO ballots(election_id,candidate_id,cast_at,receipt_nonce) VALUES(?,?,?,?)",
                {std::to_string(electionId), std::to_string(candidateId),
                 "2026-01-01T00:00:00Z", "integrity-test-nonce"});
    };
    auto addParticipation = [&](long long electionId) {
        db.exec("INSERT INTO participation(election_id,user_id,voted_at) VALUES(?,?,?)",
                {std::to_string(electionId), std::to_string(voter.id), "2026-01-01T00:00:00Z"});
    };
    auto verifyRejectedCleanly = [&](long long electionId, const char* label) {
        const bool rejected = !store.transitionElection(electionId, "published", "test-admin");
        auto election = store.getElection(electionId);
        auto runs = db.query("SELECT COUNT(*) AS n FROM tally_runs WHERE election_id=?",
                             {std::to_string(electionId)});
        auto results = db.query("SELECT COUNT(*) AS n FROM tally_results WHERE tally_id IN "
                                "(SELECT id FROM tally_runs WHERE election_id=?)",
                                {std::to_string(electionId)});
        const bool clean = election && election->status == "closed" && !election->resultsPublic &&
            !runs.empty() && runs[0].at("n") == "0" && !results.empty() && results[0].at("n") == "0";
        check(rejected && clean, label);
    };

    long long candidate = 0;
    long long electionId = makeClosed("Participation mismatch", candidate);
    addBallot(electionId, candidate);
    verifyRejectedCleanly(electionId, "more ballots than participation records blocks publication without partial results");

    electionId = makeClosed("Participation without ballot", candidate);
    addParticipation(electionId);
    verifyRejectedCleanly(electionId, "more participation records than ballots blocks publication without partial results");

    electionId = makeClosed("Invalid candidate ballot", candidate);
    addParticipation(electionId);
    db.exec("PRAGMA foreign_keys=OFF");
    addBallot(electionId, 999999);
    db.exec("PRAGMA foreign_keys=ON");
    verifyRejectedCleanly(electionId, "invalid candidate reference blocks publication without persisted tally/results");

    electionId = makeClosed("Stored tally corruption", candidate);
    addBallot(electionId, candidate);
    addParticipation(electionId);
    db.exec("CREATE TRIGGER corrupt_tally_result AFTER INSERT ON tally_results BEGIN "
            "UPDATE tally_results SET votes=votes+1 WHERE tally_id=NEW.tally_id "
            "AND candidate_id=NEW.candidate_id; END");
    verifyRejectedCleanly(electionId, "stored candidate tally inconsistency blocks publication and rolls back result rows");
    db.exec("DROP TRIGGER corrupt_tally_result");

    electionId = makeClosed("Valid publication", candidate);
    addBallot(electionId, candidate);
    addParticipation(electionId);
    check(store.transitionElection(electionId, "published", "test-admin"),
          "valid election publishes successfully");
    auto tally = store.latestTally(electionId);
    auto published = store.getElection(electionId);
    check(tally && tally->integrityOk && tally->totalBallots == 1 &&
          tally->totalParticipations == 1 && tally->candidates.size() == 1 &&
          tally->candidates[0].votes == 1 && published && published->status == "published" &&
          published->resultsPublic, "valid publication persists complete consistent results");

    if (failures) std::cerr << failures << " lifecycle integrity checks failed\n";
    std::remove(path.c_str());
    std::remove((path + "-shm").c_str());
    std::remove((path + "-wal").c_str());
    return failures ? 1 : 0;
}
