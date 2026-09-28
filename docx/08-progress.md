# Development progress and verification

Updated: 2026-09-28. This board distinguishes implementation, review, test execution,
packaging and publication. Product completion has not been reached.

## Ownership and checkout

Current implementation/integration: Herdr `w1:pJ`. Independent reviewer and
coordinator: `w1:pA`. The user explicitly transferred these roles after the
accepted Release14 publication `cbf921ef9ccfe91699267a4f990ecefc0db3ddd2`.
Historical entries before this handoff retain their original owner/reviewer names.
Both were discovered through live `herdr agent list` with the same project cwd;
HERDR_ENV=1. Requests are asynchronous, without --wait. The owner alone performs
Git writes and device operations. Reviewer has a completed disjoint translation
assignment for docx/01,02,04,05,06. The current owner alone now manages all files,
integration, device operations, tests, reviewed commits and pushes. The other
panel remains read-only; neither panel starts extra panes or agents.

Initial cwd was not a Git repository. Existing documents were backed up outside
the checkout (an ephemeral local backup, not a published artifact). Cloned the authorized remote into
that backup, verified it contained only LICENSE, copied LICENSE and moved its .git
into the existing document directory. No independent root history was created.
Branch main tracks origin/main; upstream initial commit is
`3254eb7e357b703154f175beccfc03650775034d`. LICENSE SHA256:
`c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`.

## Phase state

| Phase | State | Evidence / next action |
|---|---|---|
| P00 | IN_PROGRESS | Baseline accepted; concrete ABI r4 accepted; environment inspected |
| P01 | FOUNDATION VERIFIED | Reviewed C++20/C ABI, host/native tests, RPM build/install; async/platform gates open |
| P02 | IN_PROGRESS | Catalog/parser core and offline subprocess harness tested; authoritative finalizer/MIC integration BLOCKED |
| P03 | IN_PROGRESS | Queries and Action import core tested; source feed/reconnect BLOCKED |
| P04 | IN_PROGRESS | Private worker/session/bootstrap integrated fixture accepted; Release15 ordinary installed package checks verified; authenticated app_fw/TIDL broker and resource gates open |
| P05 | IN_PROGRESS | Private identity experiments tested; PATH-01 pending; production identity, policy, namespace isolation and mount gates open |
| P06 | IN_PROGRESS | Private read lease/grant/TIDL handoff, API-only peers and stress accepted; Release15 installed tests/transport ACCEPTED; production admission/policy gates open |
| P07 | IN_PROGRESS | Native x86_64 builds/tests verified; full product integration NOT_RUN |
| P08 | IN_PROGRESS | Fixture benchmark/tool work; product stability and physical-device tests NOT_RUN |
| P09 | NOT_RUN | Gate closed until P08 completion; no ARM build attempted |

## Review ledger

| Request | Revision/scope | Disposition | Follow-up |
|---|---|---|---|
| P00-CONTRACT-r1 | SHA256 ae0ec3e78bb83e7efd95bb9b3dc577119e0619d62fc77b17b4fc2fcc5dca9803; AGENTS + sorted docx concatenation | ACCEPTED by w1:pJ | API/INSTALL/DB/PRIV/SYNC/ENV gates remain open; no product tests |
| P00-ABI-r2 | 07 SHA256 5cac0caa6d7398895d54e434b9dc38916782eb80c635e55fba2bd768761d33f9 | CHANGES_REQUESTED | Exact declarations; destroy callback behavior; stable key vs name; fail-closed parser |
| P00-ABI-r3 | 07 + proposed header | CHANGES_REQUESTED | Clarify callback setter lifetime |
| P00-ABI-r4 | 07: 613273012d667c0e995e5e46aa77611a576f702a192194676e2236161f16f5bc; header: a37b5b6b1feab61c98c2c6304b5e1aa83d25607e2d84f0536348da9c0cb5deef | ACCEPTED | Contract only; product tests NOT_RUN at acceptance |
| P00-DOCS-r1 | 01/02/04/05/06 bundle 5618a64d36b56b3e7a8b9d3fc5da789fa101938e01c6a8b764b27ae8f0bf6e75 | ACCEPTED by owner | Source requirements/gates and translation compared; product tests NOT_RUN |
| P00-PUBLISH-r1 | Publication docs | CHANGES_REQUESTED | Include reviewed header, align accepted decisions, restore native isError treatment, update board/ignore |
| P00-PUBLISH-r2 | Docs + reviewed contract header only | CHANGES_REQUESTED | Stale status corrected and accepted in P00-PUBLISH-r3 below |

Baseline acceptance preserved the nine core features, Action projection, local RO
queries, parser/MIC separation, explicit remount, CLI argv/dual-stream behavior and
P09 gate. It did not accept implementation or close engineering evidence gates.

## Executed environment checks

| Check / command | Exit / result | Scope |
|---|---|---|
| git status before checkout | 128, not a repository | Initial observation |
| git ls-remote --symref authorized URL HEAD | 0, main and initial SHA above | Remote identity |
| git clone authorized URL temporary-dir | 0; main...origin/main after preserving docs | Checkout established |
| herdr --help / agent / pane / agent list / pane current --current | 0; actual w1:pA and w1:pJ | Discovery only |
| sdb devices | 0; emulator-26101 device calendar-resolution-p5 | One clear target |
| selected-target uname / os-release / id | 0; Tizen 10.1 x86_64, root shell User::Shell | Emulator, not ARM/physical device |
| rpm -q gcc gcc-c++ cmake make ninja rpm-build tidl gtest-devel gmock-devel libsqlite-devel | Individual packages absent | Package DB corroborates PATH/header inspection |
| sqlite3 :memory: pragma compile_options | 0; ENABLE_FTS5, SQLite 3.50.2 | Target SQLite capability only |
| host pkg-config sqlite3/gtest/gmock/json-c | 1; development packages unavailable | Host prerequisite gap |
| sudo -n true | 1; password required | No system-wide host installation used |
| apt-get download libsqlite3-dev libsqlite3-0 libgtest-dev libgmock-dev nlohmann-json3-dev | 0 | Downloaded Ubuntu deps, extracted with dpkg-deb under /tmp/capmgr-host-deps/root |

Emulator build ID:
`tizen-10.1-unified_20260905.101134_tizen-headed-emulator64-wayland`.
Its standard/local/opt candidate paths and RPM DB were checked, rather than inferring
absence from PATH alone. /usr/include contains only consent headers in this image.
Root filesystem has approximately 2.2 GiB free. Existing host GBS x86_64 cache has
GCC 14.2, make, ninja and rpm-build; the scratch root lacks cmake/tidlc/SQLite/test
headers. Existing GBS root is read-only reference, not modified by this task.

AUL source evidence: `test/unit_tests/CMakeLists.txt` links gmock and registers
ADD_TEST; `test/unit_tests/mock/test_fixture.h` has TestFixture; root CMake enables
tests; packaging/aul.spec executes ctest in %check. Existing cached graph was read
before targeted source verification. No creator-named precedent was identified.
Watcher TIDL uses tidlc -p/-s -l C++ and the internal/default/platform privilege;
parser .info marks vitalness=true. These are precedents, not CapMgr PASS results.

## Publication and limitations

Reviewed development checkpoints through `154a9b4e216e02e881dc9759cde36a16fe70880c`
are committed and pushed on main, with exact remote refs verified. The chronological
records below retain earlier failures and pending states as historical evidence;
later entries supersede their status. Raw evidence/builds/dependencies are excluded
from Git. Each publication records SHA, branch, push exit and verified remote ref
in a subsequent entry to avoid a self-referential commit SHA.

Full R01–R18 product acceptance: NOT_RUN; scoped tests are linked below. Physical
device: none discovered; NOT_RUN. ARM build/runtime: NOT_RUN, intentionally gated.
INSTALL-01 and SYNC-01 require platform integration beyond private fixture tests;
fail-closed behavior must not be reported as successful online registration.

Next: implement and review the causal read-admission policy/direct-open fixture
before native policy changes; Release15 runtime/publication review is ACCEPTED. PATH-01 remains a
product question, not a reason to block unrelated catalog/CLI work.

### P00 publication checkpoint review

