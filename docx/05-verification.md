# Capability Manager verification plan and evidence standard

Document status: **preimplementation verification plan**. This work performed documentation and read-only environment inspection only. Capability Manager tests, builds, RPM creation or installation, service changes, mounts, and ARM builds are all **NOT_RUN**. The test/tool names below are future deliverables, not tools already runnable.

Requirement IDs refer to [R01–R18](01-requirements.md), responsibilities to the [architecture](02-architecture.md), P00–P09 to the [implementation plan](04-implementation-plan.md), open gates to the [decision record](07-decisions-and-open-items.md), and actual result status to the [progress board](08-progress.md). Phase reports distinguish executed verification from work not yet run.

## 1. Environment observed so far

Observation date: 2026-09-27. These are connectivity and tool observations, not product acceptance results.

| Target | Read-only observation | Interpretation |
|---|---|---|
| Work host | Ubuntu 24.04.4 LTS, WSL2 x86_64 | Candidate build host |
| Host tools | GBS 2.0.6, RPM/rpmbuild 4.18.2, CMake 3.28.3, GCC C++ 13.3.0, pkg-config path exists | Tizen target dependencies and GBS profile remain unverified |
| SDB | Absent from PATH search; both Linux Tizen Studio `tools/sdb` and Windows Tizen Studio `tools/sdb.exe` candidates succeeded | Select an explicit executable path on later runs |
| Connected target | Both SDB candidates reported one `emulator-26101`, `device`, `calendar-resolution-p5` | One unambiguous emulator at observation time |
| Device OS/architecture | Tizen 10.1.0 Unified, Linux 4.4.35-x86_64, x86_64 | Not ARMv7l verification |
| Device build ID | `tizen-10.1-unified_20260905.101134_tizen-headed-emulator64-wayland` | Image identifier to record with results |
| Device runtime tools | `/usr/bin/rpm`, `/usr/bin/pkg-config`, `/usr/bin/sqlite3`, `/usr/bin/gdb` | Inspection tools present |
| Device versions | RPM 4.14.1, pkg-config 0.29.2, SQLite 3.50.2 (64-bit) | FTS compile options, permissions, and real WAL behavior remain unverified |
| Device native build tools | `g++`, `c++`, `gcc`, `cmake`, `make`, `ninja`, `rpmbuild`, `tidlc` were not found in the inspected PATH | Native development is required by the plan but not yet prepared |
| Device development pkg-config queries | No modversion output for `sqlite3`, `rpc-port`, `pkgmgr-parser`, `pkgmgr-info`, `gtest`, `gmock` | Development metadata was not confirmed in that query environment; this does not prove all runtime packages are absent |

The observations used only `sdb devices`, and `uname`, `os-release`, `command -v`, and version queries on the one selected device. No host/device files were transferred or packages installed. Recheck SDB connection before implementation; do not guess a serial when multiple targets appear.

### 1.1 Read-only commands and evidence location

No separate log file was generated. The durable record of this inspection is the table above and extracts below; raw output appeared in the conversation in which the commands ran. These are the actual read-only command shapes, expressed with portable tool-location variables, and must not be reused as evidence of a future product run.

Host PATH and version queries:

```bash
command -v sdb
command -v gbs
command -v rpmbuild
command -v cmake
command -v ninja
command -v c++
uname -srm
cat /etc/os-release
gbs --version
rpmbuild --version
cmake --version
c++ --version
command -v pkg-config
command -v sqlite3
```

`command -v sdb` produced no output. Executable checks covered the Linux Tizen Studio `tools/sdb`, Windows Tizen Studio `tools/sdb.exe`, and two other candidate install locations. Both existing executables were called with `devices`. In the environment inspected, `TIZEN_STUDIO_ROOT` identified the Linux Tizen Studio installation and `TIZEN_STUDIO_WINDOWS_ROOT` its Windows installation:

```text
SDB_CANDIDATE=${TIZEN_STUDIO_ROOT}/tools/sdb
List of devices attached
emulator-26101    device    calendar-resolution-p5
SDB_CANDIDATE=${TIZEN_STUDIO_WINDOWS_ROOT}/tools/sdb.exe
List of devices attached
emulator-26101    device    calendar-resolution-p5
```

