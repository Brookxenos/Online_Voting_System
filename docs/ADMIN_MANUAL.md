# ElectraVote Administrator Manual

This guide describes the existing administration screens and server-enforced
permissions. UI controls do not replace backend authorization.

## Roles and scope

- **Platform administrator (`platform_admin`)** can manage organizations and
  accounts across organizations, and view platform-wide administration data.
- **Organization administrator (`org_admin`)** manages elections and users in
  their own organization. They cannot create or assign platform-admin users.
- **Observer (`observer`)** has read-only access to permitted organization
  election information, eligibility lists, results when allowed, and scoped
  audit entries.
- **Voter (`voter`)** can view permitted elections and vote only when separately
  eligible for that election.

## Sign in and administration dashboard

1. Sign in using the administrator account provisioned by the operator.
2. Open **Administration**. The dashboard summary includes total, active,
   upcoming, and published elections, candidates, active/eligible voters,
   participation, and accepted ballots.
3. Organization-admin summary data is scoped to their organization. Platform
   administrators see platform-wide counts.
4. If the page denies access, verify the signed-in role. Do not attempt to use
   a hidden UI route as a substitute for required permissions.

## Manage organizations (platform administrators)

1. Open the Organizations area and choose **Create organization**.
2. Enter a name and unique URL-safe slug. Choose whether self-registration is
   allowed.
3. Submit and confirm the organization appears in the list.
4. To change its name, slug, or self-registration setting, open the
   organization and save the update. Organization administrators can change
   only their own self-registration setting; they cannot rename the org.

## Manage user accounts

1. Open **Users**. The list shows users visible within your role scope.
2. To create an account, enter a username, password, optional display name,
   role, and organization where applicable. Usernames are 3–32 permitted
   characters; passwords are 8–256 characters.
3. `org_admin`, `observer`, and `voter` accounts must belong to an existing
   organization. Platform-admin accounts have no organization assignment.
4. Organization administrators are always scoped to their own organization
   and cannot assign platform-admin. Platform admins may manage users across
   organizations.
5. Use the account actions to enable or disable a user. Disabling an account
   revokes its active sessions. Re-enabling permits a new login but does not
   restore revoked sessions.
6. Password reset operations do not return the password. The affected user
   must receive the new credential through an approved private channel.
7. Account creation does not grant eligibility for any election. Eligibility
   is configured separately for each election.

## Create and configure an election

1. Choose **Create election** and select an organization you administer.
2. Provide a name, description, and optional start/end timestamps in UTC form
   (`YYYY-MM-DDTHH:MM:SSZ`). The end must follow the start.
3. Decide whether to enable public discovery and live results. Discovery is
   opt-in. Live results can expose partial totals and should be enabled only
   when the election rules intentionally permit them.
4. Save the election. It begins in **Draft**. Only draft elections can have
   their configuration, candidates, or eligibility changed.

## Manage candidates or ballot options

1. Open the draft election and choose its Candidates section.
2. Add each option using the same relevant information fields for each
   candidate. Keep the list order neutral and consistent with published rules.
3. Review names, affiliations, descriptions, and order before opening. Candidate
   changes are blocked after the election leaves Draft.

## Manage voter eligibility

1. Open the draft election's Voter list.
2. Add the username of an active voter in the same organization.
3. Confirm the user appears in the list. Remove an entry if needed while the
   election is still Draft.
4. Eligibility is election-specific. Registration or account creation alone
   never grants eligibility. Eligibility changes are blocked after opening.

## Open, close, and publish

1. Before opening, verify the election description, dates, candidates, voter
   list, and rules. At least one candidate/option is required.
2. Choose **Open election**. The server checks lifecycle rules and voting
   schedule. Configuration becomes locked and eligible voters may submit one
   ballot.
3. When voting ends, choose **Close election**. New ballots are rejected.
4. Review the election record and choose **Publish results**. The existing
   lifecycle operation calculates and persists the tally, checks internal
   consistency, and publishes the aggregate results only if the check passes.
5. Result reads do not create a tally. If no tally exists, investigate the
   lifecycle state instead of trying to trigger recalculation through a read.
6. Open the results page to inspect candidate totals, percentages, ballot and
   participation totals, publication status, and tally integrity metadata.
7. Choose **Download CSV report** for an aggregate report. It does not contain
   voter identities or per-ballot selections.

The tally is single-choice. Tie handling is unspecified, and the application
does not announce an official winner or provide independent tally verification.

## Review audit entries

1. Open the Audit section in Administration or Observer view.
2. Platform administrators can see platform-wide entries. Organization
   administrators and observers see election-linked events for their own
   organization; organization-wide events without an election link are
   platform-admin-only under the current schema.
3. Audit entries include actions such as login outcomes, account changes,
   election lifecycle changes, eligibility changes, and ballot submission.
   Vote audit details intentionally omit candidate choice.
4. Audit access is read-only in the application. Protect the database and
   operational logs; the audit table is not cryptographically tamper-evident.

## Archive an election

1. Archive after publication when the election is finished and should leave
   active discovery.
2. Confirm the archived status. Records and published results are retained;
   archiving does not delete ballots or participation records.
3. Draft archives are terminal and likewise retain election records.

## Common administrative errors

| Error | Likely cause and response |
| --- | --- |
| Access denied / election not found | Your role or organization does not cover the target. Verify the election's organization with a platform administrator. |
| Organization not found | Select an existing organization ID/name. |
| Invalid user information | Check username, display name, password length, role, and required organization assignment. |
| Organization administrators cannot create platform administrators | Only a platform administrator can assign that role. |
| Cannot edit election | Configuration, candidates, and eligibility are draft-only. Use the lifecycle workflow. |
| Cannot open election | Check for at least one option and whether its scheduled start has arrived. |
| Results not available | Confirm the election was closed and published successfully. Reads do not run tallies. |
| Session ended after disabling | Disabling revokes sessions. Re-enable the account and have the user sign in again. |

For password recovery, the operator must stop the service and use the local
`./start.sh --reset-admin-password` procedure in [the deployment guide](DEPLOYMENT_GUIDE.md).