P00-PUBLISH-r3 ACCEPTED by w1:pJ for 13 files (AGENTS.md, .gitignore,
filename-sorted docx/*.md, src/api/capmgr.h), concatenated-byte SHA256
`b1fa94b2a16a63bc277630c6d07a452626e02bd81963062b8697e0388230408b`.
07 publication-status hash is
`7db2275c624b86d55097b65690b5a079d0db8f4d37c9dc7c80da7edfd83d42c8`;
its accepted r4 contract clauses/header are unchanged. Reviewer explicitly permits
this append-only bookkeeping before commit. Fence/local-link/JSON validation
passed (exit 0). Commit/push/ref outcome will be appended after execution.

### Initial publication result

Commit `28cd7cb267eecef318b5100c2b09f088ad2a2b30` on main preserved the initial
upstream commit and LICENSE. `git push origin HEAD:main` exited 0; `git ls-remote
origin refs/heads/main` returned that exact SHA. No force push or history rewrite.

### P01 host foundation verification (working revision, review pending)

Host Ubuntu x86_64 / GCC 13.3 / CMake 3.28 / SQLite 3.45.1 / GTest 1.14:
`cmake -S . -B build -DCMAKE_PREFIX_PATH=<extracted-deps>/usr
-DCMAKE_BUILD_TYPE=Debug`, `cmake --build build -j 4`, and
`cmake --build build --target check` succeeded. CTest: 2/2, including 17 actual
SQLite/mock tests and a pure C consumer. `nm -D --defined-only build/libcapmgr.so`
shows exactly the 12 capmgr functions plus the version node, no writer exports.
The first build found duplicate GoogleTest macro labels on one source line;
separating the assertions fixed it, and the full affected tests passed.
Injected `/bin/false` into the generated CTest file: CTest exited 8 and build check
exited 2; restored the generated file afterward. Temporary logs reside outside
Git under a local capmgr evidence directory (p01-configure/build/ctest/check and
p01-negative-check). These results do not validate async callbacks, TIDL runtime,
installation, mounting, Action synchronization, RPM or ARM behavior.

### P01 RPM and emulator-native foundation evidence

Host `rpmbuild -ba --nodeps` used the actual extracted Ubuntu development prefix
(the host RPM database does not describe its Debian packages); CMake resolved
SQLite/GTest/GMock and compiled/linked them. %check executed CTest 2/2, exit 0.
Generated runtime/devel/tests and source RPMs under a temporary rpmbuild root.
Host RPMs are host artifacts, not Tizen deployment evidence. Third-party
nlohmann/json v3.11.3 source SHA256 is
`0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406`;
its MIT notice is retained in the runtime package (RPM License Apache-2.0 AND MIT).

Emulator dependency RPMs were downloaded from the configured Tizen 10.1 emulator
and base repositories and checked against primary-metadata SHA256. RPM --test
exited 0 before installation. Native packages now provide GCC 14.2, CMake 3.31.2,
TIDL 3.1.1, SQLite 3.50.2 and GTest/GMock 1.15.2. Initial compilation failed because
cc1 is in the separate cpp package; installing matching cpp fixed it. RPM reported
some ldconfig SMACK permission warnings; actual native linking/tests succeeded,
which does not clear platform privilege-policy gates.

In an isolated emulator development directory, CMake with CAPMGR_REQUIRE_TIDL=ON
and the pinned JSON source generated TIDL proxy/stub files, built C++ and the C
consumer, and ran CTest 2/2 successfully (17 unit cases at this earlier snapshot).
Remote shell sentinel CAPMGR_REMOTE_EXIT=0 verifies command status independently
of the SDB transport exit. Logs: native-install-test, native-install, native-build
(initial failure), native-cpp-install, native-build-retry in local temporary
evidence. Native product RPM build/install is still NOT_RUN at this point.

Subsequent host working revision adds UTF-8 identity validation, idempotent final
outcome replay, and foreach callback destroy protection: 20 unit cases pass plus
C consumer (CTest 2/2). Native rerun of that changed revision remains pending.
Graphify AST update exited 0 (229 nodes initially) with a parser warning at the C
visibility macro in capmgr.h; actual C compile/link passed. Graph artifacts are
ignored, and the graph warning is not hidden as a clean parse.

### P01 review corrections (r3)

P01-FOUNDATION-r2: CHANGES_REQUESTED by w1:pJ. Corrected the schema-version
collision by introducing v2 and a transactional v1-to-v2 completed-ledger migration;
read-only v1 clients reject until a writer migrates. Removed the direct
ReplacePackage API: package publication now requires explicit Stage + successful
Finalize. Fixtures supply authoritative outcomes themselves. Stored revision types,
row types and JSON are validated as DATABASE errors; revision overflow rolls back
catalog/FTS rather than becoming a floating-point generation. Added migration,
corruption, and overflow fixtures. Host check now passes 24 tests plus C consumer,
with 12 exported C functions; native RPM revalidation of r3 is in progress.

SEARCH-01 remains partial: cap, basic Porter matching, exact-name preference and
AND/no-padding tests are not the representative English relevance corpus.
Native RPM retry initially produced artifacts but its unpackaged-file checker
could not run because diff was missing. Added BuildRequires diffutils, installed
the matching Tizen dependency, and scheduled a full rebuild of the exact spec.
The earlier OOM is confirmed by kernel `Killed process ... (cc1plus)` evidence;
current spec uses Release -O1/-DNDEBUG and one compile job. Neither failed/warning
run is treated as a fully verified native RPM checkpoint.

The new tools/verify.py preflight/foundation/native-build runner was exercised:
explicit emulator serial, command logs, transport and remote exit codes, pinned
JSON archive validation, isolated remote build scope, and cleanup. Its r2 source
snapshot built and passed CTest 2/2, then removed only its marked temporary scope.
TIDL --version deliberately returns 1 with a version string; preflight records
that observed exit separately as expected rather than masking arbitrary failures.
Tool implementation review/commit is separate from P01 foundation.

### P01 foundation r3 acceptance and native RPM result

P01-FOUNDATION-r3 ACCEPTED by w1:pJ for the private-catalog foundation
(R01/R02/R06/R09/R13/R14/R16), excluding launcher, parser, Action importer and
verification-tool work in progress. Reviewed catalog.cc SHA256
`a4f063d85b12b1204aa3bd3e596a695d0cacf066ecaade629ff7aa52b8814fbb`,
catalog.hh `167c50a43d64139e89ac59fbc7119899b3174788d63c1ecd0381b5ff8fb1880e`,
database.cc `f2c6e942015594b0b697ee989db2aa6044570aebb657d08a78ad229ea319e07f`,
client.cc `b5320e09b8e5ae719b3df2f0a36e1387c70808f98813da39b74788688c15d42e`,
and spec `1d8412485cb093f307ad5253009e0960b10fc1f7d54f1196779f44d777283185`.
Reviewer read source/logs; did not independently rerun tests or operate devices.

Exact r3 source archive SHA256:
`9e5ab491eb84575941290d9374095ab1909e3f2b085b313c195ccd564389e57e`.
On emulator-26101 x86_64, `rpmbuild -ba` with the reviewed spec completed
TIDL generation, CTest 2/2 (24 GoogleTests and C consumer), unpackaged-file checks
and runtime/devel/tests/source RPM creation. Remote exit 0, evidence
`native-rpm-r3.log`; the earlier missing-diff warning is absent. Payload and
runtime dependency inspection plus `rpm -U --test` exited 0 in
`native-rpm-inspect.log` (RPM msm tsm_post warning retained). Private writer
headers are not installed. Package installation remains NOT_RUN at this point.

This acceptance is not completion of P01/P02 or product R01-R18: public production
create remains fail-closed pending privilege/TIDL integration; asynchronous
execution, mount, authoritative installer finalization, Action change feed,
representative search quality and device permission checks remain open.
Next checkpoint integrates and reviews parser/importer/CLI cores and their tests.

### Foundation publication and installed emulator tests

Foundation commit `008ecbb94ddf010ca5dc87a820ef053335a991c9` on main:
`git push origin HEAD:main` exit 0; remote refs/heads/main verified identical.
Only reviewed foundation files were staged; parser/importer/launcher/tools remained
outside this checkpoint. Existing LICENSE and published history were preserved.

On emulator-26101, installed the three native foundation RPMs with `rpm -U`, then
ran installed `/usr/libexec/capmgr/capmgr-unit-tests` (24/24) and
`capmgr-c-consumer`, remote exit 0. Evidence: `native-rpm-install-tests.log`.
This proves installed x86_64 foundation tests; platform service/privilege/mount
integration and real-device execution remain NOT_RUN.


### P02-P04 adapter-core checkpoint r2

P02-P04-CORES-r2 ACCEPTED by w1:pJ for 14 scoped source/build/test files;
review manifest SHA256
`8c21051147c299ff9375da2ea9aa0b6abea2966473f10a5587d88330072a07cc`.
Action import now accepts omitted outputSchema, rejects malformed present schemas,
and replays the committed revision during reconciliation even after failed event
submission. Receivers must deduplicate; replay still needs an actual source or
reconnect trigger. No autonomous delivery claim. Source archive SHA256
`d203e3d4dc34c236664a39570af2b96bbb5c2720ac0b7b6dc0e3a34c9e2b14c4`.
The reviewer permitted a comment-only grammar cleanup in action_import.hh.

Host `cmake --build build --target check -j 3`: CTest 3/3 PASS
(24 foundation cases, 23 adapter cases, pure C consumer), exit 0, evidence
`adapters-check-r2.log`. Exact r2 emulator-native `rpmbuild -ba` completed
TIDL generation, compilation, CTest 3/3, unpackaged-file validation and four RPMs,
remote exit 0 (`adapters-native-rpm-r2.log`). Prior r1 native CMake run and
owned-scope cleanup passed (`adapters-native-r1/summary.json`).

Production StagePackage rejects before staging without authoritative finalization;
only the explicit offline harness supplies outcomes. Action-only atomic import
cannot publish parser kinds. CLI core validates single literal argv, separate
streams, native errors, timeout/cancel, child process groups and FD isolation.
Actual MIC/finalizer, source-writer feed, app_fw service/cgroup/IPC and public async
are outside this checkpoint. Search corpus, dispatcher and tools remain separate
working changes, excluded from staging this checkpoint.

### Adapter RPM upgrade correction and installed verification

Same-NEVRA Release 1 reinstall failed with file conflicts (remote exit 3,
`adapters-native-install-r2.log`) and an msm tsm_post warning. No force or
replacefiles was used. P02-P04-PACKAGE-r3 ACCEPTED by w1:pJ for Release 1 -> 2
and the permitted comment tidy; reviewed spec SHA256
`9f6bcdad58a1d556e8b697b7808073ef22ae59e61050bc133c4e67bdf1876067`.
Release 2 source archive SHA256
`c70119960550c499e79e531ac83681ddd9066895d3281aacc05e3482cfa2c289`.

Exact Release 2 native RPM build/check passed CTest 3/3 and package validation,
remote exit 0 (`adapters-native-rpm-r3.log`). Normal `rpm -U` of the three
0.1.0-2 packages succeeded; `rpm -q` confirmed runtime/devel/tests at 0.1.0-2.
Installed adapter tests passed 23/23 and installed C consumer exited 0; remote
exit 0 (`adapters-native-install-r3.log`). The earlier msm warning was on the
failed same-NEVRA attempt; the Release 2 successful transaction did not emit it.
Platform policy correctness is still unverified independently of package outcome.

### Adapter publication and async/API checkpoint

Adapter core commit `247b6bac60a2deab891651a4dafc79f2a45f9e98` pushed to
origin/main, exit 0; remote ref verified equal. No unreviewed async/tool changes
were staged with it.

P06-ASYNC-r2 ACCEPTED by w1:pJ for CMake, private client/backend and dispatcher,
public callback integration tests and search corpus. Dispatcher SHA256
`efaeb029ebdb1b20ceb0b1903cab49c19f9250cd99c7c5d0d10b06fbc4e84012`.
R1 rejected unchecked backend replies; r2 validates bounded JSON-RPC envelopes,
matching exact IDs and error shapes before callbacks and preserves valid native
bytes. Synthetic transport errors retain a confirmed data.cause. Event envelopes
are also checked; actual Action event mapping remains unimplemented.

Host `cmake --build build --target check -j 3` passed CTest 3/3: 36 foundation/API/
search tests, 23 adapter tests and C consumer (`async-check-r4.log`). The 12 public
exports remain unchanged. Tests cover callback-active BUSY/no partial destroy,
setter data lifetime, callback cancellation, successful cleanup without callbacks,
unsupported cancellation, token exhaustion, duplicate in-flight IDs, two/client
and four/process limits, bounded queue and changed-revision deduplication.
The 13-entry English/Unicode search corpus tests expected ordering, AND/no padding
and corrupted FTS handling. It does not establish optimal relevance weights.

Exact source snapshot built inside emulator-26101 with TIDL generation and
CTest 3/3 PASS, remote exit 0 and marked-scope cleanup PASS:
`async-native-r2-retry/summary.json`. First attempt failed before compilation,
exit 127, because target coreutils 6.9 lacks timeout (`timeout-probe.log`).
The verification tool now uses a Python process-group watchdog; this tool change
is reviewed separately. The failed run and its successful cleanup remain recorded.

This checkpoint implements injected-backend callback/lifetime behavior. Production
create still denies access until platform privilege/DB policy and TIDL session
integration are verified. Transport deadlines, service-wide cross-process limits,
Action subscriptions, remount and actual source-feed reconnection remain gates.

### Async publication and fixture-tooling checkpoint

Async/API commit `9b39a8490315ac64817733de423d658b65844c10` pushed to
origin/main, exit 0; remote ref verified identical. Its native source archive was
`5afb87787671a88327473bc25acf2cd8732cb28cd6e970e0567c9e68efc2da37`.

P07-TOOLS-r2 ACCEPTED for the seven tooling/build/package files; r3 ACCEPTED for
the native test portability correction, verify_test.py SHA256
`b72ddab497d26878e11e0edd21b3d54b8e36615bbe5cf9abd2395f7e88afc0fe`.
The runner requires explicit target selection, records actual remote/transport
exits and source hashes, preserves earlier evidence, checks archive paths/types/
size, and preserves native scope when completion is unknown. The watchdog holds
the leader PID until process-group cleanup even after normal shell exit. Tests
cover failed commands, deadlines, descendants, missing CTest tree, lost transport,
missing remote sentinel and retained evidence. Deliberate process-group escape
still requires production cgroup supervision.

First native Release 3 RPM %check failed (exit 1) because /tmp is noexec and the
fake-SDB fixture tried to execute there. Actual mount/shebang evidence is in
`tool-native-failure-probe.log`; no target mount flags were changed. Executable
fixtures now use owned build-directory temporary scopes. An already-reaped child
is accepted only when its /proc/PID is absent. Host/native Python tests 7/7 passed
(`tool-tests-r6.log`, `tools-native-tests-r3.log`).

Exact corrected Release 3 archive SHA256
`d6eaa3df7fdb4bfc48c2a99220b97ae60e0b26d1cd711d37b4bfa99357e4eb6b`:
native `rpmbuild -ba`, CTest 4/4, unpackaged-file check and RPM creation passed,
remote exit 0 (`tools-native-rpm-r3.log`). Normal `rpm -U` upgraded all three
packages to 0.1.0-3. Installed tests passed 36 API/catalog/search + 23 adapter
GoogleTests and the pure C consumer; benchmark also passed, remote exit 0
(`tools-native-install-r3.log`). No platform policy acceptance follows from this.

Reusable modes and actual commands are documented in [tools/README.md](../tools/README.md).
Installed SDB benchmark report: `installed-perf-r3/summary.json`. Earlier isolated
1000-row/100-iteration measurements: host warm median 494 us/p95 537 us and peak
RSS 18304 KiB; emulator warm median 463 us/p95 539 us and peak RSS 8240 KiB.
These are different builds/SQLite versions and are not a comparative optimization
claim. Reopen measurements retain OS caches; peak RSS includes seeding. Later
installed-run timings are separately retained and must not replace earlier data.
Platform smoke/integration/performance and physical-device results remain NOT_RUN.

### Tool publication and platform connection identity investigation

Tools commit `e32ce8ef2137cdc3600947c1bb7fdd30a0e0c230` pushed to origin/main,
exit 0; remote ref verified identical. Release 3 package evidence is above.

P06-PEER-r1 CHANGES_REQUESTED correctly identified connection-vs-message identity,
initial PID reuse uncertainty, zombie liveness and missing policy integration.
The revised helper rejects Z/X states. Tests explicitly demonstrate inherited FD
and SCM_RIGHTS retain the connector identity, while an endpoint recipient cannot
keep a zombie connector authorized. Native inherited-FD sender drops to UID 65534;
connector remains UID 0. Neither case is claimed as per-message authorization.
No mount is enabled. Generated extension FD defaults fail closed, but its FD is
from the callback channel; MAIN/callback binding is a separate unimplemented gate.

Host CMake configure and `cmake --build build --target check -j 3`: exit 0,
CTest 5/5 (`peer-check-r2.log`). Standalone host/native peer tests 5/5 with
CAPMGR_REQUIRE_PEER_TESTS=1, no skips (`peer-host-tests-r2.log`,
`peer-native-tests-r2-retry.log`), native remote exit 0. First standalone host
compile failed on ambiguous GTest macro braces; corrected. First native r2 compile
failed from a wrong JSON include path (`peer-native-tests-r2.log`); corrected.
Policy decision tests inject allowed/denied/unresolved/unavailable and require a
check even for system UID. They are not target Cynara policy provisioning tests.
Actual native root/User::Shell socket passed the real Cynara check
(`peer-native-policy-probe.log`, remote exit 0). Full denied/unresolved/real-label
policy matrix remains NOT_RUN. Raw output is retained outside Git.

Native seqpacket probe sets SO_PASSCRED and SO_PASSSEC before listen: the received
packet has SCM_CREDENTIALS matching the connector and SCM_SECURITY User::Shell,
no truncation (`seqpacket-credentials-probe.log`, remote exit 0). Native kernel
4.4.35 returns ENOSYS for pidfd_open. This supports a repo-owned remount sidechannel
implementation direction; it does not establish completed remount access control.

Current repository SDK RPMs required newer capi-base-common/rpc-port runtime
versions; `rpm -U --test` rejected them, exit 27. Existing runtimes were preserved.
Development-only local SDK RPMs were reconstructed in an owned temporary scope
from capi-base-common 0.4.82 source commit
`0e569d4774a27d6435ae3b490abb81783b8d750b` and rpc-port 1.21.17 commit
`6292196d115a4b736f41fb170c39f1925bf48336`. They contain headers, linker symlinks,
pkg-config metadata, provenance and upstream licenses, no runtime binaries.
They are local reconstructions, not official upstream binary artifacts.
Build/payload inspection/test/install passed (`matched-sdk-install.log`, remote
exit 0). Matching remaining development dependencies installed; the script's final
pkg-config command used the wrong name pkgmgr-info-parser and returned 1, then
inspection confirmed the actual package is pkgmgr-parser. rpc-port 1.21.17,
tizen-core 0.2.4, glib 2.80.5, bundle 0.18.15, Cynara 0.26.0 and libsmack 1.3.1
are available; original runtime versions remain unchanged. Observed msm post-hook
and ldconfig SMACK permission warnings remain in the logs; compilation/linking and
RPM queries succeeded, but no broad platform policy correctness is inferred.

P06-PEER-r2 ACCEPTED by w1:pJ for the eight-file manifest (not nine) plus this
administrative evidence ledger. Scope is private connection-principal code/tests,
not TIDL request authorization, per-message identity, public create or remount.
The 07 heading is updated only to reflect this acceptance; no contract clause
changed. Initial PID reuse and MAIN/callback verification remain hard integration
gates. A second actual target probe with UID 65534 and the same User::Shell label
was denied or unresolved by real Cynara (`peer-native-policy-dropped-uid-probe.log`,
remote exit 0); it does not distinguish the underlying denial code or substitute
for a full real-label policy matrix. The new credential_packet source is separate
unreviewed WIP and excluded from this checkpoint.

Connection-principal checkpoint `e38252ff56af6b5807b17391532251e566db0cf3`
pushed to origin/main, exit 0; remote ref verified identical.

P06-PACKET-r1 review requested for private seqpacket/ticket primitives. The packet
receiver requires AF_UNIX SOCK_SEQPACKET, enabled credential/security ancillary,
one PID/UID/GID cmsg and one matching security label, no truncation, no unexpected
ancillary and a live connector. Unexpected SCM_RIGHTS FDs are closed before
rejection. An inherited sender is rejected even while the connector stays alive.
Tickets use 256 bits from getrandom, five-second expiry, a 64-ticket bound and
atomic one-use consumption; destination is bound server-side. A valid packet from
another connector cannot use a stolen ticket. No mount or public API is enabled;
MAIN binding, actual policy recheck at mount, namespace transaction and rollback
remain separate gates. Native standalone peer/packet tests passed 15/15 with no
skips (`packet-native-tests-r1.log`, remote exit 0). This is private transport
validation, not platform remount integration acceptance.

P06-PACKET-r1 CHANGES_REQUESTED for the distinction between socket and current task
label. r2 corrects credential_packet.hh and 07: SCM_SECURITY carries the sending
socket's Smack label, not proof of the current sender task label after exec/relabel.
No implementation bytes changed. Current-label validation/race strategy is an
explicit production authorization gate. The tests cover payload truncation; a
received ancillary truncation fixture remains pending. Host final CTest5/5 is in
`packet-check-r2.log`; native 15/15 remains the exact tested implementation.

P06-PACKET-r2 ACCEPTED by w1:pJ for the nine-file private packet/ticket scope.
Only socket-label comments/contracts changed from r1; tested implementation bytes
remain identical. No acceptance closes current-task label, MAIN binding, initial
PID reuse, privilege, mount or rollback gates. TidlChannels and generator binding
work are excluded from this publication.

### TIDL MAIN/callback integration checkpoint (review pending)

Packet/ticket commit `a010f81d6ad1c448fc3ede07565a57e7ede0c110` pushed to
origin/main, exit 0; remote ref verified identical.

The local generator adapter binds distinct MAIN and callback sockets before
OnCreate, rejects unchecked internal API failures, and validates actual request
MAIN/callback FDs plus Cynara before decoding a parcel. It fails generation on an
unknown template layout. TidlChannels tests cover invalid/default/swapped FDs,
repeated binding, MAIN-specific policy checks, policy revocation, callback
closure, and a callback connected by another live process. This is still a
connection-principal boundary; no current-message/task identity is claimed.

Host CTest5/5 PASS (`tidl-binding-check-r3.log`): 36 catalog/API/search +23 adapter
+18 peer/packet/channel GoogleTests, pure C consumer and nine Python tests.
Native CMake with REQUIRE_TIDL, REQUIRE_CYNARA and BUILD_TIDL_TRANSPORT all ON,
Release -O1, parallel1: CTest5/5 PASS (`tidl-full-native-r4.log`, remote exit 0).
Both generated proxy and stub compile/link against the installed runtime SDK.
Native TIDL3.1.1 and host2.10.2 generation are exercised. libdlog-devel was an
additional matching dependency, checksum validated; installed runtime untouched.

Actual separate server/client TIDL fixture passed MAIN principal PID equality,
real root/User::Shell Cynara and unsupported-remount response. Both processes
exited 0 (`tidl-ipc-probe-r1.log` and final `tidl-full-native-r4.log`). No product
service was enabled and no mounts occurred. The fixture is a reusable test source
and driver; it is not the public API or production deployment. Release4 packaging
adds these tests and declares native transport build dependencies; exact RPM
build/upgrade verification is pending before publication.

Failed setup attempts are retained: missing dlog development header in
`tidl-binding-native-compile-r1.log`, corrected compile in r2; relative generated
proxy include required the source root include path in standalone fixture compile
(`tidl-probe-compile-r1.log` failed, r2 passed). Two full-build launch attempts used
a nonexistent watchdog path and then an incorrect CLI argument; both exited2
before building (tidl-full-native-r1/r2.log). The installed watchdog with
`--seconds 1200 -- sh ...` ran r3/r4 successfully. No failure is counted as PASS.

P06-TIDL-BIND-r1 CHANGES_REQUESTED: disconnecting borrowed rpc-port handles in
OnConnected removed their watchers before the owning Stub removed its instance
entries. Revised generation calls shutdown on known socket FDs and leaves the
watchers/Port ownership intact; their HUP path removes both accepted ports.
The fixture observes rejected instance IDs through a no-op-by-default rejection
hook and uses the C API to check both retained ports after cleanup.

Host `tidl-binding-check-r4.log` CTest5/5 PASS. Native exact revised CMake build,
CTest5/5 and adversarial IPC fixture passed (`tidl-full-native-r5.log`, remote
exit0): root authorized, 32 connected UID1 requests denied, subsequent root request
accepted; rejected=32, retained_ports=0, services=0, final_fds=10, warm_fds=20.
Separate real Cynara UID1 probe also denied/unresolved, exit0
(`peer-native-policy-system-uid.log`). No target privilege rules changed.

Release4 first RPM build passed %check and payload/dependency inspection
(`tidl-native-rpm-r1.log`, remote exit0), but its pre-fix code was NOT installed.
The revised source will be rebuilt under the still-unpublished Release4 before
upgrade. R01 reconciliation is explicit: private seqpacket code is an experiment,
not a product IPC protocol; production resource transport must remain TIDL.

P06-TIDL-BIND-r2 ACCEPTED by w1:pJ for the exact 12-file binding/probe/source
packaging manifest, all hashes verified again before publication. The rejected
connection lifetime finding is closed for this scope. Production create/remount,
initial PID binding and current task label remain outside acceptance.

Exact Release4 revised archive SHA256:
`37deb9b82a83c53ff730e3ab530d31215924f20ef497508edd970440ceb3b485`.
Native `rpmbuild -ba --define 'capmgr_tizen 1'` completed %check CTest5/5,
unpackaged-file validation, four RPM outputs and payload/dependency inspection
(`tidl-native-rpm-r2.log`, remote exit0). Normal `rpm -U --test` then `rpm -U`
for runtime/devel/tests succeeded; RPM DB reports all three 0.1.0-4. Installed
36 unit +23 adapter +18 platform GoogleTests and pure C consumer passed, followed
by the actual installed TIDL probe: 32 UID1 rejections, later root success,
retained_ports=0, services=0, final_fds=10 <= warm_fds=20, all process exits0
(`tidl-native-install-r2.log`, remote exit0). The first installed platform run
used an unrecognized require-mode variable; it had no skips. A corrected explicit
`CAPMGR_REQUIRE_PEER_TESTS=1` rerun passed18/18 without skips
(`tidl-installed-peer-required-r2.log`, remote exit0).

The upgrade emitted `Plugin msm: hook tsm_post failed`; it is retained as an
observed platform warning. Transaction exit0, installed versions and execution
establish this fixture upgrade, not general SMACK policy health. No privilege
policy or mount configuration was changed. Graphify update exited0; generated
output and all raw evidence remain outside the commit. Commit/push results follow
in the next board update. Next work: actual parser plugin/offline harness and
INSTALL-01 finalization integration investigation; ARMv7 remains NOT_RUN until P08.

Binding checkpoint commit `ef3f87964c46f81a2f30cacbfcde7eac27235d20` on main
pushed to origin/main, exit0; `git ls-remote` confirmed that exact commit.
Graphify's successful update retained a partial C-header extraction warning; it
is navigation evidence only, not a compiler or test result.

### Offline package harness (P02-OFFLINE-r1 review pending)

The explicit administrative package tool stages complete metadata for an offline
image catalog, reports pending/completed operations and accepts an explicit final
outcome. It uses private parser/SQLite code directly, with no AMD/TIDL service.
Seven subprocess integration tests exercise hidden pending rows across process
exit, stage/finalize replay, publication/FTS, failed and successful update/removal,
foreign ownership, mixed keys/App Skill scope, malformed/oversized manifests,
missing outcome, and no DB creation after invalid descriptors. Host check6/6 PASS
(`offline-check-r1.log`, exit0). Release5 packaging adds an offline-tools subpackage;
exact native RPM build/install remain pending. This is not production MIC acceptance.

Independent INSTALL-01 source investigation by w1:pJ confirms no authoritative
finalizer in the installed pkgmgr-parser surface. In app-installers,
StepRunParserPlugin is interior to install/update/uninstall; TagPlugin POST runs
inside that same step. CLEAN results are discarded both by the step and runner.
Uninstall lacks plugin UNDO, and Process can reset a late error after non-undoable
RemoveFiles to OK. Thus even backend exit0 does not universally prove a clean
uninstall. pkg_initdb invokes backends and checks child exits without a post-result
plugin callback; offline operation omits online signals. Existing backend symlinks
are image-owned and direct invocations can bypass them.

Online/MIC publication therefore requires a supported post-transaction/rollback
hook, or an image-adopted orchestrator covering every invocation path, with durable
operation IDs and explicit success/failure/partial/unknown outcomes. It must work
without AMD and reconcile partial uninstall from authoritative platform state.
Neither this harness nor a parser-only library closes that prerequisite. No
unrelated installer or image configuration was modified. Source pointers are
app-installers app_installer.cc:254-268,364-441; installer_runner.cc:71-92;
step_run_parser_plugins.cc:53-99,135-193; tag_plugin.cc:115-160; pkgmgr-info
pkg_initdb/init_pkg_db.cc:67-150; pkgmgr-server backend_queue.rs:271-298,336-386.

P02-OFFLINE-r1 ACCEPTED by w1:pJ for five-file manifest; all hashes remain exact.
Release5 source archive SHA256
`d074db46d61e41f31d9cf26b42b164835371aa5097b6175af81266b704c9b10b`.
Native `rpmbuild -ba` with capmgr_tizen1 passed CTest6/6, unpackaged-file checks,
five RPM outputs and payload/dependency inspection (`offline-native-rpm-r1.log`,
remote exit0). Normal `rpm -U --test` then `rpm -U` installed runtime, devel,
offline-tools and tests at 0.1.0-5. Installed36+23+18 GoogleTests, C consumer and
seven subprocess offline tests passed (`offline-native-install-r1.log`, remote
exit0). ldd confirms the offline tool uses SQLite/C++/C runtime libraries without
AMD, TIDL or Cynara. The repeated msm post-hook warning is retained; version and
execution checks establish this package transaction, not general policy health.
No production parser/MIC finalizer acceptance is inferred. Commit/push follow.

Offline checkpoint commit `7594e991cf968e33560d2a4833cdbb55f22640f6` on main
pushed to origin/main, exit0; exact remote ref verified.

### Action execution compatibility investigation

Native RPM DB reports capi-appfw-tizen-action/tizen-action1.3.27-1. Runtime exports
create/destroy/execute but not action_client_cancel (`action-native-symbols.log`).
Read-only local source release f9c43cb matches version1.3.27; callback is disposed
after its first result and TIDL has no Cancel. Subscription/event/closed/cancel
support was introduced by de4d7fc after that release. Modern local HEAD headers
must not be linked as evidence that this runtime supports subscriptions. A matched
Action service/C API/TIDL upgrade and target tests are prerequisites for R11
subscription completion; ordinary Action cancellation remains NOT_SUPPORTED.

A bounded ctypes probe against the actual existing C API connected successfully
(create0), submitted a unique nonexistent Action name (execute-5), then destroyed
its client (destroy0), remote exit0 (`action-native-capi-probe.log`). This validates
a connection and synchronous rejection path, not successful Action execution or
streaming. It changed no Action definitions, providers or source DB. An initial
package probe used unavailable target rg; the corrected grep/RPM/symbol probes
establish actual installation rather than inferring absence from PATH.

Independent review also identified the service-delegation privilege gate: the
Action service authorizes its own peer, so an app_fw proxy must not substitute
its privilege for an unverified original caller. Callback data are borrowed and
the C API has no proven cross-thread destroy fence. Production integration needs
original-caller authorization, serialized native callback ownership and bounded
cleanup. No production backend is enabled by protocol-only tests below.

P04-ACTION-WIRE-r1 ACCEPTED by w1:pJ for the four-file private mapper scope.
It binds canonical Action identity, an independently supplied positive native int,
and original string/int64 ID; rejects unverified provider override and unsupported
subscription runtime; rewrites only the outer reply ID and preserves payload bytes.
Its explicit frame distinguishes acknowledgement, event and completion. Seven new
GoogleTests pass; host `action-exchange-check-r1.log` CTest6/6 and isolated emulator
native Debug CTest6/6 PASS (`action-wire-native-r1/summary.json`, all transport and
remote exits0, owned scope cleanup0). Native archive SHA256
`71f708f8d8b83b1b6e3dc677f19dc65c38b51d221817e62c18903b348bb063f9`.
This is a source/core checkpoint, not an RPM replacement: installed Release5
remains the previously verified offline/TIDL fixture package. Dispatcher framed
response changes are separate WIP and excluded from this mapper commit.
Allocator uniqueness, native callback lifetime/deadline, original caller privilege
and upgraded subscription runtime remain production adapter gates.

Action mapper commit `a46f1bcaffaf22b9a6b360f89768b06b56df6497` pushed on main to
origin/main, exit0, exact remote ref verified. A separate native read-only snapshot
probe imported all245 existing Action rows from `/opt/dbspace/.tizen_action.db`
into an owned temporary CapDB, projected245 rows with no top-level private execution
fields, and reached revision1 (`action-native-source-snapshot.log`, remote exit0).
The probe removed only its temporary CapDB; the original DB was opened read-only.
This is actual source-format compatibility/initial projection evidence, not the
post-commit writer feed or AMD startup lifecycle gate.

### Framed asynchronous responses (P06-ACTION-FRAMES-r2)

Private frames separate event classification from completion. The public C callback
fixture retains an Action token after acknowledgement, delivers native events,
accepts cancellation while open and retires on a closed event. Production backend
and target subscription API remain unavailable; no subscription is enabled.

r1 CHANGES_REQUESTED identified terminal-marked acknowledgements, cancellation
success after completion was queued, and a post-terminal emit waiting behind a
saturated queue while dispatcher joined its worker. r2 derives Action protocol
from catalog kind (CLI data stays native), rejects a terminal Action acknowledgement,
returns NOT_FOUND for completed tokens and checks terminal before/in/after queue
wait, waking waiters on completion. Regression tests cover the public callback,
a terminal queued behind a blocked callback, and a bounded subprocess with900KiB
queued plus a post-terminal300KiB emission. Join stays outside the mutex.

P06-ACTION-FRAMES-r2 ACCEPTED by w1:pJ for eight-file manifest. Host
`action-frames-check-r4.log` CTest6/6 PASS:43 unit,30 adapter,18 platform GoogleTests,
C consumer,9 Python collector/generator and7 offline subprocess tests. The public
export list remains12 symbols. Intermediate r2 host compile failed due to GTest
macro commas/dangling-else warning; r3/r4 corrected and passed. Native Release6 r1
built successfully but was NOT installed because findings were still open.
Revised archive SHA256
`fffcb29880742640de086e969096edd3705a095089d29c35d8e00a625c349ea1`.
Exact r2 `rpmbuild -ba`/%check CTest6/6, payload/dependency/unpackaged checks and
five RPM outputs passed (`action-frames-native-rpm-r2.log`, remote exit0).
Upgrade and installed verification are pending in the following entry.

Release6 exact r2 normal `rpm -U --test` then `rpm -U` succeeded for runtime/devel/
offline-tools/tests; RPM DB reports0.1.0-6 for all four. Installed43 unit,30 adapter,
18 platform GoogleTests, pure C consumer and7 offline subprocess tests passed
(`action-frames-native-install-r2.log`, remote exit0). The known msm post-hook
warning remains recorded; this is verified package/test execution, not full policy
health or an actual Action subscription call. The accepted eight hashes were
rechecked before staging. Commit/push follows in the next board entry.

Independent P04 cleanup investigation: actual target `unshare --mount --pid --fork
--mount-proc` succeeded with namespace PID1 and distinct PID/mount namespace FDs
(`cli-pidns-native-probe.log`, remote exit0). This probes kernel support only.
A namespace setup/cleanup helper, app_fw execution policy, resource confinement
and broker authorization are not implemented or accepted by this probe.

Framed-response commit `f955ffe8c53a29be8b85fc13310d5e9cc2b88e3d` on main pushed
successfully; remote main verified identical. The tree was clean after publication.
Next independent P04 design review addresses setsid escape with a small non-exec
PID1 supervisor and a separate authenticated root setup broker while retaining
an app_fw TIDL launcher. Exact4.4 source review confirms namespace teardown but also
an unbounded kernel wait; cleanup-pending must remain distinct from confirmed exit.
No helper implementation or image security policy is implied by this design entry.

P04-ISOLATION-CONTRACT-r2 ACCEPTED by w1:pJ for development only, reviewed 07
SHA256 `e9d8cdea55eb1f54049fc0e95c551e63f6632961813c3b4931947b21e5ba5db6`.
The publication edit changes only the stale heading. Privileged bounding-set
reduction precedes credential drop; CLEANUP_PENDING stays broker-private with
existing public IO/retained-handle semantics. Jobs forbid signaling before final
reap releases the PID. Helper implementation, teardown tests, broker policy and
cgroup verification remain NOT_RUN. Source/kernel support probes are distinct
from production authorization or cleanup evidence.

Development contract commit `c861f34efc660bc877022dc3a3950b80bde21877` on main
was pushed successfully and matched the remote ref (exit0).

P04-CHILD-OWNERSHIP-r2 ACCEPTED by w1:pJ for four-file manifest. Private
OwnedChildren retains exclusive direct child ownership through WNOWAIT, disables
signals before nonblocking reap, reserves pending/uncertain slots and never reuses
job IDs. Bounded cancellation retries transient observation/signal failure and
preserves persistent errors. No namespace creation or broker policy is enabled.
`cmake --build build --target check -j2` (`owned-children-host-r2.log`) passed
CTest6/6 including11 new ownership tests (41 adapter tests total), exit0.
Same-source x86 emulator native Debug build/check via `tools/verify.py native-build`
(`owned-children-native-r2/summary.json`) passed CTest6/6; all remote steps and
confirmed-scope cleanup exit0. Initial r1 source archive used the wrong root name,
failed local archive validation before any remote scope, and was corrected; both
failure and retry evidence are retained. r1 native PASS is separate from r2 PASS.
This is a source checkpoint; installed RPM remains Release6. Namespace init, actual
root broker, authorization, cgroup and physical/ARM validation remain NOT_RUN.
Native r2 source archive SHA256 `170049841212c1fac5bb43fa6c8c27ef00708abe0643d8ba5454f5a3e04f7ed6`.


Owned-child checkpoint `3250ff75c5198db97fbf10ce4e2e242c4e16fb37` on main pushed
with exit0 and matched the remote main ref. Namespace setup proceeds as a separate
review scope; the ownership acceptance does not cover it.

P04-NAMESPACE-INIT-r2 ACCEPTED by w1:pJ for six-file private setup/fixture scope.
Manifest SHA256 `bc17e261698651bb40aa452b5a24bf37508e2d05b204a49a552fe44b77dc9570`.
The caller must be a single-threaded trusted broker and open its own proc/mount
namespace anchors before clone. The helper checks mount isolation before mounting,
uses private propagation and namespace procfs, drops app_fw credentials/capabilities,
checks its creator through the pinned proc object, waits for GO, and remains a
non-exec PID1 reaper. The public client/backend is not connected to this helper.

Review r1 found a missing mount-namespace guard and reliance on GO-pipe HUP after
creator death; r2 added both guards and negative fixtures. Host
`cmake --build build --target check -j2` (`namespace-host-r4.log`) CTest6/6 PASS,
exit0; this builds but does not execute the privileged fixture. Native direct
`g++ -std=c++20 -Wall -Wextra -Werror -pthread ...` followed by a30s watchdog
(`namespace-native-r5.log`) passed normal/closed-stdin execution, full post-exec
real/effective/saved IDs, empty groups/capability sets, exact System label, private
proc/mounts, FD/env isolation, setsid descendant exit, wrong clone flags, ENOENT/
EACCES exec, and creator death before setup/before GO/after exec. Retained-writer
cases include creator death before the first prctl. All fixture cases and remote
command exit0. Initial manual compilation caught misleading-indentation warnings
under -Werror; formatting was corrected before these successful build/test runs.
Native clock skew caused tar timestamp warnings; no device clock was changed.

Release7 r1 RPM build/%check CTest6/6 and payload/dependency checks passed
(`namespace-native-rpm-r1.log`, remote0), but it was NOT installed after review
changes. Exact r2 rebuild is pending. No broker service/launcher-unit provenance,
registration trust, cgroup policy, production execution, remount, physical-device
or ARM validation is accepted by these results.


Exact Release7 r2 archive SHA256
`a084c0b28e485ba22c7849473a8f1df6ec664fe4a9c711e211258de1f6905930`.
Native `rpmbuild -ba` built five RPMs, %check CTest6/6 passed, payload/dependency/
unpackaged-file checks completed (`namespace-native-rpm-r2.log`, remote exit0).
Normal `rpm -U --test` then `rpm -U` upgraded runtime/devel/offline-tools/tests to
0.1.0-7. Installed43 unit,41 adapter,18 platform GoogleTests, pure C consumer,
7 offline tests and the explicit root namespace fixture passed, with no skipped
platform tests (`namespace-native-install-r2.log`, remote exit0). The known msm
post-hook warning is retained; package/test outcome does not establish full image
policy health. No setuid bit or file capability is installed for the fixture.
Reviewed hashes were rechecked before publication. Commit/push follows below.


Namespace checkpoint `04cb3356fdc6c5856a5004935f20798b30f0a817` on main pushed
successfully and matched the remote ref (exit0). Subsequent read-only/native
policy investigation found app_fw UID301 with socket label System is actually
ALLOWED by Cynara (`appfw-system-policy-probe.log`, exit0); this does not distinguish
our trusted launcher unit from another process with the same credentials.
Cgroup preflight found v1 CPU/memory/devices/freezer controllers and root-owned
0644 tasks files; no pids controller appears in /proc/cgroups, and its directory
lookup failed as expected (`cli-cgroup-preflight.log`). No cgroup/policy was changed.
The exact image's privilege-group mapping contains priv_platform GID10212, while
security-manager source supports privilege-to-SMACK templates. These are candidate
DB access integration mechanisms, not a verified CapMgr DB policy.


BROKER-01 follow-up source review supports pre/post MAIN socket HUP checks only
under strict trusted-launcher endpoint nondelegation; it does not authorize public
clients or remount. A separately exec'd single-threaded spawn worker is proposed
because rpc-port creates internal threads even without TIDL -t. Contract refinement
is pending peer review; no root broker implementation or service is installed.
Native forced-PID-reuse probe could not complete: unshare --kill-child required
unavailable pidfd_open (`broker-pid-reuse-probe.log`, remote1); retry using a private
PID/mount namespace without that option found no /proc/sys/kernel/ns_last_pid
(`broker-pid-reuse-probe-r2.log`, remote1). Neither attempt proves PID reuse behavior.
DB fixture r1 review requested durable recovery before policy mutation and safe
root deletion. r2 adds a fsynced UUID recovery journal before load2 changes, an
inherited flock preventing recovery while any fixed role survives, best-effort
rule revocation and symlink-resistant cleanup checks. Four unprivileged recovery
tests pass (`db-access-recovery-host-r2.log`, exit0); native policy execution is
NOT_RUN pending re-review. Actual production DB policy remains unprovisioned.


BROKER-01-CONTRACT-r2 ACCEPTED by w1:pJ for doc07 contract SHA256
`fa21036f38cfba562ef747e4268ba41589608a767d1bada9af08cbdc10574698`.
Publication changes only its pending-review heading to accepted. The contract now
requires a separately exec'd single-threaded worker and persistent uncertainty
across worker/front-end crashes; no new START until independent old-job absence
proof. Root services, recovery supervisor and production authorization remain
NOT_RUN. This contract checkpoint is separate from DB fixture implementation.

P07-DB-ACCESS-FIXTURE-r3 ACCEPTED for safety-gated execution, exact script SHA256
`0d6975b2450b89e9da519b7c04cbf435707b1f658817e3912c350cd634d813c9` and tests
`5893088cb5b8cf8a84a157261afedb3563fc319db207e9c36eb2b5b83adc2b1e`.
Host CTest6/6 and seven native recovery tests PASS (db-access-host-check-r3.log,
db-access-recovery-native-r3.log, exit0). Enforced native trusted-parent preflight
passes without policy writes (db-access-parent-native-r3.log, remote0). Explicit
root fixture under180s watchdog passes all eight role/generation checks: real
mode=ro SQLite WAL queries, separate DAC and SMACK read denials, same-UID different
label denial, MAC write denial on DAC-writable fixture objects, and DB/WAL/SHM
0640 owner/group/label preservation after last-close removal and recreation
(db-access-native-r3.log, remote0). Cleanup PASS with no remaining children/rules.
Injected SIGKILL immediately after the first actual load2 write leaves the fsynced
journal recoverable; --recover removes the owned scope and revokes all planned
rules (db-access-abrupt-recovery-r3.log, remote0). Inactive UUID labels and root-only
recovery receipts remain intentionally. These are fixture policy results, not
security-manager provisioning, privileged application end-to-end access or a
production catalog policy. Tool publication/package review follows separately.


Broker contract checkpoint `aa9120236f925f50770b63609ec11dee266b6865` on main
pushed with exit0 and matched the remote ref. P07-DB-PACKAGE-r5 ACCEPTED by w1:pJ
for the unchanged r3 fixture/tests plus Release8 spec and usage documentation;
r4 was superseded before any build after self-check caught a stray install line.
Exact Release8 source archive SHA256
`3b4b0f550226d5409573845f1431b3fe8c13f4380289a406f50c607fdf56f19d`.
Native rpmbuild -ba, %check CTest6/6, payload/dependency/unpackaged checks and five
RPM outputs PASS (db-access-native-rpm-r5.log, remote0). Normal rpm -U --test then
rpm -U installed runtime/devel/offline-tools/tests0.1.0-8; installed102 GoogleTests,
pure C consumer, seven offline tests, namespace fixture and DB policy fixture PASS
(db-access-native-install-r5.log, remote0). Known msm post-hook warning retained.
The fixture's remaining_rules reports failed revocation writes, not enumeration.
A separate read-only kernel load2 audit verified all three run journals had zero
active fixture rule pairs and no remaining data scopes (db-access-kernel-rule-audit.log,
remote0); each journal planned three distinct pairs. Root-only receipts remain.
No production DB policy/template, privilege provisioning or public create/remount
was enabled. Broker journal source WIP is excluded from this package checkpoint.


DB fixture checkpoint `add9c3bcfd5f9e37f69f2211fd6aa3d4f953cfe6` on main
pushed successfully and matched the remote ref (exit0).
P04-BROKER-JOURNAL-r2 ACCEPTED by w1:pJ for the four-file private durable store;
implementation SHA256 `82f8dc74a6756a73694795405d432dfccade62e1b2851d37b96e96938994c49a`.
Generation/reservation writes fsync the file, rename and fsync the directory before
returning admission. Active/uncertain/corrupt/missing/partial state blocks restart;
there is no reset or inferred worker-cleanup path. Four global slots and monotonic
64-bit tokens are serialized with getters; snapshots own their data. Private
completion methods require the caller's trusted normal-cleanup proof. Exact root
storage provenance/image seed and caller quiescence at destruction remain explicit
preconditions. No worker/front-end or production service is connected yet.
Review r1 found unsynchronized access and special-mode acceptance; r2 fixed both
and added concurrent admission plus write/file-fsync/rename/dir-fsync failure tests.
Host `cmake --build build --target check -j2` CTest6/6 PASS
(broker-journal-host-r2.log, exit0); exact-source native g++ build and11 journal
GoogleTests PASS (broker-journal-native-r2.log, remote0). Earlier r1 native8/8 is
separate; initial r1 GTest same-line macro compile failure was corrected before
successful host retries. Installed RPM remains Release8 and excludes this source
checkpoint. Actual worker-crash/live-or-stuck-PID1 recovery is NOT_RUN, not proved
by the journal's separate-process persistence fixture.


Broker journal checkpoint `52d3b96acaaabc4ef2d485678261952ae704795d` pushed to
main with exit0 and matched the remote ref. P04-CHILD-RESERVATION-r1 ACCEPTED by
w1:pJ for reserve-before-clone ownership (owned_children.cc SHA256
`0065c801e8f61d0cf33fbb489ba78581f54eb5650e23cf81d03f4fc3be16ae5c`).
Fixed capacity/token reservation precedes clone; positive return immediately
attaches without allocation or Observe/reap, while failed clone abandons the
unspawned slot and burns its token. Stop still requires fresh ownership observation;
EINTR retries and ECHILD/ESRCH keep uncertainty without signaling. Invalid internal
attach ordering fails stop; a lost worker must preserve frontend journal uncertainty.
The main namespace fixture now uses the exact adjacent attach/abandon branch.
Host CTest6/6 including15 ownership tests PASS (child-reservation-host-r1.log,
exit0); exact-source native15/15 and all namespace isolation/failure/parent-death
fixtures PASS (child-reservation-native-r1.log, remote0). Installed RPM remains
Release8. This acceptance does not cover a worker loop or production CLI route.

Independent Action runtime investigation is reopening the dependency build route:
local source includes a newer matched service/API/TIDL stack, so a read-only source
archive build may be possible. No Action repository edit/push, runtime upgrade or
operational database mutation has occurred; migration/compatibility review and
actual build dependencies must precede any concrete upgrade proposal. Current
installed1.3.27 still lacks subscription/cancel and those paths stay disabled.


Child reservation checkpoint `532d80ca9ccc9b6e643340820b4bd563c77c1016` pushed
to main with exit0 and matched the remote ref. P04-WORKER-COMMAND-r2 ACCEPTED
by w1:pJ for the private anonymous-pipe reader/encoder and tests; implementation
SHA256 `91a1d4b4bb5ba4bbe4ca3209a6bc25ed57306c020c6363cab6a777c7fa5fb9ed`.
Fixed version/generation/sequence/token framing bounds START to64KiB; independent
CANCEL input can progress while START is partial. EOF/HUP, framing/deadline errors
and every escaping allocation exception permanently poison admission. The r1
allocation failure gap is closed with bad_alloc/length_error injection. Host
`cmake --build build --target check -j2` CTest6/6 PASS
(worker-command-host-r2.log, exit0); exact-source native11/11 GoogleTests PASS
(worker-command-native-r2.log, remote0). Earlier r1 native10/10 covers earlier
bytes only. A cli: prefix is syntax, not registered-executable authorization.
Actual worker scheduling, output, parent-loss cleanup and trusted catalog resolution
remain unimplemented; installed RPM remains Release8.

Action migration feasibility remains copy-only. The target snapshot has245 stored
actions and59 legacy provider rows without enabled. A transactional same-version
v4 migration adds enabled DEFAULT1; repeated no-op, SQL-error rollback and crash
rollback preserve existing row digests/integrity (action-v4-copy-migration.log,
remote0). The old f9c43cb listing and new de4d7fc enabled-provider listing each
return exactly the same78 sorted names on pre/post copies, with zero additions or
removals (action-copy-list-comparison.log, remote0; sorted-name SHA256
`46b8dce8f0a7f6a033415c008499a649c359458187d0d620946f04e5f6d53b4e`).
All59 migrated providers remain enabled. Stored245 vs candidate78 is not an
upgrade regression. New ActionSequence deprecated filtering, runtime availability
and actual execution require separate verification against the image resource.
Missing requires_confirmation is not a de4d7fc1.4.2 requirement. Installed package
layout differs from the nominal f9c43cb spec, so version alone does not establish
binary provenance. Scratch SDK downloads are isolated, not installed or claimed
ABI-matched; no operational Action DB, service or runtime package was modified.


Worker command checkpoint `cfd0dd25baea298ad969e822a217a81dacbe8aba` pushed
to main with exit0 and matched the remote ref. P04-WORKER-LOOP-r4 ACCEPTED by
w1:pJ for the seven-file private engine/registry/native fixture scope. Host
`cmake --build build --target check -j2` CTest6/6 PASS
(worker-loop-host-r7.log, exit0). Exact native g++ build and13 GoogleTests PASS;
actual NamespaceInit normal execution, setsid descendants, cancel and output queue
pressure PASS (worker-loop-native-r4.log, remote0). Early failure removes the owned
scope; injected cleanup EIO returns1 without PASS, reports/preserves the exact
root-only directory and is separately verified/recovered. Unknown child cleanup
retains state and fails stop. No native test skips. Earlier compile/include-path
failures and the r1 GO-writer HUP failure remain in their original logs; they are
superseded only by the corresponding successful exact-source runs.

The four-slot single-thread engine prioritizes parent/channel loss and CANCEL,
uses bounded incremental I/O and a fixed reply queue, and sends no-child Complete
only after exclusive WNOWAIT/reap/Release or proven no clone. GO's writer stays
open through cleanup. Review fixed stale precancel slots on capacity rejection,
replaced potentially blocking resolution with an immutable bounded registry,
and made cleanup part of the native fixture verdict. The native fixture loads
one entry from its own temporary SQLite catalog before admission; production
snapshot provenance/subset/invalidation is still a gate. No broker daemon,
frontend durable-journal wiring, image policy, cgroups or public execution is
installed. Package Release9 is a separate pending build/upgrade checkpoint;
installed RPMs remain0.1.0-8 at this source checkpoint.

Action de4d7fc1.4.2 candidate investigation now has a reproducible isolated native
build path. Pinned source tar SHA256
`8719caee9b55f7db594c2e77389e42525eb85ae06eb8629dcc235e11b74c0ad3`;
98 official SDK archives were hash-checked and extracted only under an owned
scratch tree. Existing image SDK/runtime paths fill missing overlay paths; newer
AMD/AUL headers are not thereby declared ABI-compatible. All four TIDL3.1.1
code generations are individually checked. Correct version flags, system-header
classification, basename-only TIDL output and scratch library/include search paths
resolved the recorded configure/compiler/link failures. All candidate targets
build PASS (action-scratch-build-r6.log, remote0). Twenty-five candidate shared
objects pass loader relocation checks (action-candidate-elf-audit.log, remote0);
this verifies symbol resolution, not semantic ABI or service integration.

Full Action CTest4/4 PASS in a distinct mount namespace with recursively private
propagation and a fresh64MiB tmpfs /tmp (action-isolated-ctest-r1.log, remote0).
The host /tmp device/inode stayed unchanged. Isolation was required because two
upstream tests truncate fixed /tmp paths; deploy scripts and live smoke/install
paths were excluded. No Action checkout/Git/runtime/operational DB change occurred.
Image shared action.seq hash
`7620a541552ee57590029d4738795e21f691f4bdd506413e124762840ef18226`
matches f9c43cb and has zero deprecated markers. AMD has no sequence env override;
pkgmgr reports the preloaded catalog root but its res/global sequence is absent.
Ordinary root and app_fw/System fixture resource-API calls return invalid context,
which is not proof of AMD's cached context. Actual resolved sequence and provider
mapping remain gates: de4's bundled sequence moves ten common positional method
IDs and changes names despite having zero deprecated markers. Never replace that
sequence alongside old provider stubs without a verified compatibility matrix.


Worker engine checkpoint `ad4615fe9c242215a8cca0999ebf9b7b375baccf` pushed to
main with exit0 and exact remote-ref match. P04-WORKER-PACKAGE-r1 source and final
publication evidence ACCEPTED by w1:pJ. Release9 source archive SHA256
`e9828d5c86447d96eef012e19d6e107f167d0204786976742389165ff9b730bf`.
Native rpmbuild -ba, %check CTest6/6, payload/dependency/unpackaged checks and five
RPMs PASS (worker-native-rpm-r1.log, remote0). Normal rpm -U --test then rpm -U
installed runtime/devel/offline-tools/tests0.1.0-9; installed43 unit +80 adapter
+18 required platform tests (141 total), pure C consumer, seven offline tests,
namespace fixture and worker fixture PASS (worker-native-install-r1.log, remote0).
Known msm post-hook warning remains an observation. Existing DB-policy fixture
was unchanged and not rerun merely for this packaging increment. The tests-only
worker binary has no service activation, setuid or file capabilities. Root fixture
success requires checked scope removal. The earlier r1 failed fixture scope was
separately removed after confirmed quiescent normal process exit
(worker-r1-scope-cleanup.log, remote0). No Action runtime package was installed.

Release9 checkpoint `fd7520c20ca0beff02ad0faced055f9dc3674a9d` pushed to main
with exit0 and exact remote-ref match. The reviewer confirmed its final packaging
publication gate separately from production broker/public execution enablement.

P04-WORKER-SESSION-r1 is IN_REVIEW. The private frontend session durably begins a
worker generation and reserves every START before transmitting its first byte.
Independent bounded command/cancel queues and a bounded reply decoder correlate
exact generation/sequence/token, enforce partial-transfer deadlines and retain
uncertainty on channel/protocol/persistence failure. Only a validated Complete
followed by successful journal persistence releases a reservation. Buffered
Complete frames are drained before reply EOF; clean EOF still requires independent
normal-worker-exit proof. No-child worker rejection is integrated with the actual
WorkerLoop. Spawn/authentication/unit/cgroup/production activation remain absent.
Host `cmake --build build --target check -j2` CTest6/6 PASS
(worker-session-host-r4.log, exit0). Exact four-file manifest checks and native
g++/GoogleTest34/34 PASS (worker-session-native-r1.log, remote0). Persistence tests
inject write, file-fsync, rename and directory-fsync errors at both Reserve and
Complete; other tests cover partial START cancellation, malformed/lost replies,
concurrent capacity, post-completion duplicates and restart refusal. Earlier host
GTest same-line macro/compiler diagnostics were fixed before these passing runs.
These tests do not spawn a privileged namespace workload or change image policy.

Action candidate follow-up corrects the initial build-shape limitation: the first
successful scratch configuration built its core static by upstream default.
An explicit BUILD_SHARED_LIBS=ON rebuild passes (action-scratch-shared-build-r1.log,
remote0); twenty-seven candidate shared objects pass loader relocation checks
(action-shared-elf-audit.log, remote0). Full CTest4/4 again passes in the private
mount namespace/tmpfs /tmp with unchanged host /tmp identity
(action-shared-isolated-ctest-r1.log, remote0). These are symbol/test evidence, not
semantic SDK ABI or live service compatibility. Candidate shared SqliteDb code
reads only the owned migrated copy with an explicit image sequence, lists78 names,
gets each schema and enumerates providers (action-copy-runtime-r3.log, remote0).
The sorted native name digest exactly matches the old/new SQL copy-query digest
`46b8dce8f0a7f6a033415c008499a649c359458187d0d620946f04e5f6d53b4e`
(action-copy-native-names-comparison.json, PASS). Stored action rows remain245.
Read-only image AUL lookup identifies AMD2305/uid301 as
`d::org.tizen.action-framework.service`; pkgmgr usr-appinfo lookup for that exact
identity returns -3 (action-directory-probe.log, remote0). Local DirectoryInfo
source would return no resource context on that result, consistent with shared
sequence fallback, but this does not inspect AMD's cached in-process context.
Provider positional-ID compatibility and actual matched service integration stay
open. No Action runtime upgrade or operational database mutation was performed.

P04-WORKER-SESSION-r2 ACCEPTED by w1:pJ for the exact four-file manifest.
The r1 review found that a late State/ENOENT was accepted for any retired token;
r2 instead records four exact CANCEL entitlements, marks them sent only after the
full frame write and consumes each at most once. Cancelled Complete proves live
consumption; other outcomes retain ambiguity until a matching late State or EOF.
Correlation exhaustion closes admission and persists uncertainty. Host
worker-session-host-r5.log CTest6/6 PASS and exact native
worker-session-native-r2.log38/38 PASS, remote0. No-CANCEL, duplicate and consumed
CANCEL State fixtures now reject the former permissive path. This closes the
private session finding only; no spawned production worker, external exit proof,
TIDL admission/authentication, cgroup or public backend is enabled. Installed
packages remain Release9, whose tests predate this private source checkpoint.

WorkerSession checkpoint `1ccf11115f147ec6a9e3e8c50fa056d416e44ede` pushed to
main with exit0 and exact remote-ref match. P04-WORKER-SPAWN-r2 is IN_REVIEW for
six files: a private fixed-image posix_spawn/descriptor primitive and its fixtures.
Production image path is compiled in but that worker image is not installed by
this checkpoint. Five source FDs are duplicated above the fixed child slots,
null stdio and slots3..7 are mapped, then closefrom8 removes every unrelated FD.
Signals/environment are fixed, and OwnedChildren reservation precedes spawn with
immediate Attach on success or Abandon on failure. Success only establishes direct
child ownership, not exec/READY, catalog trust, no old jobs or production admission.
No live SQLite object is inherited; passing a directory does not itself implement
trusted WAL/SHM lookup or snapshot invalidation.

Host worker-spawn-host-r3.log CTest8/8 PASS (exit0). Native first run deliberately
failed before spawn because target umask0000 generated a root:root0777 fixture;
worker-spawn-native-r1.log records1/6 PASS,5/6 FAIL, remote1. Mode probes explicitly
show euid0, old mode777 and new mode755. Only the generated fixture build step was
changed to chmod0755; image permission checks remain strict. Exact r2 native hash
checks6/6, six spawn tests and one real ENOEXEC test PASS
(worker-spawn-native-r2.log, remote0). Fixtures cover non-CLOEXEC socket/high FD
isolation, closed standard FDs, signal mask/environment reset, immediate owned
cleanup, exit7/127 distinction and exec-format failure with burned ownership token.
The separate single-thread worker bootstrap, actual TIDL FD fixture, trusted
catalog/namespace identity checks, image policy and frontend normal-exit proof
wiring remain later gates. Installed Release9 does not contain these new tests.
P04-WORKER-SPAWN-r2 ACCEPTED by w1:pJ for those exact six files and the separate
host/native evidence. Only the generated fixture's build permissions changed from
r1; no production permission guard was relaxed. This closes the private descriptor
transport/ownership review, with all bootstrap/auth/catalog/exit-proof gates above
still open.

Fixed-worker spawn checkpoint `6adf4660320ba0bb74bc483bd357d8f290613cb4` pushed
to main with exit0 and exact remote-ref match. P04-WORKER-CATALOG-r1 ACCEPTED by
w1:pJ for four files implementing startup-only CLI snapshot loading. The caller
supplies the provisioned writer UID/group and exact directory/file modes; this
supports a live writable catalog instead of incorrectly requiring root ownership.
Directory/DB/WAL/SHM DAC, ACL, links, types and identities are checked. One RO
transaction reads schema/revision and published CLI entries, excluding pending and
other kinds and rejecting more than256 entries. The SQLite connection closes
before return; no worker-loop lookup performs database I/O.

Host worker-catalog-host-r4.log CTest8/8 PASS; exact native
worker-catalog-native-r1.log10/10 loader tests PASS, remote0. Tests include live
WAL updates, pending invisibility, file policy rejection, missing/linked sidecars,
verified sidecar recreation, schema/corruption, pre-load directory rename and
unchanged catalog data. An initial development test run used BEGIN IMMEDIATE on
the RO connection and failed; it was corrected to an explicit read transaction
before these final runs. Host SQLite3.45.1 and native3.50.2 both canonicalize the
procfd input into a normal pathname. This is expressly not an FD-only VFS or a
hostile rename/ABA proof: stable trusted ancestors, authorized writer and SMACK
policy remain prerequisites. Source invalidation, bounded production subset,
execution path trust and bootstrap READY/spawn wiring remain unimplemented gates.
Installed packages remain Release9; no production worker image is installed.

Worker catalog checkpoint `f86f41ee12c2aa93cc2bf7f0121b0fa3adfd1999` pushed to
main with exit0 and exact remote-ref match. P04-WORKER-SUPERVISOR-r1 is IN_REVIEW.
The private coordinator takes sole WorkerSession ownership, requires a complete
fixed32-byte CWB1 bootstrap record, exact EOF and an owned-live worker before
admission, and rechecks ownership immediately before START/Reserve. A five-second
absolute deadline includes no-data and retained-writer cases. Normal exit combines
observed/reaped owned worker exit0 with Session's already confirmed jobs and clean
reply EOF; buffered Complete remains readable after worker exit. Failure retains
journal uncertainty and external child-table cleanup responsibility.
Host worker-supervisor-host-r2.log CTest8/8 PASS and native exact-source
worker-supervisor-native-r1.log18/18 PASS, remote0. These use injected child
operations, not a real FD8 bootstrap emitter. Actual fixed descriptor mapping,
single-thread root worker bootstrap, trusted snapshot/invalidation, backend and
production activation remain unimplemented; installed RPMs remain Release9.

The reviewer inspected exact AMD1.80.14 source85fbad35 vs1.80.19f0d272f6 and
AUL0.83.17 source00ea297 vs0.83.21 source13cb18c without mutations. AMD module C-hook
header/loader bytes are identical: int AMD_MOD_INIT(void), void AMD_MOD_FINI(void),
matching de4d7fc Action's exports and module path. No struct/vtable ABI crosses that
loader boundary. Used AUL appid/pkgid-by-PID-and-UID signatures are unchanged;
newer same-process/team appid behavior is contextual and not exercised by de4's
external-peer lookups. No concrete direct hook/signature incompatibility was found.
This supports source compatibility only, not exact installed binary provenance,
AMD load/start/stop, team-context semantics or matched Action runtime upgrade.
P04-WORKER-SUPERVISOR-r1 ACCEPTED by w1:pJ for the exact four-file coordinator
scope and separate host/native mocked-ownership evidence. No blocking finding;
actual FD8/bootstrap image behavior remains outside this acceptance.

Worker supervisor checkpoint `f8e8d81444817cc742aaff2f9923edffedb648ec` pushed to
main, exit0, exact remote-ref match. P04-WORKER-FD8-r1 ACCEPTED by w1:pJ for the
five-file fixed-spawn/fixture revision. Six sources duplicate above FD8 before
ordered mapping to3..8; closefrom9 excludes other handles. Dedicated READY pipe
must be independent and write-only. The separate fixture emits CWB1 revision12
(synthetic, no catalog load), closes8 and exercises real supervisor admission.
Host `cmake --build build --target check -j2`, worker-ready-host-r1.log, exit0,
CTest8/8 PASS. Native worker-ready-native-r1.sh on emulator-26101 x86_64 verifies
manifest5/5, spawn9/9 and ENOEXEC1/1 PASS, worker-ready-native-r1.log remote0.
Closed stdio/high descriptors, retained READY writer, clean owned exit and death
after READY are covered. Generated fixture mode0755 is retained; image checks
are not relaxed. Installed packages remain Release9.

Root bootstrap remains unimplemented. The independent engineering review requires
actual creator/procfs anchoring, fixed privilege/SMACK/namespace context, exact
FD table and CLOEXEC reset, provisioned stable live-WAL catalog path policy and
executable provenance. Verify one task after startup loading before READY, then
preserve single-thread behavior structurally; do not add per-clone procfs
enumeration to the cancellation-critical loop. Quiescence alone cannot prove
queued Complete/output was flushed. Source revision is not an authorization or
validity lease. Root service/authentication/cgroups and production enablement are
still OPEN; these fixture tests do not close them.

FD8 checkpoint `77a1391e30543ebb4c63644697172afdcde96bd0` pushed to main with
exit0 and exact remote-ref match. P04-WORKER-DRAIN-r1 ACCEPTED by w1:pJ for the
three-file local exit predicate. CanExitCleanly requires closed admission, zero
owned children/job slots, empty queue/offset and no transport loss. Saturated
output may be child-quiescent while this predicate remains false; lost reply
consumer never qualifies. This reports local writes only, not frontend receipt.
WorkerSession still must durably confirm Complete, drain EOF and combine that
with owned worker exit0 before releasing the generation.
Host worker-drain-host-r1.log: `cmake --build build --target check -j2`, exit0,
CTest8/8 PASS. Native worker-drain-native-r1.sh, manifest3/3 and14/14 loop/registry
tests PASS, worker-drain-native-r1.log remote0. Native tests use forked protocol
fixtures; no new root worker image or production route is installed or activated.

Worker drain checkpoint `cd5d71f93649bfa764303a6865fda6e5ad3e3a61` pushed to
main with exit0 and exact remote-ref match. P04-WORKER-BOOTSTRAP-r1 received
CHANGES_REQUESTED before native root execution. Root fixture cleanup lacked
ancestor/mount/ACL and pinned-scope checks; fixed-image leaf permissions did not
prove trusted ancestors; post-load validation did not detect unexpected FDs.
Native r1 was BUILD_ONLY (bootstrap-build-native-r1.log remote0), never executed.
Read-only bootstrap-path-preflight-r1.log found the historical development parent
root:root0777 even though image/probe leaves were0755. The old tree is preserved.

The r2 source revision adds enforced fixture ancestor/no-ACL/local-ext4/PID1-
namespace preflight, scope inode pinning and cleanup checks, and exact post-load
FD verification including only six known WorkerLoop aliases. Separate fork-only
context checks cover invalid expected caps/label/groups, NNP, early/late threads,
missing/extra/aliased descriptors and post-load/post-loop FD leakage. These are
build-only fixtures with no installed service, global policy write or operational
DB use. Host bootstrap-host-r8.log CTest8/8 PASS; earlier build diagnostics (new
target before reconfigure and -Werror indentation) are retained and superseded.
A new root0700 build scope directly under verified /opt/usr was created using
fail-closed preflight; bootstrap-trusted-build-create.log records its exact local
path. Source and pinned JSON3.11.3 archive hashes are checked before fresh native
compilation there. Root execution remains NOT_RUN pending revised fixture review;
production root image/policy/proc provenance/catalog invalidation remain OPEN.

Bootstrap r2 safety/source review was ACCEPTED before execution, but the first
native context run failed (bootstrap-context-native-r2-retry.log, manifest10/10,
remote1). An earlier watchdog invocation used incorrect CLI syntax and exited2
without executing the fixture; that log is retained separately. The reduced
fixture could inspect itself but not the full-cap parent's namespace. Exact
4.4-string source and bootstrap-cap-inspection-native.log identify ptrace cap
checks. Adding SYS_PTRACE only to the explicit fixture policy was reviewed;
a diagnostic aliased-FD SIGPIPE bug was fixed before native r3 execution (NOT_RUN).

R4 source/staged-execution review was ACCEPTED, but its named valid-only context
also failed before every other case/main workload: bootstrap-context-valid-native-r4.log
remote1, creator procfs stage. bootstrap-cap-inspection-native-r4.log confirms the
parent namespace now opens, but /proc/1 directory and namespace still return EACCES.
The actual PID1 label is System::Privileged; read-only smack policy evidence shows
User::Shell has wx, not read, to that label. No MAC_OVERRIDE, task relabel or global
policy rule was added. Main NamespaceInit fixture remains NOT_RUN.

A full-cap read-only pre-drop probe opens PID1 pid/mnt namespace objects and
matches self (bootstrap-predrop-namespace-probe.log remote0). The r5 revision
captures these two handles before reduction, checks their original identity
against self after loading, and closes them plus rechecks the exact final FD table
before READY. It never reopens cross-label PID1 after reduction. Four additional
negative witness cases bring the context matrix to19; native outcomes are pending
r5 review. Host bootstrap-host-r13.log CTest8/8 PASS. The namespace checks still
assume image-trusted procfs/platform PID1 and no hostile root mount administrator;
this is no production policy/provenance acceptance.

P04-WORKER-BOOTSTRAP-r5 source/staged execution was ACCEPTED by w1:pJ for
exact10-file manifest capmgr-bootstrap-r5.sha256. Native build in the protected
root0700 scope: bootstrap-trusted-build-r5.log manifest10/10, remote0. The required
execution order then passed: bootstrap-context-valid-native-r5.log valid-only;
bootstrap-context-native-r5.log all19 context cases; bootstrap-main-native-r5.log
all6 modes (wrong-parent, wrong-directory, unsafe-catalog, retained-ready, idle,
run). Each invocation checked manifest10/10 and used
`python3 tools/run_bounded.py --seconds 120 -- build/<probe> --run-root-fixture`
(with `valid` only on the first context invocation). All three logs record
CAPMGR_REMOTE_EXIT=0 and SDB transport0. Earlier failed r2/r4 logs remain evidence
of the resolved namespace-read issue, not successful runs.

The main run uses real NamespaceInit with registered fixture /usr/bin/true,
verifies durable Complete and owned normal worker exit, and checks cleanup before
PASS. All6 uniquely named scope paths report REMOVED_SCOPE; no uncertainty scope
was retained. This is lifecycle evidence, not CLI JSON result/product execution.
No operational DB, global SMACK rule, live service, package installation or old
writable build tree was changed. Native runtime evidence was sent to w1:pJ for
final checkpoint review. Fixture-only SYS_PTRACE, trusted initial procfs/PID1,
no hostile root and structural single-thread assumptions remain explicit.
Production catalog SMACK/writer isolation, revision invalidation, executable
registration, caller/broker authorization and resource controls remain OPEN.

P04-WORKER-BOOTSTRAP-r5 runtime checkpoint ACCEPTED by w1:pJ after exact10/10
hash recheck and independent read-only log/source review. The clock-skew warning
in the native build is retained; explicit affected-target recompilation and the
subsequent exact-hash executions provide the stated evidence. This acceptance
is for the private fixture only. Publication scope: reviewed10files plus this
append-only evidence ledger; raw logs, generated binaries and packages excluded.

Bootstrap checkpoint aed8ffd3d193313b49cf854401131befee78d927 committed and
pushed to main, push exit0, exact remote-ref SHA match. Next checkpoint prepares
tests-only Release10 packaging for the accepted bootstrap fixtures with a separate
installed-path probe. Packaging/source review and native RPM/upgrade evidence are
pending; no production worker path or service activation is proposed.

### Current requirement coverage after the bootstrap checkpoint

These are scoped implementation/evidence states, not full product acceptance.
Chronological entries above identify exact revisions and logs; source/test paths
below are portable entry points. OPEN/NOT_RUN gates prevent claiming completion.

| Requirement | Implemented and verified scope | Remaining product gate |
|---|---|---|
| R01 | C++20, C ABI, RPM, generated TIDL and MAIN/callback fixture | Production AMD module and service deployment OPEN |
| R02 | Local RO foreach/search/get and result lifetime tests in test/unit/client_test.cc | Authorized production create and real client DB policy OPEN |
| R03 | Direct parser core and explicit offline transaction tool, separate-process tests | Authoritative installer/MIC finalizer BLOCKED; production parser fails before staging |
| R04 | Action snapshot/import, atomic publish and retry revision replay tests | Actual source writer feed, AMD startup and reconnect notification OPEN |
| R05 | Repeated/semicolon metadata parser fixtures in test/unit/parser_test.cc | Installed parser lifecycle remains blocked by INSTALL-01 |
| R06 | Ownership, update, App Skill identity and reservation tests against SQLite | Real installer conflict/rollback propagation OPEN |
| R07 | Kind/path contracts and private namespace identity experiments | PATH-01 unresolved; actual remount/access/rollback NOT_RUN |
| R08 | Action projection/entity/provider fixtures and optional outputSchema handling | Full installed source-to-public-client path OPEN |
| R09 | FTS5/BM25 corpus and fixture benchmark | Product/device corpus and latency acceptance NOT_RUN |
| R10 | Literal argv runner, private worker/PID1/session/bootstrap fixtures | Authenticated app_fw launcher, executable trust and live invalidation OPEN |
| R11 | Mock public lifetime/cancel/stream frames; private worker cleanup | Matched Action subscription runtime and production per-client job route OPEN |
| R12 | Dual-stream errors, native envelope/ID mapper and public callback validation | Actual CLI/Action adapters through production transport NOT_RUN |
| R13 | Cynara/peer primitives, TIDL binding, UUID DB DAC/SMACK and bootstrap fixtures | Original-client authority, provisioned catalog/job labels and safe remount OPEN |
| R14 | WAL/FTS publication, schema migration, corruption/replay tests | Production AMD recovery, installer finalization and sidecar policy OPEN |
| R15 | Bounded queues/requests/output, private admission limits and runtime cleanup | Cross-process client/global enforcement and image resource controls OPEN; pids controller absent |
| R16 | GTest/GMock, pure C consumer, CTest/RPM failure propagation | Exact Release10 native RPM check PASS; installed test scope recorded below |
| R17 | Explicit target build/verification tools and installed fixture packages | Whole-product emulator smoke and physical-device execution NOT_RUN |
| R18 | Fixed-width API/pipe values and overflow tests | ARMv7 build/runtime NOT_RUN; P09 gate remains closed |

### Release10 bootstrap fixture packaging and installed execution

P04-BOOTSTRAP-PACKAGE-r1 source proposal (CMake/spec/tools README, manifest3/3)
and administrative current-coverage board were ACCEPTED by w1:pJ. The new target
compiles the accepted probe against the installed fixture path; the build-tree
probe and default production worker path are unchanged. Host reconfigure/new
target build and CTest8/8 PASS in bootstrap-package-configure-r1.log and
bootstrap-package-host-r2.log. An earlier attempt to name the new target before
reconfigure failed; bootstrap-package-host-r1.log is preserved.

Native archive SHA256:
51cb9d4881553b1210ebbc1bd59ff7023179ffda056d76ce4d0373f951b45c14.
Source, spec and pinned JSON archive hashes were checked in a new root0700 RPM
subtree of the protected bootstrap build scope; no old writable tree was used.
`rpmbuild -ba --define "_topdir <protected-root>" --define 'capmgr_tizen 1'
SPECS/capability-manager.spec` used Release -O1/-DNDEBUG, parallel1 and real TIDL/
Cynara. bootstrap-package-native-r1.log records CTest8/8, a clean unpackaged-file
check, five RPM outputs, remote0/transport0.

bootstrap-package-path-r1.log confirms RPM _libexecdir and the generated installed
probe definition agree on /usr/libexec. bootstrap-package-payload-r1.log records
each binary RPM's SHA/dependencies/scripts/payload/permissions: all three new
binaries belong only to tests, root:root0755, no file capabilities or scriptlets.
Tests RPM SHA256 ab4c93dea3e370b398e8f567eae0cce83b3da457c4d737c051f53dc6208a7267.

bootstrap-package-install-r1.log records normal `rpm -U --test` then `rpm -U`
without force, and runtime/devel/offline-tools/tests all0.1.0-10. Installed unit/
adapter/peer GoogleTests pass43+147+18=208, pure C consumer exit0, offline tests7/7.
Then installed valid-only context, all19 context cases, and all6 main modes pass
in the documented order under120s watchdogs. Each main scope is removed before
PASS; no retained scope appears. Final remote0/transport0. The known platform
`Plugin msm: hook tsm_post failed` warning remains in the log; it did not change
the transaction/test outcomes. This is fixture/package validation, not product
service activation. P04-BOOTSTRAP-PACKAGE-r1 final publication/runtime review
ACCEPTED by w1:pJ after unchanged manifest3/3 and read-only evidence inspection.

Release10 packaging checkpoint9865198a50bd4e7cb119abdfe7d4059d073a231f pushed
to main, exit0, exact remote-ref match. Next private WorkerResult work assembles
separate provisional output and waits for durable Complete, with no Dispatcher
backend connection. Review guidance requires coordinator handling of valid late
CANCEL State separately, lossless conservative dual-response comparison, exact
client/worker/RPC correlation and retained cleanup uncertainty on session loss.
Those constraints are in the proposed development clause and unit/session tests;
source review and native same-source tests remain pending.

P04-WORKER-RESULT-r1 exact6-file review submitted to w1:pJ: CMake, private
collector header/source, collector tests, Session integration tests and proposed
07 clause. Host worker-result-host-r3.log `cmake --build build --target check -j2`
passes CTest8/8. Focused host and native13/13 tests pass: worker-result-host-focused-r1.log
and worker-result-native-r1.log. Native checks manifest6/6 then configures/builds
capmgr-adapter-tests in the protected root and runs
`--gtest_filter=WorkerResult.*:SessionTest.ResultCollector*:SessionTest.Coordinator*`,
remote0/transport0. Tests include real temporary BrokerJournal fsync failure:
WorkerSession withholds Complete, the collector remains uncertain and no result
is delivered. A separate real Session fixture consumes a valid late CANCEL State
without reentering the sealed collector or producing a second result.

The comparison tree preserves numeric lexemes to prevent high-precision conflicts
from becoming equal through double rounding, while tolerating object order and
string-escape differences. One native envelope is returned unchanged only after
durable Complete; worker failure and signal termination override provisional
output. Production result delivery and bounded retained-cleanup coordination remain
OPEN. Installed Release10 predates this collector and is not evidence of its
production use; this same-source native run is a separate private-test result.

WorkerResult-r1 source review requested changes for a terminal allocation window:
Complete() could become true before a RunResult existed. The r2 revision retains
the confirmed event in a fixed terminal-pending state, rejects further events,
and retries result construction without new cleanup evidence. Only fully built
nothrow-movable results seal delivery. Parse/comparison/synthetic-failure injection
covers bad_alloc and length_error, plus one synthetic comparison-range failure.
Admission now matches WorkerCommand depth64 and nonempty CLI suffix.
Host worker-result-host-r5.log CTest8/8 PASS supersedes a test compile error caused
by two EXPECT_THROW macros on one source line (r4 log retained). R2 exact source
re-review and native focused run are pending; R1 native results do not substitute.

P04-WORKER-RESULT-r2 source/contract and native completion ACCEPTED by w1:pJ.
worker-result-native-r2.log verifies manifest6/6, recompiles worker_result.cc and
both changed tests, then focused15/15 passes (13collector+2Session), remote0 and
transport0. Host CTest8/8 is separate evidence; no installed Release10 collector
or root/native workload/backend activation is inferred.
Original accepted07 SHA256: 857f600ee279ac8b0ba6be8e1b032f8fcb1d3fe28c4e1040555c940215a8ccd5.
The reviewer-authorized heading-only accepted-status edit preserves every clause;
publication07 SHA256: 9340cecdea254906f5a23e69fca367ec46d5e24088e559cca9d505a93cccf2e7.
Publication scope is the reviewed6files plus08; generated artifacts/logs excluded.
Next integration gate is retained cleanup coordination with Dispatcher/API destroy:
existing framed work returning/throwing while cleanup is uncertain would emit an
early synthetic terminal and must not be used as the production adapter.

## P06 bounded destroy foundation (2026-09-28, review pending)

Prior WorkerResult-r2 was committed/pushed as
1b5cd464f3c9504fb63845f54307f55a19fe4a29 on main; push exit0 and exact
origin/main were verified at publication. The next eight-file CLOSE-BOUNDS-r1
manifest is /tmp/capmgr-close-bounds-r1.sha256 (ephemeral local evidence).
It changes private Close to DONE/BUSY/IO_PENDING, preserves handles/capacity on IO,
rejects new execute before backend Admit, and moves observed-exited joins outside
the active callback interval. Public symbols/declarations/errors are unchanged;
header changes document the existing retained-IO lifetime contract.

Commands: cmake --build build --parallel2 then ctest --test-dir build
--output-on-failure: close-bounds-host-r3.log exit0, CTest8/8 PASS. R1/r2 host logs
are earlier successful iterations, superseded by exact r3 source. In the protected
root0700 /opt/usr/capmgr-bootstrap-build-p4h4og7o/source scope, sha256sum -c
verifies8/8; cmake --build build --target capmgr-unit-tests capmgr-c-consumer
--parallel1, capmgr-unit-tests and capmgr-c-consumer complete with remote0 and
transport0 in close-bounds-native-r1.log. Native49/49 GTests PASS (33 CatalogTest,
16 Dispatcher); native clock-skew warnings are retained alongside explicit affected
source recompiles. close-bounds-exports-r1.log lists the unchanged12 C exports.

Tests cover bounded IO/retry, closed admission before Admit, retained process
capacity, callback BUSY without mutation, post-callback unfinished workers, and
both worker and dispatcher blocked thread-local destructors. Exit readiness uses
std::promise::set_value_at_thread_exit, whose shared-state readiness follows TLS
destruction (C++ draft futures.promise); a body-done flag is not equivalent.
No test claims real-time scheduling or bounded arbitrary backend destructors.

This is a legacy thread-lifetime foundation, not managed child-absence proof.
WorkerResult remains unwired; a distinct retained-owner path and concrete
Session/journal coordinator remain OPEN. No RPM/installed Release10, root workload,
production backend, authentication or ARM result is inferred from these checks.

P06-CLOSE-BOUNDS-r1 ACCEPTED by w1:pJ for exact8files and host/native evidence.
Accepted07 hash: 56f9f46372f6453444e5808d4307297a0cab76a23f6105f1f6fd47d3d8904851.
The publication-only heading edit marks acceptance without changing clauses.
Managed cleanup/journal proof remains the next scope; legacy thread exit is not
child-absence evidence. Publication includes those8files plus08 bookkeeping.

## P06 retained managed cleanup path (2026-09-28, review pending)

CLOSE-BOUNDS-r1 was committed/pushed as
9a8b9dee5d8c92a129800f294507dfd62ee54e32 on main; push exit0 and ls-remote
refs/heads/main matched exactly. Managed cleanup is a separate seven-file scope,
tracked by /tmp/capmgr-managed-cleanup-r2.sha256 (ephemeral local evidence).

The new private ExecuteManaged retains one-use owner/token/capacity before Run.
A base-owned coordination thread publishes immutable cleanup/terminal/version
snapshots and alone performs Session/Journal operations/destruction. Its exit
future/join covers TLS independently of Run's exit; no subclass bool can declare
quiescence. Confirmed proof and publication storage are preallocated, with a
retained materialization retry test. Poll/Cancel/Close never acquire the mutex
held by a fsyncing Session/Journal. Final owner release occurs outside dispatch
locks. There is no production backend/factory/API route or generation reset.

Host managed-host-r2.log: cmake --build build --parallel2, then ctest --test-dir
build --output-on-failure, exit0 and CTest8/8 PASS. The new11 tests exercise real
WorkerSession ConfirmJobGone blocked/failed fsync, retained journal/reservation,
coordinator TLS, callback cancellation, pending Run return/exception, capacity,
terminal construction/publication retry and owner-destruction reentry. The first
focused host run failed because the test expected a recoverable journal object
after failed fsync; existing state.next intentionally makes the constructor reject.
The corrected fixture checks the preserved marker/reservation and refusal instead;
managed-focused-host-r2.log focused8/8 PASS preceded the final three added tests.

Exact native r1 verified7/7 but reported59/60 PASS and remote1/transport0. The
invalid-terminal test exposed an initially empty dispatcher wait missing managed
job progress: a body-done notification could precede exit-future readiness and
managed work emits no legacy reply. R2 wakes that wait on job presence and notifies
admission. The failed log is preserved; managed-wakeup-host-r2.log repeats the
initially-idle regression25times PASS. Native exact-r2 full60+C+25repeat run and
independent r2 verdict remain pending. Installed Release10 is earlier bytes and
supplies no evidence for these new private paths; production/ARM remain unclaimed.

P06-MANAGED-CLEANUP-r3 ACCEPTED by w1:pJ for exact7files, host CTest8/8 and
native61/61 GTests plus C consumer, remote0/transport0 (managed-native-r3.log).
The r2 native60/60+C+25repeat PASS closed the wake failure, but review then found
map insertion could throw before outside-lock retirement. R3 moves insertion into
the catch and tests injected bad_alloc with reentrant owner destruction, token0,
next token1 and all four capacity slots. No owner is dropped under that lock on
the tested admission failure. Earlier rejected revisions remain historical evidence.
Accepted07 SHA256: dd708035d0d694cd78a14dbe7f16bd4145bd1e6b861fb96e5361402e624d657d.
Publication changes only the heading status; all accepted clauses remain intact.
The next checkpoint is private injected C API managed admission and lifetime tests;
production factory, worker service, authority/resource controls remain OPEN.

## P06 injected managed public C route (2026-09-28, review pending)

Managed cleanup-r3 was committed/pushed as
b5dd09925e51eef33c9002c3cffd960da3dc6fbd on main; push exit0 and exact remote
refs/heads/main verified. The next eight-file CLIENT-MANAGED-r1 manifest is
/tmp/capmgr-client-managed-r1.sha256 (ephemeral local evidence).

Private PrepareManagedCli copies the catalog-bound entry/original request before
any thread, reservation or START. Only CLI uses it; Action/default legacy behavior
is unchanged. Public execute returns token0 on factory permission/allocation
failure and never calls Admit/Prepare after destroy has closed admission. Managed
cancel now reports IO if the coordinator body already ended, retaining the flag
without claiming an available processor or termination proof.

Host client-managed-host-r6.log: build plus CTest8/8 PASS, exit0. Exact native
client-managed-native-r1.log verifies8/8, rebuilds affected API/managed/test targets,
then capmgr-unit-tests66/66 and C consumer PASS, remote0/transport0. Five new public
API fixtures cover blocked/failed ConfirmJobGone fsync, IO-retained handle/data,
pre-Admit/Prepare counters, cancellation request observation and ended-coordinator
IO, original native bytes after durable proof/nonzero exit, factory denial/OOM
without reservation, and Action excluding the CLI factory.

Earlier host r1/r3 and focused r2/r4 failures were fixture assumptions that bounded
CMW1/CWR1 decoders consume header plus body in one call. Diagnostics identified
request binding before the intended fsync gate. Bounded repeated ReadOne/Step
resolved this; focused r5 four cases passed before adding factory OOM for final r6.
The logs are retained. These are anonymous-pipe test-owner results, not a real CLI
worker job, deployed backend, installed Release10 update or platform authorization.
The concrete test owner scopes all Session/Journal destruction within Coordinate.
Production prepared admission, service/generation/resource control gates remain
OPEN and the platform create gate remains fail-closed.

P06-CLIENT-MANAGED-r2 ACCEPTED by w1:pJ for exact8files, host r7 CTest8/8 and
native r2 66/66 plus C consumer (remote0/transport0); unchanged12 C exports.
R1 review requested precise cancellation wording: OK means the atomic request won
against Run's end marker, not that Coordinate observed/processed it; IO means that
marker was already visible. R2 also uses bounded receive for Complete. Earlier r1
native66/66 is separate evidence, superseded by exact r2 rebuild/run.
Accepted07 SHA256: f27761cce9a05b4a20fc22830cb11ce85b954d66a98a25c9139efc1dc011932a.
Only the pending-review heading changes for publication; clauses are unchanged.
The separately accepted Release11 spec revision is only10->11; RPM build/%check,
payload/script/dependency audit, normal upgrade and installed tests remain pending.


## P06 cleanup Release11 package checkpoint (2026-09-28)

CLIENT-MANAGED-r2 was committed/pushed as
8f100b8d8a98f6adcebe1b11fef6c7abf5bad4da on main; push exit0 and exact remote
refs/heads/main verified. The accepted spec-only CLEANUP-PACKAGE-r1 revision
changes Release10 to11 (SHA256
d926e46429547319ed7740578f779ac8a262bb37537fd48513e3a06174d22fef).
P06-CLEANUP-PACKAGE-r1 final publication gate is ACCEPTED by w1:pJ; source
and package/runtime evidence are separately recorded.

Native x86_64 source archive SHA256:
cca993007b1654557af8e53228e0b1efd22376fed11c411440e0a31c5103ef46.
Pinned JSON archive SHA256:
0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406.
The source archive contains reviewed source8f100b8 plus the accepted Release11
spec, without raw logs or build products. Evidence remains ephemeral under
/tmp/capmgr-evidence; paths here identify local records, not portable dependencies.

- cleanup-package-native-r1.log: inputs.sha256 verified; protected root0700
  build scope, umask022, run_bounded.py1800s, rpmbuild -ba with capmgr_tizen1;
  native RPM %check CTest8/8 PASS, unpackaged-file check completed, four binary
  RPMs plus SRPM written, CAPMGR_REMOTE_EXIT0 / TRANSPORT_EXIT0.
- cleanup-package-payload-r1.log: rpm -qp file owners/modes/caps, requirements
  and scriptlets for all four packages. Root-owned executable0755 payload;
  no setuid/filecap, new service or scriptlet. Private bootstrap fixtures remain
  tests-only. Runtime/devel/offline/tests dependency resolution is also exercised
  by the subsequent normal rpm -U --test transaction. Remote0/transport0.
- cleanup-package-install-r1.log: all four installed0.1.0-11. Owner-run method
  was bounded180s normal rpm -U --test followed by rpm -U for runtime/devel/
  offline-tools/tests (script /tmp/capmgr-install-release11.sh); command lines
  are not echoed in the captured log. The reviewer verified resulting versions
  and tests, not those command flags from log text.
  Installed unit66/66, adapter162/162, peer18/18 (required no-skip mode), pure C
  consumer and offline Python7/7 PASS; remote0/transport0. The earlier request's
  adapter147 count was corrected to162 from the actual log:246 total GoogleTests.
  Known Plugin msm: hook tsm_post failed warning remains observed; exit0 and tests
  do not establish production SMACK policy. No force or replacefiles used.

Release11 runtime RPM SHA256:
acb217b06467581d9981372a146b12313cfe64143bc669ddd5e5829c97457189.
Release11 tests RPM SHA256:
0b287247374dac2a9e5d65ad83d2f518faadbfc76f38d135c3c5b255ad6d44a1.
Installed tests exercise private/injected paths. No production managed factory,
platform authorization, live worker service, catalog invalidation or resource
control is enabled. The unchanged privileged bootstrap fixture was not rerun for
this cleanup-only scope; its Release10 evidence remains historical. ARM/P09 and
physical-device verification remain NOT_RUN.


Release11 publication: 973dae817fc59b357a5ecbfafc6b1e089340e106 on main,
commit exit0 / push exit0. git ls-remote origin refs/heads/main returned that exact
SHA after push; the remote initial history and Apache-2.0 LICENSE remain intact.
Only the reviewed spec and administrative progress record were committed.


## P06 local read admission lease (2026-09-28, r2 accepted)

Design r2 was ACCEPTED by w1:pJ for /tmp/capmgr-read-admission-design-r2.md,
SHA25652aa8160b72c96f9ea6fdb9e499ac108df3cf060de6963ae307d4ece82742eb2.
The contract is copied into07 with same-connection handoff and fork inheritance
clarifications. P06-READ-LEASE-r2 exact8-file source manifest is
/tmp/capmgr-read-lease-r2.sha256; peer ACCEPTED after host/native review.
Accepted07 SHA256:
f783c12af31c0e982ed131833e821421a5e2ab610e4439676e4da6fe684001c9.

CatalogReadLease independently opens an O_RDONLY whole-file OFD RDLCK outside the
writer catalog directory, pins DB/WAL/SHM, and checks exact generation/metadata.
Local ReadAccess is retained before SQLite construction until after its close,
including public destroy IO retry. Open validation completes before the dispatcher
thread exists and failed create never publishes a handle. Query failure defaults
remain NULL. Every client-handle C entry rejects inherited creator TGID before
locks, SQLite or callbacks; child exit/exec, not inherited destroy, releases its
OFD references. No hostile-child or relabel revocation guarantee is inferred.

Evidence (ephemeral /tmp/capmgr-evidence):
- read-lease-preflight-r1.log: target OFD shared/exclusive separation,
  O_RDONLY->WRLCK EBADF, release, persistent WAL/SHM identity through last writer
  close and fresh READONLY query PASS; owned scope removed, remote0/transport0.
- read-lease-host-check-r3.log: CTest8/8 PASS after build-r5. Fourteen lease cases
  include independent lifetimes, maintenance BUSY, unsafe/missing/symlink/hardlink
  files, poisoned replacement/policy, local committed-update visibility, explicit
  persistent-WAL fixture, failed post-open admission, destroy-IO retention and fork.
- read-lease-native-r2.log: exact8/8 manifest and explicit affected recompiles,
  80/80 unit tests PASS, remote0/transport0. read-lease-native-abi-r2.log explicitly
  records C_CONSUMER_EXIT0 and unchanged12 C exports, remote0/transport0.
- Historical native-r1 failed only because the owner selected the wrong build
  directory (remote1/transport0); native-r1-retry at source/build passed79/79+C.
  R2 adds the inherited public-handle case. Earlier host build-r1/r2 rejected a
  misleading test indentation; build-r3 and later supersede it. Native retry/r2
  used the existing protected scope and 600s watchdog; no global policy changes.

The Label operation is explicitly substituted in these unit tests; native execution
is not real SMACK/ACL or image-provenance proof. Existing path-only AccessGate
compatibility remains private test behavior. Production PlatformAccessGate stays
denied and must override owned admission before eventual activation. Ordinary
Catalog writers still do not take the generation lease or set PERSIST_WAL; only
the explicit fixture sets the latter. This checkpoint is not installed Release11.
Next: bounded identity receipt plus SAME TIDL connection through local open and
validation, followed by separately reviewed recoverable native policy fixture.
Read-only vertical-slice completion still requires that integration and policy
matrix. CLI/remount, physical device and ARM/P09 gates are unchanged.


Read-lease publication: d776f25d791bef00308b3bedac37c89424a7ca95 on main,
commit/push exit0 and exact remote ref verified. Installed Release11 remains older.

## P06 catalog receipt/local handoff (2026-09-28, r2 accepted)

P06-READ-HANDOFF-r2 was ACCEPTED by w1:pJ for the exact seven-file manifest
/tmp/capmgr-read-handoff-r2.sha256. CMR1 describes five held file identities in
165 canonical ASCII bytes; it conveys no authority. LeasedCatalogGate acquires
an independent lease, validates local SQLite and descriptor, then requires
ConfirmCatalog and Finish before any C handle is published. Afterward there is
no channel IPC/state in queries or destroy. Channel ownership remains with the
caller during failed-create teardown. This is an injected local channel seam.

Evidence under ephemeral /tmp/capmgr-evidence:
- read-handoff-host-check-r2.log: cmake --build build --target check --parallel2,
  exit0, CTest8/8 PASS. Initial r1 compile rejected two EXPECT_THROW declarations
  on the same line; that test-only compile error was corrected before r1 checks.
- read-handoff-native-r2.log: exact manifest7/7, affected recompile under600s
  watchdog in protected source/build, unit86/86 PASS, C_CONSUMER_EXIT0,
  CAPMGR_REMOTE_EXIT0 / TRANSPORT_EXIT0. Native r1 passed85/85+C separately.
  R2 adds explicit same-instance confirmation to the seam and failure despite
  locally live checks; no real TIDL or policy operation was run here.

The target rpc-port1.21.17 uses four split sockets in release source6292196;
read-FD-only polling cannot prove MAIN write-half lifetime. The proposed capture
route is abandoned. Next: a separate unpredictable per-ServiceBase grant, expiry,
same-proxy confirmation and private uniterated GLib context teardown, followed by
exact native failure/lifetime tests. Full real-policy/direct-open matrix remains
NOT_RUN; production create, CLI/remount and ARM/P09 remain gated.

Original accepted07 SHA256: e3f9f490c5d57e8465b28d2a2ea04a244ea018625d7aaff32c6b4fe2963c4c9c.
Publication07 SHA256: c159d2c7aa2480925169340247ed86ad672c73ef3bfab86845ae1e4c01a172e3.
The only post-review07 change is pending-review -> accepted-local heading.


Read-handoff publication: 28d0c435272a3b2d3ac7d6046c2a33dace815a00 on main,
commit/push exit0 and exact remote ref verified.

## P06 per-instance catalog grant (2026-09-28, r1 accepted)

P06-READ-GRANT-r1 ACCEPTED by w1:pJ: exact five-file manifest
/tmp/capmgr-read-grant-r1.sha256. One service-instance object owns a fresh
getrandom256 nonce, fixed235-byte CMG1 envelope and issuer lease. Shared64 budget
bounds pending grants; confirmation checks exact nonce, descriptor and deadline
again after metadata validation, consuming once. Wrong instance/replay, stale
receipt, malformed envelope, expiry and policy changes deny. Clear releases the
lease then its budget exactly once on every error/revoke/destruction path.

Evidence under ephemeral /tmp/capmgr-evidence:
- read-grant-host-check-r2.log: cmake --build build --target check --parallel2,
  CTest8/8 PASS exit0. Earlier r1 compile failed on two GTest assertion labels
  sharing one source line; split assertions fix it, with failure log preserved.
- read-grant-native-r1.log: exact5/5, affected rebuild,95/95 GoogleTests,
  C_CONSUMER_EXIT0, CAPMGR_REMOTE_EXIT0 / TRANSPORT_EXIT0, under600s watchdog.
  Nine grant fixtures include concurrency, entropy, allocation failure/capacity,
  explicit delayed expiry and deadline crossing during metadata validation.

Five seconds is a confirmation deadline, not hard wall-clock reclamation. A
stalled service-context timer retains lease/capacity; no actual timer, service
instance, TIDL/private-context transport or policy matrix is implemented by this
primitive. Those are next integration gates. Production create stays denied.
Accepted07 hash: c0081a32f5f49b97eef149561fb927b0d1ff070026adc6f2c0afb4f54a3330fb.
Publication07 hash: 6c81e32fed1c8d1a8f2bbbd05755d9d49ba4f79fb9935003df4305382a4307b4.
Only the review-status heading changed after acceptance.


Read-grant publication: 75145f4dc005a5b732ce4c046b7c470bd579607e on main,
commit/push exit0, exact remote ref verified. Apache LICENSE remains SHA256
c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4.

## P06 optional TIDL read transport (2026-09-28, integration in progress)

READ-TRANSPORT-r1 exact11-file manifest /tmp/capmgr-read-transport-r1.sha256
received pre-execution source/safety ACCEPTED only. It adds a same-proxy nonce
confirmation, private uniterated client context and owner-context service timer;
no production PlatformAccessGate/factory or package activation. New generated
ConfirmCatalog method10 leaves0..9 unchanged. Method checker positive exit0 and
intentional temporary renumbering negative exit1 are in
read-transport-method-ids-r1.log. Host check-r1 and native check-r1 CTest8/8 PASS;
native optional adapter/probe compiled, exact11/11 hashes, remote0/transport0.

First native root fixture FAILED (read-transport-native-fixture-r1.log): exact
source/binary/owner-mode checks passed, then client Connect threw before any grant.
Dlog records AUL PrepareStub -13 for a non-daemon test endpoint. Target-version
rpc-port1.21.17 release6292196 treats only d::/ud:: names as daemon; registration
alone does not make an arbitrary application endpoint installed. No private-context
or Cynara failure is inferred from this pre-connect error. Child was reaped and
owned catalog scope removed; remote1/transport0, no runtime PASS.

The forced server stop left its task-owned hashed socket endpoint. Actual
libaul get_path identified /run/aul/rpcport/.0@6916fd2540efe6712107a858e82be4983a559f4d;
inspection confirmed root socket dev20/inode13628357, single link. Owner confirmed
ECONNREFUSED and unchanged identity before unlinking ONLY that fixture entry.
read-transport-endpoint-inspection-r1.log and endpoint-cleanup-r1.log record this
additional cleanup; scope deletion alone was not complete endpoint cleanup.

R2 uses a unique d:: fixture endpoint, per-stage/native-type diagnostics and
process-registration cleanup. Client failure first asks the server to drain and
destroy Stub; success requires endpoint removal, while failed cleanup reports its
exact retained path. Only fixture/doc bytes differ from r1; exact manifest is
/tmp/capmgr-read-transport-r2.sha256. Native check-r2 verifies11/11, rebuilds the
changed fixture and passes CTest8/8, remote0/transport0. Root rerun awaits narrow
source/safety re-review; no runtime acceptance is inferred here.


READ-TRANSPORT-r2 final runtime/publication ACCEPTED by w1:pJ after the separately
accepted bounded rerun. read-transport-native-fixture-r2.log checks exact11/11,
binary SHA256a6a7f7898b73972498f40657716f0da38070729566792209d01e95fff656c44f,
root0755 executable and protected root0700 build scope, then runs under180s watchdog.
All eight cases PASS: normal24 create/local-query/destroy with stable FD baseline;
raw wrong-instance/replay/expiry; actual four split sockets with each of the two
write halves shut down; fixed UID-rule denial; malformed and oversized responses;
stalled context; and consumed confirmation with lost reply. Both split cases
observe read_revents0,0 immediately after write-half shutdown, yet Confirm fails.
This experimentally confirms why read-FD polling alone is insufficient.

All eight servers report GRANTS_SERVICES_DRAINED; parent verifies normal child
exit/reap and endpoint absence. REMOVED_SCOPE precedes FIXTURE_PASS, with no
retained scope/endpoint, remote0/transport0. No global policy, operational DB,
CLI/root-worker job, package install or production access route was enabled.
Negative modes each run once; their FD counts across different processes prove
neither repeated-failure leak nor leak freedom. A separate negative stress revision
will test that boundary. Real same-UID/different-label/direct-open and writer
cooperation remain required before production admission.

Original accepted07 hash: a5bece1712e9d9e3a1a857df5031de2e0c6d212b177556e658f09ed69751368b.
Publication07 hash: a6ead476cf19e3541cf7a213088bb0c29f97446c722a352d4f35d0d2f2a9db7c.
Only heading/runtime-status text changed administratively after acceptance;
all transport/development clauses remain unchanged.


Read-transport publication: dde3d6b3b8d31193d51cf0bef9044d0badfa4567 on main,
commit/push exit0 and exact remote ref verified.

READ-TRANSPORT-STRESS-r1 ACCEPTED for fixture-only manifest
/tmp/capmgr-read-transport-stress-r1.sha256, source SHA256
26e64f1ee67aaa38ed6ab0c9a4c7df5ba268c730fdcbea368b053d5234bd5a47.
Negative public modes repeat4 with45s owned-client bound; normal24 and180s outer
watchdog are unchanged. read-transport-stress-native-build-r1.log checks1/1 and
recompiles the fixture, remote0/transport0. read-transport-stress-native-r1.log
checks source, protected path and binary SHA256
a09bf1d2e327ebdf92ff33d60cf03f43c447356dd42949ae2589d38d74a91c10.
All eight cases PASS. Deny/malformed/oversize FD counts are9/9/9/9; stalled/lost
reply10/10/10/10. Counts remain stable within each process; differences across
modes are not leak evidence. Eight server drains and owned normal exits, endpoint
absence, REMOVED_SCOPE before FIXTURE_PASS; remote0/transport0, no retained scope.
This extends failure-path fixture evidence only, not production policy or RPM12.

Read-transport stress publication: a4c19f7dafd3f5ebd2b8a128582fa86506ef1b49
on main, commit/push exit0 and exact remote ref verified.

## P06 read-handoff Release12 package checkpoint (2026-09-28)

READ-PACKAGE-r1 source-only ACCEPTED by w1:pJ for the exact two-file manifest
/tmp/capmgr-read-package-r1.sha256: spec SHA256
 deefb867db52ac5ab327995e0db6f0b24fb0b2c9879938c8277135eb0d3e0130
and tools/README SHA256
 5c283103d6c1dc63f3ec434925c760f04918c0bf715dedf4111746b45fc81829.
The spec advances11->12 and adds only the Tizen tests read-transport-probe path;
no new service, scriptlet, setuid or file capability. Source acceptance alone
is not runtime acceptance; the separate final verdict is recorded below.

Source archive /tmp/capmgr-release12.tar.gz SHA256:
09e79b1f566658d9387e03928cfdcafccda8529a76284b3cf8c36ac84cd9108f.
It contains134 tracked files at source checkpoint a4c19f7 plus the accepted
spec/README working-tree proposal, with each archived byte verified locally.
JSON source SHA256 remains
0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406.
Native input hashes were checked before rpmbuild in new root0700
/opt/usr/capmgr-bootstrap-build-p4h4og7o/rpmbuild-release12, under900s watchdog.
The historical root0777 development tree was not used.

Ephemeral evidence under /tmp/capmgr-evidence:
- read-package-native-r1.log: exact three input hashes, Release -O1/-DNDEBUG,
  parallel1; optional TIDL/transport targets and new fixture rebuilt, native
  RPM %check CTest8/8 PASS, clean unpackaged-file check, four binary RPMs and
  SRPM written, CAPMGR_REMOTE_EXIT0 / TRANSPORT_EXIT0.
- read-package-payload-r1.log: audit FAILED because the initial script assumed
  identical automatically generated ELF dependencies. This is preserved evidence,
  not a package/build failure or permission to ignore dependency differences.
- read-package-payload-r2.log and read-package-symbols-r1.log: runtime adds
  GLIBC_2.3 (fgetxattr/TLS), GLIBC_2.33 (fstat/lstat), and GLIBCXX_3.4.26
  (filesystem path operations). Target glibc2.40 and libstdc++14.2 provide all
  three. Other package requirements match Release11 after version normalization;
  spec-declared dependencies did not change. Payload paths match Release11 except
  the one new tests-only /usr/libexec/capmgr/capmgr-read-transport-probe,
  root:root0755, no filecaps. All four packages have no scripts or setuid/setgid
  files. Audit PASS, remote0/transport0.
- read-package-install-r1.log: prints the actual normal four-package
  rpm -U --test followed by rpm -U, with no force/replacefiles. RPM DB shows all
  four 0.1.0-12 packages. Installed unit95/95 + adapter162/162 + peer18/18 =275
  GoogleTests PASS, explicit C_CONSUMER_EXIT0 and offline Python7/7 PASS;
  rpm -V all four packages is clean, remote0/transport0. The known
  'Plugin msm: hook tsm_post failed' warning occurs during the test transaction;
  it did not change these outcomes and does not prove production SMACK policy.
- read-package-installed-fixture-r1.log: installed tests package ownership,
  root0755 mode, RPM verification and binary SHA256
  420e4146b7fceef4c0a2e40ee2bcac964364207d1431e3915d4b47ede183a2c5
  precede explicit180s root/User::Shell fixture. All eight cases PASS; normal24,
  deny/malformed/oversize each FD9/9/9/9, stalled/lost-reply each10/10/10/10.
  Eight server grant/service drains, normal child reap and endpoint-absence
  checks, REMOVED_SCOPE before FIXTURE_PASS, no retained markers;
  remote0/transport0. Cross-process baseline differences are not leak evidence.

Release12 RPM SHA256:
- runtime: 46e7543cc2dd2585debb886050aed78fb00803e81e3e59ddca13b21bdb74bc6b
- devel: b29a7ea6a6d1b9b97706c53a81ebe055c6fa36901d445014043b9a044a2dd26b
- offline-tools: 4811f8a1ba7607cdec6926397a858e2f33385ec7deb9436f87897c26d2da94a6
- tests: 315c0f8ec3de999ba7b25176e02a5a94c18a0552489e25b60e70f3847567b9ae
- source: 44a917ee4d9f9a0e4d1f293c1b27447e6a8f0d731725921cdfbb4832d5c425e3

No unchanged root bootstrap rerun, global SMACK rule, operational DB write,
production service/factory activation or CLI/remount occurred. Real causal
same-UID/different-label/direct-open policy, endpoint/ancestor provisioning,
ordinary writer lease/PERSIST cooperation, delegation/relabel and remaining
worker/resource gates are still open. P08 product/physical-device and P09/ARM
acceptance are not inferred from these fixture results.

READ-PACKAGE-r1 final publication/runtime ACCEPTED by w1:pJ for the unchanged
spec/README manifest and this administrative evidence diff. Reviewer independently
verified all134 archive bytes, native build/payload/dependency audit, normal upgrade
and installed fixtures above. The initial failed audit remains preserved. This
closes only the Release12 tests-package checkpoint; production/policy gates remain
open. Pending-to-accepted status edits do not change source or contract clauses.

Release12 publication: 8a5c560e6554df7b947efce42ced1b95109d0326 on main,
commit/push exit0 and exact remote ref verified. Working tree was clean.

## STYLE-01 Watcher conventions and build organization (2026-09-28)

The user requested continued development plus Watcher-derived C/C++, CMake/config
and spec style, a global hjhun-coding-style skill, and the pkg-config filename and
Name changed to capability-manager. Reference source was read only at
~/tizen/platform/core/appfw/tizen-watcher: AGENTS, watcher_server/header,
plugin_registry, root/component CMake, ApplyPkgConfig, pc.in and spec. The referenced
coding_rules.md is absent there; observed source is the basis, not invented rules.

STYLE-SKILL-r1 ACCEPTED for three global files under
~/.codex/skills/hjhun-coding-style (the global skills root resolves to ~/.agents/skills).
The entrypoint, agents metadata and build/package reference pass skill-creator's
quick_validate. They preserve repository/user authority, C++20/ABI/security gates,
and do not import Watcher deployment, Gerrit or policy permissions. The skill is
used for this checkpoint and referenced in AGENTS. It is installed globally, not
included in the CapMgr source archive. Entry SHA256:
5be7c4318d1ba5169925858af4d9e49e088e9219dc1ae0c16be0f0f0152775c2.

STYLE-FORMAT-r1 final source/native verification ACCEPTED by w1:pJ. Exact106-file manifest includes AGENTS/.clang-format and104 tracked
C/C++ files. Google/2-space/80-column layout expands compressed statements while
keeping include order, string/raw literals and tokens. Owner and reviewer each
compared against8a5c560 with preprocessing line splices accounted for; all104 token
streams match. Generated/vendor/build files are excluded. Initial naive token
comparison stopped on C macro backslash/newline formatting; corrected comparison
and formatter dry-run pass. No identifier, lifetime or ABI changes were included.

STYLE-BUILD-r1 final source/native/installed verification ACCEPTED by w1:pJ. Exact12-file build manifest uses uppercase CMake, root dependency
configuration and component source/target files. Runtime/library/archive outputs,
generated TIDL and invalid-image fixture remain in the root build directory.
Explicit variant source lists, imported dependencies, visibility/version script,
required optional flags and root CTest behavior are preserved. check_method_ids.py
is now an explicit generator dependency. Host BUILD_TESTING=OFF remains supported.
Spec Release13 aligns fields and consolidates explicit fixture installs while
preserving Release-O1/parallel1, optional platform flags and four-package split.
The requested capability-manager.pc / Name: capability-manager replaces capmgr.pc;
libcapmgr and public capmgr_ names are unchanged. Downstream pkg-config rename is
documented in tools/README. No service, scriptlet, setuid/filecap or policy is added.

Host evidence under /tmp/capmgr-evidence:
- style-build-host-r2.log: fresh configure/build/check CTest8/8 PASS.
- style-testing-off-r1.log: fresh BUILD_TESTING=OFF configure/full build PASS.
- style-target-map-r1.log: all73 compilation source/variant definition/language,
  PIC and visibility entries match a separately configured8a5c560 baseline.
- style-host-all-r1.log and style-host-install-r2.log: full targets/root siblings,
  generated/invalid paths, unchanged8 CTest names, staged capability-manager.pc,
  C consumer built with its pkg-config flags exits0,12 unchanged C exports and
  SONAME libcapmgr.so.0. The initial install-r1 assertion assumed the installed
  bootstrap target was included in check; historically it is not. Full all-target
  build resolves that evidence gap without a source workaround.

Frozen source archive /tmp/capmgr-release13.tar.gz contains143 regular project
files, SHA256 a307a65e95658c753d049d0a34614472be763fadc29d4f00ab29673f2d181f07.
Reviewer independently compared every member against the frozen tree. First native
style-build-native-r1.log failed at adapter-test linking: No space left on device,
remote1/transport0; no native PASS inferred. style-native-space-recovery-r1.log
records deletion only of reproducible owned Release11/12 BUILD caches after trust
and process-use checks. Source archives, RPMs and evidence were preserved;701MiB
became available. Exact-source style-build-native-r2 retry passed as recorded below.
No explicit root bootstrap/read-transport/policy workload is rerun by style work.

STYLE native/installed evidence completed and independently ACCEPTED:
- style-build-native-r2.log: same archive/input hashes, full native TIDL/Cynara/
  transport ON build, %check CTest8/8, clean unpackaged check, four binary RPMs and
  SRPM, remote0/transport0. The ENOSPC r1 failure remains preserved separately.
- style-package-payload-r1.log: normalized runtime/devel/offline/tests requirements
  match Release12; paths match except the explicitly requested devel replacement
  /usr/lib64/pkgconfig/capmgr.pc -> capability-manager.pc. Auto Provides is now
  pkgconfig(capability-manager). No scripts, filecaps or setuid/setgid files;
  fixture executables retain root0755. Audit PASS, remote0/transport0.
- style-package-install-r1.log: printed normal four-package rpm -U --test then -U,
  installed0.1.0-13,275 GoogleTests(95+162+18), C consumer0, offline7 PASS and clean
  rpm -V. Old capmgr.pc is absent; pkg-config capability-manager reports0.1.0 and
  builds a second native pure C consumer which exits0. The known msm tsm_post
  warning is retained, without a transaction/test failure; remote0/transport0.
- style-native-abi-r1.log: installed12 C exports, SONAME libcapmgr.so.0,
  Name: capability-manager and existing -lcapmgr / include flags preserved.
  Explicit root transport/bootstrap/policy fixtures were NOT_RERUN for style-only
  work; no new runtime/production or ARM acceptance is claimed.

Release13 RPM SHA256:
- runtime: 7377c1be43f8ed5c35d83b233cfeaa158d5f59297c042212d3b314a6991ea611
- devel: e92ff275e7fade121472d3d4e43b278eb0090516ed5325689c44535b9af9574f
- offline-tools: a7d361fc330d8542f810bd99a7473125bd9a29a1f5eac5ade57843c4493b093d
- tests: b636a8d8f20958da21e1fbebc6234043781ce9d09b2993f7bc134c8ce7c0bed8
- source: c2c4fdfdb1a2ae1bd04394d5c9ee573559bcd0b9785a2ef2134cc1dbbe626449

STYLE-01 final disposition: FORMAT-r1, BUILD-r1 and SKILL-r1 ACCEPTED for
the unchanged106/12/3-file manifests. Administrative08 evidence was accepted
at SHA256 e2d096b90fb4677272c8f8c6130b917ee9e0671701155d40d1b6865c934aeae7;
subsequent status/publication updates do not change the reviewed source clauses.
Formatting publication f0b520d4d8edb3151143f890a4b799c657ad7027: commit/push0
and exact origin/main verified. Build/package publication follows separately.


STYLE build/package publication: 9883709c5921138fcf7317598ff5cee5fe3508be,
commit/push0 and exact origin/main verified; tree clean before functional work.

## P06-WRITER-WAL-r2 (2026-09-28, accepted local source)

Exact five-file manifest /tmp/capmgr-writer-wal-r2.sha256 reviewed by w1:pJ:
Database setter, test-only linker wrapping, lifecycle tests, existing missing-
sidecar negative fixture and07 contract. Original accepted07 SHA256:
3aaef5cdc31ebf0b043c5967b844a0ac90cc376f32297345de559ad9cf68fb8b.
Only the heading changes administratively after acceptance.

Every internal Database writer sets checked PERSIST_WAL after verifying WAL.
Failure rejects construction with operation/rc/errstr; no public ABI or production
injection seam. Tests exercise real SQLite independent/reopened connections,
last-writer/last-reader close without keepers, stable DB/WAL/SHM identities,
revision/FTS queries, live update and TRUNCATE checkpoint; NOTFOUND/IOERR injection
is one-shot/thread-local and linked only into capmgr-unit-tests.

Evidence under /tmp/capmgr-evidence:
- writer-wal-host-check-r1 and writer-wal-native-r1: retained failures in the old
  WorkerCatalog test which expected normal writer close to delete sidecars.
  No PASS claimed. The revised test explicitly removes only owned fixture files
  after its sole writer closes, preserving unsafe-generation denial/recreation.
- writer-wal-host-check-r2: CTest8/8 PASS. Unchanged focused4/4 test bytes pass
  against host SQLite3.45.1 (writer-wal-host-tests-r1).
- writer-wal-native-r2: exact5/5, affected rebuild, CTest8/8 and focused4/4 PASS,
  SQLite3.50.2/sourceid logged, C_CONSUMER_EXIT0, unchanged12 C exports,
  CAPMGR_REMOTE_EXIT0/TRANSPORT_EXIT0. Owned Release13 build tree only; no RPM
  rebuild/upgrade, root workload or policy change. InstalledRelease13 does not
  include this subsequent source change.

OFD maintenance cooperation, trusted image/path/generation, real DAC/SMACK and
direct-read authorization subset remain open. No empty/lazy-sidecar provisioning,
raw external writer, crash recovery, production create/CLI/remount guarantee.
User's later peer-API request is a separate pending design/source checkpoint.


WRITER-WAL publication: cf8e446dec98797d8aee40aa41bbaedaf4de44d5,
commit/push0 and exact origin/main verified before the peer API work.

## P06-PEER-API-r2 (2026-09-28, private source/runtime accepted)

The user requested platform APIs instead of direct procfs peer-credential reads.
Development contract accepted by w1:pJ at temporary contract SHA256
f251af269e3c2696e62b050f1f2d969388df61a0bef354faa3dff5912e8a155e.
Exact18-file source manifest /tmp/capmgr-peer-api-r2.sha256 is a separate review.
Peer uses native Cynara PID/explicit raw UID/GID/SMACK helpers plus kernel socket
cross-checks, a shared helper mutex, and separate DEFAULT policy identities on
MAIN. Checked rpc-port metadata validates callback extension defaults and owner
UID without confusing it with raw UID. No proc/namespace acquisition in Peer.

Connected/SameCredentials are connection-only. The earlier proc-backed zombie
rejection is removed from read binding and is not inferred from Cynara session.
HasVerifiedLiveTask is explicitly unavailable; experimental packet/ticket entry
points now return NOT_SUPPORTED. Their old positive tests are historical. New
negative tests prove rejection before receive/issue/consume. Trusted internal
worker-parent anchors and namespace setup are outside peer credential extraction.
Production create, broker/task authorization and remount remain disabled.

Ephemeral evidence:
- peer-api-provider-session-r1.log: installed Cynara commons/creds/session0.26.0,
  rpc-port1.21.17 and TIDL3.1.1; nonexistent PID -1/2147483647 produce the decimal
  session strings, remote0/transport0. No live-task meaning is inferred. Local
  source/header disagreement is not substituted for actual target behavior.
- peer-api-host-check-r2.log: host CTest8/8 PASS. Kernel socket data in non-Cynara
  host builds does not provide real policy authorization.
- peer-api-native-r1.log: preserved FAIL, remote2/transport0; three new tests used
  socketpair, which has an empty target security label and is rejected before
  entering helpers. Existing12 peer/channel/unsupported-proof tests passed.
- peer-api-socketpair-probe-r1.log: Python buffer-limit error, not kernel evidence.
  Corrected r2 uses1024 bytes and records SO_PEERSEC as a lone NUL; other socket
  options are valid. Probe remote0/transport0, no policy writes.
- peer-api-native-r2.log: exact18/18, affected platform/TIDL/test rebuild,
  CTest8/8 plus peer15/15 no skips, C_CONSUMER_EXIT0,12 unchanged exports,
  remote0/transport0. The three new Cynara tests use real accepted abstract Unix
  connections; every helper-error path rejects without fallback, cross-instance
  peak concurrent helper entry is1, raw UID/GID0 and User::Shell match while
  DEFAULT policy user/client are separately observed as0/User::Shell.

New root transport integration remains NOT_RUN pending exact-source fixture
safety review. Existing root0755 protected-build probe SHA256:
1f09327b51df8cc03ab6b1c13b98eae2406a76231890d63570f4689b9ac4bf95.
No package rebuild/install, production activation or new policy mutation occurred.
InstalledRelease13 remains the earlier accepted style checkpoint.


PEER-API-r2 independent local/source and pre-execution safety ACCEPTED by w1:pJ.
Manifest digest c2b30977bae4b34af55e1a3fc1cf5ea1542d98bfa62294c5fe7217c718ddb3b4;
all18 entries independently checked. Unchanged root fixture source SHA256
 e27a36f5c4103e384ce37433c3e3b46cd90a10f2efa78c28631c0c41e3104a85
was separately checked for bounded owned-child/endpoint/scope cleanup. No runtime
acceptance was implied by source acceptance.

peer-api-transport-native-r2.log preserves a host SDB command-length rejection
('service name too long', transport1), before any fixture launch/remote sentinel.
The retry uses a short command invoking an owner script in the protected scope;
source/binary preflight and watchdog remain enforced. No source changes for retry.


peer-api-transport-native-r2-retry.log: exact18/18 recheck; explicit optional
transport/probe rebuild with generated method IDs unchanged; root0755 binary
hash above and protected non-writable/no-ACL ancestry checked before180s watchdog
fixture. All8 modes PASS: normal24; raw wrong-instance/replay/expiry; split four
sockets/write-half loss denied; deny/malformed/oversize/stalled/lost-reply each4
iterations. Negative FD baselines9/9/9/9 or10/10/10/10 stay stable within each
process; cross-mode baseline differences do not establish leak freedom.
Eight service/grant drains and normal owned-child/endpoint-absence checks;
REMOVED_SCOPE=/opt/usr/capmgr-read-fixture-OKqWxd precedes FIXTURE_PASS; no retained
scope/endpoint markers, remote0/transport0. Final independent runtime disposition
requested; no production task/namespace identity or image policy inference.


PEER-API-r2 final private runtime/publication gate ACCEPTED by w1:pJ. The earlier
"New root transport integration remains NOT_RUN pending ..." paragraph records
only the historical pre-execution state; the exact subsequent retry and final
review above supersede it. Original accepted08 SHA256:
99fadb6f1da0ce9553c075b7df691bb0e212e91b2ab9bab3d8050cafa75d6033.
Original accepted07 SHA256: 69b847b72add44a2eb5d3c5c2350ac6812ddec779442963d8238214b93547488.
Only07 heading and administrative08 status/publication wording follow acceptance.

Owner-run retry method (outer command not echoed by the result log):
`python3 /opt/usr/capmgr-bootstrap-build-p4h4og7o/source/tools/run_bounded.py
--seconds 180 -- sh /opt/usr/capmgr-bootstrap-build-p4h4og7o/peer-api-transport-r2.sh`.
The uploaded script uses set -eu, verifies18 hashes, explicitly builds the probe,
checks protected ancestry/binary hash, then runs the unchanged fixture. The owner
captures the unique remote exit sentinel and SDB transport exit separately.
InstalledRelease13 is unchanged; no RPM, production authority or global policy
acceptance is inferred. Reviewed source publication follows this disposition.

PEER-API source publication: 3ca98ab37e2a3485dfd73064ef1bf0012374fb4f,
commit/push0 and exact origin/main verified; no package change at that checkpoint.

## P06-PEER-WAL-PACKAGE-r1 (2026-09-28, package/runtime accepted)

The source-only revision changes Release13 to14, packaging the separately accepted
WRITER-WAL cf8e446 and PEER-API3ca98ab checkpoints. Independent source review
ACCEPTED the one-file manifest /tmp/capmgr-peer-wal-package-r1.sha256;
spec SHA256: 29e0d20cdbfeef7b62d675007634c8bb54aa2a663628f99c83290afdf8c8112c.
No declared payload/dependency/scriptlet/service/capability or policy changes.

Exact source archive /tmp/capmgr-release14.tar.gz SHA256:
0b43e6cd434e06e14d89c0409d4eeccb68414983d348a2b670986de1d5dd6e6e.
Owner and reviewer independently compared all146 regular files against3ca98ab
plus the accepted spec override. The archive has no duplicate/traversal/link or
special members. Pinned JSON SHA256:
0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406.
The RPM-tree-relative input manifest pins those two archives and the spec; this
later administrative progress append is outside the source archive.

peer-wal-package-native-r1.log preserves an environment failure: all targets built
and RPM CTest8/8 passed, but tests-RPM cpio compression failed with ENOSPC,
CAPMGR_REMOTE_EXIT1/TRANSPORT_EXIT0. This is not a successful RPM build.
peer-wal-package-space-r1.log records only prior Release13 BUILD-cache removal
after trusted ancestry/ACL, no active executable/cwd, pinned identity and
symlink-safe cleanup checks. Source archives, all four prior RPMs and logs remain;
351MiB free, remote0/transport0. Exact unchanged inputs are used for the retry.


peer-wal-package-native-r2.log: unchanged input3/3, full native build with
TIDL/Cynara/transport ON, RPM CTest8/8, clean unpackaged check, four binary RPMs
and SRPM, remote0/transport0. No source workaround or additional cache deletion.
peer-wal-package-payload-r1.log: normalized Requires/Provides unchanged from13;
all four package path/mode/owner/group sets unchanged, scripts empty, no filecaps
or setid modes, audit PASS remote0/transport0.

Exact Release14 artifact SHA256:
- runtime: 08490d569ea19e404b216f3cf349fba4175ad73afcede4ca33995424265bfcaf
- devel: 465cfca670639da7af435e82faa50a6b25e49092a0b17af47885297ee30d9a44
- offline-tools: 011e5f21e5e1915a8581e02f94c40d8f109808311f04cedd48e72d10e605afab
- tests: 2a85dc9005b5c2930dd18b14d7711bc5e479416cc14531e3268c1e0aecfe7433
- SRPM: 89677311e87a8ef16dcfdcf00f7dc0195fc62f0c11ecf19cfc769aa9798525cc

peer-wal-package-install-r1.log prints rpm -U --test followed by normal rpm -U
for all four packages, then installed0.1.0-14. Installed99 unit +162 adapter +15
peer tests =276 GoogleTests PASS, C_CONSUMER_EXIT0, offline7/7, rpm-V clean;
capability-manager pkg-config links a second C consumer successfully. The library
retains12 capmgr_ exports at CAPMGR_0 and SONAME libcapmgr.so.0. Remote0/transport0.
Known msm tsm_post warning remains recorded; no transaction/test failure or
production SMACK-policy conclusion follows. The peer count deliberately replaces
historical packet/lifecycle positives with unavailable-proof negatives.


peer-wal-package-installed-fixture-r1.log verifies the installed tests RPM and
root:root0755 probe SHA256
cb2f99bac54bdf776d129a0a6aa9dc48359501381decdccdbadb956d82c1af68.
Owner-run method: outer210s watchdog invokes the installed180s watchdog/probe;
no source or policy change. All8 modes PASS, normal24 and each public negative4
iterations. Deny/malformed/oversize FD9/9/9/9 and stalled/lost-reply10/10/10/10
are stable within each process; different process baselines are not leak proof.
Raw same-instance/wrong-instance/replay/expiry and both split write-half losses
pass; read_revents remains0,0 while confirmation rejects. Four stalled-slot and
four consumed-confirm/lost-reply observations, no failed create publishes a handle.
Exactly8 SERVER_GRANTS_SERVICES_DRAINED; source requires owned normal child exit
and endpoint absence before each successful case. REMOVED_SCOPE at
/opt/usr/capmgr-read-fixture-eRWfGF precedes READ_TRANSPORT_FIXTURE_PASS;
no retained scope/endpoint, remote0/transport0.

Final independent package/runtime review requested for the unchanged spec and
this administrative08 diff. New root bootstrap/worker/policy fixtures NOT_RERUN;
ARM/P09 NOT_RUN. Production create/CLI/broker/remount, actual direct-read policy
subset, writer OFD maintenance cooperation, task/sender identity and image gates
remain open. Next development remains read-admission policy and coordinated
catalog-generation maintenance; package success does not enable those routes.


P06-PEER-WAL-PACKAGE-r1 final publication/runtime gate ACCEPTED by w1:pJ for
exact spec14 and the administrative08 diff, original reviewed08 SHA256
b080082d7806bb39c89b46e7ee19766f00ed7f6ea95a1ff8229fc8ccf9108cd4.
This disposition supersedes the preceding request/pending state only; the failed
first build, recovery and scoped product limits remain unchanged. Reviewed
spec+progress publication follows; no other product source changes in this commit.

## User-directed implementation ownership and OFD maintenance (2026-09-28)

After cbf921ef9ccfe91699267a4f990ecefc0db3ddd2, the user assigned implementation,
all source/device/Git operations and integration to w1:pJ; w1:pA is read-only
reviewer/coordinator. Historical role attribution is preserved. Per later user
steering, neither panel polls Herdr status/read/wait; incoming messages provide
review replies. Concrete requests are sent asynchronously without --wait.
No extra panes/agents. Full product completion has not been reached.

P06-OFD-MAINTENANCE-DESIGN-r1 accepted for development contract only by w1:pA,
SHA256 26d5cf80da76dd5d9ae882250cf59f1de56c3821c8f02b2d1cf2b5144c41f10f.
Existing generation opens RW without CREATE/Migrate under independent shared OFD;
EX is separate bootstrap/migration/sidecar lifecycle. Private owning-value writer
facade, physical-close lease retention and inherited SQLite rejection are required.
No source or production acceptance follows from the design disposition.

Host configure-r1 failed for missing SQLite without dependency prefix; an initial
build attempt had no generated makefile. Both logs were preserved. Corrected fresh
build-ofd configure-r2 uses /tmp/capmgr-host-deps/root/usr and Release -O1/-DNDEBUG;
no host system package installed. Host check-r4 CTest8/8 and focused-r3 19/19 PASS.
The first source request mistakenly stated19 while r1 focused evidence showed15;
corrected explicitly before acceptance. Four additional fixtures were added in r2
(post-open mismatch, retained fork reference, final EX policy and ACL-only deny).

Frozen r2 source16-file manifest, source archive SHA256
05260dae82e027ba74d93899f6071b4672650ce7735833b29ec58de90d01a549.
Native r1 safely rejected the legitimate JSON archive root directory before
extraction/build; remote1/transport0, preserved. Corrected validator accepts only
that exact root directory plus safe bounded regular/directory members. A new
protected root0700 ofd-maintenance-r2 build checks input hashes and16/16 manifest,
uses all three native platform flags ON and runs check. ofd-native-r2.log records
CTest8/8, focused19/19, C_CONSUMER_EXIT0 and unchanged12 exports, remote0/transport0.
Installed Release14 is unchanged; no RPM build/install or privileged workload.

P06-OFD-MAINTENANCE-r2 remains CHANGES_REQUESTED. Independent review found ordinary
DB/SHM identity-pin FD close cancels same-process SQLite POSIX locks while another
connection's transaction is live. The separate generation OFD does not fix this.
Owned independent-exec reproduction confirms the bug on host SQLite3.45.1
(ofd-posix-close-repro-host-r3.log) and target3.50.2
(ofd-posix-close-repro-native-r1.log): B BEGIN_IMMEDIATE returns BUSY5 before a
second CatalogReadLease closes, then OK0 with A's transaction still active.
Both processes hold independent cooperative shared leases. Native remote0 means
the diagnostic ran, not a safe implementation PASS. Fixture DB is removed after
rollback/physical closes. Expanded safe-metadata-pin design/source review follows;
no accepted primitive is silently assumed safe or production enabled.

STYLE-HEADER-SKILL-r1 accepted by w1:pA for the global SKILL.md only, SHA256
bb8b1e55eff049ef5e4858acba9682c24adf2a667650c0b6edeef2533d6d80d6.
Skill-creator quick_validate passes; UI/reference bytes unchanged. New OFD headers
use full Apache notice, retained SPDX and unique Google-style HH_ guards. Legacy37
header conversion is prepared outside the worktree only and needs separate review.
No invented Samsung attribution. No guard/style change weakens ABI/security.

P06-READ-POLICY-DESIGN-r1 received engineering feedback only, SHA256
2f6b2dc9cfa959a16706fcbd80ec5799c8b809aea91ff7888e29b0642311e95a.
The future causal real DAC/SMACK and injected C-create fixture must first resolve
OFD's SQLite-lock interaction, then freeze exact role traversal/group/capability,
root-writer scope, recovery-lock lifetime and teardown/counter proof. No global
policy writes or new runtime authorized/executed. Real Cynara matrix remains a
separate increment; generated MAIN/callback real checks are not weakened.
PATH-01 user answer remains pending. Installer/MIC, Action runtime/sync, catalog
worker invalidation, authenticated broker/resources, remount, product/physical
and final ARM/P09 verification gates remain open. No completion message sent.

### P06-OFD expanded POSIX-close correction, source r6

P06-OFD-POSIX-FIX-DESIGN-r1 was ACCEPTED FOR DEVELOPMENT CONTRACT ONLY, SHA256
1c99150fe3b2fb61983e4560645991a9679b60fd0759569373e4e1af3a0296d7.
Source scope explicitly expands to read_lease, shared file_metadata, worker_catalog
and the fixed independently execd sqlite_lock_probe. All DB/WAL/SHM metadata pins
are O_PATH/NOFOLLOW/CLOEXEC; borrowed loader source is validated as a readable
non-O_PATH directory before duplication. Source/duplicate identity/flags are
rechecked under an explicit stable-borrow premise. Loader remains startup-only
DAC/ACL with no worker generation lease, SMACK or runtime-validity grant.

Preserved native failures: ofd-native-r3.log has CTest7/8, unit120/121, only the
new user-xattr fixture failed (target /tmp does not support user.* writes, errno95).
ofd-metadata-diagnostic-native-r1.log observes real SMACK User::Shell and ACL
ENODATA61, and removes its owned scratch. r4 incorrectly expected ACL-read denial
from file mode alone; actual ACL lookup can return ENODATA. Host check-r6 and
native-r4 preserve that test failure. r5 restored user-namespace denial but the
root child zero-cap guard failed: target setresuid clears permitted/effective,
not inherited inheritable0x2000e2. ofd-native-r5.log is unit122/123, CTest7/8,
remote2/transport0. ofd-metadata-drop-diagnostic-native-r1.log confirms exact301
IDs/groups[], user metadata EACCES13, ACL ENODATA61 and real SMACK; scratch removed.
These are fixture failures, not corrected native PASS or relaxed product guards.

Frozen r6 differs from source-reviewed r5 ONLY by explicit SYS_capset zeroing in
that test child before its existing exact capget check. Manifest23-file SHA256
f998283c50b40f0d480fa67865554b0f3e8ae8331aeac48de28c0d6e1c06ef1a;
source archive SHA256
7b50f32af849bbd5828cf8793ce11ee2a10e58ed64950aa5b24c3e1c2ffa1802.
Original accepted r6 doc07 SHA256
18e3825797ed9efa336e072aefd138b2a1fed27f3a038c7619b879d93151f0d8
is preserved before any later heading-only acceptance bookkeeping. w1:pA accepted r6 at SOURCE REVIEW level,
with final checkpoint disposition deferred until exact host/native outcomes.

Owner-run host build-ofd check-r9: CTest8/8, focused-r5:24/24 new tests,
host-abi-r2 exact23/23+C0+12 exports PASS. Host SQLite3.45.1 actual WAL-write/main
and retained-read TRUNCATE exclusion survives temporary lease destruction,
second writer Close, failed constructor, loader success/early invalid DB or SHM
source rejection/post-open schema exception and O_PATH closes. Independently execd
B holds its own shared lease, stays BUSY5 before/after each perturbation and succeeds
only after A releases the write transaction or retained read snapshot. Invalid
caller-owned ordinary FDs remain open until all relevant SQLite owners close.

Owner-run ofd-native-r6.log builds a fresh protected root0700 scope with all three
native platform options ON and input/frozen23/23 checks before/after. CTest8/8,
focused24/24 with CAPMGR_REQUIRE_METADATA_TESTS=1, actual SMACK User::Shell,
USER_METADATA_XATTR=UNSUPPORTED explicitly recorded, C_CONSUMER_EXIT0 and unchanged
12 C exports, remote0/transport0. This is exact target SQLite3.50.2 correction
coverage; old unsafe diagnostic exit0 and r2/r3/r4/r5 results are not substituted.
w1:pA returned P06-OFD-MAINTENANCE-r6 ACCEPTED for the exact private source/ordinary
native checkpoint. A separate ofd-native-unit-counts-r6.log records all123/123
unit cases explicitly plus C0, remote0/transport0. Changed-source root transport
pre-execution safety is separately ACCEPTED for r6's protected path/digest; runtime
verdict remains pending until its actual new log. Installed Release14 remains unchanged; no RPM, operational DB, new SMACK
rule, production factory or root CLI workload has been enabled.

P06-READ-POLICY-DESIGN-r3 is ACCEPTED FOR DEVELOPMENT CONTRACT ONLY, SHA256
0b84c996d18e647931081aad6689944c621dbeff9e6b38f490d1292ede342828.
Exact rule matrix includes root catalog rwxlt and both reader lock rl. Same-label
transmutation intentionally measures denied O_RDONLY directory admission plus
independent file-open denial; unobserved SQLite phases are NOT_RUN. Existing
ancestor/control-pipe traversal is setup preflight, not normal DB denial, and any
additional rule needs frozen fixture review. Root-writer model, separate durable
recovery journal/retained SH, no inherited SQLite and modeled C-confirmation remain
explicit limits. No new policy fixture source/native policy execution accepted.

Legacy header license/Google guard conversion is still proposal-only outside the
worktree:37 unique guards, reverse-exact license/guard-only proof,36 host double
includes (native-generated service header excluded) and proposal C consumer0.
The accepted global skill and new OFD header conventions are unchanged. Release15
spec/README temporary proposal is source-only ACCEPTED; actual paths are not yet
applied. The new tests-only sibling sqlite_lock_probe must accompany installed
unit tests in that later independently reviewed packaging gate.

P06-OFD-READ-TRANSPORT-r6 owner-run runtime: ofd-transport-native-r6.log rechecks
source23/23, protected initial namespaces/ancestor and root:root0755 singlelink
noACL/no-filecap leaf. Exact binary SHA256
b21a5033a6fd2aa07daf4376e425e8230fd19c04541e42f5951909058f128ad9.
Owner method: push an owned short shell script into the protected r6 build scope;
SDB executes sh, which invokes hash-checked source tools/run_bounded.py --seconds180
with no-argument ../build/capmgr-read-transport-probe. The log records the exact
SDB command, source hashes before/after, and separate remote0/transport0.
All8 modes PASS: normal24; negative deny/malformed/oversize4 each stable9 FDs,
stalled/lost-reply4 each stable10 FDs in distinct processes; raw wrong-instance,
replay/expiry and split4sockets/read2/write2 behavior preserved. Eight service/grant
drains and checked owned child exits/endpoint absence precede case PASS. Exact
scope /opt/usr/capmgr-read-fixture-Kght1p is REMOVED_SCOPE before FIXTURE_PASS;
no retained scope/endpoint marker. Final independent runtime/publication verdict
pending. This is unchanged fixture transport coverage against corrected O_PATH
metadata, not new-label causal policy or production direct-open proof. No policy
rule, root namespace workload, mount, operational DB or package was changed.

P06-OFD-READ-TRANSPORT-r6 FINAL RUNTIME/PUBLICATION is ACCEPTED by w1:pA for
reviewed23-file source plus administrative08. Accepted pre-bookkeeping08 SHA256
8d8c2aaafdba1e4512da08c0ae15ba6ba2302774846c7d15d619a46a551c10db
and original07 SHA18e3825797ed9efa336e072aefd138b2a1fed27f3a038c7619b879d93151f0d8
are preserved. Earlier pending/deferred statuses above are historical and explicitly
superseded by this final disposition. Only the two new07 headings are changed to
accepted r6; reviewed contract clauses and product bytes remain unchanged.
Publication is limited to reviewed23 source files plus this administrative08.
No package15/header conversion/new policy or production activation is included.

### STYLE-HEADER-r1 mechanical conversion after OFD publication

P06-OFD accepted23+administrative08 published as
c8ec762070464e3c0657aefe8654e111c1dcb6ca; commit0/push0, exact remote main matched,
clean tree before the separate header task. Installed Release14 is unchanged.

User-directed header conversion freezes37 legacy project .h/.hh files in
/tmp/capmgr-style-header-r1.sha256; source archive SHA256
8aecfc53579a6008f7ba4c08710c2d0a995864f41518534f2142537067d918b5.
Adds Watcher-shaped full Apache notice while preserving SPDX/genuine attribution
and introduces unique Google project/path/file H_/HH_ guards with trailing `_`.
No invented Samsung ownership. Generated/vendor/build headers excluded; the three
new OFD headers already comply and are not changed. Reverse-exact proof reconstructs
every baseline header after removing only the license/guard edits, preserving all
other tokens/comments/includes. All40 project guards are unique; no old CAPMGR_H_
reference remains outside the replaced public guard. Public declarations unchanged.
Global hjhun-coding-style SKILL.md remains its separately accepted exact bytes.

Host style-header-host-check-r1 CTest8/8; actual39-header double inclusion (only
the native-generated service header excluded), C11 public double inclusion,
C consumer0 and12 exports PASS (host-double-r1/host-abi-r1 logs). Native r1 builds
fresh protected root with all platform options ON, checks frozen37/37 and completes
CTest8/8, then FAILS its harness double-TU generator due nested newline escaping:
remote1/transport0. Preserve style-header-native-r1.log; not a source failure/PASS.
Corrected file-based TU and bounded script use unchanged product37/37 in the same
protected owned scope. style-header-native-r2.log checks CTest8/8, all40 header
double inclusion syntax0 including generated TIDL, focused24/24 real-metadata tests,
C_CONSUMER_EXIT0 and12 CAPMGR_0 exports; final37/37, remote0/transport0. No header
bytes changed to repair the harness. Final independent header/admin verdict pending;
no RPM, root workload/transport rerun, new policy or production activation.

STYLE-HEADER-r1 FINAL SOURCE/NATIVE/PUBLICATION is ACCEPTED by w1:pA for exact37
headers plus administrative08. Accepted pre-bookkeeping08 SHA256
fe30da778acc08c5e78a8c7d349555b7843dfdf68faa04e721302fb04bec01b2
is preserved. Earlier header-final-pending text is historical and superseded.
Global skill and OFD implementation are separate accepted checkpoints; publication
here changes only the reviewed license/guards and accurate administrative evidence.
Package15 and the causal new-label policy matrix remain separate future gates.

### P06-OFD-PACKAGE-r1 Release15 evidence and pending final gate

STYLE-HEADER exact37+administrative08 published as
154a9b4e216e02e881dc9759cde36a16fe70880c, commit0/push0 and exact origin/main
matched. Release15 is a separate actual two-file spec/README slice. Reviewed
spec SHA5b2f3833643a83609affa9e8066788a86d8cdd0875c661be1677bbbc82186b31
and README SHAa038a077faf95fff53ee7eeedef06c5495238f1db5f1697aa95e155c68659b91
are byte-identical to the accepted temporary proposals. Independent actual source
and archive review is ACCEPTED; final runtime/publication review remains pending.
Only Release14->15 and one tests-only sqlite-lock-probe install/payload entry are
added. No declared dependency, scriptlet/service/setid/cap or policy change.

Archive113e6334c94b77b54af344877c85c5d52cacb9ed874565edc98b0294a693b182 has
154 regular tracked files, exact154a9b4 bytes except the two accepted overrides.
Inputs pin source archive, JSON3.11.3 and spec. Host spec parse PASS. Owner-run
ofd-package-native-r1.log FAILS adapter linking with ENOSPC, remote1/transport0;
no source/compiler failure or final package PASS is inferred. Prior owned OFD
r2/r3/r4/r5 build caches were safely removed before that attempt; all source
archives/directories and evidence remain. Later cleanup removes ONLY accepted
Release14/BUILD after protected ancestor/ACL/namespace, pinned identity and no
active cwd/exe checks, preserving SOURCES, four binary RPMs, SRPM and evidence.
ofd-package-space-r1.log is a command-length rejection before remote execution;
space-r1-retry.log confirms actual cleanup0/0 and345096KiB available.

Exact unchanged full rpmbuild retry ofd-package-native-r2.log passes input3/3,
all native platform flags ON, RPM %check CTest8/8, clean unpackaged-file check,
four binary RPMs plus SRPM, remote0/transport0. Native brp warnings that the
`file` helper is missing are preserved; successful RPM creation does not prove
stripping ran. Payload-r1 fails its overly strict
auto-dependency equality assertion. Discovery and payload-r2 explicitly validate
offline-tools additions GLIBC_2.3 and GLIBCXX_3.4.19 against installed providers;
package-symbols-r1 shows fgetxattr/getxattr and steady_clock::now. These arise from
accepted private metadata/generation code, not a declared dependency edit. Other
normalized requirements/provides are unchanged. Exactly one new tests-only path
/usr/libexec/capmgr/capmgr-sqlite-lock-probe is root:root0755; existing paths/modes/
owners/groups unchanged, no filecaps/setid/scripts.

Owner-run ofd-package-install-r1.log prints normal four-package rpm -U --test then
rpm -U, all installed0.1.0-15. Observed123 unit +162 adapter +15 peer =300 GoogleTests
PASS, pure C consumer0, offline7, pkg-config C consumer0, rpm-V clean, unchanged
12 C exports/SONAME. The tests-only sibling lock probe is present and actually
used by installed lock regressions; real metadata checks are required and report
User::Shell plus honest unsupported user xattrs. Known msm plugin warning is
preserved; transaction/tests exit0, remote0/transport0. No root namespace/bootstrap
fixture is rerun. Installed transport's read-only protected-path/hash preflight
passes separately; actual runtime is NOT_RUN pending source/safety review.

New-label read-policy recovery helper/tests remain a separate two-file temporary
primitive accepted for source/unprivileged12-case evidence only. They are absent
from this package archive/tree and have not written any native SMACK/Cynara policy.
OFD covers cooperating writers only; production policy/direct-open, raw external
writer cooperation, catalog-worker invalidation, installer/MIC, Action stack/sync,
authenticated broker/resources, PATH/remount, full product/physical and P09 ARM
remain open. No production factory/authentication/CLI/remount activation follows.

Release15 RPM SHA256 audit records:

- capability-manager-0.1.0-15.x86_64.rpm: 31eab3602aac4eb6e932b2bd5f901571104a553808b457c8fd2c99cf7a92facb
- capability-manager-devel-0.1.0-15.x86_64.rpm: 3d2305890f9e02cb57f46abfe51b118271c76bd79c90eb65f92c17f269f3b81e
- capability-manager-offline-tools-0.1.0-15.x86_64.rpm: bc81df60282857e38f697ff19a0a990d9489e3ceb890afe8f2f6b238f166452a
- capability-manager-tests-0.1.0-15.x86_64.rpm: beb24706d0f81fe7ce37486c35e41d339c815bfb057f8773effd864030bf855f
- capability-manager-0.1.0-15.src.rpm: 1504226ff9bb9bbf3f514b82993adf60bf6c0392eee1998ebd369bff571a35c1

P06-OFD-PACKAGE-INSTALLED-TRANSPORT-r1 pre-execution safety is ACCEPTED for the
fixed installed binary/watchdog with actual hash-equality checks immediately before
and after execution. Owner method: push exact short shell/preflight scripts into
the protected Release15 scope; SDB invokes that shell through the accepted outer
watchdog210s, shell runs /usr/libexec/capmgr/run_bounded.py --seconds180 with only
the installed no-argument capmgr-read-transport-probe. The exact invocation is
recorded in ofd-package-installed-fixture-r1.log. Probe SHA256
4a1469a19d1564d246d042bf40cbe42bcbc8b876687ac5cdbbad443e63cee117, watchdog
f53d446e0f56a0e96bbde5e71875294f88a9936aaf5b9c112dccf478dafb1b28, protected
root:root0755 singlelink/noACL/no-filecap paths, initial PID/mnt namespaces and
rpm-V are checked before/after. Runtime is PASS remote0/transport0: all8 modes,
normal24, each negative4 with stable9 (deny/malformed/oversize) or10 (stalled/lost
reply) FDs in its own process, eight drained service/grant cases, owned successful
child exits/endpoint absence required before case PASS. REMOVED_SCOPE is exactly
/opt/usr/capmgr-read-fixture-LDsdiV before READ_TRANSPORT_FIXTURE_PASS; no retained
markers. Earlier runtime NOT_RUN wording above is historical and superseded by
this measured result. Cross-mode FD9 versus10 is not a leak claim; split read-half
liveness still cannot stand for whole-channel authorization. Root bootstrap, new
policy/direct-open and production activation remain NOT_RUN/unaccepted.

Final independent P06-OFD-PACKAGE-r1 publication disposition is pending for the
unchanged accepted spec/README plus this accurate administrative08.

P06-OFD-PACKAGE-r1 FINAL RUNTIME/PUBLICATION is ACCEPTED by w1:pA for exact
spec/README and administrative08. Original reviewed08 SHA256
58e57587d6131c7bdde8a5f5d33345cb1bb6874a4e9230ad9621f5219b578e7a is preserved.
Earlier final-pending text is chronological and superseded. This publication
changes only the accepted two package files and accurate administrative evidence;
root recovery/role fixture work remains outside this tree and this archive.
No new policy or product activation is included.
