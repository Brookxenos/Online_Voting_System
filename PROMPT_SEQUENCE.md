# OpenVote --- Step-by-Step AI Coding Prompt Sequence

> **How to use:** Run these prompts one at a time in your coding agent
> while the agent has access to the repository. Do not paste all prompts
> at once. After each phase, review the diff and test results before
> moving on.
>
> **Before every new AI agent/session:** Tell it to read `AGENTS.md`,
> `PROJECT_CONTEXT.md`, and `PROGRESS_LOG.md` first.

## Master instruction (paste at the beginning of a new agent session)

You are working on my existing OpenVote voting-system repository. Before
doing anything, read `AGENTS.md`, `PROJECT_CONTEXT.md`, and
`PROGRESS_LOG.md`. Inspect the actual repository and git status. The
source code is authoritative; do not assume old documentation is
accurate. My confirmed direction is to keep the C++17 backend and
HTML/CSS/vanilla JavaScript frontend, build a modular monolith, and
improve the existing project incrementally. Do not replace the project
with a new stack or rewrite everything. Work only on the phase I
specify. Preserve data, do not perform destructive migrations without
explicit approval and a rollback plan, do not invent cryptography, and
do not claim production readiness. At the end, run relevant build/tests,
report exact results and changed files, and update `PROGRESS_LOG.md`.

------------------------------------------------------------------------

## Prompt 0 --- Repository discovery and baseline audit

**Goal:** Understand the real project before changing it.

``` text
Perform Phase 0: repository discovery and baseline audit only. Do not modify application code yet.

1. Read AGENTS.md, PROJECT_CONTEXT.md, PROGRESS_LOG.md, README.md and HOW_TO_RUN.txt.
2. Inspect the full repository tree, git status, Makefile, start.sh, all C++ headers/source files, frontend files and any existing tests.
3. Build the existing project using its documented workflow. Run it only in a safe local development environment if practical.
4. Identify all current API routes, request/response shapes, user roles, data files, models, election rules, frontend screens and existing behavior.
5. Trace authentication and authorization from browser to server. Do not infer security from frontend state.
6. Trace the ballot path end-to-end: browser selection, payload construction, HTTP request, server validation, persistence, vote counting, receipt and results.
7. Inspect concurrency and locking in DataStore and the HTTP server. Identify raw pointer lifetime hazards, races, unsafe shared state and persistence failure cases.
8. Identify security/privacy problems, accessibility gaps, missing tests, dependencies and licensing questions.
9. Compare README/API documentation with actual handlers and list every discrepancy.
10. Create docs/BASELINE_AUDIT.md with findings, severity, file/line references, current build/test result, migration risks and recommended phase order.
11. Update PROGRESS_LOG.md with what was inspected and exact results.

Do not change code, migrate data, delete files, or add dependencies in this phase. If the build fails, report the exact failure and diagnose it without silently changing implementation.
```

**Review gate:** Approve the audit and phase order before
implementation.

------------------------------------------------------------------------

## Prompt 1 --- Requirements, domain model and architecture

**Goal:** Convert the product vision into implementation-ready
specifications.

``` text
Perform Phase 1: requirements and architecture documentation only.

Use PROJECT_CONTEXT.md and docs/BASELINE_AUDIT.md, plus the actual source. Produce:
- docs/PRODUCT_REQUIREMENTS.md: user roles, functional requirements, non-functional requirements, acceptance criteria and out-of-scope items.
- docs/DOMAIN_MODEL.md: organizations, memberships, users, elections, candidates/options, eligibility, ballot definitions, ballots, tally runs, results, audit events, observers and complaints.
- docs/ARCHITECTURE.md: modular-monolith layers, module responsibilities, dependency direction, data flow and trust boundaries.
- docs/ELECTION_LIFECYCLE.md: valid state transitions, authorization, timing rules, immutable configuration and exceptional correction process.
- docs/THREAT_MODEL.md: assets, actors, trust boundaries, threats, mitigations, residual risks and abuse cases.
- docs/DECISIONS.md: architecture decision records. Mark decisions as proposed until the owner approves them.

Do not implement features in this phase. Do not claim that table separation alone guarantees anonymity. Explicitly distinguish demo elections from legally binding public elections. Include accessibility and neutrality requirements. Update PROGRESS_LOG.md.
```

