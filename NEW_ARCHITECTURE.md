# Proposed C++17 modular monolith

```text
HTTP server -> API controllers -> services -> repositories/Store -> SQLite Database
                     |                 |             |
                  middleware        models       migrations/transactions
```

Keep SQLite, libsodium, the handwritten HTTP server, custom JSON implementation and the unchanged `web/` frontend. Organize source incrementally into `src/core`, `http`, `controllers`, `middleware`, `services`, `repositories`, `models`, `security`, `database`, and `utils`. `main` remains the composition root and injects the shared database/repository/service dependencies. Controllers translate existing wire requests/responses; middleware centralizes session and role checks; services own business use cases; repositories own SQL; models remain plain domain records; security wraps vetted libsodium functions; database owns connections, migrations and transactions.

Target service boundaries: Auth, User, Organization, Election, Candidate, Voter/eligibility, Voting, Results, Audit. Target repository boundaries can initially be adapters over the existing `Store`, then split by aggregate. Preserve vote submission and close-to-publish transactions. Do not introduce microservices, a new framework, a schema change, or a frontend change.

Current transitional structure retains `src/*.cpp` until each module is extracted. `src/services/VotingService` is the first working seam. It delegates to the existing transaction-safe Store operation; this proves dependency direction but does not yet remove SQL/business logic from Store. Empty target directories are established with `.gitkeep` markers. Avoid claiming the architecture is complete.
