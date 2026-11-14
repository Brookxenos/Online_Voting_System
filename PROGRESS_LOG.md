# OpenVote --- Development Progress Log

> Every AI agent must update this file at the end of each work session.
> Keep entries concise, factual and chronological. Never mark work
> complete without evidence.

## Current project status

-   **Overall status:** Planning / baseline inspection not yet completed
    by the implementation agent.
-   **Current approved direction:** General-purpose voting platform;
    retain C++17 and HTML/CSS/vanilla JavaScript; improve the existing
    project incrementally.
-   **Current phase:** Phase 0 --- repository baseline and safety
    assessment.
-   **Production status:** Educational/demo project only. Not certified
    or approved for legally binding elections.

## Confirmed decisions

-   Keep the existing language/framework direction.
-   Prefer a modular monolith.
-   Design for multiple organizations and independent elections.
-   Prioritize privacy, fairness controls, accessibility, security and
    auditability.
-   Use a persistent project context and handoff process across AI
    agents.

## Proposed decisions awaiting confirmation

-   Final product name.
-   PostgreSQL as the target database.
-   Exact security libraries and versions.
-   Exact anonymous credential / end-to-end verification protocol.
-   Initial voting methods and release scope.
-   Deployment environment.
-   Final repository license.

## Known legacy issues from initial archive inspection

-   Custom SHA-256 password hashing rather than a modern password KDF.
-   Salt generation uses `std::mt19937_64`.
-   Repeating-key XOR cipher.
-   Hardcoded vote key in browser JavaScript.
-   Vote model directly stores voter ID and candidate ID.
-   CSV persistence and in-memory state require concurrency review.
-   Raw pointers returned by `DataStore` lookup methods after lock
    scope.
-   Default `admin` / `admin123` bootstrap credentials.
-   Frontend admin state is not server-side authorization.
-   Existing plain HTTP development server.
-   Live results/leader display.
-   Existing project docs and actual handlers must be compared before
    migration.

## Session entries

### Initial handoff package

-   **Date:** 2026-11-14
-   **Task:** Create persistent AI project context and staged prompt
    plan based on the uploaded `voting_system.zip`.
-   **Status:** Handoff documents prepared; implementation phases have
    not yet been executed.
-   **Source inspected:** `README.md`, `HOW_TO_RUN.txt`, `models.h`,
    `data_store.h`, `http_server.h`, `crypto.h`, `crypto.cpp`,
    `data_store.cpp`, `main.cpp`, `http_server.cpp` endpoint references,
    `web/app.js`, `Makefile`.
-   **Next task:** Run Prompt 0 from `PROMPT_SEQUENCE.md` against the
    actual working repository. Do not begin broad refactoring before the
    baseline report and backup plan are complete.

## Template for future entries

### YYYY-MM-DD --- \[Agent name/tool\] --- \[Phase/task\]

-   **Status:** In progress / completed / blocked
-   **Summary:**
-   **Files changed:**
-   **Decisions:** Confirmed / proposed / needs owner approval
-   **Commands run:**
-   **Build result:**
-   **Tests and results:**
-   **Risks or known gaps:**
-   **Next exact task:**

### 2026-11-14 --- OpenCode (GLM) --- Phases 1-9: OpenVote full implementation

-   **Status:** Completed (all 9 build steps implemented and tested)
-   **Summary:** Rebuilt the backend on SQLite (WAL) behind a `Database`
    adapter (`src/db.{h,cpp}`) because PostgreSQL is running but
    inaccessible (no role, no sudo, no libpq headers); a libpq backend can
    implement the same interface later. Added libsodium Argon2id password
    hashing (`src/crypto.{h,cpp}`; note: `crypto_pwhash_str` fails on this
    libsodium 1.0.18 build — `crypto_pwhash_str_alg` with Argon2id is used).
    Rewrote HTTP layer (`src/http.{h,cpp}`) with timeouts, body caps,
    cookie parsing, centralized errors. Rewrote API (`src/api.{h,cpp}`) and
    store (`src/store.{h,cpp}`) with: sessions (hashed tokens), RBAC
    (platform_admin/org_admin/observer/voter), organizations, elections
    with enforced lifecycle draft→open→closed→published→archived,
    candidates, eligibility, transactional voting with participation and
    ballot rows sharing no cross-reference, tally + integrity check,
    publication controls, sanitized audit log, login rate limiting.
    Legacy CSV import (one-time, marker-gated; CSVs never modified).
    Frontend rewritten (`web/index.html`, `web/app.js`, `web/style.css`):
    hash-router SPA covering home/login/register/elections/ballot/
    confirmation/results/admin (orgs, users, audit)/observer/settings.
    Removed entirely: hardcoded VOTE_KEY, XOR ballot cipher, CSV
    persistence, default admin password (bootstrap password now comes from
    `OV_INITIAL_ADMIN_PASSWORD` or is randomly generated and printed once).
    Old sources preserved uncompiled in `src/legacy_impl/`.
-   **Files changed:** Makefile, src/{db,crypto,store,http,api,main}.{h,cpp},
    src/models.h, web/{index.html,app.js,style.css}, tests/smoke.sh,
    src/legacy_impl/* (moved, uncompiled). Owner committed intermediate
    states; commit 64adbf7 intentionally reset data/voters.csv,
    data/candidates.csv, data/audit.log (originals recoverable at commit
    61b0a1f).
-   **Decisions:** SQLite instead of PostgreSQL (environment constraint,
    documented in db.h); privacy separation is educational, not anonymity
    (documented in store.h); legacy CSV import skips empty/header-only
    files entirely and writes no marker in that case (fixed this session —
    previously an empty "Imported legacy election" was created).
-   **Commands run:** `make` (clean, zero warnings); `bash tests/smoke.sh`.
-   **Build result:** voting_server builds clean on g++ C++17 linking
    /usr/lib/x86_64-linux-gnu/{libsodium.so.23,libsqlite3.so.0}.
-   **Tests and results:** tests/smoke.sh — 53 passed, 0 failed
    (auth, RBAC, org isolation, duplicate-vote rejection, non-eligible
    rejection, lifecycle enforcement, results hiding, publication,
    CSV download, 5-way concurrent voting, rate limiting, audit hygiene).
    Legacy import verified for three scenarios: current reset files
    (admin only, no empty election), header-only files (no import, no
    marker), full historical data from git 61b0a1f (3 voters + admin +
    closed election with 2 candidates).
-   **Risks or known gaps:** Not production election software; no
    independent review of the privacy design; ballots are separated from
    identity at the schema level only (not cryptographic anonymity); no
    HTTPS termination in the server (reverse proxy required for TLS);
    voter identity↔choice correlation remains possible via timing/logs;
    legacy Argon2id upgrade happens on first login of imported accounts.
-   **Next exact task:** Owner decision: either restore the historical
    CSVs from commit 61b0a1f into `data/` before first production run of
    `voting_server` against `data/` (import is one-time), or accept the
    reset. No db file exists in `data/` yet, so the import has not run
    there. After that: manual browser walkthrough of the frontend flows.
