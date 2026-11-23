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
jdelete() { curl -s -X DELETE "$BASE$1" "${@:2}"; }

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
check "serves the administrator password reset dialog" "admin-password-dialog" "$(jget /app.js)"
check "password reset dialog includes generate and copy actions" "admin-account-copy" "$(jget /app.js)"
check "generated temporary password is revealed for copying" "resetPassword.type = 'text'" "$(jget /app.js)"
check "serves password reset awareness banner component" "password-reset-alert-banner" "$(jget /app.js)"
check "serves change password dedicated view" "change-password-page" "$(jget /app.js)"
check "serves redirect click handler for warning banner" "btn-banner-change-password" "$(jget /app.js)"
check "serves password reset awareness styles" ".password-reset-alert-banner" "$(jget /style.css)"
check "serves the shared custom checkbox style" "label.check-label" "$(jget /style.css)"
check "custom checkbox retains visible keyboard focus" "input[type=\"checkbox\"]:focus-visible" "$(jget /style.css)"
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
check "platform admin views organization detail" '"slug":"org-a"' "$(jget /api/orgs/1 -b "$TMP/admin.jar")"
check "voter views own organization only" '"slug":"org-a"' "$(jget /api/orgs/1 -b "$TMP/voterA.jar")"
check "voter cannot view another organization" "Access denied" "$(jget /api/orgs/2 -b "$TMP/voterA.jar")"
check "invalid organization ID rejected" "Invalid organization id" "$(jget /api/orgs/not-an-id -b "$TMP/admin.jar")"
check "platform admin updates organization settings" '"allowSelfRegistration":false' \
  "$(jpost /api/orgs/1 '{"name":"Org A Updated","slug":"org-a","allowSelfRegistration":false}' -b "$TMP/admin.jar")"
check "platform admin restores self-registration setting" '"allowSelfRegistration":true' \
  "$(jpost /api/orgs/1 '{"name":"Org A","slug":"org-a","allowSelfRegistration":true}' -b "$TMP/admin.jar")"
check "org admin created in org B" '"role":"org_admin"' \
  "$(jpost /api/users '{"username":"admb","password":"password123","role":"org_admin","orgId":2}' -b "$TMP/admin.jar")"
check "create voter in org B" '"role":"voter"' \
  "$(jpost /api/users '{"username":"outsider","password":"password123","orgId":2}' -b "$TMP/admin.jar")"
check "organization-bound role requires organization assignment" "Invalid user information" \
  "$(jpost /api/users '{"username":"noorg","password":"password123","role":"observer"}' -b "$TMP/admin.jar")"
check "platform administrator creation clears organization scope" '"orgId":0' \
  "$(jpost /api/users '{"username":"platform2","password":"password123","role":"platform_admin","orgId":2}' -b "$TMP/admin.jar")"
jpost /api/auth/login '{"username":"admb","password":"password123"}' -c "$TMP/admb.jar" > /dev/null
check "create disposable account for safe deletion checks" '"username":"cleanup"' \
  "$(jpost /api/users '{"username":"cleanup","password":"password123","role":"voter","orgId":2}' -b "$TMP/admin.jar")"
jpost /api/auth/login '{"username":"cleanup","password":"password123"}' -c "$TMP/cleanup.jar" > /dev/null
check "active unused account cannot be deleted before disabling" "Disable the account before deleting it" \
  "$(jdelete /api/users/6 -b "$TMP/admb.jar")"
check "org admin disables disposable account before deletion" '"ok":true' \
  "$(jpost /api/users/6/disable '{"disabled":true}' -b "$TMP/admb.jar")"
check "authorized administrator can delete a disabled unused account" '"recordsRetained":false' \
  "$(jdelete /api/users/6 -b "$TMP/admb.jar")"
check "hard-deleted account is absent from user-list API" 'User not found' \
  "$(jget /api/users/6 -b "$TMP/admin.jar")"
check "hard-deleted account cannot log in" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"cleanup","password":"password123"}')"
check "hard-deleted account's existing session is invalid" "Not authenticated" \
  "$(jget /api/me -b "$TMP/cleanup.jar")"
check "organization admin cannot delete users outside its organization" "Access denied" \
  "$(jdelete /api/users/2 -b "$TMP/admb.jar")"
check "platform administrator account cannot be deleted" "Platform administrator accounts are protected" \
  "$(jdelete /api/users/5 -b "$TMP/admin.jar")"
