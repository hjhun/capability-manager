# Capability Manager agent instructions

## Start here

Read this file and applicable parent instructions before editing. Then read
`docx/README.md`, `docx/01-requirements.md`,
`docx/07-decisions-and-open-items.md`, and the contracts relevant to the task.
The current project is documented but not implemented. Do not interpret a plan,
fixture description, or reference-project result as completed CapMgr work.

Before product implementation, the implementation owner and independent reviewer
must review this instruction baseline, the contracts, and the phase plan. If this
file is missing in another checkout, create it from the current agreed documents
first. If it exists, preserve it and propose targeted updates rather than replacing
it wholesale. Resolve instruction or contract changes through the same review loop
as code. Keep this file concise; detailed contracts belong in `docx/`.

## Product constraints

- Implement in C++20 with a public `capmgr_` C API, RPM packaging, and TIDL IPC.
  Follow verified Tizen Watcher conventions; keep private writer headers private.
- `client_create` prepares authorization and read-only DB access. Resource mounts
  require an explicit remount request with the caller's destination path.
- Catalog enumeration, search, and detail queries read the local SQLite catalog
  without per-query IPC or loading the whole catalog into memory.
- Skill/App Skill/CLI metadata parsers write directly through the private DB layer,
  including MIC without AMD. The Capability AMD module handles maintenance and
  imports Action data from the existing Action DB. Do not reparse Action metadata.
- Publish catalog and FTS changes together before notifying clients. Keep Action
  execution internals out of public detail, while retaining schema types, required
  Entity information, provider app IDs, and the default provider app ID.
- Return skill directories for the agent to read. Preserve the agreed ownership
  rules and allow same-named App Skills from different apps. Resolve PATH-01 before
  fixing the ambiguous App Skill postfix layout.
- Use English FTS5/BM25 search with at most five relevant results and no embeddings.
- Launch registered CLI executables as `app_fw`, with `--json` followed by the full
  JSON-RPC request as one argv element. Handle stdout and stderr independently.
- Enforce platform privilege and actual file/namespace access controls. Preserve
  native tool errors and the agreed cancellation and resource-limit contracts.
- Consider 32-bit ABI and arithmetic from the beginning. Run the actual ARMv7l
  build only in P09 after functional development and primary-environment validation.

## Two-panel ownership

The implementation/integration owner manages code changes, device operations,
integration, the progress board, staging, commits, and pushes. The independent
reviewer reads and reports findings unless explicitly assigned disjoint files.
Never edit the same files or operate on the same device fixture concurrently.
Never stage another panel's unfinished changes without coordinating ownership.

The user has authorized task-related requests, reviews, and replies between the
two selected Codex panels. Use the installed Herdr skill and live CLI help. Verify
`HERDR_ENV=1` before Herdr control; discover the actual peer and reply target.
Do not guess panel IDs, control an external session, or create extra panels by
default. Preserve focus. Do not use `--wait` on peer-to-peer requests.

## Required review loop

1. Assign a task ID, phase, requirement IDs, owner, file scope, and review revision.
2. Send the other panel a concrete review request with the changed files/diff,
   expected behavior, test evidence, open questions, and an explicit reply target.
3. The reviewer returns `ACCEPTED`, `CHANGES_REQUESTED`, or `BLOCKED`, with the
   reviewed revision and actionable findings. These are collaboration statuses,
   not product test results.
4. Apply fixes or provide a reasoned response to each finding; rerun the affected
   checks. Send the changed revision and evidence back to the reviewer explicitly.
   The reviewer must confirm the disposition; the author cannot self-close an
   unresolved finding merely by writing that it was fixed.
5. Repeat while relevant findings remain. A materially changed diff invalidates
   acceptance for the affected scope. Silence, a timeout, or an idle panel never
   counts as acceptance. Continue independent work while review is pending.
6. After acceptance and the required checks, commit and push the reviewed scope,
   record the result, and send the next concrete implementation/review request.
   Do not require the user to relay each message or say "continue" at each phase.

Use task/review IDs and explicit replies to avoid duplicate execution and empty
acknowledgement loops. Either panel may request additional evidence or a focused
review from its peer. Route requests to the actual owner and include the requested
action. Resolve technical disagreements with source evidence and tests; ask the
user only when product intent is ambiguous or an actual external decision is needed.

## Implementation and validation

Use the global `hjhun-coding-style` skill under `~/.codex/skills/` for C/C++,
CMake/configuration and RPM work. The repository `.clang-format` records the
Watcher-derived Google/2-space layout. Preserve C++20, the public C ABI, explicit
target/install paths and fail-closed contracts. Keep mechanical formatting,
build/package restructuring and behavior changes in separate review checkpoints.
Do not copy Watcher's service activation or platform policy as a style change.

Follow P00–P09 in `docx/04-implementation-plan.md` and the acceptance matrix in
`docx/05-verification.md`. Establish Google Test/Google Mock, CTest/RPM checks, and a
pure C consumer early. Use actual temporary SQLite databases for transaction/FTS
tests and mocks for external adapters. Build reusable integration, smoke, and
performance tools for the emulator and real devices.

Recheck the device and toolchain before using them. Missing PATH entries do not
prove packages are absent. Distinguish host builds, emulator-native builds, mock
tests, package creation, device execution, ARM builds, and ARM runtime tests.
Record commands, exit status, target, revision, evidence paths, and cleanup results
in `docx/08-progress.md`. Mark unexecuted checks `NOT_RUN` or `BLOCKED`; do not claim
success from a panel's lifecycle state. Do not invent currently runnable targets
or test commands before the build system exists.

## Repository, license, and publication

The authorized development repository is
`https://github.com/hjhun/capability-manager`; its license is Apache-2.0.
The user has explicitly authorized incremental commits and pushes during development.
The implementation owner should use the installed git-commit skill and make small,
meaningful English commits after review and relevant checks. Push each checkpoint
and verify the remote ref; do not defer all publication to the final phase.

Preserve the remote's initial history and LICENSE. Inspect the checkout, remotes,
branches, and existing changes before Git operations. If a checkout is missing,
preserve existing documents while establishing work from the remote history.
Do not force-push or rewrite published history. Follow branch protection and use
a work branch when direct publication is restricted. Preserve local commits and
report unpushed work if authentication or network access fails.

Preserve third-party notices and apply Apache-2.0 identifiers to new project-owned
code and packaging. Keep published technical documentation and commit messages in
English; Korean coordination prompts may remain available for the user. Exclude
credentials, raw device logs, generated binaries, and temporary evidence from Git.
Retain portable verification summaries and meaningful source/documentation changes.

## Handoff and completion

Update the progress board and decision records when contracts or implementation
change. On handoff, state the last accepted revision, open findings, pending review
requests, next action, test state, and commit/push state. Preserve existing work and
operational data. Changes to unrelated repositories are outside this task.

Conclude only with evidence for the required implementation and checks, or a precise
remaining blocker. Final review, Git publication, and product completion are
separate facts. A separate peer message `[완료]` may close a reviewed assignment;
quoted examples do not. The user's direct stop or scope-change instruction takes
precedence over the workflow.
