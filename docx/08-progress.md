# Development progress and verification

Updated: 2026-09-28. This board distinguishes implementation, review, test execution,
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
| P02 | IN_PROGRESS | Catalog/parser core and offline subprocess harness tested; authoritative finalizer/MIC integration BLOCKED |
| P03 | IN_PROGRESS | Queries and Action import core tested; source feed/reconnect BLOCKED |
| P04 | IN_PROGRESS | Private PID1 isolation r2 accepted and installed fixture passed; authenticated app_fw/TIDL broker and cgroup integration pending |
| P05 | IN_PROGRESS | Private identity experiments tested; PATH-01 pending; production identity, policy, namespace isolation and mount gates open |
| P06 | IN_PROGRESS | Release7 installed; TIDL binding and injected async frames tested; public transport/AMD integration pending |
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

Reviewed development checkpoints through `ef3f87964c46f81a2f30cacbfcde7eac27235d20`
are committed and pushed on main, with exact remote refs verified. The chronological
records below retain earlier failures and pending states as historical evidence;
later entries supersede their status. Raw evidence/builds/dependencies are excluded
from Git. Each publication records SHA, branch, push exit and verified remote ref
in a subsequent entry to avoid a self-referential commit SHA.

Full R01–R18 product acceptance: NOT_RUN; scoped tests are linked below. Physical
device: none discovered; NOT_RUN. ARM build/runtime: NOT_RUN, intentionally gated.
INSTALL-01 and SYNC-01 require platform integration beyond private fixture tests;
fail-closed behavior must not be reported as successful online registration.

Next: finish exact Release5 offline-harness RPM upgrade/installed tests and publish
the accepted checkpoint; continue platform integration work. PATH-01 remains a
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
