# OpenVote --- Development Progress Log

> Every AI agent must update this file at the end of each work session.
> Keep entries concise, factual and chronological. Never mark work
> complete without evidence.

## Current project status

-   **Overall status:** Core demo implementation completed; run guide
    corrected; manual browser walkthrough still pending.
-   **Current approved direction:** General-purpose voting platform;
    retain C++17 and HTML/CSS/vanilla JavaScript; improve the existing
    project incrementally.
-   **Current phase:** Post-implementation review --- documentation
    alignment and frontend walkthrough.
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

### 2026-11-14 --- Codex --- Update navbar color and slogan

-   **Status:** Completed.
-   **Summary:** Changed the navbar to slate blue with pale-yellow text
    and selected states, and added the slogan “A clearer way to decide
    together” beside the OpenVote brand.
-   **Files changed:** `web/app.js`, `web/style.css`, `PROGRESS_LOG.md`.
-   **Decisions:** Used the existing palette and local styling; routes and
    navigation items are unchanged.
-   **Commands run:** `node --check web/app.js`; `git diff --check`.
-   **Build result:** Not run; frontend-only change.
-   **Tests and results:** No tests run; syntax and whitespace checks
    passed.
-   **Risks or known gaps:** Browser visual review remains pending.
-   **Next exact task:** Review navbar contrast and mobile wrapping in a
    browser.

### 2026-11-14 --- Codex --- Remove Home navbar link

-   **Status:** Completed.
-   **Summary:** Removed the redundant Home item from the global navbar;
    the brand link still returns to the landing page.
-   **Files changed:** `web/app.js`, `PROGRESS_LOG.md`.
-   **Decisions:** No routes or role-specific navigation changed.
-   **Commands run:** `node --check web/app.js`; `git diff --check`.
-   **Build result:** Not run; frontend-only link removal.
-   **Tests and results:** No tests run; JavaScript syntax check and
    whitespace check passed.
-   **Risks or known gaps:** None identified.
-   **Next exact task:** Continue UI refinement based on browser review.

### 2026-11-14 --- Codex --- UI visual and responsive refinement

-   **Status:** Completed; frontend styling only.
-   **Summary:** Refined the existing OpenVote UI with a calm green
    palette, improved surface and navigation hierarchy, more consistent
    spacing and controls, subtle motion, and narrower-screen layouts for
    navigation, tabs and forms. Added dark-mode token overrides and kept
    reduced-motion behavior. Existing app routes and ballot interactions
    were not changed.
-   **Files changed:** `web/style.css`, `PROGRESS_LOG.md`.
-   **Decisions:** Visual direction is an incremental CSS-only treatment;
    no UI framework, external asset, or dependency added.
-   **Commands run:** `make`; `git diff --check`.
-   **Build result:** `make` exited successfully and reported
    `Nothing to be done for 'all'`; the build does not process frontend
    assets.
-   **Tests and results:** Not run; styling-only change. Manual browser
    walkthrough remains pending because browser access is unavailable.
-   **Risks or known gaps:** Responsive styling has not been visually
    checked in a browser or against WCAG contrast measurements. Existing
    product screens and copy were preserved.
-   **Next exact task:** Review the refined UI at mobile, tablet, and
    desktop widths in a browser and adjust any observed layout issues.

### 2026-11-14 --- Codex --- Apply supplied palette and navigation polish

-   **Status:** Completed; frontend presentation and copy update.
-   **Summary:** Applied the supplied charcoal (`#4A4A4A`), light gray
    (`#CBCBCB`), pale yellow (`#FFFFE3`) and slate blue (`#6D8196`)
    direction to the app theme, with compatible dark-mode tokens. Added a
    serif display face using local system fallbacks, made the active route
    visible in the shared navigation, and rewrote landing-page headline,
    supporting text, and calls to action.
-   **Files changed:** `web/style.css`, `web/app.js`, `PROGRESS_LOG.md`.
-   **Decisions:** Reused existing role-aware navigation and routes. No
    dependency or remote font was introduced.
-   **Commands run:** `node --check web/app.js`; `make`; `git diff --check`.
-   **Build result:** `make` succeeded and reported there was no backend
    rebuild; the Makefile does not process frontend assets.
-   **Tests and results:** No tests run. JavaScript syntax check and
    whitespace check passed. Browser visual review remains pending.
-   **Risks or known gaps:** Palette and contrast have not been visually
    or programmatically assessed in an actual browser. Existing non-landing
    screen copy remains largely unchanged.
-   **Next exact task:** Inspect the updated navigation and landing view at
    desktop and phone widths, then tune contrast and wrapping from observed
    results.

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

### 2026-11-14 --- Codex --- Run guide alignment and smoke verification

-   **Status:** Documentation and launch messaging updated; automated
    smoke suite passed. Manual browser walkthrough could not run because
    the browser access host was unavailable.
-   **Summary:** Replaced obsolete HOW_TO_RUN.txt guidance about CSV
    persistence, XOR ballots, live results, and default credentials with
    instructions matching the current SQLite application. Corrected
    `start.sh` so it no longer prints the removed `admin/admin123`
    credentials and explains first-run password provisioning.
-   **Files changed:** `HOW_TO_RUN.txt`, `start.sh`, `PROGRESS_LOG.md`.
-   **Decisions:** Followed the current source for run behavior. Did not
    start the server against the repository `data/` directory because its
    existing CSV files could be imported into a newly created database.
    The README still describes the legacy implementation and remains a
    documentation discrepancy for owner review.