check "org admin B exists" '"role":"org_admin"' "$(jget /api/me -b "$TMP/admb.jar")"
check "organization admin views own organization" '"slug":"org-b"' "$(jget /api/orgs/2 -b "$TMP/admb.jar")"
check "organization admin cannot view another organization" "Access denied" "$(jget /api/orgs/1 -b "$TMP/admb.jar")"
check "organization admin updates self-registration setting" '"allowSelfRegistration":false' \
  "$(jpost /api/orgs/2 '{"allowSelfRegistration":false}' -b "$TMP/admb.jar")"
check "organization admin cannot rename own organization" "Access denied" \
  "$(jpost /api/orgs/2 '{"name":"Renamed","allowSelfRegistration":true}' -b "$TMP/admb.jar")"
check "org admin can view own organization user" '"username":"outsider"' "$(jget /api/users/4 -b "$TMP/admb.jar")"
check "org admin cannot view user in another organization" "Access denied" "$(jget /api/users/2 -b "$TMP/admb.jar")"
check "voter can view own account details" '"username":"voterA"' "$(jget /api/users/2 -b "$TMP/voterA.jar")"
check "voter cannot view another account" "Access denied" "$(jget /api/users/4 -b "$TMP/voterA.jar")"
check "org admin cannot assign platform administrator role" "Access denied" \
  "$(jpost /api/users/4 '{"displayName":"Blocked","role":"platform_admin","orgId":2}' -b "$TMP/admb.jar")"
check "org admin can update own organization user" '"displayName":"Outsider Updated"' \
  "$(jpost /api/users/4 '{"displayName":"Outsider Updated","role":"voter","orgId":2}' -b "$TMP/admb.jar")"
check "platform admin cannot assign a user to missing organization" "Organization not found" \
  "$(jpost /api/users/4 '{"displayName":"Outsider Updated","role":"voter","orgId":999}' -b "$TMP/admin.jar")"
 jpost /api/auth/login '{"username":"outsider","password":"password123"}' -c "$TMP/outsider.jar" > /dev/null
check "org admin cannot reset password outside organization" "Access denied" \
  "$(jpost /api/users/2/password '{"password":"new-password-123"}' -b "$TMP/admb.jar")"
check "administrator reset endpoint cannot bypass current-password self-change flow" "Access denied" \
  "$(jpost /api/users/3/password '{"password":"unsafe-self-reset-123"}' -b "$TMP/admb.jar")"
check "org admin deactivates organization user" '"ok":true' \
  "$(jpost /api/users/4/disable '{"disabled":true}' -b "$TMP/admb.jar")"
check "deactivation revokes active sessions" "Not authenticated" "$(jget /api/me -b "$TMP/outsider.jar")"
check "org admin reactivates organization user" '"ok":true' \
  "$(jpost /api/users/4/disable '{"disabled":false}' -b "$TMP/admb.jar")"
jpost /api/auth/login '{"username":"outsider","password":"password123"}' -c "$TMP/outsider.jar" > /dev/null
RESET_RESP="$(jpost /api/users/4/password '{"password":"reset-password-123"}' -b "$TMP/admin.jar")"
check "platform admin resets user password" '"ok":true' "$RESET_RESP"
check "reset password metadata specifies 120 seconds expiry" '"expiresInSeconds":120' "$RESET_RESP"
check "administrative password reset revokes active sessions" "Not authenticated" "$(jget /api/me -b "$TMP/outsider.jar")"
jpost /api/auth/login '{"username":"outsider","password":"reset-password-123"}' -c "$TMP/outsider-new.jar" > /dev/null
check "administrative password reset enables new password login" '"role":"voter"' "$(jget /api/me -b "$TMP/outsider-new.jar")"
check "administrative password reset flags user to change password" '"mustChangePassword":true' "$(jget /api/me -b "$TMP/outsider-new.jar")"
check "voter with temporary reset password cannot cast ballot" "You must change your temporary password before casting a ballot" \
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/outsider-new.jar")"
check "one-time reset password cannot be used for a second login" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"outsider","password":"reset-password-123"}')"
check "administrator disables account before reset regression check" '"ok":true' \
  "$(jpost /api/users/4/disable '{"disabled":true}' -b "$TMP/admin.jar")"
check "password reset on a disabled account succeeds without re-enabling it" '"ok":true' \
  "$(jpost /api/users/4/password '{"password":"still-disabled-pw-1"}' -b "$TMP/admin.jar")"
