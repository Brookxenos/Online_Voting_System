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