The selected-target query, with the observed Linux SDB location represented portably:

```bash
"${TIZEN_STUDIO_ROOT}/tools/sdb" -s emulator-26101 shell 'uname -srm; cat /etc/os-release; for capmgr_tool in g++ c++ gcc cmake make ninja rpmbuild rpm pkg-config sqlite3 tidlc gdb; do command -v "$capmgr_tool"; done; if command -v rpm >/dev/null 2>&1; then rpm --version; fi; if command -v pkg-config >/dev/null 2>&1; then pkg-config --version; for capmgr_pkg in sqlite3 rpc-port pkgmgr-parser pkgmgr-info gtest gmock; do pkg-config --modversion "$capmgr_pkg" 2>/dev/null; done; fi; if command -v sqlite3 >/dev/null 2>&1; then sqlite3 --version; fi'
```

Selected output:

```text
Linux 4.4.35-x86_64 x86_64
PRETTY_NAME="Tizen 10.1.0 (Tizen10.1/Unified)"
BUILD_ID=tizen-10.1-unified_20260905.101134_tizen-headed-emulator64-wayland
/usr/bin/rpm
/usr/bin/pkg-config
/usr/bin/sqlite3
/usr/bin/gdb
RPM version 4.14.1
0.29.2
3.50.2 2025-06-28 14:00:48 ... (64-bit)
```

The SQLite source hash is omitted from this extract. Since stderr from `pkg-config --modversion` was suppressed, missing output does not identify why development metadata was unavailable. Likewise, empty `command -v` output proves only absence from that PATH.

## 2. Build-stage unit test precedent

The AUL repository instructions and an external cached graph were inspected read-only, followed by source verification. The graphify CLI was not found in PATH, so only existing graph JSON fixture nodes/adjacency were read. No graph was created or updated.

Set `TIZEN_SOURCE_ROOT` to the root of the Tizen platform source tree when following these references.

| Verified precedent | Source evidence |
|---|---|
| Test activation and target separation | `${TIZEN_SOURCE_ROOT}/platform/core/appfw/aul-1/CMakeLists.txt:43` (`ENABLE_TESTING`, unit/app-control targets) |
| GoogleMock build dependency | Same file `:56`, `packaging/aul.spec:28` (`pkgconfig(gmock)`) |
| Test/mock source collection and linking | `test/unit_tests/CMakeLists.txt:1`, `:18`, `:36` |
| CTest registration | `test/unit_tests/CMakeLists.txt:43` (`ADD_TEST`) |
| Run during RPM build | `packaging/aul.spec:187` (`%check`), `:189` (`ctest -V`) |
| GoogleTest runner | `test/unit_tests/main.cc:21` onward (`InitGoogleTest`, `RUN_ALL_TESTS`) |
| Real mock and fixture use | `test/unit_tests/mock/test_fixture.h:29`, `test_launch.cc:44` onward (`NiceMock`, `TEST_F`, `EXPECT_CALL`) |

No file or symbol named `creator` was found in the inspected `src`, `test`, major CMake, and spec ranges. Recheck the precise creator location the user meant. The actual GoogleTest/GoogleMock build structure above is verified and can guide CapMgr's test setup.

In CapMgr, separate external OS/TIDL/launcher/pkgmgr/Action-source effects behind interfaces and mock them. Test SQLite transaction and FTS behavior using **real temporary DBs**. Test invariants, failures, races, and boundaries instead of mirroring implementation lines. CTest failure at build stage must fail RPM verification. [R16]

## 3. Required verification-tool deliverables

Names are proposals; the development plan assigns owners and final command interfaces. These tools do not expand the public C API. [R17]

