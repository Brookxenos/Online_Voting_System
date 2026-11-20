# ElectraVote Deployment Guide

ElectraVote is an educational/demo application. This guide explains a
restricted Linux deployment pattern; it does not make the system suitable for
legally binding elections or replace independent operational/security review.

## Requirements and build

Use Linux or WSL with `g++` supporting C++17, GNU Make, and the runtime
libraries configured in `Makefile` (`libsodium.so.23` and `libsqlite3.so.0`).
The integration suite additionally uses bash and curl.

From the repository root:

```sh
make -j2
bash -n tests/smoke.sh
```

The build produces `voting_server`. `./start.sh` builds again if necessary.

## Local run

```sh
./start.sh
```

This listens on `127.0.0.1:8080` and uses `web/` plus `data/`. Visit
`http://localhost:8080`. To change the port, use `./start.sh 9000` or set
`OV_PORT=9000`. The explicit positional port takes precedence. Stop with
Ctrl+C; the script `exec`s the server so the signal reaches the process.

For a separate local database, use `OV_DATA_DIR=./demo-data ./start.sh`. Keep
the same directory on later launches to retain its records. Do not point a
fresh run at a directory containing legacy CSV files unless that import is
intended; the one-time import preserves source CSVs but labels historical
identity-linked records honestly.

## Linux server and reverse proxy

1. Install the repository and runtime dependencies under a controlled path.
2. Run as a dedicated unprivileged service account. Give it read access to the
   repository's `web/` files and write access only to the chosen data path.
3. Keep the application listener on loopback with `OV_BIND_ADDRESS=127.0.0.1`.
   Do not allow public firewall access to the application port.
4. Terminate HTTPS at a maintained reverse proxy. An Nginx site fragment is:

   ```nginx
   server {
       listen 443 ssl;
       server_name vote.example.org;
       ssl_certificate     /etc/letsencrypt/live/vote.example.org/fullchain.pem;
       ssl_certificate_key /etc/letsencrypt/live/vote.example.org/privkey.pem;

       location / {
           proxy_pass http://127.0.0.1:8080;
           proxy_http_version 1.1;
           proxy_set_header Host $host;
           proxy_set_header X-Forwarded-For $remote_addr;
           proxy_set_header X-Forwarded-Proto https;
       }
   }
   ```

   Obtain and renew certificates and set TLS protocol/cipher policy using the
   host's current security policy. Restrict the app port to loopback. The app
   does not trust forwarded client-IP headers; its rate limiter will see the
   proxy address.
5. Set `OV_SECURE_COOKIES=1` only after HTTPS is correctly working end-to-end.
   The backend itself remains plain HTTP on loopback.
6. For a service manager, configure a durable data path and place secrets in a
   protected secret manager or root-owned mode-0600 environment file. Do not
   put secrets in source control, world-readable unit files, command history,
   or logs.

## Supported configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `OV_PORT` | `8080` | Listening TCP port; `start.sh PORT` overrides it |
| `OV_BIND_ADDRESS` | `127.0.0.1` | IPv4 listen address; use `0.0.0.0` only with deliberate network controls |
| `OV_WEB_DIR` | `web` | Static frontend root |
| `OV_DATA_DIR` | `data` | Persistent data directory; created if absent |
| `OV_DB_FILE` | `<OV_DATA_DIR>/openvote.db` | SQLite database file override |
| `OV_SESSION_HOURS` | `168` | Session lifetime in hours; values below 1 are raised to 1 |
| `OV_SECURE_COOKIES` | `0` | Set to `1` behind correctly configured HTTPS to add Secure to session cookies |
| `OV_INITIAL_ADMIN_PASSWORD` | unset | Initial password for a fresh database; 8–256 characters |

Positional binary arguments remain supported:
`./voting_server [port] [web_dir] [data_dir]`. Those positional values override
the corresponding environment settings. `OV_DB_FILE` remains the independent
database file override.

## Initial administrator

On a fresh database, the server creates username `admin` if there is no enabled
platform administrator. In an interactive terminal, an unset initial password
causes a random password to be printed once to that terminal. For
non-interactive startup, set `OV_INITIAL_ADMIN_PASSWORD` through a secret
manager or protected environment file. Without it, startup exits rather than
printing a generated secret to service logs. The application does not print a
supplied password, and an existing admin password is not replaced by setting
this variable later.

There is no default `admin123` password. For local password reset, stop the
service and run:

```sh
./start.sh --reset-admin-password [username]
```

The command prompts without echo, updates only the selected enabled platform
admin's hash, and revokes that account's sessions. Use the same `OV_DATA_DIR`
or `OV_DB_FILE` as the service. It does not reset elections or ballots.

## Persistent database, backup, and restore

The default database is `data/openvote.db`. SQLite uses WAL mode and can have
`-wal`/`-shm` sidecars. Store the complete data directory on durable local
storage with restrictive ownership/permissions. Do not delete sidecars by
hand.

- For a simple filesystem copy, stop the application cleanly first, then copy
  the database and any sidecars together to access-restricted backup storage.
- For a live backup, use SQLite's supported backup API/tooling; the application
  does not currently provide a backup command.
- Restore to a separate data directory first and verify the copy before
  changing service configuration. Preserve the original backup until recovery
  has been confirmed.
- Establish and test an organizational backup schedule, retention period,
  encryption/access controls, and restore procedure. Do not treat a copied
  open WAL database as a validated backup.

## Start, stop, and logs

Start in the foreground with `./start.sh`; service managers should invoke the
same script or binary with explicit environment. Stop with SIGTERM (for
example, `systemctl stop <service>`) or Ctrl+C. Wait for process exit before
filesystem backup or database maintenance. Restrict and rotate logs. Do not
log passwords, session tokens, or ballot choices.

## Verification

```sh
make -j2
bash -n tests/smoke.sh
make test
git diff --check
```

`make test` starts a local HTTP server using isolated temporary databases; it
does not need the runtime database.

## Troubleshooting

- **Address already in use:** choose another port with `./start.sh 9000` or
  `OV_PORT=9000`.
- **Bind fails:** ensure `OV_BIND_ADDRESS` is a valid local IPv4 address and
  the port is free. The server exits with a failure status on socket setup
  errors.
- **Service exits on fresh non-interactive startup:** provide
  `OV_INITIAL_ADMIN_PASSWORD` securely before first startup.
- **Browser cannot reach site:** verify Nginx TLS/service status, local proxy
  upstream at `127.0.0.1:8080`, firewall policy, and the configured port.
- **Login cookie is not retained:** use HTTPS and `OV_SECURE_COOKIES=1` only
  when TLS termination is correct; `Secure` cookies are not sent over HTTP.
- **Database not found during password reset:** verify `OV_DATA_DIR` and
  `OV_DB_FILE` match the running service.
- **Missing library/compiler:** install the C++17 compiler/build tools and
  runtime packages matching the paths in `Makefile`.

## Limits and operational risks

The built-in server is not a hardened production HTTP stack. Login throttling
is process-local and resets on restart; it is not distributed and sees the
proxy IP. SQLite administration and backup/restore are operator-managed.
Ballot/participation separation does not provide cryptographic anonymity.
Tally verification is internal only; tie rules are undefined. Audit rows are
not tamper-evident. This application is not certified and must not be described
as production-ready for legally binding elections without independent review
and applicable certification.
