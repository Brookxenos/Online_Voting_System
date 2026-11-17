# ElectraVote Stage 2 Backend Checklist

Audit date: 2026-11-17. Scope is the existing C++17, SQLite, POSIX HTTP
application; the source tree is authoritative where older project notes differ.

## Existing implementation verified

- [x] SQLite storage with versioned schema, parameterized queries, foreign keys,
  WAL mode, and serialized access.
- [x] Account registration (username/display name), Argon2id password hashes,
  one-time admin bootstrap, login/logout, HttpOnly SameSite cookie sessions,
  password update, and profile display-name update.
- [x] Database-backed roles and organization-scoped checks on administrative
  routes; login and registration failure throttling in process memory.
- [x] Election create/read/draft-update and server-checked lifecycle transitions.
- [x] Candidate create/read/remove in draft; voter eligibility grant/revoke.
- [x] Transactional ballot and participation writes with a uniqueness
  constraint; ballot rows and participation rows have no direct key in common.
- [x] Tally persistence, publication gate, published result detail/CSV, audit
  events, and a HTTP smoke suite.
- [x] Legacy CSV import preserves original files and marks imported votes as
  identity-linked historical records.

## Implementation checklist

- [x] Add opt-in public election discovery with filtering and pagination.
- [x] Add public index of published results with filtering and pagination.
- [x] Add voter dashboard/participation history that never exposes choices.
- [x] Add scoped administrator summary queries.
- [x] Add candidate update for draft elections.
- [x] Add safe draft-election archival workflow; preserve the record and clear
  public discovery/live-results flags. Draft deletion remains intentionally
  unsupported to avoid irreversible data loss.
- [x] Document current lifecycle decision: for this nonbinding demo, retain
  `closed -> published`, which generates a tally and publishes only when its
  integrity check passes. A separate review/finalization state is deferred
  until product requirements define independent reviewers and approval rules;
  this demo does not claim independent tally verification.
- [x] Review and strengthen migration transaction/error handling and database
  query error reporting.
- [x] Expand HTTP negative, concurrency, migration, and privacy regression
  tests; the suite covers duplicate requests from the same voter, CSV
  preservation/import/idempotence, public hiding, and ballot privacy.
- [x] Document API methods, routes, authentication, request fields, responses,
  and security limits in `API.md`; align the README with the current source.
- [x] Review and document deployment requirements: HTTPS termination, secure
  cookies, persistent rate limiting, backups/restore, and operational logging.
  Implementation and independent production review remain outstanding.
- [x] Reconcile the current implementation facts in `PROJECT_CONTEXT.md` and
  align the README/run guide with the current source after owner direction to
  continue and finish the existing project.

## Gaps that cannot be claimed as solved by this implementation

- The separated ballot and participation tables are an educational separation,
  not cryptographic anonymity; timing, privileged DB access, backups, and the
  application process remain correlation risks.
- No email/verification, password recovery, notification service, candidate
  image upload, independent tally verification, configured tie rule, or
  production deployment/certification exists.
- SQLite is the current development persistence choice. PostgreSQL remains a
  proposed production direction and has not been implemented or approved here.
