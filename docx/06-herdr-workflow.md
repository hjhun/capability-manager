# Development with two Codex panels in Herdr

This document describes the collaboration workflow to execute later. Writing the original document did not control Herdr panels or start product development. The user prepares two Codex panels on the same project and pastes the block from the [start prompt](09-start-prompt.md) into one panel. That panel owns implementation/integration and assigns review to the other actual panel it discovers.

## 1. Roles and file ownership

| Role | Responsibility | Default writable scope |
|---|---|---|
| Implementation/integration owner | Phase planning, product/tests/tools, emulator operations, issue resolution, document/result integration | Code/build files, API contract, progress board |
| Independent reviewer | Contract/design/code review, failure scenarios, evidence checks, completion assessment | Read-only review replies by default; only separately assigned disjoint files may be edited |

A shared checkout exposes changes immediately to both panels. Do not edit the same header, schema, or CMake file concurrently. Assign a task ID and owned files before delegating test implementation to the reviewer. The implementation owner exclusively operates device installations, restarts, and fixtures so review does not change the test environment.

Only the implementation owner handles Git staging, commits, integration after fetch, and pushes. A reviewer assigned a writable file still does not independently commit or push. Do not stage another panel's unfinished work indiscriminately.

Use the reviewer throughout development, not only as a final judge: P00 contract review, failure-path review at each phase, code review after changes, and final evidence comparison. Resolve implementation choices that do not change product meaning by comparing source evidence and tests between panels.

### Root AGENTS.md before product implementation

Both panels read applicable parent instructions and [root AGENTS.md](../AGENTS.md) first. The original documentation work created a baseline file. If it is missing in a development checkout, the implementation owner creates it; if present, preserve and update it narrowly.

The first review covers AGENTS.md and development contracts. The other panel checks roles, writable scope, review exchange, testing, single Git owner, and completion conditions. Re-review requested changes before product implementation. Do not request the user's permission at every instruction change. During execution, tell the peer the changed revision and applicable scope.

## 2. Checks before connecting panels

Read the installed Herdr skill and live CLI help. The skill was found in the original environment under a user-local agent-skill installation; use the installed path in the current environment rather than a copied absolute path.

```bash
test "${HERDR_ENV:-}" = 1
```

If this fails, do not control another panel in the current UI from outside Herdr. Report the environment issue and ask to start inside a Herdr panel; document reading and independent work may still proceed.

After it passes, use these read commands to confirm installed syntax and actual targets:

```bash
herdr --help
herdr agent
herdr pane
herdr pane current --current
herdr agent list
```

- Do not reuse example IDs from documents or old sessions.
- Identify the current panel and another Codex panel in the same project from the live list. If multiple peers fit, clarify the target once.
- Do not create another panel, tab, or workspace by default; the user prepared two.
- Preserve user focus. When a separate panel is explicitly requested, preserve cwd and use `--no-focus`.
- Do not automatically approve a blocked panel's permission/authentication dialog. Read its cause and request only needed information.

## 3. First review request and later communication

After checking installed syntax, use `herdr agent prompt` with an explicit target. Do not use `--wait` on peer-to-peer requests: the implementation owner should continue independent work while review runs, avoiding mutual waits. A single owner may use a time-limited state wait when necessary.

Example initial role card:

```text
Please independently review Capability Manager in the two-panel development setup.
First read applicable parent instructions, root AGENTS.md, and the contracts below:
docx/README.md, 01-requirements.md, 07-decisions-and-open-items.md,
03-api-contracts.md, 04-implementation-plan.md, and 05-verification.md.
Review P00 contracts and the P01 testing foundation while preserving agreed behavior.
Report errors, missing failure paths, and unsupported claims to the implementer.
This is read-only by default; edit files or operate devices only under a separate assignment.
Reply to me asynchronously, without --wait on peer messages.
Include requirement/gate ID, file location, problematic behavior, proposed correction,
and needed verification. State revision and ACCEPTED/CHANGES_REQUESTED/BLOCKED.
Re-review a changed revision; ask me directly for concrete missing evidence.
Do not treat panel state or documentation alone as product completion.
Current request: [phase/task ID and exact review scope]
Reply target: [actual live implementation-owner name or pane ID]
```

Replace bracketed fields with real values. Include the reply target and permission for peer coordination in the first request so the reviewer need not guess the recipient.

### Minimum request/response content

```text
[Progress / Verification result / Decision request / Final report]
Task ID / phase / requirement IDs:
Review request ID / request being answered / target revision / owned files:
Review decision: ACCEPTED / CHANGES_REQUESTED / BLOCKED (for a review reply)
Behavior assessed or implemented:
Evidence: source location or command, exit status, result file
Unverified scope / required correction:
Next request and reply target:
```

Do not execute a repeated request for the same task as a new job. Check progress-board state and earlier results first. If terminal output is truncated, read `recent-unwrapped`; if that still omits content, request a file-backed report and read it. An `idle`, `done`, or `unknown` panel state is neither test success nor review acceptance.

