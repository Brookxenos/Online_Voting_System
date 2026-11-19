# Security and migration risks

## Observed boundaries

- The custom POSIX HTTP server is a development server; plain HTTP requires a correctly configured trusted TLS boundary for deployment.
- Login/registration throttling resides in process memory and is neither durable nor shared across instances.
- Cookie-backed sessions require careful CSRF/deployment review. Secure-cookie behavior is opt-in.
- Ballot and participation rows are separated but timing, logs, privileged access and backups can correlate them; this is not anonymity.
- Results publication relies on an internal tally integrity check; no independent verification/reviewer approval or tie handling exists.
- Current persistence is SQLite with versioned migrations, transactions, foreign keys and WAL. Existing DB and WAL/SHM files are live user data and must not be removed, copied over, reset, or used as test targets.
- Legacy CSV import retains source files and marks historical linkage limitations.

## Refactor controls

No schema changes in this stage. Use temporary test databases only. Keep prepared statements and transaction boundaries intact, especially ballot submission and publication. Preserve sanitized audit events and avoid logging credentials, raw session tokens or ballot choices. Compile only intended source paths; exclude `src/legacy_impl`. Review API status/body/cookie compatibility after every controller/service change. Do not claim election readiness or cryptographic anonymity.

## Open work

Durable rate limiting, CSRF assessment/mitigation, production HTTPS/deployment hardening, backup/restore procedures, independent tally verification, tie policy and independent security review remain outside this initial extraction.
