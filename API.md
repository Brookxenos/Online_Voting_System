# ElectraVote API reference

The server exposes JSON endpoints under `/api/`. Success responses use
`{"ok":true,...}`; failures use `{"ok":false,"error":"..."}`. Sessions use
the HttpOnly `ov_session` cookie. Do not trust a role supplied by a client.

## Authentication and account

| Method and route | Access | Request | Result |
| --- | --- | --- | --- |
| `GET /api/bootstrap/status` | Public | — | `needsSetup` |
| `POST /api/bootstrap/setup` | Public, only before an enabled platform admin exists | `username`, `password` | `user`; 400 validation, 409 already configured |
| `POST /api/auth/register` | Public, if an organization allows registration | `username`, `password`, optional `displayName` | `user`; creates a voter, 400 invalid, 409 duplicate, 429 throttled |
| `POST /api/auth/login` | Public | `username`, `password` | `user` and session cookie; 401 uniform credential failure, 429 throttled |
| `POST /api/auth/logout` | Public/session optional | — | Revokes current session and clears cookie |
| `GET /api/me` | Authenticated | — | Current user's permitted profile |
| `GET /api/me/dashboard` | Voter | — | Visible elections and own participation timestamps; no ballot choices |
| `POST /api/me/profile` | Authenticated | `displayName` | Updated profile |
| `POST /api/auth/password` | Authenticated | `currentPassword`, `newPassword` | Updates password and revokes all sessions |

## Public discovery

| Method and route | Access | Query/request | Result |
| --- | --- | --- | --- |
| `GET /api/public/elections` | Public | optional `q`, `status`, `from`, `to`, `sort`, `limit`, `offset` | Discoverable non-draft elections and page metadata |
| `GET /api/public/elections/:id` | Public | — | Discoverable election details and candidate information; hidden records return 404 |
| `GET /api/public/results` | Public | optional `q`, `from`, `to`, `sort`, `limit`, `offset` | Published/public election index with publication time |

`status` is `open`, `closed`, `published`, or `archived`. `from`/`to` use
`YYYY-MM-DD`; `sort` is `start-asc`, `start-desc`, `title`, or `newest`;
`limit` is 1–100 (default 20), and `offset` is 0–1,000,000 (default 0).
Search text is limited to 100 characters. Public discovery requires an
administrator to opt an election in while it is a draft. Existing elections
are not discoverable after migration unless opted in.

## Organizations, elections, voting and results

| Method and route | Access | Request | Result |
| --- | --- | --- | --- |
| `GET /api/orgs` | Authenticated; platform admin sees all, others their organization | — | `orgs` |
| `POST /api/orgs` | Platform admin | `name`, `slug`, optional `allowSelfRegistration` | Created `org`; 409 duplicate slug |
| `GET /api/elections` | Authenticated | — | Elections visible to the session; voters also receive `eligible` and `hasVoted` |
| `POST /api/elections` | Platform admin or organization admin for `orgId` | `orgId`, `name`, optional `description`, `discoverable`, `showLiveResults`, `startAt`, `endAt` | Created draft election |
| `GET /api/elections/:id` | Authenticated and election-visible | — | Election, candidates, permitted participation state |
| `POST /api/elections/:id` | Election administrator; draft only | Any supported election fields: `name`, `description`, `discoverable`, `showLiveResults`, `startAt`, `endAt` | Updated election; 409 outside draft |
| `POST /api/elections/:id/status` | Election administrator | `status`: `open`, `closed`, `published`, or `archived` | Updated lifecycle state; invalid transitions return 409. A draft may be archived; the record is retained and removed from public discovery. |
| `GET /api/elections/:id/candidates` | Authenticated and election-visible | — | Candidates and election status |
| `POST /api/elections/:id/candidates` | Election administrator; draft only | `name`, optional `party`, `description`, `position` | Created candidate |
| `POST /api/elections/:id/candidates/:candidateId` | Election administrator; draft only | Optional `name`, `party`, `description`, `position` | Updated candidate |
| `POST /api/elections/:id/candidates/remove` | Election administrator; draft only | `candidateId` | Removes candidate |
| `GET /api/elections/:id/eligibility` | Election administrator or observer | — | Eligible voter identities for that election |
| `POST /api/elections/:id/eligibility` | Election administrator | `username` or `userId`, `action` (`grant`/`revoke`) | Updated eligibility; grant accepts only an active voter from the election's organization |
| `POST /api/elections/:id/vote` | Authenticated eligible voter | `candidateId` | Non-revealing confirmation receipt; no choice is returned |
| `GET /api/elections/:id/results` | Public after publication; otherwise permitted observer/admin or configured live-results viewer | — | Candidate totals, vote percentages, and tally integrity metadata |
| `GET /api/elections/:id/results.csv` | Public after publication; otherwise administrator/observer or configured live-results viewer | — | CSV result export |

Election timestamps use UTC `YYYY-MM-DDTHH:MM:SSZ`; end must follow start.
An election cannot be opened before its configured start or without at least
one candidate. Archiving a draft is terminal and does not delete its election,
candidate, or audit records; it clears public discovery and live-results flags.
Ballot, participation, eligibility and publication checks happen on the server.
For the current nonbinding demo, `closed -> published` runs the tally and makes
results public after the internal tally-integrity check passes. There is no
separate reviewer approval state or independent tally verification.

## Administration and audit

| Method and route | Access | Request | Result |
| --- | --- | --- | --- |
| `GET /api/admin/summary` | Platform admin (global) or organization admin (scoped) | — | Election, candidate, active voter, eligible voter, participation and accepted-ballot counts |
| `GET /api/users` | Platform admin or organization admin (scoped) | optional `orgId` for platform admin | User records without password hashes |
| `POST /api/users` | Platform admin or organization admin (scoped) | `username`, `password`, optional `displayName`, `role`, `orgId` | Created account; self-registration cannot grant roles |
| `POST /api/users/:id/disable` | Platform admin or organization admin in same organization | optional `disabled` (defaults true) | Changes account state; disabling revokes sessions |
| `GET /api/audit` | Platform admin, organization admin, or observer (scoped) | optional `electionId` | Recent sanitized audit events |

## Current limits

- Password recovery, email verification, notifications, and draft-election
  deletion are not implemented. A separate result-review/finalization state is
  deferred until reviewer roles and approval rules are specified.
- Tally publication uses the current single-choice model. Tie resolution is
  undefined; the API does not claim an official winner.
- Participation and ballot records have no direct shared key, but the design
  is not cryptographic anonymity. Timing, privileged database access,
  application access, and backups remain correlation risks.
- The HTTP service is a development server. Production requires independently
  reviewed HTTPS deployment, durable rate limiting, backup/restore operations,
  and security review.
