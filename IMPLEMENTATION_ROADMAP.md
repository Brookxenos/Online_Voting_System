# Implementation roadmap

1. Introduce service boundaries behind existing controllers; compile and run isolated HTTP smoke suite after each seam. First: voting service.
2. Extract authentication/session and account services; preserve cookie and error contracts.
3. Extract organization/election/candidate/eligibility services and centralize authorization.
4. Split Store into repository interfaces/SQLite implementations while retaining migrations and transaction semantics.
5. Separate database/migration and security utilities into target directories; adjust Makefile without compiling legacy implementations.
6. Add focused service/repository tests using temporary databases, then expand API compatibility checks.
7. Review security/operations separately. No schema migration, production-readiness claim, or Step 3 work is implied by this roadmap.

Each step requires build, smoke tests against temporary data, route/response comparison and a data-preservation check. Never point tests at `data/openvote.db`.
