# OPENVOTE_FRONTEND_BACKEND_INTEGRATION_REQUIREMENTS

Frontend-only Stage 1 contract inventory, based on `src/api.cpp`,
`src/models.h`, `src/store.h`, and `web/app.js`. No backend changes are
included in Stage 1. The frontend shows an unavailable, loading, or error state
until an existing endpoint returns the requested data.

## Existing integrations

| Feature | Existing API | Current support |
| --- | --- | --- |
| Registration | `POST /api/auth/register` | `username`, optional `displayName`, `password`; assigns the first organization allowing self-registration. No email. |
| Login/session/logout | `POST /api/auth/login`, `GET /api/me`, `POST /api/auth/logout` | Username/password and HttpOnly session cookie; user fields are id, username, display name, role, org id. |
| Profile/password | `POST /api/me/profile`, `POST /api/auth/password` | Display name edit and password change. |
| Elections | `GET /api/elections`, `GET /api/elections/:id`, `POST /api/elections`, `POST /api/elections/:id` | Authenticated, visibility-scoped list/details; detail contains candidate records and voter eligibility/participation state where relevant; election edits are draft-only. |
| Lifecycle | `POST /api/elections/:id/status` | Server-validated lifecycle transition. |
| Candidates | `GET /api/elections/:id/candidates`, `POST /api/elections/:id/candidates`, `POST /api/elections/:id/candidates/remove` | Read/add/remove in draft. Fields: name, party, description, position. |
| Eligibility | `GET /api/elections/:id/eligibility`, `POST /api/elections/:id/eligibility` | Authorized admins/observers read the list; admins grant/revoke. |
| Ballot | `POST /api/elections/:id/vote` | Authenticated eligible voter sends `candidateId`; successful response returns a real receipt. |
| Results | `GET /api/elections/:id/results`, `GET /api/elections/:id/results.csv` | Published results are public by known id. Unpublished results require an authorized admin/observer or configured live-results policy. Response contains status, published flag, ballots/participations, integrity flag, and candidate vote totals. |
| Admin operations | `GET/POST /api/orgs`, `GET/POST /api/users`, `POST /api/users/:id/disable`, `GET /api/audit` | Existing scoped organization/user management and read-only audit. |

## Required backend support

### Public election discovery and details

- **Feature:** Anonymous election discovery and public election details.
- **Operation:** Anonymous `GET /api/public/elections` and `GET /api/public/elections/:id` (or equivalent).
- **Query fields:** `q`, `status`, `from`, `to`, `sort`, `limit`, and `cursor` (or page/offset).
- **Response:** Only elections explicitly marked discoverable, with id, name, description, start/end, lifecycle status, and pagination metadata. Details may include candidate information and instructions approved for public display.
- **Authorization:** Anonymous access only to public records; voter-specific eligibility/participation must remain session-scoped.
- **Errors:** 400 invalid filter/cursor; 404 hidden/unavailable election; 429 rate limit; 5xx generic service error.
- **Current behavior:** `/api/elections` and `/api/elections/:id` require authentication. The frontend shows a sign-in explanation rather than fake or empty public data.

### Public results index

- **Feature:** Find published results without an election id.
- **Operation:** Anonymous `GET /api/public/results` with search, date, sort, and pagination filters.
- **Response:** Finalized/published elections only, with id, name, status, publication time, and pagination metadata. Detailed data can use the existing result response shape.
- **Authorization:** Anonymous only when results are already public; never return unpublished/live totals.
- **Errors:** 400 invalid filters; 404 unavailable result; 5xx generic error.
- **Current behavior:** A published result can be read publicly if its id is known, but there is no public index.

### Voter dashboard and participation history

- **Feature:** Stable dashboard statistics and participation history.
- **Operation:** Session-scoped `GET /api/me/dashboard` or equivalent.
- **Response:** Available/upcoming/cast/completed counts and election participation records with id/name/status and a timestamp when policy permits.
- **Authorization:** Current user only. Never return a candidate choice or ballot-to-user join.
- **Errors:** 401 expired session; 5xx generic error.
- **Current behavior:** The frontend derives counts from the authenticated election list (`eligible` and `hasVoted`). No participation timestamp/history endpoint exists.

### Candidate profiles

- **Feature:** Official image, biography, manifesto, and policy priorities.
- **Operation:** Extend `GET /api/elections/:id` candidate objects or add `GET /api/elections/:id/candidates/:candidateId`.
- **Response fields:** Existing id/name/party/description plus optional image URL, biography, manifesto, priorities, and other approved public fields.
- **Authorization:** Same election visibility policy as election details. Validate image source/type if uploads are later supported.
- **Errors:** 404 candidate is not part of election; 5xx generic error.
- **Current behavior:** Only name, party, description, and position are represented. The UI uses those fields and does not invent a photograph or manifesto.

### Profile fields and notifications

- **Feature:** Email/profile image and notification preferences/center.
- **Operations:** Extend `GET /api/me` and `POST /api/me/profile` only for account-service-supported fields; add authenticated notification list/read and preference operations if that service is implemented.
- **Response fields:** Email/image only if stored and verified by account service; notification id/type/message/created/read timestamps/read state.
- **Authorization:** Own account only; no voter profile exposure to other voters.
- **Errors:** 400 validation; 401 expired session; 404 missing notification; 5xx generic error.
- **Current behavior:** API exposes username, display name, role, and org id; only display name is editable. No notification service exists.

### Password recovery

- **Feature:** Recovery request, token validation, password reset.
- **Operations:** Rate-limited request, token validation, one-time token-based password update with generic anti-enumeration responses.
- **Security requirements:** High-entropy, short-lived, single-use token; never return or log reset tokens in the frontend.
- **Errors:** Generic request response, invalid/expired token, rate limit, 5xx generic error.
- **Current behavior:** No recovery endpoints exist; no success is simulated.

### Administrator aggregates and management

- **Feature:** Efficient admin summary and per-election eligible-voter counts.
- **Operation:** Scoped `GET /api/admin/summary` (or equivalent) returning elections by status, candidate count, active voter accounts, participation totals, and optional recent activity.
- **Authorization:** Platform admins receive global totals; organization admins receive only their organization. Never return ballot selections or identity-choice links.
- **Errors:** 401/403 unauthenticated or out-of-scope; 5xx generic error.
- **Current behavior:** Frontend derives counts from the existing scoped election, detail, candidate, and user endpoints; it shows unavailable values when required data cannot load.

### Election deletion and candidate editing

- **Feature:** Delete permitted draft elections and edit candidate details.
- **Operations:** Authorized deletion with lifecycle/retention checks; candidate update for draft elections.
- **Request fields:** Election id and deletion preconditions; candidate id plus supported candidate fields.
- **Authorization:** Election administrator scoped to the organization; backend enforces state and referential constraints.
- **Errors:** 400 invalid fields; 403 unauthorized; 404 missing record; 409 lifecycle/constraint conflict.
- **Current behavior:** No election-delete or candidate-update route exists. The frontend exposes existing draft create/edit and candidate add/remove operations only.

## Stage 1 limitations

- Public election discovery/details and results indexing need anonymous list endpoints.
- Registration has no email field or verification; password recovery is unsupported.
- Candidate image, extended biography, manifesto, and priorities are absent from the current model/API.
- Profile email/photo, notifications, participation timestamps, deletion, candidate editing, and a distinct admin summary API are unsupported.
- The results response does not identify an official winner; the frontend does not invent one.
