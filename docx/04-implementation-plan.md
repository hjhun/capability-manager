# Phased implementation plan

Each phase follows prerequisites → work → deliverables → verification → completion gate. This is a development plan for work not yet implemented; it does not presume any command succeeded. Update these documents with actual CMake targets, RPM package names, and CLI options once implemented.

## Phase dependencies

```text
P00 AGENTS.md, contract, and environment review → P01 build and test foundation
                                           ├→ P02 DB and parsers → P03 queries, search, Action sync
                                           └→ P04 CLI, execution, cancellation
P02 + path/privilege contract → P05 mount helper
P03 + P04 + P05 → P06 public API, TIDL, RPM integration
P06 → P07 emulator integration and smoke → P08 stabilization and performance
P08 functional completion → P09 final ARMv7l build and verification → final decision
```

P02 and P04 may proceed in parallel after P00 aligns common API, identity, and error contracts. Only one owner edits shared headers, schemas, and top-level build files.

For each verifiable change set, the implementation/integration owner makes an English commit and pushes to the authorized GitHub repository. Split large phases into smaller checkpoints. Preserve remote history and the Apache-2.0 LICENSE, and link commit/push results to verification evidence on the progress board. Follow the [Herdr workflow](06-herdr-workflow.md) for detailed commit and push rules.

## P00 — Contract and environment review

- Input: R01–R18, existing reference source, and connected target-environment information.
- First task: read applicable parent instructions and root AGENTS.md. Create it first if absent; otherwise preserve it and make targeted updates. Send instructions, roles, review procedure, and development contracts to the other panel. Resolve changes and obtain re-review before product implementation.
- Repository: establish/check checkout and branch from the history at `https://github.com/hjhun/capability-manager`. Preserve current documents and remote LICENSE; commit/push the initial document checkpoint after review.
- Work: review API-01/SCHEMA-01/RPC-01/EVENT-01 and record development decisions. Clarify the user's PATH-01 wording. Inspect AUL creator GoogleTest/GoogleMock, Watcher RPM/TIDL, Action DB/event paths, and installer completion/recovery.
- Environment: inspect emulator OS, architecture, C++ compiler, CMake, RPM, pkg-config, TIDL, SQLite FTS5, and test dependencies. Record target and repository paths. Do not build for ARMv7 yet.
- Deliverables: AGENTS.md reviewed by both panels, finalized contract draft, environment report, actionable next build plan, and method for a preimplementation baseline measurement. Record review request, response revision, and decision.
- Gate: do not silently decide ambiguous product meaning. Distinguish missing installed tools from a PATH issue. Hold only the blocked phase and continue independent work.

## P01 — C++20/C API/RPM/test foundation

- Input: R01/R16 and the P00 build environment.
- Work: establish Watcher-style structure, CMake and RPM basics, a public C header/C++ implementation boundary, and TIDL code generation.
- License: align Apache-2.0 LICENSE, SPDX identifiers in new project-owned code, and the RPM License field; preserve third-party notices.
- Test foundation: integrate Google Test/Google Mock and CTest. Create dependency-injection seams and fake adapters following actual AUL test precedent.
- Deliverables: minimum library and test targets, pure C consumer, RPM check path, and tests runnable without external services.
- Verification: passing empty tests is insufficient. Include tests of minimal real behavior such as read-only client access and error returns. Record pure C consumer compilation/link evidence.
- Gate: C++20 actually applies, public headers work from C, build-stage tests execute, and failures propagate.

## P02 — DB, metadata, and ownership

- Input: R03/R05/R06/R14 plus schema and installer contracts.
- Work: private DB library, schema version, WAL, FTS transaction, parser prefix registration, repeated keys and semicolons, package ownership, update/removal.
- MIC: provide a test path for DB bootstrap and registration without a service.
- Deliverables: DB/FTS/schema code, parser plugin, manifest/definition fixtures, offline test harness.
- Verification: installation rejects conflicts with another owner; same-owner updates and same-named App Skills in different apps work; mixed metadata, concurrent writers, rollback on failure, and replay work.
- Gate: there is no Action metadata reparsing path. Do not claim normal completion while installer finalization/rollback remains unverified.

## P03 — Search, public queries, and Action synchronization

- Input: P02 and R02/R04/R08/R09.
- Work: local read-only foreach/search/get, top-five search, common and kind-specific projection, Entity closure, and Action DB read adapter.
- Synchronization: startup import, change events after source commit, client notification after Cap DB+FTS commit, and reconnection resynchronization.
- Deliverables: representative English query corpus, expected relevant IDs, Action fixtures, and a way to count RPC calls.
- Verification: no per-query IPC; no internal fields in public detail; preserve schema `type`; include provider app ID list/default ID; fail on unknown/missing Entity; keep search/detail consistent across updates.
- Gate: do not assume notification integration exists. Test the actual change writer through the receiving adapter.

