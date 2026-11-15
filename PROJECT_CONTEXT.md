# ElectraVote --- Project Context and AI Agent Handoff

> **Purpose:** This is the persistent source-of-truth document for every
> AI coding agent working on this repository. Read it before inspecting,
> planning, or changing code.
>
> **Project status:** Existing educational/demo application undergoing a
> staged redesign. It is NOT certified or approved for legally binding
> public elections.

## 1. Project identity

-   Working project name: **ElectraVote** (confirmed by the project owner
    on 2026-11-15; internal database and repository names remain
    unchanged).
-   Product: A general-purpose voting and election-management platform.
-   Intended use cases: school/student elections, clubs and
    associations, organizations, community participation, and
    configurable nonbinding polls. Any legally binding election requires
    separate jurisdiction-specific legal, security, accessibility,
    operational and certification review.
-   Main goals: neutrality, inclusion, privacy, integrity, transparency,
    auditability, accessibility, maintainability.
-   User's chosen technology direction: **keep and improve the existing
    C++17 backend and HTML/CSS/vanilla JavaScript frontend**. Do not
    replace the stack with React, Node, Python, Java, or microservices
    unless the project owner explicitly changes this decision.
-   Preferred architecture: a modular monolith first; avoid premature
    microservices.

## 2. Existing repository --- facts observed in the uploaded ZIP

The original archive contains:

``` text
voting_system/
├── web/
│   ├── index.html
│   ├── app.js
│   └── style.css
├── src/
│   ├── data_store.h
│   ├── data_store.cpp
│   ├── http_server.h
│   ├── http_server.cpp
│   ├── main.cpp
│   ├── json.h
│   ├── json.cpp
│   ├── crypto.h
│   ├── crypto.cpp
│   └── models.h
├── .vscode/
├── start.sh
├── Makefile
├── README.md
└── HOW_TO_RUN.txt
```

### Current implementation facts

-   Backend language: C++17.
-   HTTP server: hand-written POSIX-socket HTTP server, multi-threaded
    (one worker thread per client connection, according to the source
    comments).
-   Frontend: HTML, CSS, vanilla JavaScript single-page application.
-   JSON: custom JSON parser/serializer in `json.cpp` and `json.h`.
-   Persistence: CSV files and `audit.log` under a runtime `data/`
    directory.
-   Build: Makefile using `g++`, C++17, pthreads; `start.sh` starts the
    application.
-   Current API prefix: `/api/` (legacy routes).
-   Current data models include `Voter`, `Candidate`, `Admin`, `Vote`,
    and `AuditEntry`.
-   The current `Vote` model includes `voterId`, `candidateId`,
    `timestamp`, and `signature`.
-   Current election state is a single boolean `electionOpen_`, not a
    multi-election state machine.
-   Current candidate records include a vote count.
-   Current README describes voter registration/login, admin login,
    candidate management, ballot casting, live results, election
    open/close, audit logging and responsive UI.
-   Current README describes the system as educational/demo software and
    identifies security limitations.

### Current security limitations observed in source

These are existing weaknesses to fix, not features to preserve:

1.  `crypto.cpp` implements SHA-256 password hashing as
    `salt + password` followed by SHA-256. This is not a modern password
    hashing/KDF design.
2.  `crypto.cpp` generates salts using `std::mt19937_64` seeded from a
    clock. This is not a cryptographically secure random generator.
3.  `crypto.cpp` implements a repeating-key XOR cipher.
4.  `web/app.js` contains a hardcoded `VOTE_KEY` and performs
    client-side XOR ballot encryption. A key embedded in browser
    JavaScript is public to users and cannot protect ballot secrecy.
5.  `Vote` stores voter identity and candidate choice together, directly
    linking voter and choice.
6.  CSV files are not an adequate long-term transactional foundation for
    concurrent voting.
7.  `DataStore` returns raw pointers from lookup methods after its lock
    guard has gone out of scope (for example `findVoter`,
    `findCandidate`, `findAdmin`); this creates lifetime/concurrency
    hazards.
8.  Some `DataStore` methods access or mutate shared vectors without
    consistently acquiring the mutex; review all methods rather than
    assuming the class is thread-safe.
9.  The initial admin account is bootstrapped with username `admin` and
    password `admin123`, using a fixed salt. Remove predictable default
    credentials and replace with a secure one-time setup process.
10. Frontend state such as `state.admin` is not proof of authentication.
    Every protected operation must be authorized by the backend.
11. The current server is documented as plain HTTP. Production
    deployment requires correctly configured HTTPS/TLS.