**Review gate:** Owner approves database direction, initial election
types, voting methods and scope.

------------------------------------------------------------------------

## Prompt 2 --- Safe project structure and build foundation

**Goal:** Improve maintainability without changing user-visible
behavior.

``` text
Perform Phase 2: C++ project structure and build foundation.

First inspect the current source and build. Propose the smallest refactor that creates clear boundaries for:
- server/router
- controllers
- services
- repositories
- middleware
- security adapters
- domain models
- database access
- tests

Keep C++17 and the existing frontend. Do not introduce microservices. Do not move or rename everything at once. Keep the existing Makefile/start.sh workflow working, or document and justify any required change. Add a test target and a clear development build mode if appropriate.

Make only safe, behavior-preserving structural changes. Avoid mixing database migration, authentication redesign and UI redesign into this phase. Build after each logical change. Add/update docs/DEVELOPMENT.md and PROGRESS_LOG.md. Report all changed paths and build/test evidence.
```

**Review gate:** Existing app still builds and its baseline behavior
remains available.

------------------------------------------------------------------------

## Prompt 3 --- Database design and PostgreSQL migration plan

**Goal:** Replace CSV as the future transactional store without losing
legacy data.

``` text
Perform Phase 3: database implementation planning and PostgreSQL foundation.

Before coding, inspect every CSV read/write path and document exact legacy formats. Create:
- docs/DATABASE_DESIGN.md with tables, relationships, constraints, indexes, transaction boundaries, access permissions and data-retention rules.
- database/migrations/ with versioned, reversible SQL migrations.
- a database adapter/repository interface so services do not depend on raw SQL everywhere.
- local development setup instructions, including configuration through environment variables.

Target PostgreSQL for transactional application storage. Use parameterized queries. Do not place credentials in source control. Do not commit real secrets.

Important legacy-data rule: old votes.csv records contain voter and candidate identifiers together. They are identity-linked historical records and must not be relabeled anonymous. Create an encrypted backup and a migration/rollback plan. Do not execute destructive migration, delete CSV files, or transform historical ballots without my explicit approval.

Do not invent a dual-write scheme without documenting consistency risks. Add migration tests and update PROGRESS_LOG.md.
```

**Review gate:** Approve schema, backup, migration mapping and rollback
before executing data migration.

------------------------------------------------------------------------

## Prompt 4 --- Authentication, sessions and authorization

**Goal:** Replace frontend-only trust and unsafe credentials.

``` text
Perform Phase 4: authentication and authorization hardening.

Inspect every protected endpoint and every frontend admin/voter action. Implement:
- secure password hashing using a maintained Argon2id implementation/library;
- cryptographically secure random generation from an operating-system CSPRNG or vetted library;
- server-side sessions with unpredictable identifiers, hashed session-token storage where appropriate, expiry and revocation;
- secure cookie flags (HttpOnly, Secure in HTTPS deployment, SameSite) and CSRF protection for cookie-authenticated state changes;
- server-side role-based authorization and organization/election scoping on every protected endpoint;
- login rate limiting, safe error messages, input validation and session invalidation after sensitive changes;
- secure first-admin provisioning without a known default password;
- secrets supplied through environment/configuration, never hardcoded.

Remove the old SHA-256 password scheme and XOR-based ballot security only after a safe migration strategy is designed. Do not log passwords, raw tokens, keys or sensitive ballot content. Do not assume a hidden button or JavaScript variable is authorization.

Add tests for unauthenticated access, role escalation, cross-organization access, expired sessions, CSRF and login abuse. Keep the existing UI usable, updating its API client as required. Do not claim ballot anonymity is solved by this phase. Update security docs and PROGRESS_LOG.md.
```

**Review gate:** Verify protected routes server-side before proceeding.

------------------------------------------------------------------------

## Prompt 5 --- Multi-organization and multi-election domain

**Goal:** Turn the single-election demo into a configurable platform.