## P04 — CLI execution, cancellation, and errors

- Input: R10/R11/R12/R15 plus RPC and CLI contracts.
- Work: `app_fw` systemd launcher, TIDL server, registered executable binding, single argv after `--json`, concurrent drain of both pipes, bounded output, request state machine, and owned process cleanup.
- Deliverables: fixture CLIs for success, error, timeout, large output, forked child, and invalid JSON, plus execution test tools.
- Verification: responses from stdout and stderr, partial reads, duplicate/conflicting responses, native-error preservation, cancellation/completion races, client disconnect, limit overflow, and no impact on another client's job.
- Gate: no shell command assembly, no duplicate terminal result, and no false success for unsupported Action cancellation.

## P05 — Resource mounts and paths

- Input: resolved PATH-01/MOUNT-01, P02, and R07/R13.
- Work: separate helper/TIDL, target-namespace validation, caller destination, source-resource adapter, read and required execute permission, remount state and lifetime.
- Deliverables: package/app Skill fixtures, mount-namespace integration harness, and failure cleanup path.
- Verification: create alone does not mount Skills; explicit first remount works; two clients can use different roots; same-named App Skills remain distinct; deny unauthorized access; refresh query after update/removal; inspect state after failed remount.
- Gate: the returned path must allow the Agent to read `SKILL.md` and relative resources with its actual privileges. A successful mount syscall alone is insufficient.

## P06 — Integration, packaging, and development tools

- Input: P03/P04/P05.
- Work: connect public C API to TIDL callbacks, AMD module init/fini, service/unit/plugin-info installation, RPM dependencies and devel split.
- Required tools: `capmgr-tool` (proposed name) to check catalog search/detail, execution/cancellation, and explicit remount; environment preflight, fixture installation/removal, test runner, and evidence collector.
- Tool contract: select target explicitly and separate preflight/smoke/integration/perf modes. Report results and exit status, and clean only owned fixtures and requests. Document actual options after implementation.
- Gate: confirm RPM/service lifecycle failure paths as well as success, and execute the C consumer.

## P07 — Emulator integration and smoke

- Input: P06 RPMs, tools, fixtures, and one unambiguous target.
- Work: build C++20 and run build-stage tests inside the emulator; verify RPMs and dependencies before installation/execution. Exercise package install/update/uninstall and functional calls end to end.
- Tests: connect real SQLite, TIDL/Cynara, AMD module, `app_fw` launcher, and resource namespace beyond unit/mocks. Use isolated fixture state without damaging the operational DB.
- Deliverables: commands, exit codes, target-environment details, logs, result files, and cleanup outcomes.
- Gate: actual fixture execution, events, and privilege results are required. SDB connectivity or RPM creation alone does not prove integration success.

## P08 — Stabilization, performance, and real-device verification

- Input: working P07 functions.
- Work: cold/warm search, top-five relevance, memory/RSS, bounded output/concurrency, repeated create/destroy, long-running WAL/checkpoint, service restart/resync, and failure-recovery measurements.
- Real device: use the same smoke/integration tools on an accessible target. Mark this NOT_RUN if no real device is available.
- Deliverables: baseline/results with repetitions, device, and data size; reasons for tuning settings; residual risks.
- Gate: do not call weights or initial resource values optimal without measurements. The integration owner confirms functional, tool, and test completion before P09.

## P09 — Final ARMv7l build and 32-bit verification

- Prerequisite: all functional development and primary-environment verification through P08 are complete. Do not advance the real ARMv7 build earlier than the user requested.
- Work: build C++20/C API/RPM using an official 32-bit ARMv7 build profile. Check sizeof, integer/pointer casts, formats, overflow, alignment, and dynamic-library ABI.
- Verification: ELF/RPM architecture and C consumer compile/link, plus unit/smoke/integration runs on an ARMv7 target where possible. Record cross-build success separately from target execution success.
- Gate: fix and rerun after final build failure. If no target execution environment exists, leave that verification incomplete. Report evidence by requirement and remaining blockers.

## Phase work card

Connect any additional task in a phase to the progress board using this form:

```text
Task ID / phase / requirement IDs:
Prerequisites and decision revision:
Owner / writable files:
Expected successful and failing behavior:
Deliverables:
Executed verification and exit code:
Evidence path / target environment:
Unverified or blocked scope:
Reviewer / integration outcome / next task:
Review request ID / decision / open findings / revision for re-review:
```
