# ElectraVote

ElectraVote is an educational/demo voting and election-management application.
It uses a C++17 backend, an HTML/CSS/vanilla JavaScript frontend, SQLite
persistence, and libsodium for password hashing and secure random generation.
It is not certified or approved for legally binding public elections.

## Current features

- Organization-scoped accounts with platform administrator, organization
  administrator, observer, and voter roles.
- Server-side sessions, password updates, one-time initial administrator
  provisioning, and in-process login/registration throttling.
- Independent elections with draft/open/closed/published/archived lifecycle
  states, candidate management, and election-specific eligibility.
- Transactional ballot and participation writes with one accepted ballot per
  eligible voter in an election.
- Tally integrity checks, controlled result publication, CSV result export,
  and sanitized audit events.
- Opt-in public election discovery and published-results browsing.
- Responsive single-page frontend using the existing project visual design.

The separation between participation and ballot rows does not provide
cryptographic anonymity. Timing, application/database access, logs, and
backups can still correlate a voter with a choice. The current tally is
single-choice and does not define tie resolution or claim an official winner.

## Build and run

Requirements: Linux or WSL, `g++` with C++17 support, `make`, and the runtime
libraries at the paths configured in the Makefile (`libsodium.so.23` and
`libsqlite3.so.0`).

```sh
make
./start.sh
```

Open <http://localhost:8080>. To use another port, run `./start.sh 9000` and
open <http://localhost:9000>. `Ctrl+C` stops the server. `make test` runs the
HTTP integration smoke suite and requires `bash` and `curl`.

On a fresh data directory the server creates the `admin` account. In an
interactive terminal it generates a random password and displays it once. For
non-interactive startup, set `OV_INITIAL_ADMIN_PASSWORD` through a secret
manager; otherwise startup refuses to print a generated password into service
logs. Do not place real passwords in shared shell history or documentation.
There is no default `admin123` credential.

The default database is `data/openvote.db`. Set `OV_DATA_DIR` or `OV_DB_FILE`
to select a different data location. Legacy CSV files may be imported into a
fresh database and are preserved. Imported historical votes remain
identity-linked.

## Documentation

- [API reference](API.md): routes, authentication, request fields, responses,
  and current security limits.
- [How to run](HOW_TO_RUN.txt): setup, initial admin account, local password
  recovery, and data-directory behavior.
- [Project context](PROJECT_CONTEXT.md): architecture, decisions, security
  boundaries, and handoff guidance.
- [Backend checklist](BACKEND_STAGE2_CHECKLIST.md): implementation status and
  remaining Stage 2 work.

## Security and deployment limits

The local server speaks plain HTTP and binds to loopback by default. It is
still a development server, not a production deployment. [How to run](HOW_TO_RUN.txt)
documents reverse-proxy configuration, persistent storage, backups, and the
remaining requirements for durable rate limiting, log handling, and independent review.
Do not use this demo for a legally binding election or claim anonymity,
end-to-end verifiability, or certification.

## License

The project context records the licensing choice as pending confirmation; this
README does not assert a license that is not present in the checkout.
