# 🗳️ ElectraVote — Online Voting System (C++ Client-Server + Responsive Web App)

A complete electronic voting system written in **C++17** with **zero external
dependencies** — the HTTP server, JSON parser, SHA-256 implementation, and
cipher are all hand-rolled. The frontend is a responsive HTML/CSS/JS single-page
application served by the C++ backend.

## Features

- **Voter registration** with unique voter IDs + SHA-256-hashed passwords
- **Voter login** + ballot casting with one-vote-per-voter enforcement
- **Candidate management** (admin can add/remove candidates with party, symbol, position)
- **Live results** with vote tally + percentage share + leader declaration
- **Admin panel** with role-based access (separate admin credentials)
- **Audit log** — every login, vote, and admin action is timestamped and persisted
- **Vote encryption** — ballots are XOR-stream encrypted client-side before transmission
  (and tamper-checked server-side via voter-id binding + signature hash)
- **CSV file persistence** — `voters.csv`, `candidates.csv`, `votes.csv`,
  `admins.csv`, `audit.log`
- **Responsive web UI** — works on phones, tablets, desktops; dark-mode aware
- **Multi-threaded** — one thread per client connection

## Quick start

```bash
cd voting_system
make
./start.sh              # listens on http://localhost:8080
# or specify a port:
./start.sh 9000
```

Then open <http://localhost:8080> in your browser.

**Default admin credentials** (bootstrapped on first run):
- username: `admin`
- password: `admin123`

Please change the admin password immediately via **Admin → Settings**.

## Project layout

```
voting_system/
├── Makefile
├── README.md
├── start.sh                # build + run helper
├── src/
│   ├── main.cpp            # entry point
│   ├── crypto.{h,cpp}      # SHA-256, XOR cipher, base64
│   ├── models.h            # Voter / Candidate / Vote / Admin / AuditEntry
│   ├── data_store.{h,cpp}  # thread-safe CSV-backed persistence
│   ├── json.{h,cpp}        # tiny JSON parser + serializer
│   └── http_server.{h,cpp} # POSIX-socket HTTP server + API handlers
├── web/
│   ├── index.html          # SPA shell with <template> views
│   ├── style.css           # responsive + dark-mode aware
│   └── app.js              # router + API client + ballot encryption
└── data/                   # CSV files (auto-created on first run)
    ├── voters.csv
    ├── candidates.csv
    ├── votes.csv
    ├── admins.csv
    └── audit.log
```

## API reference

All endpoints are JSON, under `/api/`.

| Method | Endpoint                       | Description |
|--------|--------------------------------|-------------|
| POST   | `/api/register`                | Voter self-registration |
| POST   | `/api/voter/login`             | Voter authentication |
| POST   | `/api/admin/login`             | Admin authentication |
| GET    | `/api/candidates`              | List all candidates |
| POST   | `/api/vote`                    | Cast an encrypted ballot |
| GET    | `/api/results`                 | Live tally + leader |
| GET    | `/api/election/status`         | Poll open/closed state |
| POST   | `/api/election/status`         | Open / close the election (body: `{open: bool}`) |
| POST   | `/api/admin/candidates`        | Add a candidate |
| POST   | `/api/admin/candidates/remove` | Remove a candidate |
| POST   | `/api/admin/voters`            | Admin-add a voter |
| GET    | `/api/admin/voters`            | List all voters |
| GET    | `/api/admin/audit`             | Last 200 audit entries |
| POST   | `/api/admin/password`          | Change admin password |

## Example API usage (curl)

```bash
# Admin login
curl -s localhost:8080/api/admin/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}'

# Add a candidate
curl -s localhost:8080/api/admin/candidates \
  -H 'Content-Type: application/json' \
  -d '{"name":"Alice Anderson","party":"Reform","symbol":"🌹","position":"President"}'

# Register a voter
curl -s localhost:8080/api/register \
  -H 'Content-Type: application/json' \
  -d '{"name":"John Doe","password":"secret123","nationalId":"N-9999"}'

# Cast a vote (the "ballot" field is base64(xorCrypt("C001|V001", KEY)))
curl -s localhost:8080/api/vote \
  -H 'Content-Type: application/json' \
  -d '{"voterId":"V001","password":"secret123","ballot":"...base64..."}'

# Live results
curl -s localhost:8080/api/results
```

## Security notes

This is an **educational / demo** project. Before any real-world use you would need:

1. **TLS** for transport encryption (the server speaks plain HTTP).
2. A real **key exchange** instead of a shared hardcoded `VOTE_KEY`.
3. **CSRF tokens** for state-changing endpoints.
4. A vetted crypto library (e.g. libsodium / OpenSSL) instead of a hand-rolled SHA-256
   and XOR stream cipher.
5. Rate limiting on login + registration endpoints.

The included measures (salted SHA-256 passwords, encrypted + signed ballots,
one-vote enforcement, audit log) demonstrate the *mechanics* of a voting
system but are not sufficient for a production election.

## License

MIT — use it, learn from it, improve it.
