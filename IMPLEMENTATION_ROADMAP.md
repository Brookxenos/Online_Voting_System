# Implementation roadmap

1. Introduce service boundaries behind existing controllers; compile and run isolated HTTP smoke suite after each seam. First: voting service.
2. Extract authentication/session and account services; preserve cookie and error contracts.
3. Extract organization/election/candidate/eligibility services and centralize authorization.
4. Split Store into repository interfaces/SQLite implementations while retaining migrations and transaction semantics.
5. Separate database/migration and security utilities into target directories; adjust Makefile without compiling legacy implementations.
6. Add focused service/repository tests using temporary databases, then expand API compatibility checks.
7. Review security/operations separately. No schema migration, production-readiness claim, or Step 3 work is implied by this roadmap.

Each step requires build, smoke tests against temporary data, route/response comparison and a data-preservation check. Never point tests at `data/openvote.db`.

## Recent incremental completions

- Account deletion now requires the target to be disabled first; active accounts return HTTP 409.
- Published and archived elections can be permanently purged through a scoped, name-confirmed, explicitly acknowledged route that retains a sanitized aggregate audit snapshot.
- Legacy CSV import tolerates CRLF, deduplicates repeated vote rows, runs in one transaction, and disables self-registration for the import organization.
- Election-admin tabs support keyboard navigation; the published-results index filters on `resultsPublic`.

Next recommended task: complete the manual browser walkthrough of the admin and voter flows, including the new disable-then-delete and permanent-purge confirmations, and record any visual or accessibility defects before starting the next backend seam.