| Tool | Minimum function | Required output |
|---|---|---|
| Integration runner | Combine parser/offline writer/Action import/events/launcher/helper, inject failures, clean fixture packages | Per-scenario PASS/FAIL/NOT_RUN, cause, exit code, log path |
| Emulator smoke runner | Allowed caller create → query → explicit remount → CLI execution/cancel → destroy; quick Action import/changed-event check | OS/architecture/RPM/fixture IDs, API outcomes, process/callback cleanup evidence |
| Performance runner | Cold/warm create/search/get/remount/CLI/Action-import timing under concurrent clients | p50/p95/max including failures/timeouts, CPU/RSS/I/O/DB-WAL changes |
| Fixture builder/validator | Valid/invalid data of all four kinds, RPK CLI `--json`, same-named App Skills, Entity references, invalid JSON, large-output/delayed/forking CLIs | Input specs, package manifest/content list, generated-result validation |

Tools must act only on the named target and fixture scope and record target details before execution. Use synthetic data for raw payload evidence, never real user information or secrets. Do not initialize by deleting the entire operational DB.

## 4. Verification gates by phase

| Verification group | Meaningful verification | Condition to proceed | Current status |
|---|---|---|---|
| V0 environment/contract | Discover target, native build tools/dependencies, AMD module ABI, TIDL compiler, RPM paths, open contracts | Identify execution location and path to tools/dependencies | Read-only environment observation only; product checks NOT_RUN |
| V1 build skeleton | C++20 CMake, C compilation of ABI header, symbol exports, TIDL generation, GTest/GMock and RPM `%check` | x86_64 build and meaningful basic unit tests pass | NOT_RUN |
| V2 parser/catalog | Direct writer, MIC/offline, ownership/repeated metadata, DB/FTS atomicity, migration/recovery failure injection | Related unit/integration tests pass; publication/recovery gates stated | NOT_RUN |
| V3 query/resources | Local read-only queries, scope/availability, Skill path and explicit remount, DAC/SMACK | No query IPC; namespace and privilege behavior verified | NOT_RUN |
| V4 execution/sync | CLI launcher, JSON/native errors, cancel/destroy, Action import/Entity/provider projection and event ordering | Cross-process integration and race tests pass | NOT_RUN |
| V5 emulator acceptance | Native device build/RPM path, deployment, integration/smoke/performance, repeated lifecycle | Actual device logs and measurements obtained | NOT_RUN |
| V6 final ARMv7l | After all functions complete, 32-bit ARM build, ABI/link/RPM checks, possible runtime checks | ARM evidence and unverified runtime scope recorded | NOT_RUN |

V0 read-only observations do not substitute for V1 onward. V0–V6 name verification groups, not development phases. The **real ARMv7l build is only in P09 after P08 functional completion**; earlier phases review portable types, overflow, alignment, atomic-operation dependencies, and C ABI design without an early ARM build. [R18]

## 5. Acceptance tests by requirement

Every test in this table is planned and **currently NOT_RUN**.