check "disabled account remains unable to sign in after password reset" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"outsider","password":"still-disabled-pw-1"}')"
check "administrator re-enables account for later checks" '"ok":true' \
  "$(jpost /api/users/4/disable '{"disabled":false}' -b "$TMP/admin.jar")"
jpost /api/auth/login '{"username":"outsider","password":"still-disabled-pw-1"}' -c "$TMP/outsider-dis.jar" > /dev/null
check "re-enabled user signs in with reset password and receives mustChangePassword flag" '"mustChangePassword":true' "$(jget /api/me -b "$TMP/outsider-dis.jar")"
check "second login with temporary reset password is rejected" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"outsider","password":"still-disabled-pw-1"}')"
check "user updates password from temporary to permanent" "Password updated" \
  "$(jpost /api/auth/password '{"currentPassword":"still-disabled-pw-1","newPassword":"permanent-password-1"}' -b "$TMP/outsider-dis.jar")"
jpost /api/auth/login '{"username":"outsider","password":"permanent-password-1"}' -c "$TMP/outsider-perm.jar" > /dev/null
check "permanent password login clears mustChangePassword" '"mustChangePassword":false' "$(jget /api/me -b "$TMP/outsider-perm.jar")"
check "permanent password can be used multiple times" '"role":"voter"' \
  "$(jpost /api/auth/login '{"username":"outsider","password":"permanent-password-1"}' -c "$TMP/outsider-perm2.jar" > /dev/null; jget /api/me -b "$TMP/outsider-perm2.jar")"

# Expiration test: reset password with expired timestamp
jpost /api/users/4/password '{"password":"expired-test-pw-1"}' -b "$TMP/admin.jar" > /dev/null
python3 -c "import sqlite3; conn = sqlite3.connect('$TMP/data/openvote.db'); conn.execute(\"UPDATE users SET password_reset_expires_at='2020-01-01T00:00:00Z' WHERE id=4\"); conn.commit(); conn.close()"
check "expired reset password rejected after 2 minutes" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"outsider","password":"expired-test-pw-1"}')"
jpost /api/users/4/password '{"password":"temp-restore-pw-1"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/auth/login '{"username":"outsider","password":"temp-restore-pw-1"}' -c "$TMP/outsider-rest.jar" > /dev/null
jpost /api/auth/password '{"currentPassword":"temp-restore-pw-1","newPassword":"permanent-password-1"}' -b "$TMP/outsider-rest.jar" > /dev/null
check "user API never exposes the reset password hash" 'passwordHash' \
  "$(jget /api/users/4 -b "$TMP/admin.jar" | if grep -q 'passwordHash\|password_hash'; then echo 'passwordHash exposed'; else echo 'passwordHash absent'; fi)"
AUTO_RESET="$(jpost /api/users/5/password '{}' -b "$TMP/admin.jar")"
check "administrative password reset without explicit password auto-generates credential" '"ok":true' "$AUTO_RESET"
check "auto-generated password reset includes temporary password in response" '"temporaryPassword":' "$AUTO_RESET"
AUTO_PW="$(python3 -c "import json; print(json.loads('''$AUTO_RESET''').get('temporaryPassword',''))")"
jpost /api/auth/login "{\"username\":\"noorg\",\"password\":\"$AUTO_PW\"}" -c "$TMP/noorg-temp.jar" > /dev/null
check "auto-generated temporary password logs in and flags mustChangePassword" '"mustChangePassword":true' "$(jget /api/me -b "$TMP/noorg-temp.jar")"
check "admin summary is organization scoped" '"totalElections":0' \
  "$(jget /api/admin/summary -b "$TMP/admb.jar")"
check "voter cannot list users" "Administrator access required" "$(jget /api/users -b "$TMP/voterA.jar")"
check "voter cannot read admin summary" "Administrator access required" \
  "$(jget /api/admin/summary -b "$TMP/voterA.jar")"

echo "-- election creation and lifecycle --"
check "org admin B cannot create election in org A" "do not administer" \
  "$(jpost /api/elections '{"orgId":1,"name":"Hijack"}' -b "$TMP/admb.jar")"
check "voter cannot create election" "You do not administer this organization" \
  "$(jpost /api/elections '{"orgId":1,"name":"Unauthorized"}' -b "$TMP/voterA.jar")"
check "unknown organization rejected on election creation" "Organization not found" \
  "$(jpost /api/elections '{"orgId":999,"name":"Invalid organization"}' -b "$TMP/admin.jar")"
check "missing election title rejected" "Election name required" \
  "$(jpost /api/elections '{"orgId":1,"description":"Missing title"}' -b "$TMP/admin.jar")"
