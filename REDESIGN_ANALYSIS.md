# ElectraVote redesign analysis (source checked)

## Current implementation

The application is a C++17 POSIX HTTP server serving the existing static single-page frontend. `main.cpp` reads environment/legacy positional configuration, initializes libsodium, opens SQLite, applies migrations/imports legacy CSV once, provisions an initial platform administrator if needed, and registers routes. `http.cpp` parses HTTP requests and dispatches registered routes; `api.cpp` registers `/api` handlers and currently combines request validation, session lookup, authorization, response shaping, throttling and business orchestration. `Store` in `store.cpp` contains SQLite queries, schema migrations, CSV import, transaction logic and election rules. `db.cpp` owns SQLite statements/transactions; `crypto.cpp` wraps libsodium; `json.cpp` is a custom limited JSON parser/serializer.

The database stores organizations, users, sessions, elections, candidates, eligibility, participation, ballots, tallies and audit events. Migrations use SQLite `user_version`; SQL uses bound parameters. Ballot and participation inserts share a transaction and uniqueness prevents repeat participation. They have no direct cross-reference, which does not establish anonymity.

## Verified feature status

- Authentication/session: implemented (bootstrap, registration, login/logout, profile/password, DB sessions, password hashing, revocation, in-process throttle).
- Roles/organizations/users: implemented with platform/org scoping and disabled-account handling.
- Elections/candidates/eligibility: implemented; draft configuration and lifecycle checks are server-side.
- Voting: single-choice vote implemented transactionally with duplicate prevention and non-revealing receipt.
- Results: tally is persisted and publication is guarded by internal integrity check; CSV export exists. No independent verifier or tie rule.
- Observer: read-only scoped visibility is implemented in authorization helpers and routes.
- Audit: sanitized audit events are persisted and exposed by scoped API.
- Notifications, password recovery, email verification, independent tally verification: absent per project context/API docs.

## Architectural constraints and risks

Handlers in `api.cpp` are large and contain duplicated access decisions. `Store` has broad responsibility, making service-level unit testing difficult. HTTP is a custom development server and needs production HTTPS termination and independent review. Rate limiting is in-process. Schema/data are user data and must remain unchanged during this refactor. The source, not stale historical notes, is authoritative.

## First refactoring boundary

The first extraction introduces `VotingService` as an application-facing boundary around vote submission while preserving the existing `Store::castBallot` transaction as the repository/persistence implementation. The route holds shared ownership because registered callbacks outlive route registration. This is deliberately transitional: subsequent work should move validation/business decisions out of `Store` into services and split repository concerns without changing schema or HTTP contracts.
