# Instructions for AI Coding Agents

Before doing any work, read `PROJECT_CONTEXT.md` and `PROGRESS_LOG.md`.

## Non-negotiable workflow

1.  Inspect the actual repository, git status, build files, source code
    and tests before proposing changes.
2.  Treat source code as authoritative when it conflicts with old
    documentation. Report the discrepancy and update documentation after
    confirmation.
3.  Work only on the phase explicitly requested by the project owner. Do
    not perform a full rewrite.
4.  Preserve C++17 and the existing HTML/CSS/vanilla JavaScript frontend
    unless the owner explicitly approves a stack change.
5.  Make small, reviewable changes. Build and test after each logical
    change.
6.  Never delete user data, overwrite the only copy of data, or perform
    irreversible migrations without explicit approval and a
    backup/rollback plan.
7.  Never expose secrets, passwords, tokens, private ballot data or
    personally identifying data in logs, screenshots, examples or
    documentation.
8.  Do not invent cryptography. Use maintained libraries and
    established, reviewed protocols. If a requested privacy property
    cannot be responsibly implemented with the available design, explain
    the gap and stop short of claiming it is solved.
9.  Do not claim production election readiness, anonymity, end-to-end
    verifiability, certification, or "bias-free" operation without
    evidence and independent review.
10. Do not copy proprietary code, branding, layouts, images or other
    protected expression from reference products.

## Required response at the start of a task

-   State which phase/task you understand is approved.
-   Summarize the current implementation relevant to that task.
-   List files you expect to inspect/change.
-   Identify risks, assumptions and any required approval.
-   Then proceed with the smallest safe implementation.

## Required response at the end of a task

-   Summary of implementation.
-   Files changed.
-   Build command and actual result.
-   Tests run and actual results.
-   Security/privacy implications.
-   Known gaps and deferred work.
-   Exact next recommended task.

## Handoff maintenance

Update `PROGRESS_LOG.md` at the end of every work session. Record: -
Date - Agent/tool (if known) - Phase and task - Files changed -
Decisions made and whether approved or proposed - Commands run and
results - Tests run and results - Known issues - Next exact task

If context is missing, do not guess. Inspect the repository and ask the
owner only for decisions that cannot be safely inferred.