check "create election in org A" '"status":"draft"' \
  "$(jpost /api/elections '{"orgId":1,"name":"Election A1","discoverable":true}' -b "$TMP/admin.jar")"
check "administrator election list includes new draft" '"name":"Election A1"' "$(jget /api/elections -b "$TMP/admin.jar")"
check "administrator can view election detail" '"name":"Election A1"' "$(jget /api/elections/1 -b "$TMP/admin.jar")"
check "account creation does not grant election eligibility" '"eligible":[]' \
  "$(jget /api/elections/1/eligibility -b "$TMP/admin.jar")"
check "draft election stays hidden from public discovery" '"total":0' \
  "$(jget /api/public/elections)"
check "invalid public page size rejected" 'Invalid limit or offset' \
  "$(jget '/api/public/elections?limit=101')"
check "public detail hides draft election" 'Election not found' \
  "$(jget /api/public/elections/1)"
check "invalid status transition draft->closed rejected" "Invalid transition" \
  "$(jpost /api/elections/1/status '{"status":"closed"}' -b "$TMP/admin.jar")"
check "voter cannot change election lifecycle" "Administrator access required" \
  "$(jpost /api/elections/1/status '{"status":"archived"}' -b "$TMP/voterA.jar")"
check "publishing before closure rejected" "Invalid transition" \
  "$(jpost /api/elections/1/status '{"status":"published"}' -b "$TMP/admin.jar")"
check "invalid calendar date rejected" "Dates must be" \
  "$(jpost /api/elections '{"orgId":1,"name":"Bad date","startAt":"2026-02-31T10:00:00Z"}' -b "$TMP/admin.jar")"
check "end before start rejected" "Dates must be" \
  "$(jpost /api/elections '{"orgId":1,"name":"Bad range","startAt":"2026-12-02T10:00:00Z","endAt":"2026-12-01T10:00:00Z"}' -b "$TMP/admin.jar")"
check "candidate added while draft" '"name":"Alice"' \
  "$(jpost /api/elections/1/candidates '{"name":"Alice","party":"A"}' -b "$TMP/admin.jar")"
check "blank candidate name rejected by service" "Candidate name required" \
  "$(jpost /api/elections/1/candidates '{"name":"   "}' -b "$TMP/admin.jar")"
check "candidate 2 added" '"name":"Bob"' \
  "$(jpost /api/elections/1/candidates '{"name":"Bob","party":"B"}' -b "$TMP/admin.jar")"
check "candidate can be edited while draft" '"name":"Bobby"' \
  "$(jpost /api/elections/1/candidates/2 '{"name":"Bobby","party":"=2+2","description":"Updated profile"}' -b "$TMP/admin.jar")"
check "candidate update requires existing candidate" 'Candidate not found' \
  "$(jpost /api/elections/1/candidates/999 '{"name":"Missing"}' -b "$TMP/admin.jar")"
check "candidate list returns election-scoped candidates" '"name":"Bobby"' \
  "$(jget /api/elections/1/candidates -b "$TMP/admin.jar")"
check "candidate can be removed while draft" '"ok":true' \
  "$(jpost /api/elections/1/candidates/remove '{"candidateId":2}' -b "$TMP/admin.jar")"
check "removed candidate no longer listed" '"name":"Alice"' \
  "$(jget /api/elections/1/candidates -b "$TMP/admin.jar")"
check "candidate can be re-added with original fields" '"party":"=2+2"' \
  "$(jpost /api/elections/1/candidates '{"name":"Bobby","party":"=2+2","description":"Updated profile"}' -b "$TMP/admin.jar")"
check "voter cannot add candidate" "Administrator access required" \
  "$(jpost /api/elections/1/candidates '{"name":"Evil"}' -b "$TMP/voterA.jar")"
check "org admin B cannot see election A1" "Election not found" "$(jget /api/elections/1 -b "$TMP/admb.jar")"
check "org admin B cannot add candidate to A1" "Administrator access required" \
  "$(jpost /api/elections/1/candidates '{"name":"Evil"}' -b "$TMP/admb.jar")"

echo "-- eligibility and voting --"
check "unauthenticated vote submission rejected" "Not authenticated" \
  "$(jpost /api/elections/1/vote '{"candidateId":1}')"
check "administrator role cannot submit voter ballot" "Only voters can cast ballots" \
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/admin.jar")"
check "vote while draft rejected" "not open for voting" \
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/voterA.jar")"
check "draft election configuration update succeeds" '"description":"Updated draft"' \
  "$(jpost /api/elections/1 '{"description":"Updated draft"}' -b "$TMP/admin.jar")"