``` text
Perform Phase 5: multi-organization and multi-election support.

Implement the approved domain model:
- organizations/tenants;
- organization memberships and scoped roles;
- multiple independent elections;
- election-specific candidates or ballot options;
- eligibility policy and records;
- election-specific start/end dates, timezone handling, voting method, ballot version and publication rules;
- observers with read-only permissions.

Replace the single global electionOpen boolean with an election-specific lifecycle:
DRAFT → REVIEW/APPROVED (if configured) → SCHEDULED → OPEN → CLOSED → TALLYING → PUBLISHED → ARCHIVED.

Enforce valid transitions server-side and transactionally. Prevent silent modification of a published/open ballot definition. Record configuration changes and actor permissions. Ensure one election's users, candidates, ballots, results and settings cannot leak into another election.

Do not add all voting methods yet. Start with the simplest approved method and formalize its rules. Add tests for cross-election isolation, invalid transitions, date boundaries, timezone behavior and concurrent state changes. Update API and database documentation and PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 6 --- Ballot privacy and eligibility architecture (security-critical)

**Goal:** Design a responsible separation between eligibility and ballot
choices.

``` text
Perform Phase 6: ballot privacy architecture. This is security-critical. Start with a written design review; do not rush into code.

The legacy Vote model stores voterId and candidateId together. Do not preserve that structure for new elections. Design separate trust domains for:
1. identity and eligibility;
2. authorization/credential issuance or redemption;
3. anonymous ballot storage;
4. tally and verification.

Analyze correlation risks from timestamps, IP addresses, HTTP logs, database logs, backups, analytics, receipt identifiers, administrator access and operational monitoring. Define data minimization, access controls, retention and threat mitigations.

Do not claim anonymity from separate tables alone. Do not invent anonymous credentials, blind signatures, mixnets, homomorphic encryption, zero-knowledge proofs or a custom cryptographic protocol. Evaluate established, reviewed protocols and maintained implementations, document license and integration requirements, and identify independent review needed.

If a trustworthy protocol cannot be integrated safely in this project phase, implement only a clearly labeled educational/demo ballot flow with explicit limitations; do not represent it as anonymous or production-secure. Do not use real personal data or conduct a real election.

Produce docs/BALLOT_PRIVACY_DESIGN.md, threat analysis, test strategy and a decision request for the project owner. Wait for approval before implementing a cryptographic protocol. Update PROGRESS_LOG.md.
```

**Review gate:** Mandatory owner approval and expert review of the
selected privacy protocol.

------------------------------------------------------------------------

## Prompt 7 --- Ballot API and transaction-safe submission

**Goal:** Implement a well-defined ballot workflow after privacy design
approval.

``` text
Perform Phase 7: ballot API and submission workflow, only to the level supported by the approved ballot privacy design.

Define and implement:
- retrieval of a published ballot definition;
- validation of election state and eligibility authorization;
- validation of ballot structure against the configured voting method;
- safe submission with idempotency/retry behavior;
- atomic enforcement of the election's permitted submission count;
- protected persistence;
- non-revealing confirmation/receipt;
- error handling that does not leak eligibility or ballot secrets unnecessarily.

Use database transactions and constraints for race-safe acceptance. Test simultaneous duplicate submissions, retries after network failure, closed elections, malformed ballots, wrong election IDs, invalid options, database failures and process restart.

Do not bind voter identity to a plaintext choice. Do not expose raw ballots in logs or admin endpoints. Do not invent cryptographic proof. Update API docs, tests and PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 8 --- Counting methods and results

**Goal:** Build deterministic, testable tallying.

``` text
Perform Phase 8: tally and results.

Begin with a formal specification for the approved initial voting method. Document:
- valid ballot definition;
- invalid ballot rules;
- abstention behavior;
- counting formula;
- tie handling;
- rounding and percentage rules;
- publication conditions;
- independent test vectors.

Implement tallying as a separate service, not as vote counters updated directly on candidate records. Make tally runs reproducible and record the input ballot-set/version reference and algorithm version. Ensure only closed elections can be tallied and only approved results can be published. Keep results hidden until the configured publication point.

Create independent expected-result test fixtures and edge-case tests. Add protection against small-group statistical disclosure where appropriate. Do not show a live leader during active voting by default. Update docs and PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 9 --- Audit trail and observer transparency

**Goal:** Make election administration reviewable without leaking
private data.

``` text
Perform Phase 9: audit and observer transparency.