## 4. Repeated development procedure

1. The implementation owner sets prerequisites, changed files, and success/failure checks for the current phase. Start with review of AGENTS.md and development contracts.
2. Send the reviewer a concrete task ID, revision, files/diff, expected behavior, verification evidence, questions, and reply target. Continue independent work.
3. The reviewer returns the reviewed revision, ACCEPTED/CHANGES_REQUESTED/BLOCKED, evidence, and corrections. Ask the owner directly for missing evidence or explanation.
4. The implementation owner fixes product/tests, runs affected checks, and reports the disposition of every finding or reason for disagreement. **Send the revised revision and evidence back for re-review.**
5. The reviewer checks the disposition and changed scope and responds. Repeat steps 2–5 for remaining findings; the author cannot self-close them.
6. After acceptance of that revision and required checks, commit/push and update the progress board. Then send the next concrete implementation or review request. The user need not relay messages or approve each phase.

```text
Owner: review request → reviewer: findings/questions
Owner: corrections and checks → reviewer: changed-revision review
             ↑ repeat when needed ↓
Review acceptance + checks → commit/push → next task and review request
```

Review decisions are collaboration statuses, separate from test PASS. Material changes after review require renewed acceptance for the affected scope. Silence, timeout, or idle never means acceptance. Use task/review IDs to prevent duplicate execution and empty acknowledgement loops. Do not have both panels wait with `--wait`. Continue independent work during review delay. Resolve technical disagreements with evidence/tests, asking the user only for ambiguous product intent.

### Commits and pushes during development

The user explicitly authorized incremental commits and pushes to [hjhun/capability-manager](https://github.com/hjhun/capability-manager). An older instruction forbidding remote pushes was withdrawn for this repository.

- At start, inspect checkout, remote, current branch, and remote default branch. On 2026-09-27, remote `main` had initial history and an Apache-2.0 `LICENSE`; recheck before Git work.
- If no checkout exists, preserve current documents and establish work from remote history. Do not erase the LICENSE/remote commits or replace them with an unrelated root history.
- Read the available `git-commit` skill and stage only intended files. For each meaningful reviewed and verified change set, use an English commit, push, and confirm the remote ref.
- Make small verifiable checkpoints for initial documents, build foundation, parser/DB, queries/search, launcher, mounts, and integration tools. Do not defer all pushes to the final phase or call a document commit product test PASS.
- Use the remote default branch or appropriate tracking branch subject to protection policy. If direct push is restricted, publish a work branch and follow the required review process. Never force-push or rewrite published history.
- Fetch remote changes and integrate while preserving local and remote work. On authentication/network failure, preserve local commits, list unpushed changes, and report the actual failure. Do not ask for renewed permission for ordinary authorized commits/pushes.
- Preserve the Apache-2.0 LICENSE; align SPDX in new project-owned code and the RPM License field. Preserve third-party notices. Publish English technical documentation; Korean coordination prompts may remain available for the user.
- Exclude generated builds, raw device logs, and credentials from tracking. In shareable verification summaries, include reproduction and outcome and convert personal absolute paths to portable references.
- Link phases and verification evidence to commit, branch, push success/failure, and remote confirmation on the progress board. Do not call a commit published when it is absent from the remote.

For long work or a new session, resume from `08-progress.md`: last completed phase, remaining gates, evidence locations, and next action. Do not convert a failed verification to PASS from a conversation summary.

## 5. Clarification and blockage boundaries

- Ask the user only about ambiguous product meaning such as the exact App Skill suffix. PATH-01 does not halt independent DB or CLI work.
- Record ABI, framing, locking, and mock-boundary implementation decisions from platform source/tests and peer review. Do not turn every gate into a user-approval request.
- If an environment dependency is missing, record exactly what is absent, which phase it blocks, and continue independent work. Ask only for actually needed authentication/privileges.
- Ordinary commits/pushes to the approved repository are included. Changes or pushes to unrelated repositories, deletion of operational data, and closing another user's panel are outside scope. Preserve existing work.

## 6. Completion assessment

Perform the actual P09 ARMv7l build only after P01–P08 functionality, tests, and tools are complete. The final report separately covers implementation by requirement, unit tests, emulator integration, real-device execution status, ARM build, and ARM execution status.

The reviewer compares residual defects to evidence; the implementation owner submits the final report. Report mandatory checks blocked by the environment as `BLOCKED` or `NOT_RUN`, never as complete. An optional real-device test not run is different from a failed mandatory ARM build.

A separate peer message consisting of `[완료]` may signal closure of a reviewed assignment. This project-specific collaboration signal is not test evidence. A quoted example or code containing the string is not an actual closure request. Direct user instructions to stop or change scope take precedence.

## 7. Work boundary at original documentation time

The initial documentation work inspected reference source and the emulator read-only and wrote/validated documentation. It did not perform actual two-panel Herdr communication, CapMgr build, installation, or product tests. The workflow above was to be verified in the development session.