| ID | Test | Observable passing evidence | Status |
|---|---|---|---|
| R01 | C++20, C API, CMake, RPM, TIDL | C caller compile/link, exported symbols/public headers, generated proxy/stub regeneration, runtime/devel file lists | NOT_RUN |
| R02 | Four-kind catalog and local read-only queries | Foreach/search/get agree on all four fixtures, writes denied, query IPC count zero, create starts no import/mount/execute | NOT_RUN |
| R03 | Direct registration by online/MIC parser | Same private writer creates/updates DB in a fixture environment without AMD/launcher; registration without boot services | NOT_RUN |
| R04 | Startup Action import and change sync | Source commit → CapDB/FTS commit → client changed; restart/missed/duplicate-event reconciliation; no CapMgr writes to source DB | NOT_RUN |
| R05 | Repeated metadata and semicolons | Shared collection rule, whitespace/empty entries/duplicate paths/bad files, no partial package registration | NOT_RUN |
| R06 | Ownership and App Skill scope | Cross-owner Skill/CLI conflict rejects installation; same-owner update succeeds; same-named App Skills from distinct apps both query | NOT_RUN |
| R07 | Skill path and explicit remount | No mount immediately after create; first explicit remount succeeds; Agent reads SKILL.md/assets in its namespace; requery after update/removal | NOT_RUN |
| R08 | Action projection, Entity, providers | Top-level internal type/details hidden, nested schema type retained, transitive Entity bundle, only provider app IDs/default ID | NOT_RUN |
| R09 | English FTS/BM25 | At most top five; exact ID/name and English normalization separated; known relevance corpus ranking; ties/empty/no match/filter/limit; no embedding calls | NOT_RUN |
| R10 | CLI JSON launch and `app_fw` | systemd/TIDL path and process identity; entire JSON-RPC request as one argv after `--json`; no shell; JSON from either output stream | NOT_RUN |
| R11 | Cancel and destroy | CLI process tree cleaned, Action subscription ended, ordinary Action cancel unsupported, no callback after destroy or duplicate terminal result in races | NOT_RUN |
| R12 | Errors and JSON-RPC | Synchronous admission vs asynchronous result, ID mapping, native error/data intact, exit/signal retained, invalid JSON handled | NOT_RUN |
| R13 | Platform privilege and DAC/SMACK | Allowed/denied callers, TIDL Cynara denial, direct DB/WAL/SHM/resource reads, caller-provided app-ID spoof rejected | NOT_RUN |
| R14 | WAL, migration, recovery | Concurrent reads/writes, catalog/FTS atomicity, pending hidden, valid generation retained, failure/restart/replay/disk-full recovery | NOT_RUN |
| R15 | Resource limits | Boundary tests below and actual RSS/CPU measurements; no unbounded output/jobs/DB retry | NOT_RUN |
| R16 | GTest/GMock build integration | Meaningful unit tests registered in CTest, mocked failure paths, `%check` failure propagates to build result | NOT_RUN |
| R17 | Emulator integration/smoke/perf tools | Reproducible runner connects target/RPM/fixture, commands, exit result, logs, including performance failures | NOT_RUN |
| R18 | Final 32-bit ARMv7l verification | Final-phase ARM build, ELF/ABI/link/RPM architecture; device execution status separate | NOT_RUN |

### 5.1 DB, installation, and recovery failure injection

Use fixture DBs and test packages, not the operational DB.

- Test repeated metadata callbacks, failure of the last callback, and failure during catalog update: valid generation and FTS must not diverge.
- Test later installer-step failure after install/update/delete, process crash, and reboot for pending/valid-generation recovery.
- Include uninstall failure paths where plugin UNDO is not called. Testing only a path that invokes UNDO does not prove complete recovery.
- Distinguish DB commit/I/O failure during CLEAN, crashes before/after cleanup, and replay of the same operation.
- Test online and MIC/offline completion separately. Record the currently unconfirmed finalization hook as a blocker to the applicable acceptance test.
- Include stale and long-lived readers, competing writers, database busy, disk full, WAL/SHM recreation, and schema mismatch.
- Separate the fixture actor that writes the Action source DB from the CapMgr importer to prove the importer's read-only boundary.

### 5.2 Execution and boundary values

Initial settings precede measurements; test **below, at, and above** each limit.

| Initial setting | Required boundary tests | Additional observation |
|---|---|---|
| Search max 5 | 0/1/5/6 matching items; requested limit and filter combinations | Maximum, relevance exclusion, ranking, ties, allocation |
| CLI 2 per client / 4 overall | Three jobs in one client, five jobs across clients, completion and new admission together | Defined reject/wait policy and counter leaks |
| One-shot default 30 seconds | Normal completion, just under limit, over limit, completion/cancel race | Timeout result and child cleanup; do not automatically apply to Action subscriptions |
| JSON argv 64 KiB | UTF-8 byte boundary, spaces/quotes/newlines/Unicode, OS exec limit | Whole request stays one argv element; no truncation |
| Combined output 1 MiB | stdout only, stderr only, both together, concurrent burst, exit just after overflow | Bounded memory, defined overflow error, native-error preservation scope |
| DB cache 1 MiB per connection | One/multiple connections, small/large catalogs, cold/warm | Measure RSS separately from cache setting |