Design event categories for election creation, rule publication, candidate changes, opening/closing, administrative access, eligibility administration, tally initiation, verification and result publication.

Audit records must:
- be append-only at the application level and protected against unauthorized modification;
- use reliable timestamps and event identifiers;
- avoid passwords, session tokens, secret keys, plaintext ballots, voter-choice pairs and unnecessary personal data;
- support integrity checking and documented retention;
- distinguish public events from restricted operational/security events.

Build observer read-only permissions and public reports containing only approved information. Do not expose voter lists, individual ballots, network identifiers or sensitive admin details. Add tamper-detection tests and document the limits of hash chaining (a hash chain alone does not prevent a privileged attacker from rewriting the whole log). Update PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 10 --- Frontend design system and public homepage

**Goal:** Establish the original OpenVote UI without changing backend
behavior unnecessarily.

``` text
Perform Phase 10: frontend design system and public homepage.

Keep HTML, CSS and vanilla JavaScript. Inspect the existing index.html, app.js and style.css before changing them. Create an original, consistent, neutral civic-technology design. Do not copy the exact UI, layout, branding, images or source code of another voting product.

Design principles:
- calm, trustworthy, neutral visual identity;
- responsive mobile-first layout;
- clear hierarchy and plain-language content;
- consistent reusable components;
- keyboard focus and semantic HTML;
- WCAG 2.2 AA target;
- reduced-motion support;
- text scaling and sufficient contrast;
- localization-ready strings;
- no candidate promotion, persuasive political language or partisan colors.

Build the design tokens, buttons, form controls, cards, alerts, navigation, footer, language selector and public homepage. Preserve working API behavior. Use neutral, clearly labeled placeholder content only. Do not invent election data or claim security certification. Add screenshots or a visual QA checklist if supported. Update PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 11 --- Voter experience

**Goal:** Create an accessible, understandable voting journey.

``` text
Perform Phase 11: voter portal and ballot UI.

Build:
1. registration/login and account recovery states;
2. voter dashboard with eligible elections;
3. election details and rules;
4. standardized candidate/option information;
5. ballot selection UI based on the actual configured voting method;
6. ballot review screen;
7. explicit final confirmation;
8. submission success/error/retry states;
9. non-revealing receipt/inclusion verification instructions.

Fairness rules:
- same information fields, card size, visual hierarchy and typography for every candidate;
- documented neutral ordering;
- no preselected choice;
- no recommended candidate;
- no countdown pressure or manipulative urgency;
- no candidate-specific styling;
- clear abstention and invalid-ballot rules.

Accessibility:
- full keyboard use;
- screen-reader labels and announcements;
- clear validation summary;
- large touch targets;
- responsive mobile layout;
- adjustable text and reduced motion;
- language support.

Connect to existing approved API contracts only. Do not fake successful vote submission in the UI. Test all loading, empty, error, expired-session and success states. Update PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 12 --- Admin, observer and public transparency UI

**Goal:** Complete the remaining user interfaces.

``` text
Perform Phase 12: administrator, observer and public results interfaces.

Admin portal:
- overview;
- election creation wizard;
- election configuration and preview;
- candidate/option management;
- eligibility management;
- schedule and lifecycle controls;
- result publication workflow;
- audit and verification reports;
- observer access management.

Observer portal:
- read-only election configuration;
- published rules and changes;
- permitted audit evidence;
- tally and verification reports.

Public transparency:
- published results only;
- counting method;
- accepted ballot totals;
- safe aggregate participation statistics;
- tie and invalid-ballot information;
- verification status and downloadable reports.

Enforce permissions in the backend. Frontend route guards are only a usability layer. Never expose personal voter data or individual ballot choices. Use clear empty, loading, error and permission-denied states. Test each role. Update PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 13 --- Accessibility, localization and inclusive testing

**Goal:** Validate inclusion with actual tests.

``` text
Perform Phase 13: accessibility and localization.