12. The existing design exposes live results/leader information. Do not
    show partial election results during an active election unless a
    specific election type explicitly permits it and the risk is
    documented.
13. Audit records currently include actor and detail fields. Redesign
    logging to avoid recording passwords, session secrets, ballot
    selections, or identity-to-choice associations.

Do not assume that a previous AI's analysis is correct without verifying
the current checked-out source. Repository code and tests are the
authority.

## 3. Product principles

### Fairness and neutrality

-   No candidate, party, option, or group should receive preferential UI
    treatment.
-   Use the same candidate information fields, layout, typography,
    visual weight and available space for all candidates.
-   Candidate ordering must follow a documented neutral rule selected
    before the election (for example, a reproducible and auditable
    draw). Do not hardcode a favored order.
-   No preselected candidate/option.
-   No recommendation labels, promotional badges, candidate-specific
    colors, or attention-grabbing animations.
-   Publish election rules, eligibility criteria, counting method, tie
    rules and schedule before voting begins.
-   Record and disclose relevant changes to published election
    configuration.
-   Do not announce leaders or show partial results during voting by
    default.
-   Do not claim a system is "bias-free." Use measurable fairness
    controls and disclose limitations.

### Inclusion and accessibility

-   Target WCAG 2.2 AA; test rather than merely claim conformance.
-   Responsive desktop, tablet and mobile layouts.
-   Keyboard-only operation and visible focus states.
-   Semantic HTML and screen-reader support.
-   Adjustable text size, adequate contrast, reduced motion,
    plain-language instructions.
-   Do not communicate status through color alone.
-   Multilingual architecture using translation resources, not hardcoded
    interface strings.
-   Validate translations with competent native-language reviewers.
-   Amharic may be a useful initial language option, but do not treat a
    machine-generated translation as approved.
-   Low-bandwidth support and graceful error/retry handling.
-   Accommodations must not change the meaning of the ballot.

### Privacy and security

-   Minimize personal data collection.
-   Do not store voter identity and candidate choice in the same ballot
    record.
-   Do not assume that separating tables alone guarantees anonymity;
    timing, IP, logs, metadata, backups and administrator access can
    re-link records.
-   Use established, reviewed cryptographic protocols and libraries.
    Never invent a cipher, signature scheme, anonymous credential scheme
    or end-to-end verifiability protocol.
-   Do not claim end-to-end verifiability until a defined protocol is
    implemented and independently tested.
-   Server-side authorization on every protected endpoint.
-   Password hashing with a suitable modern password KDF such as
    Argon2id via a maintained library.
-   Cryptographically secure random number generation.
-   HTTPS/TLS in deployment.
-   Secure session cookies, session expiry/revocation, CSRF protection
    where cookie authentication is used, input validation, output
    encoding, rate limiting and safe error handling.
-   Least privilege for database accounts and application roles.
-   Never log passwords, raw tokens, secret keys, plaintext ballot
    selections, or unnecessary personal information.
-   Secure backup, restoration, retention and deletion policies.
-   No real election use until independent security review, legal
    review, operational testing and any required certification are
    complete.

## 4. Target users and roles

1.  **Voter/participant:** can view elections they are permitted to
    access, read neutral information, cast a ballot during the permitted
    period, and receive a non-revealing confirmation/receipt where
    supported.
2.  **Election administrator:** manages assigned elections and
    configuration, but must not automatically gain access to individual
    ballot contents or voter choices.
3.  **Independent observer:** read-only access to approved
    configuration, audit evidence, tally verification and published
    reports.
4.  **Platform administrator:** manages platform-level accounts,
    configuration and service operations. This role must be separated
    from election-specific privileges.

Use least-privilege role-based authorization. Scope organization and
election permissions explicitly.

## 5. Target architecture

Keep a C++17 modular monolith initially.

Suggested layers: - HTTP server/router - Controllers/handlers -
Middleware (authentication, authorization, CSRF, rate limiting, request
validation) - Services (auth, organization, election, eligibility,
ballot, tally, audit, verification) - Repositories (database access) -
Models/domain types - Security adapters (password KDF, random
generation, vetted crypto) - Database migrations - Tests

Preferred database direction: PostgreSQL for transactional persistence.
A lightweight local database may be used only if justified for
development; do not create two competing production data models.

Do not introduce a microservice architecture unless a documented
requirement justifies it.

## 6. Core domain concepts

The target system should support: - Organizations/tenants - Users and
authentication accounts - Organization memberships and roles -
Elections - Election administrators and observers - Candidates or ballot
options - Eligibility rules and eligibility records - Ballot definitions
and versions - Ballot submissions - Election-specific voting method -
Tally runs and result snapshots - Audit events - Verification
evidence/receipts - Localization resources - Complaints/correction
workflow (later phase)