-   **Commands run:** `make clean`; `make`; `make test` (first attempt
    could not bind a local socket in the sandbox; rerun with approved
    elevated socket access).
-   **Build result:** `make` succeeded with C++17 and no compiler
    warnings.
-   **Tests and results:** `make test` passed: 53 passed, 0 failed,
    including auth, role checks, election lifecycle, ballot rules,
    publication behavior, concurrent voting, and audit hygiene.
-   **Risks or known gaps:** Browser access host unavailable, so no manual
    UI walkthrough. `README.md` still documents the retired CSV/XOR
    implementation and default credentials. Database separation does
    not establish cryptographic anonymity; the project remains demo-only.
-   **Next exact task:** Complete the manual browser walkthrough using an
    isolated temporary data directory when browser access is available;
    review with the owner whether to align `README.md` with current source.

### 2026-11-14 --- Codex --- Expand HOW_TO_RUN walkthrough

-   **Status:** Completed; documentation only.
-   **Summary:** Expanded the run guide with numbered build/start/browser
    steps, explicit port and data directory examples, persistent database
    behavior, first-login guidance, admin organization/user setup, election
    preparation and lifecycle, voter eligibility and casting steps, results
    publication, and observer usage.
-   **Files changed:** `HOW_TO_RUN.txt`, `PROGRESS_LOG.md`.
-   **Decisions:** Instructions were checked against the current frontend
    flow and backend-backed feature descriptions. A separate
    `OV_DATA_DIR=./demo-data` example is included so a new demo database can
    be kept apart from existing CSV data.
-   **Commands run:** Inspected current guide and relevant frontend routes;
    `git diff --check`.
-   **Build result:** Not run; no source or build files changed.
-   **Tests and results:** Not run; no application behavior changed.
-   **Risks or known gaps:** Manual browser walkthrough remains pending
    because browser access is unavailable. `README.md` still contains
    outdated legacy instructions.
-   **Next exact task:** Complete a manual walkthrough with an isolated
    data directory when browser access is available; align `README.md`
    with current source after owner confirmation.

### 2026-11-14 --- Codex --- Fix start.sh environment overrides

-   **Status:** Completed; startup configuration bug corrected.
-   **Summary:** `start.sh` previously always passed `web data` to the
    binary, which overrode `OV_WEB_DIR` and `OV_DATA_DIR` and contradicted
    the documented isolated-data example. The script now uses environment
    values when set, with the first positional argument taking precedence
    for the port, and reports the selected paths at startup.
-   **Files changed:** `start.sh`, `PROGRESS_LOG.md`.
-   **Decisions:** Existing CLI behavior remains available; `OV_PORT` is
    the default when no explicit port argument is supplied.
-   **Commands run:** `bash -n start.sh`; `make`; `make test`.
-   **Build result:** `make` reported no work needed; prior binary remains
    built. Start script syntax check passed.
-   **Tests and results:** `make test` passed: 53 passed, 0 failed.
-   **Risks or known gaps:** The user has not provided the exact error
    text. If startup still fails on their machine, request the full terminal
    output and OS details; runtime library paths in Makefile are specific
    to the current Linux environment.
-   **Next exact task:** User should retry `./start.sh`; if it still fails,
    diagnose from the exact error output and their OS/environment.

### 2026-11-14 --- Codex --- Recover imported administrator access

-   **Status:** Completed; local credential recovery is available.
-   **Summary:** Confirmed the active `admin` in the current database is an
    enabled legacy-hash platform administrator imported from
    `data/admins.csv`. This explains why no new random initial password was
    emitted and why a newly chosen `OV_INITIAL_ADMIN_PASSWORD` could not
    replace this account's existing credential. Added a local
    `--reset-admin-password` command, available through `start.sh`, which
    prompts without echoing, requires a matching 8-256 character password,
    updates only that platform admin hash, revokes their sessions, and records
    a password-free audit event. Documented recovery and legacy-import behavior.
-   **Files changed:** `src/main.cpp`, `start.sh`, `HOW_TO_RUN.txt`,
    `PROGRESS_LOG.md`.
-   **Decisions:** Recovery is restricted to the local operator with access
    to the selected database; it refuses non-interactive input, nonexistent
    databases, disabled accounts, and non-platform-admin users. No project
    database or election data was changed during verification.
-   **Commands run:** `bash -n start.sh`; `make clean`; `make`;
    `make test`; exercised reset command against a synthetic database under
    `/tmp` using a terminal, then checked hash format, session revocation,
    and audit action without printing the hash or test password;
    `git diff --check`.
-   **Build result:** `make` succeeded with C++17 and no warnings.
-   **Tests and results:** `make test` passed: 53 passed, 0 failed.
    Interactive reset check passed against the isolated synthetic DB; new
    hash uses Argon2id, prior session was revoked, and reset audit event was
    written. Non-terminal reset invocation correctly refused to read a
    password.
-   **Risks or known gaps:** A person with local filesystem access can reset
    the platform admin password; stop the server first and carefully select
    the same database path used for normal operation. This does not provide
    web-based account recovery.
-   **Next exact task:** User should stop OpenVote and run
    `./start.sh --reset-admin-password`, enter a new password twice, then
    restart with `./start.sh` and log in as `admin`.
