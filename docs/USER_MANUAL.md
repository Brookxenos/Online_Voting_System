# ElectraVote User Manual

This guide is for voters and people browsing public elections. Elections may
be visible only to signed-in members of the organization running them.

## Open ElectraVote

1. Open the address supplied by your organization. On a local installation it
   may be `http://localhost:8080`; deployed installations should use the
   organization's HTTPS address.
2. The home page links to election discovery, results, login, and registration.
   If an election is not listed, sign in or ask its organizer whether it is
   discoverable and whether your account can view it.

## Register and sign in

1. Select **Get started** or **Register** and enter a username and password.
   Usernames must be 3–32 letters, digits, underscores, dots, or hyphens.
   Passwords must be 8–256 characters. A display name is optional.
2. Registration is available only when an organization enables self-registration.
   Registration creates a voter account; it does not put you on any election's
   voter list.
3. Open **Log in**, enter your username and password, and submit.
4. If login fails, check spelling and ask the organization administrator for
   account help. The application has no web-based password recovery; an
   administrator can reset your password after verifying your identity.

## Use your voter dashboard

1. After login, open **Dashboard**. It shows elections available to your
   account, your participation history, and summary counts for available,
   voted, upcoming, and completed elections.
2. An election marked **Not on the voter list** is visible to you, but you
   cannot cast a ballot until its administrator grants your eligibility.
3. The dashboard may show a participation timestamp after your ballot is
   accepted. It does not show your selected option.

## Find an election and check access

1. Choose **Explore elections** or **Elections**.
2. Public visitors see elections the organizer opted into discovery. Signed-in
   users may see elections available under their organization and election
   access settings.
3. Open an election to read its name, description, schedule, status, and
   candidate or ballot-option information.
4. If voting is active, the page indicates whether you are eligible. If it says
   you are not on the voter list, contact the election organizer. Do not create
   another account to try to bypass eligibility.

## Cast and confirm one ballot

1. Sign in with a voter account and open an eligible election during its active
   voting period.
2. Review the election instructions and every candidate or option. Candidate
   information is available from the election page.
3. Select one option. Nothing is selected in advance. Choose **Review
   selection**.
4. Check the option shown in the review section. Choose **Go back** to change
   it, or **Confirm and submit vote** to submit it.
5. Wait for the confirmation page and save the receipt code if you need it.
   The server accepts at most one ballot per eligible account in an election.
   A submitted ballot cannot be changed or withdrawn.
6. If submission reports an error, do not repeatedly submit if the page may
   have timed out. Check your dashboard or ask the organizer to confirm whether
   participation was recorded.

The system keeps participation records separate from ballot-choice records,
but that is not cryptographic anonymity. Privileged access, timing, logs, and
backups may still create correlation risks.

## View published results

1. Open **Results** from the navigation or follow **View Election Results** on
   an election page after publication.
2. The results page shows the published aggregate candidate totals and
   percentages. Results may be unavailable before publication unless the
   organizer deliberately enabled live results for authorized viewers.
3. Use **Download CSV report** if you need a spreadsheet copy. The report
   contains aggregate election and candidate information, not voter identities
   or individual ballot selections.
4. The system does not announce an official winner or define tie-breaking.

## Log out

1. Open your account menu and choose **Log out**.
2. On shared computers, close the browser after logging out. Do not share your
   password or session cookie.

## Common problems

| What you see | What to do |
| --- | --- |
| Registration is unavailable | Ask the organization whether self-registration is enabled, or request an account. |
| Election is missing | Sign in and check the organization/election access with the organizer. Public discovery is opt-in. |
| Not on the voter list | Ask the election administrator to check your eligibility for that specific election. |
| Voting is not open | Check the election schedule and status; the server enforces its voting window. |
| Already voted | The account has an accepted participation record. Ballots cannot be submitted twice. |
| Results are not published | Ask the organizer when results will be released. |
| Login fails | Recheck username/password and contact an administrator; there is no web recovery flow, but an administrator can reset the password. |
| Page cannot connect | Check the supplied site address and contact the service operator. |

For legal, privacy, or accessibility concerns, contact the organization
running the election. ElectraVote is an educational/demo system, not certified
for legally binding elections.