An election must be independent of other elections. Each election has
its own dates, status, ballot definition, eligibility policy, voting
method, counting rules, and result publication policy.

## 7. Election lifecycle

Proposed lifecycle:
`DRAFT → REVIEW/APPROVED → SCHEDULED → OPEN → CLOSED → TALLYING → PUBLISHED → ARCHIVED`

Define valid transitions and enforce them server-side and
transactionally. Published/open ballot definitions must not be silently
changed. Any exceptional correction/reopening must follow a documented,
authorized, auditable process.

## 8. Voting methods

Possible configurable methods: - Single-choice - Yes/No or multi-option
referendum - Approval voting - Ranked-choice voting (later, after a
fully specified and tested tally implementation)

For each method, document: - Ballot validation - Counting algorithm -
Invalid ballot handling - Abstention behavior - Tie resolution -
Publication rules - Test vectors and independent expected results

Do not implement a voting method based on an informal description alone.
Write its formal rules and test cases first.

## 9. UI surfaces

Build consistent, neutral, accessible interfaces for: 1. Public landing
page and election discovery 2. Registration/login and account recovery
3. Voter dashboard 4. Election details and candidate/option information
5. Ballot selection 6. Ballot review and confirmation 7. Submission
confirmation/receipt 8. Election administration dashboard 9. Observer
dashboard 10. Public results and transparency page 11. Accessibility and
language settings 12. Help, complaints and support

Do not redesign or remove existing working functionality without
documenting the reason and preserving equivalent behavior where
appropriate.

## 10. Proposed technology constraints

-   Keep C++17.
-   Keep HTML/CSS/vanilla JavaScript unless the owner approves a change.
-   Prefer maintained, well-supported libraries for security-sensitive
    functions.
-   PostgreSQL is the target transactional database.
-   Keep the existing Makefile/start workflow working or document any
    deliberate replacement.
-   Do not add dependencies without explaining why, their license,
    maintenance status and build impact.
-   Do not copy proprietary code, branding, assets, or layouts from
    reference voting systems. Learn from public standards and documented
    concepts; implement original code and UI.
-   Check licenses of every third-party dependency and preserve notices.

## 11. Data migration rules

The old CSV `votes.csv` format contains voter and candidate identifiers
in the same record. These historical records are identity-linked and
must NOT be described as anonymous.

Before migration: - Inspect and document all CSV formats and actual
sample data without exposing secrets. - Create an encrypted,
access-restricted backup. - Build a migration plan and rollback plan. -
Do not silently delete, rewrite, or fabricate historical records. - Do
not claim historical votes can be converted into anonymous ballots; the
original association already exists. - Ask the project owner before
destructive migration, data deletion, or any irreversible
transformation. - Keep legacy demo data clearly separated from new
election data.

## 12. Definition of done

A phase is complete only when: - The agent lists files changed and
why. - The project builds using the documented command. - Relevant
automated tests pass. - New behavior has tests. - Security-sensitive
behavior has explicit negative tests. - No secrets or unsafe defaults
are introduced. - Documentation is updated. - Known limitations and
uncompleted tasks are recorded. - The next step is stated clearly.

Never claim a test passed unless it was actually run. Never claim a
security property merely because code was added.

## 13. How a new AI agent should start

1.  Read `AGENTS.md`.
2.  Read this `PROJECT_CONTEXT.md`.
3.  Read `PROGRESS_LOG.md`.
4.  Inspect the actual repository and git status.
5.  Read the files relevant to the requested task.
6.  Summarize current state and identify discrepancies between docs and
    code.
7.  Ask before destructive or irreversible changes.
8.  Work only on the current approved phase.
9.  Run build and tests.
10. Update `PROGRESS_LOG.md`, architecture docs and relevant
    API/database docs.
11. Leave a handoff note with exact next actions.

## 14. Decision log

Confirmed by project owner: - ElectraVote as the product name. -
General-purpose voting platform. - Retain C++17 backend and
HTML/CSS/vanilla JavaScript frontend. - Improve
existing project incrementally. - Aim for inclusive, neutral,
privacy-preserving, transparent and auditable design.

Proposed but not yet owner-approved: - PostgreSQL as production database. -
Argon2id through a maintained
library. - Modular monolith. - Initial voting methods and exact release
scope. - Exact cryptographic protocol and anonymous credential
approach. - Deployment provider and production hosting. - Final
licensing choice.

Do not silently convert proposed decisions into confirmed facts.
