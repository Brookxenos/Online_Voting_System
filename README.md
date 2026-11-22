# ElectraVote

ElectraVote is an educational/demo voting and election-management application
for organizations, clubs, schools, and community polls. It provides
organization-scoped accounts, election administration, voter eligibility,
ballot submission, aggregate tally publication, CSV reporting, and an audit
trail. It is **not certified or approved for legally binding public
elections**.

## Implemented features

- Platform administrator, organization administrator, observer, and voter
  roles with server-side authorization and organization scoping.
- Self-registration where enabled, account administration, database-backed
  sessions, session revocation, and password management.
- Draft/open/closed/published/archived election lifecycle, candidate/option
  management, and election-specific voter eligibility.
- Single-choice ballots with transactional participation and ballot writes,
  duplicate participation protection, and a non-revealing submission receipt.
- Lifecycle-controlled tally persistence, internal publication integrity
  checks, published results, and aggregate CSV export.
- Two-step account removal (disable first) and scoped, name-confirmed permanent
  deletion of posted elections with a retained sanitized audit snapshot.
- Legacy CSV migration that preserves source files, accepts CRLF, deduplicates
  repeated vote rows, and keeps identity-linked records clearly labeled.
- Sanitized audit events, public discovery opt-in, and the existing responsive
  HTML/CSS/vanilla JavaScript interface.

The current tally does not define tie resolution or announce an official
winner. Participation and ballot tables are separate, but this does not
provide cryptographic anonymity. Timing, application/database access, logs,
and backups can still create correlation risks.

## Technology

- C++17 modular monolith and hand-written POSIX HTTP server
- HTML, CSS, and vanilla JavaScript single-page frontend
- SQLite persistence with WAL mode and migrations
- libsodium password hashing and secure random generation

## Requirements

Linux or WSL, `g++` with C++17, `make`, and runtime libraries at paths in the
Makefile (`libsodium.so.23` and `libsqlite3.so.0`). The test suite additionally
uses bash and curl.

## Build and run

From the repository root:

```sh
make -j2
./start.sh
```

The server defaults to `127.0.0.1:8080`; open <http://localhost:8080>. Use
`./start.sh 9000` to select another port. Ctrl+C stops a foreground server.
The default database is `data/openvote.db`; set `OV_DATA_DIR` to choose a
persistent data directory. See the deployment guide for all supported
configuration and HTTPS reverse-proxy instructions.

On a fresh interactive run, an initial random admin password is shown once in
the terminal. A non-interactive first run requires `OV_INITIAL_ADMIN_PASSWORD`
from a protected secret manager/environment file; otherwise startup exits
without writing a generated password into service logs. No default
`admin/admin123` credentials exist.

## Tests

```sh
bash -n tests/smoke.sh
make test
```

The suite uses temporary databases and a local HTTP test server. Do not point
tests at the runtime database.

## Documentation

- [Voter and public user manual](docs/USER_MANUAL.md)
- [Administrator manual](docs/ADMIN_MANUAL.md)
- [System architecture and developer guide](docs/SYSTEM_ARCHITECTURE.md)
- [Deployment guide](docs/DEPLOYMENT_GUIDE.md)
- [API reference](API.md)
- [Detailed build/run notes](HOW_TO_RUN.txt)
- [Project context and security boundaries](PROJECT_CONTEXT.md)
- [Development handoff log](PROGRESS_LOG.md)

## Security and deployment limits

The built-in server speaks HTTP only. It binds to loopback by default and
should sit behind a correctly configured HTTPS reverse proxy for remote use;
set `OV_SECURE_COOKIES=1` only when HTTPS is working. Rate limiting is
process-local and resets on restart. The app does not trust proxy-forwarded
client IPs. SQLite backup/restore is operator-managed. Audit records are not
tamper-evident, tally verification is internal only, and no independent
security, privacy, accessibility, legal, or certification review is claimed.

Never claim production election readiness, cryptographic anonymity,
end-to-end verifiability, or certification based on this demo.

## License

The repository does not currently declare a confirmed license.