check "grant eligibility" '"ok":true' \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admin.jar")"
check "eligible voters can be listed" '"username":"voterA"' \
  "$(jget /api/elections/1/eligibility -b "$TMP/admin.jar")"
check "duplicate eligibility grant remains idempotent" '"ok":true' \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admin.jar")"
ELIGIBLE_DUPES=$(jget /api/elections/1/eligibility -b "$TMP/admin.jar" | grep -o '"username":"voterA"' | wc -l)
check "duplicate grant creates only one eligibility record" "1" "$ELIGIBLE_DUPES"
check "eligibility can be revoked while draft" '"ok":true' \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"revoke"}' -b "$TMP/admin.jar")"
if [[ "$(jget /api/elections/1/eligibility -b "$TMP/admin.jar")" == *'"username":"voterA"'* ]]; then
  FAIL=$((FAIL+1)); echo "  FAIL: revoked voter remains eligible"
else
  PASS=$((PASS+1)); echo "  PASS: revoked voter removed from eligibility list"
fi
check "eligibility can be re-granted while draft" '"ok":true' \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admin.jar")"
check "cross-organization voter cannot be made eligible" "in this election's organization" \
  "$(jpost /api/elections/1/eligibility '{"username":"outsider","action":"grant"}' -b "$TMP/admin.jar")"
check "administrator account cannot be made eligible" "active voter" \
  "$(jpost /api/elections/1/eligibility '{"username":"admb","action":"grant"}' -b "$TMP/admin.jar")"
check "unknown user eligibility rejected" "User not found" \
  "$(jpost /api/elections/1/eligibility '{"username":"ghost","action":"grant"}' -b "$TMP/admin.jar")"
check "org admin B cannot grant eligibility on A1" "Administrator access required" \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admb.jar")"
check "voter cannot grant eligibility" "Administrator access required" \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/voterA.jar")"
check "invalid voter ID rejected" "User or election not found" \
  "$(jpost /api/elections/1/eligibility '{"userId":999999,"action":"grant"}' -b "$TMP/admin.jar")"
check "vote before eligible check: open election" '"status":"open"' \
  "$(jpost /api/elections/1/status '{"status":"open"}' -b "$TMP/admin.jar")"
check "open election configuration is rejected" "Only draft elections can be edited" \
  "$(jpost /api/elections/1 '{"name":"Late edit"}' -b "$TMP/admin.jar")"
check "opted-in election appears in public discovery" '"name":"Election A1"' \
  "$(jget '/api/public/elections?q=Election&status=open&limit=10&offset=0')"
check "public election detail contains candidates but no voter state" '"name":"Alice"' \
  "$(jget /api/public/elections/1)"
check "candidate change after open rejected" "draft" \
  "$(jpost /api/elections/1/candidates '{"name":"Late","party":"L"}' -b "$TMP/admin.jar")"
check "candidate removal after open rejected" "draft election only" \
  "$(jpost /api/elections/1/candidates/remove '{"candidateId":1}' -b "$TMP/admin.jar")"
check "eligibility changes after open rejected" "only be changed while the election is a draft" \
  "$(jpost /api/elections/1/eligibility '{"username":"voterA","action":"revoke"}' -b "$TMP/admin.jar")"
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
  "$(jpost /api/elections/1/vote '{"candidateId":1}' -b "$TMP/voterA.jar")"
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
check "cross-organization admin cannot read unpublished results" "Results are not published yet" \
  "$(jget /api/elections/1/results -b "$TMP/admb.jar")"
check "close election" '"status":"closed"' \
  "$(jpost /api/elections/1/status '{"status":"closed"}' -b "$TMP/admin.jar")"
check "closed result read does not calculate tally" "No tally has been run" \
  "$(jget /api/elections/1/results -b "$TMP/admin.jar")"
check "closed CSV read does not calculate tally" "No tally has been run" \
  "$(jget /api/elections/1/results.csv -b "$TMP/admin.jar")"
check "closed election configuration is rejected" "Only draft elections can be edited" \
  "$(jpost /api/elections/1 '{"description":"Late edit"}' -b "$TMP/admin.jar")"
check "publish election" '"status":"published"' \
  "$(jpost /api/elections/1/status '{"status":"published"}' -b "$TMP/admin.jar")"
check "duplicate publish request rejected" "Invalid transition from 'published' to 'published'" \
  "$(jpost /api/elections/1/status '{"status":"published"}' -b "$TMP/admin.jar")"
