# Development progress and verification

Updated: 2026-09-27. This board distinguishes implementation, review, test execution,
packaging and publication. Product completion has not been reached.

## Ownership and checkout

Implementation/integration: Herdr `w1:pA`. Independent reviewer: `w1:pJ`.
Both were discovered through live `herdr agent list` with the same project cwd;
HERDR_ENV=1. Requests are asynchronous, without --wait. The owner alone performs
Git writes and device operations. Reviewer has a completed disjoint translation
assignment for docx/01,02,04,05,06; all file ownership is now back with the owner.

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
| P02 | IN_PROGRESS | Private catalog/parser core tested; authoritative finalizer/MIC integration BLOCKED |
| P03 | IN_PROGRESS | Queries and Action import core tested; source feed/reconnect BLOCKED |
| P04 | IN_PROGRESS | CLI runner core tested; app_fw/TIDL/cgroup integration pending |
| P05 | BLOCKED (paths) | PATH-01 user question pending; independent work continues |
| P06 | IN_PROGRESS | Foundation RPM installed; TIDL/AMD runtime integration pending |
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

No development commit/push yet; initial document/contract-header checkpoint is
awaiting P00-PUBLISH-r2 review. Implementation .cc files are excluded from it. Raw evidence/builds/dependencies are excluded from Git. Future commits
record their SHA, branch, push exit and verified remote ref in a subsequent record
(to avoid a self-referential commit SHA).

R01–R18 acceptance tests: NOT_RUN until linked to actual product evidence. Physical
device: none discovered; NOT_RUN. ARM build/runtime: NOT_RUN, intentionally gated.
INSTALL-01 and SYNC-01 require platform integration beyond private fixture tests;
fail-closed behavior must not be reported as successful online registration.

Next: build meaningful GTest/GMock and C consumer tests under accepted r4;
review/commit/push P00 checkpoint; then reviewed implementation checkpoints. Pending
PATH-01 is a product question, not a reason to block unrelated catalog/CLI work.

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