Build CLI fixtures where both streams contain JSON, one has invalid JSON, multiple documents appear, output is empty, IDs differ, and native error accompanies nonzero exit. Do not freeze arbitrary expected results before the precedence/framing contract is decided.

Also measure Entity-bundle size, per-job output, callback latency, and aggregate page cache across clients. Judge memory using observed RSS and allocation lifetime, not only the configured cache.

## 6. Emulator-native development, RPM, and integration path

The user reports being able to develop inside the connected emulator. This is a **required** path in the plan. Since native build tools were not found in the inspected PATH, clear the following gate first. No environment installation, source transfer, build, RPM installation, or service start was done during this documentation work.

1. **Reconfirm target:** Recheck the SDB path and one target. Record image, architecture, user context, free space, and needed privileges. Connectivity alone does not prove compiler/devel packages are ready.
2. **Prepare native build:** Check for C++20 compiler, CMake/build tools, rpmbuild, tidlc, headers/pkg-config, GTest/GMock, and SQLite FTS inside the device. Verify repository and real package names before setting up the environment in a development phase. Do not invent installation commands now.
3. **Finalize actual build recipe:** Inspect implemented CMake targets, RPM spec, TIDL prebuild, dependencies, and working directory; produce reproducible on-device build/test/package commands. Do not report placeholder commands or host-tool presence as native build success.
4. **Native build/unit/RPM creation:** Run unit tests and RPM `%check` inside the emulator and verify runtime/devel/test RPM outputs. Distinguish RPM creation from installation.
5. **Device integration:** After test RPM installation, verify AMD module loading, parser/MIC paths, launcher identity, public API privilege, namespace/remount, Action import, and changed events. Record installed version, process, and DB state before/after.
6. **Smoke, integration, performance:** Start with short smoke, then functional/failure/race scenarios, then cold/warm measurements. Include failures and timeouts. Clean only test resources and verify cleanup outcomes.

Host GBS/RPM builds can supplement this path, but do not claim emulator-native build complete from a host build alone. If native setup cannot be obtained, record why, which steps were not run, the actual alternative, and whether requirements are met. Do not use a real ARMv7l target/profile build before P09, after P08 function completion.

## 7. Performance measurements and evidence

R15 initial limits are not proof of performance. Establish a baseline first and compare later changes under the same fixture, image, and load.

- Intervals: create, filtered foreach, FTS search, get plus Entity bundle, explicit remount, CLI admission → start → exit, Action source commit → CapDB commit → changed event.
- Cases: cold/warm, single/multiple clients, success/failure/cancel/timeout, small/large catalog, ordinary and near-limit CLI output.
- Measures: wall-time p50/p95/max, CPU, RSS/PSS (report availability), page faults/I/O, DB/WAL size, concurrent CLI count, callback count/latency.
- Interpretation: do not extrapolate x86_64 emulator figures to ARMv7l or describe a 1 MiB page cache as an RSS limit.
- Evidence: fixture ID, source revision once a repository exists, build/RPM ID, image/architecture, tool versions, exact command, exit status, and log/raw measurement paths.

A suggested evidence directory is `artifacts/verification/<run-id>/`: generated temporary output, separate from source and excluded from tracking under the chosen policy. This documentation work did not create it. The durable record of the earlier read-only observation is section 1.1; no product-verification run exists yet.

## 8. Phase-completion report

| Field | Record |
|---|---|
| Target and scope | Phase, R IDs, implemented files, test scenario |
| Implementation state | Working behavior and remaining contract/functionality |
| Verification state | PASS / FAIL / NOT_RUN; attempted versus successful work |
| Execution evidence | Exact command, target/architecture/RPM/fixture, exit outcome, log path |
| Failure cause | Reproduction, observed/expected result, one next check |
| Cleanup | Residual test process, subscription, mount, package, DB fixture |
| Open items | PATH-01, finalization hook, stdout/stderr framing, other dependencies |

A passing build is build PASS while runtime is NOT_RUN. Emulator execution is still ARM NOT_RUN. Record user-reported environment facts separately from tool-observed facts. Do not repeat a passed test without a new change or unresolved failure; proceed to the next phase.