check "published results are public without auth" '"totalBallots":1' "$(jget /api/elections/1/results)"
check "published results include election identity and publication status" '"published":true' \
  "$(jget /api/elections/1/results)"
check "published results appear in anonymous results index" '"name":"Election A1"' \
  "$(jget /api/public/results?limit=10)"
check "tally counts Alice=1" '"votes":1' "$(jget /api/elections/1/results)"
check "results include vote percentage" '"percentage":100' "$(jget /api/elections/1/results)"
check "integrity ok" '"integrityOk":true' "$(jget /api/elections/1/results)"
check "results csv downloadable" "text/csv" "$(jget /api/elections/1/results.csv -D - | tr -d '\r' | head -5)"
check "CSV escapes formula-like candidate data" "'=2+2" "$(curl -s "$BASE/api/elections/1/results.csv")"
check "admin summary reports accepted ballots" '"acceptedBallots":1' \
  "$(jget /api/admin/summary -b "$TMP/admin.jar")"
check "published election archives with records retained" '"status":"archived"' \
  "$(jpost /api/elections/1/status '{"status":"archived"}' -b "$TMP/admin.jar")"
check "archiving preserves published tally" '"totalBallots":1' "$(jget /api/elections/1/results)"

echo "-- concurrent voting (race safety) --"
jpost /api/orgs '{"name":"Race Org","slug":"race-org"}' -b "$TMP/admin.jar" > /dev/null
# election 2 with one candidate; 10 concurrent eligible voters each vote once;
# plus 10 concurrent duplicate votes from one voter: exactly one must succeed.
jpost /api/elections '{"orgId":3,"name":"Race Election"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/2/candidates '{"name":"Only"}' -b "$TMP/admin.jar" > /dev/null
for i in 1 2 3 4 5; do
  jpost /api/users "{\"username\":\"rv$i\",\"password\":\"password123\",\"role\":\"voter\",\"orgId\":3}" -b "$TMP/admin.jar" > /dev/null
  jpost /api/elections/2/eligibility "{\"username\":\"rv$i\",\"action\":\"grant\"}" -b "$TMP/admin.jar" > /dev/null
  jpost /api/auth/login "{\"username\":\"rv$i\",\"password\":\"password123\"}" -c "$TMP/rv$i.jar" > /dev/null