Audit all user-facing screens against WCAG 2.2 AA. Use automated tools where useful, but do not treat automated checks as sufficient. Test keyboard-only navigation, visible focus, screen-reader names/roles/states, heading structure, form errors, contrast, zoom/reflow, reduced motion and touch target usability.

Implement a localization resource structure for English and the additional languages approved by the owner. Do not rely on unreviewed machine translation for legally or electorally meaningful instructions. Use native-language reviewers. Test right-to-left support only if a target language requires it.

Test with representative users and assistive technologies where possible. Record limitations honestly. Create docs/ACCESSIBILITY_REPORT.md and update PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 14 --- Security, privacy, concurrency and reliability testing

**Goal:** Test failure cases and abuse cases before release.

``` text
Perform Phase 14: security and reliability verification.

Create a test plan covering:
- authentication/session attacks;
- authorization and privilege escalation;
- cross-organization/election access;
- CSRF, XSS, injection and unsafe file/path handling;
- rate limiting and brute-force resistance;
- secret/configuration handling;
- TLS deployment configuration;
- concurrent ballot submissions and election state transitions;
- database failure, restart, backup and restore;
- audit tampering and log leakage;
- privacy correlation and metadata exposure;
- accessibility and localization regression.

Use safe local test data only. Do not attack third-party systems. Do not claim a penetration test unless one was actually performed by an authorized tester. Fix issues by severity, add regression tests, and document residual risk. Create docs/SECURITY_TEST_REPORT.md and update PROGRESS_LOG.md.
```

------------------------------------------------------------------------

## Prompt 15 --- Documentation, packaging and release readiness

**Goal:** Make the repository understandable to future developers and AI
agents.

``` text
Perform Phase 15: final documentation and release-readiness review.

Update:
- README.md with accurate project scope, features, limitations and setup;
- HOW_TO_RUN.txt or docs/DEVELOPMENT.md;
- docs/ARCHITECTURE.md;
- docs/DATABASE_DESIGN.md;
- docs/API_REFERENCE.md with actual endpoints and request/response examples;
- docs/SECURITY.md;
- docs/PRIVACY.md;
- docs/ACCESSIBILITY_REPORT.md;
- docs/TESTING.md;
- LICENSE and third-party notices only after license ownership and dependency compatibility are verified;
- PROJECT_CONTEXT.md;
- AGENTS.md;
- PROGRESS_LOG.md.

Verify a clean build from documented instructions. Run the full test suite. Check that no default passwords, private keys, tokens, personal data or test secrets are committed. List what is complete, incomplete and not independently verified.

Do not call the product production-ready or certified for public elections unless all relevant legal, independent security, operational and certification requirements have actually been met.
```

------------------------------------------------------------------------

## Prompt 16 --- New-agent handoff / resume work

Use this whenever switching AI agents or starting a fresh chat:

``` text
I am continuing the existing OpenVote project. Before making any changes:
1. Read AGENTS.md.
2. Read PROJECT_CONTEXT.md.
3. Read PROGRESS_LOG.md from the beginning and identify the latest session entry.
4. Read the current phase prompt in PROMPT_SEQUENCE.md.
5. Inspect git status and the actual repository files.
6. Verify whether the previous agent's claimed changes exist and whether the project builds.
7. Summarize what is complete, what is incomplete, current known issues, and the exact next task.
8. Do not repeat completed work or assume the previous agent's claims are true without checking.
9. Ask me only for decisions that require owner approval.
10. Continue with the smallest next task, test it, and update PROGRESS_LOG.md.

Preserve all confirmed project decisions. Do not change the technology stack, erase data, rewrite the project, or claim security properties without evidence.
```

## Rules for using the sequence

-   Complete one prompt and review its result before starting the next.
-   Keep a Git commit or backup before major phases.
-   Never let an agent silently skip a failed build or test.
-   Ask the agent to show a concise diff summary.
-   Require approval before database migration, destructive data changes
    or cryptographic protocol selection.
-   If an agent runs out of context, use Prompt 16 in the new session.
-   The project context and progress log must be updated after every
    phase.
