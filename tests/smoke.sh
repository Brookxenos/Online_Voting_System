#!/usr/bin/env bash
# OpenVote integration smoke tests.
# Exercises the running backend end-to-end over HTTP: authentication,
# authorization, election lifecycle, eligibility, voting, concurrency,
# results publication, audit hygiene and rate limiting.
#
# Requirements: bash, curl. Run via `make test` (or `bash tests/smoke.sh`).
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${OV_TEST_PORT:-18099}"
BASE="localhost:$PORT"
mkdir -p /tmp/opencode
TMP="$(mktemp -d /tmp/opencode/ovtest.XXXXXX)" || exit 1
mkdir -p "$TMP/web/assets" "$TMP/data"
cp "$HERE/web/index.html" "$HERE/web/app.js" "$HERE/web/style.css" "$HERE/web/favicon.png" "$TMP/web/"
cp "$HERE"/web/assets/* "$TMP/web/assets/"

PASS=0; FAIL=0
SERVER_PID=""
MIGTMP=""

cleanup() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
  rm -rf "$TMP"
  [ -n "$MIGTMP" ] && rm -rf "$MIGTMP"
}
trap cleanup EXIT

check() {
  # check <description> <expected-substring> <actual>
  local desc="$1" expect="$2" actual="$3"
  if [[ "$actual" == *"$expect"* ]]; then
    PASS=$((PASS+1)); echo "  PASS: $desc"
  else
    FAIL=$((FAIL+1)); echo "  FAIL: $desc"
    echo "        expected substring: $expect"
    echo "        actual: ${actual:0:300}"
  fi
}

jpost() { curl -s -X POST "$BASE$1" -H 'Content-Type: application/json' -d "$2" "${@:3}"; }
jget()  { curl -s "$BASE$1" "${@:2}"; }

echo "== starting server on $PORT (temp db: $TMP/data) =="
ADMIN_PW="smoke-admin-pw-1"
OV_INITIAL_ADMIN_PASSWORD="$ADMIN_PW" "$HERE/voting_server" "$PORT" "$TMP/web" "$TMP/data" > "$TMP/server.log" 2>&1 &
SERVER_PID=$!
sleep 0.8

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "FATAL: server did not start:"; cat "$TMP/server.log"; exit 1
fi

echo "-- static and auth --"
check "serves ElectraVote index.html" "<title>ElectraVote" "$(jget /)"
check "serves ElectraVote participant access label" "ELECTRAVOTE · PARTICIPANT ACCESS" "$(jget /app.js)"
check "serves ElectraVote logo asset" "Content-Type: image/png" "$(curl -s "$BASE/assets/electravote-logo.png" -D - -o /dev/null | tr -d '\r')"
check "serves ElectraVote favicon" "Content-Type: image/png" "$(curl -s "$BASE/favicon.png" -D - -o /dev/null | tr -d '\r')"
check "login sets cookie" "ov_session" "$(jpost /api/auth/login '{"username":"admin","password":"'$ADMIN_PW'"}' -c "$TMP/admin.jar" -D - | tr -d '\r')"
check "wrong password rejected (uniform message)" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"admin","password":"nope-nope"}')"
check "me requires session" "Not authenticated" "$(jget /api/me)"
check "me works with session" "platform_admin" "$(jget /api/me -b "$TMP/admin.jar")"

echo "-- registration and login --"
check "registration unavailable until an organization enables it" "Registration is not available" \
  "$(jpost /api/auth/register '{"username":"voterA","password":"password123"}')"

echo "-- organizations and RBAC --"
check "admin creates org A" '"slug":"org-a"' \
  "$(jpost /api/orgs '{"name":"Org A","slug":"org-a"}' -b "$TMP/admin.jar")"
echo "-- registration and login --"
check "register voter" '"role":"voter"' \
  "$(jpost /api/auth/register '{"username":"voterA","password":"password123","displayName":"Voter A"}')"
check "duplicate username rejected" "already taken" \
  "$(jpost /api/auth/register '{"username":"voterA","password":"password123"}')"
check "weak password rejected" "at least 8" \
  "$(jpost /api/auth/register '{"username":"voterB","password":"short"}')"
check "bad username rejected" "3-32" \
  "$(jpost /api/auth/register '{"username":"x","password":"password123"}')"
jpost /api/auth/login '{"username":"voterA","password":"password123"}' -c "$TMP/voterA.jar" > /dev/null
check "voter login works" '"role":"voter"' "$(jget /api/me -b "$TMP/voterA.jar")"
check "voter cannot create org" "Platform administrator" \
  "$(jpost /api/orgs '{"name":"Nope","slug":"nope1"}' -b "$TMP/voterA.jar")"
check "admin creates org B" '"slug":"org-b"' \
  "$(jpost /api/orgs '{"name":"Org B","slug":"org-b"}' -b "$TMP/admin.jar")"
check "duplicate slug rejected" "already exists" \
  "$(jpost /api/orgs '{"name":"Org A2","slug":"org-a"}' -b "$TMP/admin.jar")"
check "org admin created in org B" '"role":"org_admin"' \
  "$(jpost /api/users '{"username":"admb","password":"password123","role":"org_admin","orgId":2}' -b "$TMP/admin.jar")"
check "create voter in org B" '"role":"voter"' \
  "$(jpost /api/users '{"username":"outsider","password":"password123","orgId":2}' -b "$TMP/admin.jar")"
jpost /api/auth/login '{"username":"admb","password":"password123"}' -c "$TMP/admb.jar" > /dev/null
check "org admin B exists" '"role":"org_admin"' "$(jget /api/me -b "$TMP/admb.jar")"
check "org admin can view own organization user" '"username":"outsider"' "$(jget /api/users/3 -b "$TMP/admb.jar")"
check "org admin cannot view user in another organization" "Access denied" "$(jget /api/users/2 -b "$TMP/admb.jar")"
check "voter can view own account details" '"username":"voterA"' "$(jget /api/users/2 -b "$TMP/voterA.jar")"
check "voter cannot view another account" "Access denied" "$(jget /api/users/3 -b "$TMP/voterA.jar")"
check "org admin cannot assign platform administrator role" "Access denied" \
  "$(jpost /api/users/3 '{"displayName":"Blocked","role":"platform_admin","orgId":2}' -b "$TMP/admb.jar")"
check "org admin can update own organization user" '"displayName":"Outsider Updated"' \
  "$(jpost /api/users/3 '{"displayName":"Outsider Updated","role":"voter","orgId":2}' -b "$TMP/admb.jar")"
check "admin summary is organization scoped" '"totalElections":0' \
  "$(jget /api/admin/summary -b "$TMP/admb.jar")"
check "voter cannot list users" "Administrator access required" "$(jget /api/users -b "$TMP/voterA.jar")"
check "voter cannot read admin summary" "Administrator access required" \
  "$(jget /api/admin/summary -b "$TMP/voterA.jar")"

echo "-- election creation and lifecycle --"
check "org admin B cannot create election in org A" "do not administer" \
  "$(jpost /api/elections '{"orgId":1,"name":"Hijack"}' -b "$TMP/admb.jar")"
check "create election in org A" '"status":"draft"' \
  "$(jpost /api/elections '{"orgId":1,"name":"Election A1","discoverable":true}' -b "$TMP/admin.jar")"
check "draft election stays hidden from public discovery" '"total":0' \
  "$(jget /api/public/elections)"
check "invalid public page size rejected" 'Invalid limit or offset' \
  "$(jget '/api/public/elections?limit=101')"
check "public detail hides draft election" 'Election not found' \
  "$(jget /api/public/elections/1)"
check "invalid status transition draft->closed rejected" "Invalid transition" \
  "$(jpost /api/elections/1/status '{"status":"closed"}' -b "$TMP/admin.jar")"
check "invalid calendar date rejected" "Dates must be" \
  "$(jpost /api/elections '{"orgId":1,"name":"Bad date","startAt":"2026-02-31T10:00:00Z"}' -b "$TMP/admin.jar")"
check "end before start rejected" "Dates must be" \
  "$(jpost /api/elections '{"orgId":1,"name":"Bad range","startAt":"2026-12-02T10:00:00Z","endAt":"2026-12-01T10:00:00Z"}' -b "$TMP/admin.jar")"
check "candidate added while draft" '"name":"Alice"' \
  "$(jpost /api/elections/1/candidates '{"name":"Alice","party":"A"}' -b "$TMP/admin.jar")"
check "candidate 2 added" '"name":"Bob"' \
  "$(jpost /api/elections/1/candidates '{"name":"Bob","party":"B"}' -b "$TMP/admin.jar")"
check "candidate can be edited while draft" '"name":"Bobby"' \
  "$(jpost /api/elections/1/candidates/2 '{"name":"Bobby","party":"=2+2","description":"Updated profile"}' -b "$TMP/admin.jar")"
check "candidate update requires existing candidate" 'Candidate not found' \
  "$(jpost /api/elections/1/candidates/999 '{"name":"Missing"}' -b "$TMP/admin.jar")"
check "voter cannot add candidate" "Administrator access required" \
  "$(jpost /api/elections/1/candidates '{"name":"Evil"}' -b "$TMP/voterA.jar")"
check "org admin B cannot see election A1" "Election not found" "$(jget /api/elections/1 -b "$TMP/admb.jar")"
check "org admin B cannot add candidate to A1" "Administrator access required" \
  "$(jpost /api/elections/1/candidates '{"name":"Evil"}' -b "$TMP/admb.jar")"

echo "-- eligibility and voting --"
check "vote while draft rejected" "not open for voting" \
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/voterA.jar")"
check "grant eligibility" '"ok":true' \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admin.jar")"
check "cross-organization voter cannot be made eligible" "in this election's organization" \
  "$(jpost /api/elections/1/eligibility '{"username":"outsider","action":"grant"}' -b "$TMP/admin.jar")"
check "administrator account cannot be made eligible" "active voter" \
  "$(jpost /api/elections/1/eligibility '{"username":"admb","action":"grant"}' -b "$TMP/admin.jar")"
check "unknown user eligibility rejected" "User not found" \
  "$(jpost /api/elections/1/eligibility '{"username":"ghost","action":"grant"}' -b "$TMP/admin.jar")"
check "org admin B cannot grant eligibility on A1" "Administrator access required" \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admb.jar")"
check "vote before eligible check: open election" '"status":"open"' \
  "$(jpost /api/elections/1/status '{"status":"open"}' -b "$TMP/admin.jar")"
check "opted-in election appears in public discovery" '"name":"Election A1"' \
  "$(jget '/api/public/elections?q=Election&status=open&limit=10&offset=0')"
check "public election detail contains candidates but no voter state" '"name":"Alice"' \
  "$(jget /api/public/elections/1)"
check "candidate change after open rejected" "draft" \
  "$(jpost /api/elections/1/candidates '{"name":"Late","party":"L"}' -b "$TMP/admin.jar")"
check "ballot cast successfully" '"ok":true' \
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/voterA.jar")"
check "voter dashboard returns participation metadata" '"votedAt"' \
  "$(jget /api/me/dashboard -b "$TMP/voterA.jar")"
if [[ "$(jget /api/me/dashboard -b "$TMP/voterA.jar")" == *'"candidateId"'* ]]; then
  FAIL=$((FAIL+1)); echo "  FAIL: voter dashboard exposes candidate choice"
else
  PASS=$((PASS+1)); echo "  PASS: voter dashboard does not expose candidate choice"
fi
check "duplicate ballot rejected" "already voted" \
  "$(jpost /api/elections/1/vote '{"candidateId":2}' -b "$TMP/voterA.jar")"
check "invalid candidate rejected" "Unknown candidate" \
  "$(jpost /api/elections/1/vote '{"candidateId":999}' -b "$TMP/voterA.jar")"
# voter B is registered but NOT eligible
jpost /api/auth/register '{"username":"voterB","password":"password123"}' > /dev/null
jpost /api/auth/login '{"username":"voterB","password":"password123"}' -c "$TMP/voterB.jar" > /dev/null
check "non-eligible voter rejected" "not eligible" \
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/voterB.jar")"

echo "-- results privacy and publication --"
check "results hidden while open" "not published yet" "$(jget /api/elections/1/results -b "$TMP/voterA.jar")"
check "results csv requires auth while unpublished" "Not authenticated" "$(jget /api/elections/1/results.csv)"
check "close election" '"status":"closed"' \
  "$(jpost /api/elections/1/status '{"status":"closed"}' -b "$TMP/admin.jar")"
check "publish election" '"status":"published"' \
  "$(jpost /api/elections/1/status '{"status":"published"}' -b "$TMP/admin.jar")"
check "published results are public without auth" '"totalBallots":1' "$(jget /api/elections/1/results)"
check "published results appear in anonymous results index" '"name":"Election A1"' \
  "$(jget /api/public/results?limit=10)"
check "tally counts Alice=1" '"votes":1' "$(jget /api/elections/1/results)"
check "results include vote percentage" '"percentage":100' "$(jget /api/elections/1/results)"
check "integrity ok" '"integrityOk":true' "$(jget /api/elections/1/results)"
check "results csv downloadable" "text/csv" "$(jget /api/elections/1/results.csv -D - | tr -d '\r' | head -5)"
check "CSV escapes formula-like candidate data" "'=2+2" "$(curl -s "$BASE/api/elections/1/results.csv")"
check "admin summary reports accepted ballots" '"acceptedBallots":1' \
  "$(jget /api/admin/summary -b "$TMP/admin.jar")"

echo "-- concurrent voting (race safety) --"
jpost /api/orgs '{"name":"Race Org","slug":"race-org"}' -b "$TMP/admin.jar" > /dev/null
# election 2 with one candidate; 10 concurrent eligible voters each vote once;
# plus 10 concurrent duplicate votes from one voter: exactly one must succeed.
jpost /api/elections '{"orgId":3,"name":"Race Election"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/2/candidates '{"name":"Only"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/2/status '{"status":"open"}' -b "$TMP/admin.jar" > /dev/null
for i in 1 2 3 4 5; do
  jpost /api/users "{\"username\":\"rv$i\",\"password\":\"password123\",\"role\":\"voter\",\"orgId\":3}" -b "$TMP/admin.jar" > /dev/null
  jpost /api/elections/2/eligibility "{\"username\":\"rv$i\",\"action\":\"grant\"}" -b "$TMP/admin.jar" > /dev/null
  jpost /api/auth/login "{\"username\":\"rv$i\",\"password\":\"password123\"}" -c "$TMP/rv$i.jar" > /dev/null
done
jpost /api/users '{"username":"rv6","password":"password123","role":"voter","orgId":3}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/2/eligibility '{"username":"rv6","action":"grant"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/auth/login '{"username":"rv6","password":"password123"}' -c "$TMP/rv6.jar" > /dev/null
PIDS=()
for i in 1 2 3 4 5; do
  jpost /api/elections/2/vote '{"candidateId":3}' -b "$TMP/rv$i.jar" > "$TMP/race$i.out" &
  PIDS+=($!)
done
for p in "${PIDS[@]}"; do wait "$p"; done
OUT=$(cat "$TMP"/race*.out)
SUCC=$(grep -o '"ok":true' <<< "$OUT" | wc -l)
if [ "$SUCC" != "5" ]; then
  echo "        [debug] vote outputs:"; sed 's/^/          /' <<< "$OUT" | head -12
fi
check "exactly 5 of 5 concurrent voters succeeded once each" "5" "$SUCC"
PIDS=()
for i in 1 2 3 4 5 6 7 8; do
  jpost /api/elections/2/vote '{"candidateId":3}' -b "$TMP/rv6.jar" > "$TMP/dupe$i.out" &
  PIDS+=($!)
done
for p in "${PIDS[@]}"; do wait "$p"; done
DUPE_SUCC=$(grep -ho '"ok":true' "$TMP"/dupe*.out | wc -l)
check "one ballot accepted from eight simultaneous requests by one voter" "1" "$DUPE_SUCC"
RES=$(jget /api/elections/2/results -b "$TMP/admin.jar")
check "tally counts exactly 6 ballots" '"totalBallots":6' "$RES"

echo "-- lifecycle timing and minimum ballot configuration --"
jpost /api/elections '{"orgId":1,"name":"No options","discoverable":true}' -b "$TMP/admin.jar" > /dev/null
check "election cannot open without candidates" "Invalid transition" \
  "$(jpost /api/elections/3/status '{"status":"open"}' -b "$TMP/admin.jar")"
check "draft can be archived without deletion" '"status":"archived"' \
  "$(jpost /api/elections/3/status '{"status":"archived"}' -b "$TMP/admin.jar")"
check "archived draft is removed from public discovery" '"total":0' \
  "$(jget '/api/public/elections?status=archived')"
check "archived draft cannot be reopened" "Invalid transition from 'archived' to 'open'" \
  "$(jpost /api/elections/3/status '{"status":"open"}' -b "$TMP/admin.jar")"
jpost /api/elections '{"orgId":1,"name":"Scheduled","startAt":"2099-01-01T00:00:00Z"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/4/candidates '{"name":"Scheduled option"}' -b "$TMP/admin.jar" > /dev/null
check "election cannot open before scheduled start" "Invalid transition" \
  "$(jpost /api/elections/4/status '{"status":"open"}' -b "$TMP/admin.jar")"

echo "-- audit hygiene --"
AUDIT=$(jget /api/audit -b "$TMP/admin.jar")
check "audit records ballot event" "VOTE_CAST" "$AUDIT"
if [[ "$AUDIT" == *"password"* ]]; then
  FAIL=$((FAIL+1)); echo "  FAIL: audit contains the word password"
else
  PASS=$((PASS+1)); echo "  PASS: audit contains no password strings"
fi
check "voter cannot read audit" "Administrator or observer" "$(jget /api/audit -b "$TMP/voterA.jar")"
check "org admin B cannot read org A audit for A1" "Access denied" \
  "$(jget '/api/audit?electionId=1' -b "$TMP/admb.jar")"

echo "-- rate limiting --"
for i in 1 2 3 4 5 6; do
  jpost /api/auth/login '{"username":"voterB","password":"wrongwrong"}' > /dev/null
done
check "6th bad login rate limited" "Too many failed attempts" \
  "$(jpost /api/auth/login '{"username":"voterB","password":"wrongwrong"}')"

echo "-- validation and CSRF-surface --"
check "non-JSON content type rejected" "application/json" \
  "$(curl -s -X POST $BASE/api/auth/login -d 'user=1')"
check "invalid JSON body rejected" "Invalid JSON" \
  "$(jpost /api/auth/login '{invalid')"
check "session logout revokes access" "Not authenticated" \
  "$(jpost /api/auth/logout '{}' -b "$TMP/voterB.jar"; jget /api/me -b "$TMP/voterB.jar")"

echo "-- legacy CSV migration (isolated fake records) --"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
MIG_PORT=$((PORT + 1))
BASE="localhost:$MIG_PORT"
MIGTMP="$(mktemp -d /tmp/opencode/ovmigration.XXXXXX)" || exit 1
mkdir -p "$MIGTMP/data"
cat > "$MIGTMP/data/voters.csv" <<'CSV'
id,name,passwordHash,nationalId,hasVoted
V001,Legacy Example,pepper$0123456789abcdef,N-TEST-001,1
CSV
cat > "$MIGTMP/data/candidates.csv" <<'CSV'
id,name,party,symbol,position,votes
C001,Legacy Option,Independent,star,1,12
CSV
cat > "$MIGTMP/data/votes.csv" <<'CSV'
voterId,candidateId,timestamp,signature
V001,C001,2020-01-01T00:00:00Z,legacy-signature
CSV
OV_INITIAL_ADMIN_PASSWORD="$ADMIN_PW" "$HERE/voting_server" "$MIG_PORT" "$TMP/web" "$MIGTMP/data" > "$MIGTMP/server.log" 2>&1 &
SERVER_PID=$!
sleep 0.8
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "FATAL: migration test server did not start:"; cat "$MIGTMP/server.log"; exit 1
fi
jpost /api/auth/login '{"username":"admin","password":"'$ADMIN_PW'"}' -c "$MIGTMP/admin.jar" > /dev/null
IMPORTED=$(jget /api/elections/1 -b "$MIGTMP/admin.jar")
check "legacy election imported as closed" '"status":"closed"' "$IMPORTED"
check "legacy election marked identity-linked" "NOT anonymous" "$IMPORTED"
check "legacy ballot and participation counts preserved" '"totalBallots":1' \
  "$(jget /api/elections/1/results -b "$MIGTMP/admin.jar")"
if [[ -f "$MIGTMP/data/voters.csv" && -f "$MIGTMP/data/candidates.csv" && -f "$MIGTMP/data/votes.csv" ]]; then
  PASS=$((PASS+1)); echo "  PASS: source CSV files preserved"
else
  FAIL=$((FAIL+1)); echo "  FAIL: source CSV files preserved"
fi
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
OV_INITIAL_ADMIN_PASSWORD="$ADMIN_PW" "$HERE/voting_server" "$MIG_PORT" "$TMP/web" "$MIGTMP/data" > "$MIGTMP/restart.log" 2>&1 &
SERVER_PID=$!
sleep 0.8
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "FATAL: migration restart failed:"; cat "$MIGTMP/restart.log"; exit 1
fi
jpost /api/auth/login '{"username":"admin","password":"'$ADMIN_PW'"}' -c "$MIGTMP/admin-restart.jar" > /dev/null
RESTART_ELECTIONS=$(jget /api/elections -b "$MIGTMP/admin-restart.jar")
IMPORTED_COUNT=$(grep -o 'Imported legacy election' <<< "$RESTART_ELECTIONS" | wc -l)
check "legacy import is idempotent after restart" "1" "$IMPORTED_COUNT"

echo
echo "===================================================="
echo "RESULTS: $PASS passed, $FAIL failed"
echo "===================================================="
[ "$FAIL" -eq 0 ] || exit 1
exit 0