done
jpost /api/users '{"username":"rv6","password":"password123","role":"voter","orgId":3}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/2/eligibility '{"username":"rv6","action":"grant"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/auth/login '{"username":"rv6","password":"password123"}' -c "$TMP/rv6.jar" > /dev/null
jpost /api/elections/2/status '{"status":"open"}' -b "$TMP/admin.jar" > /dev/null
PIDS=()
for i in 1 2 3 4 5; do
  jpost /api/elections/2/vote '{"candidateId":4}' -b "$TMP/rv$i.jar" > "$TMP/race$i.out" &
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
  jpost /api/elections/2/vote '{"candidateId":4}' -b "$TMP/rv6.jar" > "$TMP/dupe$i.out" &
  PIDS+=($!)
done
for p in "${PIDS[@]}"; do wait "$p"; done
DUPE_SUCC=$(grep -ho '"ok":true' "$TMP"/dupe*.out | wc -l)
check "one ballot accepted from eight simultaneous requests by one voter" "1" "$DUPE_SUCC"
jpost /api/elections/2/status '{"status":"closed"}' -b "$TMP/admin.jar" > /dev/null
check "closed tally is persisted through publication" '"status":"published"' \
  "$(jpost /api/elections/2/status '{"status":"published"}' -b "$TMP/admin.jar")"
RES=$(jget /api/elections/2/results -b "$TMP/admin.jar")
check "published tally counts exactly 6 ballots" '"totalBallots":6' "$RES"
check "closed election rejects new ballot" "not open for voting" \
  "$(jpost /api/elections/2/vote '{"candidateId":4}' -b "$TMP/rv1.jar")"

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
jpost /api/elections '{"orgId":1,"name":"Expired window","endAt":"2000-01-01T00:00:00Z"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/5/candidates '{"name":"Candidate"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/5/eligibility '{"username":"voterA","action":"grant"}' -b "$TMP/admin.jar" > /dev/null
jpost /api/elections/5/status '{"status":"open"}' -b "$TMP/admin.jar" > /dev/null
check "vote after configured end time rejected by backend" "Voting is not open at this time" \
  "$(jpost /api/elections/5/vote '{"candidateId":6}' -b "$TMP/voterA.jar")"
check "ballot-backed published election cannot be deleted" "Only draft elections with no ballots" \
  "$(jdelete /api/elections/1 -b "$TMP/admin.jar")"
check "archived election cannot be deleted" "Only draft elections with no ballots" \
  "$(jdelete /api/elections/3 -b "$TMP/admin.jar")"
jpost /api/elections '{"orgId":1,"name":"Disposable draft"}' -b "$TMP/admin.jar" > /dev/null
check "other organization cannot delete draft election" "Administrator access required" \
  "$(jdelete /api/elections/6 -b "$TMP/admb.jar")"
check "unused draft can be deleted" '"ok":true' "$(jdelete /api/elections/6 -b "$TMP/admin.jar")"
check "deleted draft no longer exists" "Election not found" "$(jget /api/elections/6 -b "$TMP/admin.jar")"
echo "-- audit hygiene --"
AUDIT=$(jget /api/audit -b "$TMP/admin.jar")
check "audit records ballot event" "VOTE_CAST" "$AUDIT"
check "audit records lifecycle open, close, publish, archive and rejected events" "LIFECYCLE_REJECTED" "$AUDIT"
check "audit records lifecycle action names" "ELECTION_PUBLISHED" "$AUDIT"
check "audit records user-management actions" "USER_CREATED" "$AUDIT"
check "audit retains user and draft election deletion events" "USER_DELETED" "$AUDIT"
check "audit retains election deletion event" "ELECTION_DELETED" "$AUDIT"
check "audit records authenticated actor with action metadata" '"actor":"admin"' "$AUDIT"
check "vote audit event omits ballot choice detail" '"action":"VOTE_CAST","actor":"voterA","detail":""' "$AUDIT"
if [[ "$AUDIT" == *"password"* ]]; then
  FAIL=$((FAIL+1)); echo "  FAIL: audit contains the word password"
else
  PASS=$((PASS+1)); echo "  PASS: audit contains no password strings"
fi
if [[ "$AUDIT" == *"reset-password-123"* ]]; then
  FAIL=$((FAIL+1)); echo "  FAIL: audit contains the temporary password"
else
  PASS=$((PASS+1)); echo "  PASS: audit omits the temporary password value"
fi
check "voter cannot read audit" "Administrator or observer" "$(jget /api/audit -b "$TMP/voterA.jar")"
check "linked active account cannot be deleted before disabling" "Disable the account before deleting it" \
  "$(jdelete /api/users/2 -b "$TMP/admin.jar")"
check "linked account is disabled before deletion" '"ok":true' \
  "$(jpost /api/users/2/disable '{"disabled":true}' -b "$TMP/admin.jar")"
check "deleting a disabled voter with historical election records retains them safely" '"recordsRetained":true' \
  "$(jdelete /api/users/2 -b "$TMP/admin.jar")"
check "deleted linked user is absent from user-list API" 'voterA absent' \
  "$(jget /api/users -b "$TMP/admin.jar" | if grep -q '"username":"voterA"'; then echo 'deleted voter is still listed'; else echo 'voterA absent'; fi)"
jpost /api/auth/login '{"username":"admin","password":"'$ADMIN_PW'"}' -c "$TMP/admin-after-delete.jar" > /dev/null
check "linked deleted user remains absent after administrator signs in again" 'voterA absent' \
  "$(jget /api/users -b "$TMP/admin-after-delete.jar" | if grep -q '"username":"voterA"'; then echo 'deleted voter is still listed'; else echo 'voterA absent'; fi)"
check "deleted user cannot authenticate again" "Invalid username or password" \
  "$(jpost /api/auth/login '{"username":"voterA","password":"password123"}')"
check "deleted user's existing session cannot be used" "Not authenticated" \
  "$(jget /api/me -b "$TMP/voterA.jar")"
check "published ballot tally remains after account removal" '"totalBallots":1' \
  "$(jget /api/elections/1/results)"
check "org admin B cannot read org A audit for A1" "Access denied" \
  "$(jget '/api/audit?electionId=1' -b "$TMP/admb.jar")"
AUDIT_B=$(jget /api/audit -b "$TMP/admb.jar")
check "org admin B unfiltered audit omits org A election events" '"entries":[]' "$AUDIT_B"

echo "-- permanent election deletion (purge) --"
check "draft election cannot be permanently deleted" "Only published or archived" \
  "$(jpost /api/elections/4/purge '{"confirmName":"Scheduled","acknowledgeIrreversible":true}' -b "$TMP/admin.jar")"
check "open election cannot be permanently deleted" "Only published or archived" \
  "$(jpost /api/elections/5/purge '{"confirmName":"Expired window","acknowledgeIrreversible":true}' -b "$TMP/admin.jar")"
check "permanent deletion requires explicit acknowledgement" "Explicit acknowledgement" \
  "$(jpost /api/elections/1/purge '{"confirmName":"Election A1"}' -b "$TMP/admin.jar")"
check "permanent deletion requires the exact election name" "does not match" \
  "$(jpost /api/elections/1/purge '{"confirmName":"election a1","acknowledgeIrreversible":true}' -b "$TMP/admin.jar")"
check "cross-organization admin cannot permanently delete an election" "Administrator access required" \
  "$(jpost /api/elections/1/purge '{"confirmName":"Election A1","acknowledgeIrreversible":true}' -b "$TMP/admb.jar")"
check "voter cannot permanently delete an election" "Administrator access required" \
  "$(jpost /api/elections/1/purge '{"confirmName":"Election A1","acknowledgeIrreversible":true}' -b "$TMP/voterB.jar")"
check "archived election can be permanently deleted" '"auditRetained":true' \
  "$(jpost /api/elections/1/purge '{"confirmName":"Election A1","acknowledgeIrreversible":true}' -b "$TMP/admin.jar")"
check "purged election is no longer readable" "Election not found" \
  "$(jget /api/elections/1 -b "$TMP/admin.jar")"
check "purged election disappears from the public results index" 'Election A1 absent' \
  "$(jget '/api/public/results?limit=100' | if grep -q 'Election A1'; then echo 'Election A1 present'; else echo 'Election A1 absent'; fi)"
check "published election can be permanently deleted" '"ballots":6' \
  "$(jpost /api/elections/2/purge '{"confirmName":"Race Election","acknowledgeIrreversible":true}' -b "$TMP/admin.jar")"
check "purged published election is no longer readable" "Election not found" \
  "$(jget /api/elections/2 -b "$TMP/admin.jar")"
check "purge removes ballots from the aggregate summary" '"acceptedBallots":0' \
  "$(jget /api/admin/summary -b "$TMP/admin.jar")"
AUDIT_PURGE=$(jget /api/audit -b "$TMP/admin.jar")
check "audit retains a permanent-deletion event" "ELECTION_PURGED" "$AUDIT_PURGE"
PURGE_DETAIL=$(grep -o '"detail":"org=[^"]*"' <<< "$AUDIT_PURGE" | grep 'ballots=')
check "purge audit snapshot retains aggregate counts only" \
  'ballots=6;participations=6;candidates=1;eligible=6' "$PURGE_DETAIL"
if [[ "$PURGE_DETAIL" == *"candidateId"* || "$PURGE_DETAIL" == *"choice"* ]]; then
  FAIL=$((FAIL+1)); echo "  FAIL: purge audit snapshot contains ballot choice data"
else
  PASS=$((PASS+1)); echo "  PASS: purge audit snapshot omits ballot choice data"
fi
AUDIT_B_AFTER=$(jget /api/audit -b "$TMP/admb.jar")
check "org-scoped audit does not expose another organization's purge event" '"entries":[]' "$AUDIT_B_AFTER"

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
check "organization admin creates election in own organization" '"orgId":2' \
  "$(jpost /api/elections '{"orgId":2,"name":"Organization B Election"}' -b "$TMP/admb.jar")"
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
V001,C001,2020-01-01T00:00:01Z,duplicate-signature
CSV
# Convert every fixture to CRLF so the import must tolerate Windows line
# endings, a common real-world CSV format.
for f in voters.csv candidates.csv votes.csv; do
  sed -i 's/$/\r/' "$MIGTMP/data/$f"
done
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
check "legacy election without published tally is not recalculated on read" 'No tally has been run' \
  "$(jget /api/elections/1/results -b "$MIGTMP/admin.jar")"
check "legacy import deduplicates repeated votes.csv rows" '"acceptedBallots":1' \
  "$(jget /api/admin/summary -b "$MIGTMP/admin.jar")"
MIG_ORGS=$(jget /api/orgs -b "$MIGTMP/admin.jar")
check "legacy import organization is identifiable" '"slug":"legacy-import"' "$MIG_ORGS"
check "legacy import organization disables public self-registration" '"allowSelfRegistration":false' "$MIG_ORGS"
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
