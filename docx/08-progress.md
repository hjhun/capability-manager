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
| P04 | IN_PROGRESS | Private worker/session and leased no-job typed startup verified; Release15 ordinary installed package checks verified; authenticated app_fw/TIDL broker and resource gates open |
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

Reviewed development checkpoints through `4b549f0270d4d8e787ee3ae34b116edb463fe071`
are committed and pushed on main, with exact remote refs verified. The chronological
records below retain earlier failures and pending states as historical evidence;
later entries supersede their status. Raw evidence/builds/dependencies are excluded
from Git. Each publication records SHA, branch, push exit and verified remote ref
in a subsequent entry to avoid a self-referential commit SHA.

Full R01–R18 product acceptance: NOT_RUN; scoped tests are linked below. Physical
device: none discovered; NOT_RUN. ARM build/runtime: NOT_RUN, intentionally gated.
INSTALL-01 and SYNC-01 require platform integration beyond private fixture tests;
fail-closed behavior must not be reported as successful online registration.

Next: review the concrete same-authorized-System-subject/different-fixture-object
real catalog admission slice. The original different-subject direct-read subset
remains BLOCKED after the diagnostic measured only one allowed UID301 context.
Worker generation-lifetime reader r2 source/ordinary-native/publication scope is
ACCEPTED; actual publication is recorded separately. A separate fixed
System301/priv_platform-group diagnostic is scoped ACCEPTED with no new policy.
Private no-job leased bootstrap is scoped ACCEPTED; NamespaceInit/CLI
nondelegation and production worker wiring remain separate. Prior failures are
preserved; installed Release15 and PATH-01/product gates are unchanged.

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

### P06-READ-POLICY-RECOVERY source/CLI slice (r4 ACCEPTED)

Release15 spec/README+accurate08 published as
faaad0b24d865bd24ea12c7a0dbcede07b73814c, commit0/push0 and exact origin/main
verified, tree clean before this source slice. Installed Release15 remains
separate from the following two newly added recovery/test files.

Temporary helper+12 host-test primitive r1 is independently ACCEPTED, original
helper34b49a5611b496d9f592c6d69b50409181ed152494cc2bf61820d20f73a3f9e1
and testc0bd0914c651f845a36efc959e306ffb95b3be1c137a11f88ab018293ef5b7fe.
Actual project integration adds explicit --recover-only CLI, root-owned protected
source ancestry/ACL validation and fixed validated sibling db_access_probe import
for the accepted trusted-parent check. No default --run, role spawn, provisioning
or policy-install CLI is supplied. Printed recovery argv remains an eventual
full-fixture obligation. Complete immutable plan/never-unlocked inherited SH and
independent recovery EX mechanics are unchanged; validated recovery.json.next is
only a discardable non-authoritative receipt, never a substitute plan.

Host read-policy-recovery-host-r2:16/16 PASS, py_compile0; existing unittest
discovery automatically includes the new test without CMake/payload changes.
Native read-policy-recovery-native-r1 checks archive
753aca9276ea2f6cff30cc70f70d42cf3863b446e6dce39f9ea244348eaa8b28 and
three exact source hashes (new helper/test plus unchanged trusted dependency),
then16/16 PASS, remote0/transport0 in protected root0700 scope. Native tests use
fake Operations under the target root test runner: real local files/flock/fork
and deletion, but no SMACK/Cynara write, SQLite, context drop, root workload or
operational data. Host/non-policy tests cover default CLI refusal, provenance
refusal before mutation/import, inherited SH lifetime, partial fake installation,
identity/ACL/ancestor/plan/deletion failures and receipt fsync retry. This does not
prove actual load2 recovery, protected CLI execution on a real rule journal,
fixed-role setup/FD lifetime, DAC/SMACK causal admission or production policy.

Frozen two-file source manifest is /tmp/capmgr-read-policy-recovery-source-r1.sha256.
Source/CLI independent verdict pending; future full fixture source/safety review
remains required before any global policy write or root-role matrix execution.
No package/install change is included here.

P06-READ-POLICY-RECOVERY-r2 is CHANGES_REQUESTED only for the added dependency
loader provenance: standard SourceFileLoader may select an unchecked valid pyc
cache instead of the validated .py. Earlier r1 journal/SH/EX/receipt acceptance
stands; the16-case host/native outcome does not close this source finding.

Revised source r4 loads the fixed dependency by opening/validating its owned
NOFOLLOW/CLOEXEC regular source FD, checking exact named/pinned identity, root
nonwritable/singlelink/noACL metadata, bounded bytes and stable size/time, then
compiling those source bytes into a fresh namespace with a non-main module name.
It never consults a bytecode cache and does not claim to prove interpreter/stdlib
or environment provenance. Three new tests include an actual valid conflicting
pyc selected by the standard loader but ignored by the source-only path, plus
actual trusted-source writable/ACL refusal. Cache test files stay inside their
owned temp directory even with an external pycache prefix.

Host-r3 preserves a test-message regex failure (not a guard bypass); subsequent
focused runs PASS. Native-r3 checks exact source but FAILS19-case run because the
conflicting-source fixture inherited target umask0000 and created a writable
.py, correctly rejected by the unchanged source guard. A read-only native umask
probe confirms0000. Revised fixture explicitly chmods only its own temporary
source0600 before validation; no product check is relaxed. Host-r6 focused19/19
and host-check-r5 CTest8/8 PASS. Native-r4 checks exact3 source/dependency hashes
then19/19 PASS remote0/transport0, fake Operations/files/flock/fixed child only.
No global policy, actual real-journal CLI recovery or role-context matrix ran.
Native r4 archive SHA256
a28de5595e8c4a0d1932778e69a1073cf27dda1654bcb9d7eaf0e9d6e6a894dd
is preserved. Final exact source/CLI+administrative08 r4 verdict remains pending;
full fixture execution still requires its own source/safety review.

P06-READ-POLICY-RECOVERY-r4 FINAL SOURCE/CLI PUBLICATION is ACCEPTED by w1:pA
for exact new helper/test plus administrative08. Original reviewed08 SHA256
cd017db0d0e97aa8d017be938a9a58a66164f2de79991158ea306ca78caf7544 is preserved.
The r2 bytecode-selection finding is CLOSED by source-only loading; previous
pending states are chronological and superseded. Actual real-rule recovery,
fixed-role/provisioning and root policy execution remain separate unaccepted gates.

### P06-READ-POLICY-ROLES-r1 — private fixed-role transport (review pending)

Owner w1:pJ implemented a build-only `capmgr-read-policy-role` image and a private
single-threaded Python coordinator transport. This is the next local slice after
recovery checkpoint `5728f81973be1f0e083782633ce7d658b5615b0e`; no fixture policy
write, root credential-drop role execution, operational DB or package is enabled.
The full provisioning/matrix orchestrator and real-journal crash/recovery remain
unimplemented and require separate frozen safety review before `load2` writes.

The parent owns no SQLite state. Each preallocated Role retains positive returned
PID ownership before handshake/logging, or marks an ambiguous spawn outcome as
uncertain. Stable source duplicates >=6 precede the final single-thread FD
snapshot; signal handlers are blocked across that snapshot and spawn/PID recording.
Ordered mappings establish null stdio, command FD3, status FD4 and the inherited
journal SH reference FD5. All other descriptors close in the child. The fixed C++
image refuses missing/extra/aliased/wrong-direction endpoints before setup/SQLite;
it never writes diagnostics to FD4 until the complete table is validated.
Recovery SH is close-only and retained through process exit. ECHILD disables
signaling; ESRCH is not absence proof. Unknown spawn/cleanup must retain the journal,
scope and rules and cannot produce PASS or automatic recovery.

The root writer owns coordinated SQLite and issuer leases in its separate process;
UID301 reader commands use real leases/local C queries with explicitly modeled
Authorize/Confirm/Finish. The fixed image has no arbitrary executable, UID, path,
policy-rule or production backend command. Root-writer ownership and injected
handoff are narrower than a production app_fw writer, TIDL or Cynara result.
Full context/ancestor/control-pipe and real object-label checks precede causal
DAC/MAC interpretation. Same-label denied-MAC is directory-admission/direct-file
veto, not a claim that SQLite ran. Actual native role/matrix results are NOT_RUN.

Host `read-policy-roles-host-build-r2.log` builds the image. Focused host
`read-policy-roles-host-tests-r5.log` runs 16 transport/table tests: 15 PASS and one
explicit root-owned-journal positive-table skip. Fake-image closed stdio/high FD,
partial writes/deadlines, inherited SH, unknown spawn, ECHILD/ESRCH and actual fixed
image invalid table guards are covered without changing IDs/labels or opening
SQLite. The first focused attempt retained a child-only response writer in the
parent, hiding child EOF until timeout; `host-tests-r1.log` FAIL is preserved.
The fix closes child-only pipe ends before handshake; r2 transport10/10 PASS.
`host-tests-r3.log` correctly refuses the non-root journal in the positive fixed
image setup and is preserved; r4/r5 report that root-only case honestly as skipped.
`read-policy-roles-host-check-r1.log` CTest8/8 PASS includes these local guards,
not privileged execution. Exact native compilation/guard-only tests and independent
source disposition remain pending. Global policy, real C admission under labels,
recovery CLI on an actual journal and production/direct-open gates stay open.


READ-POLICY-ROLES-r1 source review requested two corrections; it was not accepted.
Task/FD enumeration must distinguish readdir error from EOF, and a perpetually
writable partial/EAGAIN sender must still enforce the absolute deadline. r2 clears
errno per readdir, rejects incomplete scans/closedir failure before any FD4
message, and adds two distinct test-only GNU ld wrapped fixed images injecting EIO
after one task/all six expected FD entries. The normal image has no failure seam.
Send checks remaining time before select and again before each write; three new
ready/partial/EAGAIN and retained-owner regressions cover the finding.

The historical r1 compile-only native attempt at protected read-policy-roles-r1
matched5/5 source and75 unchanged accepted core files, reused pinned accepted
Release15 catalog/adapter archives and compiled the new role/API files successfully
(remote0/transport0). It executed no role or policy and does not supersede source
CHANGES_REQUESTED. r2 host `read-policy-roles-host-check-r3.log` CTest8/8 and
focused `read-policy-roles-host-tests-r6.log`21 cases (20 PASS/1 root-positive table
skip) pass. Guard-only native execution remains NOT_RUN pending exact corrected
archive/image/protected-path review. Neither revision proves real label context,
causal C admission, policy writes or actual real-journal CLI recovery.


READ-POLICY-ROLES-r2 source findings were independently closed. Native guard-only
method-r2 was revised before execution because nested Python lacked inherited
bytecode suppression and the repeated path audit omitted build itself. Accepted
method-r3 propagates PYTHONDONTWRITEBYTECODE=1, retains -B, audits build/source
ancestors and exact source files, and refuses any cache before/after. The bounded
native guard attempt `read-policy-roles-native-guards-r2.log` matched source6/6,
method2/2 and all three binaries/protected metadata both before and after. It ran
21 tests: 19 PASS and two FAIL, remote1/transport0; no writer/reader setup, SQLite
or policy executed. Both injected EIO enumeration guards and normal valid-table
invalid-role refusal passed. Closed-stdio driver exit120 and the4KiB byte-limit
case reaching its incidental100ms deadline are preserved as fixture failures.

The r3 amendment changes tests only: the closed-stdio driver sends the bounded exit
RPC without logging through stale Python stdio, verifies actual child exit/reap,
closes its Role/journal descriptors and then _exit(0); it cannot bypass child proof.
The4KiB byte-limit case gets2s for native one-byte processing; its separate50ms
partial-frame deadline and always-ready/partial/EAGAIN absolute-budget tests are
unchanged. Host focused-r7 runs21 cases20PASS/1 explicit root-only skip; full
`read-policy-roles-host-check-r4.log` CTest8/8 PASS. Four other code/build files are
byte-identical to accepted r2 source. Native r3 guard-only rerun is NOT_RUN pending
revised exact archive/method/protected path safety review. No fixture/root policy,
real-journal CLI recovery or production acceptance is inferred.

Final r3 host tests-r8 and check-r5 repeat the21-case (20PASS/1skip) and CTest8/8
results on the final test bytes: only the4096-byte size case uses2s; EOF/duplicate
cases retain100ms and independent absolute deadline regressions remain unchanged.


### P06-READ-POLICY-ROLES-r3 — native guard evidence (ACCEPTED)

The independently accepted r3 source/guard-only pre-execution manifest6/6 has
original08 SHA `5ab39d5aff080876513b2af8eb1837e643d7b2a719c8fa84c0c313079fc20533`; its archive SHA is
`eca2a2bbb6763ba532a757b98184a1d7b9ef2e3fccf3b4468018562296cd7091`.
Runtime `read-policy-roles-native-guards-r3.log` records actual owner SDB invocation
with outer90s, transferred method2/2 assertions, protected root600 method files,
installed hash-checked60s watchdog and fixed clean environment including inherited
PYTHONDONTWRITEBYTECODE=1/-B. Exact source6/6 and archive/protected ancestry,
initial namespaces/ext4/no-cache checks precede and follow execution.

All21 native tests PASS without skips, including both injected readdir-EIO errors,
valid-table invalid-role refusal before Drop/SQLite, corrupt table/regularFD4
non-write, closed stdio/high sentinel, inherited SH lifetime, ambiguous spawn and
ECHILD/ESRCH bookkeeping, partial-frame/always-ready/EAGAIN deadlines and actual
owned child reaps. Three identical reviewed images were copied into the new r3
scope, not freshly compiled: normal9be1b39f9c2f67309cb21a5bd25c46162a2f680199c6a7d312a7f23a6f090be4,
task1055c866c2cbe29dde45b6f961c595a1ecea13beff0ade09a94c5e010d286ad9,
FD b5e2b20d232de2185219f86273477c859fdb8bf68cb1c9252b4053152244a5a8.
Post-run preflight and guard return0, remote0/transport0, no timeout/unknown actual
child or cache mutation was observed. Earlier r2 native19PASS/2FAIL remains FAIL.
This supersedes only the historical r3 guard-runtime NOT_RUN statement.

No real root-writer/UID301 context-drop, SQLite workload, provisioning/load2,
real-journal recovery, TIDL/Cynara authorization, package/install or production
admission follows. Full causal matrix/crash-route remain separate development and
pre-execution gates. InstalledRelease15 and previous product gates are unchanged.
Next: implement/freeze normal matrix and refusal-only crash/recovery safety
scope after publishing this accepted local source/guard checkpoint. No policy write may precede that separate exact fixture review.


Independent final READ-POLICY-ROLES-r3 disposition: ACCEPTED for reviewed five
source/build/test files plus administrative08. Original reviewed08
`80d1207034f4344a35af945594f96e7a8d4638d38045e32f053092e93d71e0dd`
and earlier source08 hash are preserved above. Native21/21 and exact method,
pre/post checks, cleanup/exit evidence close only the private guard checkpoint;
full normal-role/policy/crash/recovery/package and production gates remain open.
Only pending heading/Next and this disposition were changed after that review.

### READ-POLICY-ROLES publication and P06-READ-POLICY-REFUSAL-r1 (ACCEPTED)

Accepted role/guard source+08 checkpoint
`9fe2a07a3cdd310492abfd93c7d3df2ddf19bfea` was committed with signoff and message
checks, pushed normally (exit0), and exactly matched origin/main; tree was clean.
This records owner-observed publication, not an independent reviewer remote query.
InstalledRelease15 is unchanged. Full root role/context/SQLite/policy remains open.

The next helper slice adds explicit root `--assert-contended` beside `--recover`.
It shares actual trusted journal validation and independent flock EX|NB acquisition.
On contention, it validates the immutable plan and current pinned scope and returns
EX_CONTENDED/mutations0; this does not distinguish SH versus EX, identify the owner,
prove any process/task alive, or authorize signaling a reported PID. The later
causal crash fixture must separately establish the expected exclusive child and
sole surviving SH reference. An available EX returns a failed assertion with zero
mutation; that branch does not claim to have validated the scope itself.

Both assertion outcomes return before delete/revoke/receipt persistence. Existing
recovery.json and recovery.json.next bytes are untouched. Provenance/plan/scope
errors are exceptions, not a successful contention observation. Normal --recover
still requires EX before deletion/all nine best-effort revocations and receipt
retry; its contention error text is now neutral rather than inferring a SH holder.
No source-loader, rule matrix, JournalOwner/SH ownership, package or production
permission changes accompany this flag. No operation runs by default/on import.

Host `read-policy-refusal-host-r2.log`25/25 FakeOperations tests PASS and
`read-policy-refusal-host-check-r2.log` CTest8/8 PASS. Cases cover actual independent
SH and EX descriptions, inherited child SH after parent close, specific CLI result/
exit mapping, unlocked assertion failure with no delete/revoke/persist, unchanged
existing receipts, malformed plan/scope refusal and normal cleanup/retry. The
earlier outside-project three-test prototype and host-r1 used a proposed
LIVE_SH_EX_CONTENDED label; engineering review corrected this inference before
freezing source. They are not evidence for the final neutral marker.

Exact native fake-operation tests, source acceptance and any actual assertion CLI
on a real journal are NOT_RUN/pending. New native tests may use owned temp files,
flock and bounded fork only; they cannot write load2 or execute actual root roles.
The normal matrix/crash fixture (including future hold-reference/adoption proof)
requires separate frozen source/interpreter/image/watchdog safety review before
any policy mutation. No production admission, worker/broker, direct-open matrix,
installer/Action integration, PATH/remount or physical/P09 ARM gate is closed.

Independent REFUSAL-r1 source disposition: ACCEPTED for the narrow local helper/
test contract only. Original reviewed08
`3e876611b6de8cd462169920eb3599b2f04885f04ce0d6d0d575f2a24b677a6e`
and frozen three-file manifest
`29da394e2f3584a14b83570600c354d84150d4134399e3e1e6fccdedfe9ca9ec`
are preserved. EX_CONTENDED is an acquisition-time observation, not a guarantee
that a holder survives until CLI return. No SH/EX identity or liveness inference
is added by this disposition.

Native `read-policy-refusal-native-r1.log` checks exact helper/test/unchanged
db_access_probe dependency3/3 from archive
`f3aae12734eb768508b688167a6646e9023e576513deaf5ef14303169149ff17`
in the new root0700 protected `read-policy-refusal-r1` scope. Owner driver checks
root/nonwritable/noACL ancestry and initial PID/mount namespaces before creation,
validates archive member names/types/sizes/hashes and sets extracted source0600.
The bounded60s runner invokes Python -B with fixed PATH/LANG, no user site and
inherited PYTHONDONTWRITEBYTECODE=1. All25 FakeOperations tests PASS with remote0/
transport0. Tests use owned temporary files, independent flock descriptions and
bounded fork only; no DefaultOperations load2, SQLite, root role/context drop or
operational DB execution occurs. This supersedes only the earlier native fake-
operation NOT_RUN statement. Actual assertion/normal recovery against a policy
journal, full role/crash fixture and production gates remain NOT_RUN/open.
Final native/publication review is pending; the source verdict alone is not it.

Independent final REFUSAL-r1 disposition: ACCEPTED for the reviewed helper/test
and FakeOperations checkpoint. Original source08 and accepted final-evidence08
`73a84700166369d679988ac26f0ff94cc279eef9bfa4f3b36deda3a172815575`
are preserved. This supersedes the historical final-review-pending statement.
Only the heading and this disposition changed after that review. InstalledRelease15
is unchanged; real policy-journal CLI, root role/context/SQLite matrix and crash
supervisor remain separately gated. Next: freeze the fixed recovery-reference hold
increment and full causal fixture safety scope; no policy execution before review.

### REFUSAL publication and P06-READ-POLICY-HOLD-r2 (ACCEPTED)

Accepted helper/test+08 checkpoint
`37dd5a3462b466a5d93b2eddd20d14e7a191d6f5` was committed with signoff and message
checks, pushed normally (exit0), and exactly matched remote main. The independent
fixed-role hold WIP was excluded from staging. InstalledRelease15 is unchanged.

The next build-only writer fixture adds no-argument `hold-reference`, permitted
only without a coordinated writer or issuer leases. It acknowledges a fixed20s
monotonic interval started before the ACK, stops reading commands and retains
inherited FD5 until process exit despite command HUP. Positive timeout calculation
uses one sampled now and rejects nonpositive/unbounded values; EINTR recomputes
against the same absolute end. The engineering-review negative-poll-timeout
finding was corrected before freezing. This retry deadline is not a hard bound
on scheduling or arbitrary status-pipe/system-call latency; ACK backpressure uses
up the interval. It never unlocks/converts FD5, opens SQLite, writes policy,
executes jobs, forks descendants or changes task credentials/label.

The separate hold test module is opt-in only via an explicitly reviewed fixed
image environment; ordinary CTest never selects it, even under root. Positive
proof closes only parent command/SH references after ACK, separately observes the
exclusive unreaped child with WNOHANG and independent EX contention, then requires
bounded actual normal wait/reap0 and EX success. Contention alone is not child
identity/liveness or SH-vs-EX proof. Malformed arguments must fail before ACK/hold.
A fresh nonexistent catalog path must remain absent; cleanup/unknown ownership
failure retains the owned test scope rather than inferring absence or PASS.

Host `read-policy-hold-host-check-r1.log` CTest8/8 PASS with both new root tests
explicitly skipped because the opt-in image is unselected. Separate
`read-policy-hold-host-tests-r1.log` likewise2/2 SKIP; this is discovery/build
coverage, not hold runtime proof. Exact native compile, source/safety review and
opt-in reference-only execution are pending. Real root writer/UID301 context,
SQLite, load2, causal adopted-child crash barrier, actual policy-journal CLI and
all production gates remain separately NOT_RUN/open.

Native compile-only `read-policy-hold-native-build-r1.log` FAILED before source
extraction/compilation: the driver expected four inputs but the archive/manifest
contained three (unchanged role-transport dependency was omitted). Remote1/
transport0 is retained; no hold/role ran. Corrected preparation uses a new protected
root0700 `read-policy-hold-r2` scope and four exact inputs including the unchanged
transport dependency. `read-policy-hold-native-build-r2.log` matches all75 accepted
core source files and explicitly verifies both accepted native static-library
hashes, recompiles the role and four API files, links the normal fixed image,
chmod0755 and checks root ownership/singlelink/noACL/no filecaps. Archive
`254b2dba724a13229548bc242f7ff1c7c33dcb9b85c9fdbf411eb5e10b54b8de`
checks4/4; remote0/transport0. Binary
`42324e7c5df8679a879041d7121134bdb5ff1973a7b25353d658ae21162fedca`
is build-only; no reference-hold/root role or policy execution occurred.

Read-only `read-policy-hold-native-preflight-r1.log` FAILED before execution:
Python mkdir(parents=True) created the intermediate own source/test directory0777
under target umask0000. The outside owner wrapper reported transport0 without a
remote sentinel, so that is not remote success. Path diagnostic confirms the
single offending directory inside the new exclusively owned root0700/source0700
scope; historic shared0777 tree is unrelated and untouched. The pinned correction
checks root ancestry/type/owner/noACL, exact initial0777 and dev/inode, opens
NOFOLLOW and fchmods only this newly created directory0700 with named/pinned
identity rechecks. No source or image bytes change.

`read-policy-hold-native-preflight-r2.log` records that correction, method/source
and protected ancestry/modes/ACLs, exact image digest, initial namespaces/ext4
and no-cache checks PASS, remote0/transport0. Preflight success is not hold runtime
proof. The separate selected run
method is frozen for source/safety review; opt-in two tests under installed60s
watchdog and owner90s outer bound remain NOT_RUN. Valid root writer context reads
only its own label; no credential drop/SQLite/load2/catalog creation is included.

Independent HOLD-r2 source/pre-execution safety disposition: ACCEPTED for the
fixed build-only image and two opt-in reference-lifetime tests only. Original08
`56baa1266e94ac8fae08d211f81228a57b520d0d6ed5d3ff8e2313eace1018a9`
is preserved. The accepted method SHA pair is
`7b6d61e5adc660164216bfbb3dd61e35faa4ffb45292406e6238fc0ded494b93` /
`80c20458136d1902c44b97cc2da9949859e659848a5633203307d32fc92f15c8`.

Owner `capmgr-read-policy-hold-execute-r1.py` checks exact method digests and
root0600/singlelink/noACL/caps metadata and protected ancestry immediately before
executing the frozen method with a clean environment. Log
`read-policy-hold-native-tests-r1.log` records outer90s and installed60s watchdog,
pre/post source4/4/image/interpreter/watchdog/initial namespace/ext4/no-cache
checks, selected image and actual argv. Both tests PASS, no skips, 20.052s total;
REFERENCE_HOLD_ONLY_RETURN0, remote0/transport0. Positive test separately observes
owned nonexit and lock contention after parent references close, then actual
normal reap0, EX success and absent fresh catalog; malformed command has actual
owned exit1 before ACK/hold. The elapsed check is an upper bound, not independent
precise20s lower-bound measurement. Test teardown requires actual child cleanup
before owned temporary-scope removal; no retained-scope marker/timeout occurred.
This supersedes only the opt-in reference-test NOT_RUN, not the full policy/crash
fixture. Final source/runtime/publication review is pending; no SQLite, UID301
drop, load2, actual journal CLI, adopted child, product or package gate closes.

Independent final HOLD-r2 disposition: ACCEPTED for the reviewed private image
amendment/test and reference-only runtime checkpoint. Accepted final-evidence08
`7b95b3caa8ead644e8330f5ffe8ae8f0301c9874a56fd6f458694461d34bc694`
is preserved alongside original source08. This supersedes historical final-review
pending; only this disposition and heading changed after acceptance. Next: finish
and freeze the full modeled normal-role and adopted-child crash fixture, including
causal context/pipe/ancestor checks and actual policy-journal CLI safety. No load2
or real root-writer/UID301 matrix execution before that separate review. Installed
Release15, product admission, broker/resource, Action/installer/PATH/remount and
physical/P09 ARM gates remain unchanged/open.

### HOLD publication and P06-READ-POLICY-FIXTURE-r1 (source review pending)

Accepted HOLD-r2 two-file+08 checkpoint
`9d98c40e778d073e810fc23e0b51964969749f80` was signed off, committed/pushed0 and
exactly matched remote main with a clean tree. InstalledRelease15 remains separate.

The next six-file source scope adds the explicit --run normal modeled-C matrix
and coordinator-loss fixture, a private exact-child helper/two tests, and a
fixture-only observation in the fixed role image. No product library/header,
TIDL, package, rule matrix or service/backend activation changes are included.
No operation runs on import/default entry. Fixed source layout supplies image,
protected dependencies and absolute recovery script; no runtime path/UID/label/
rule/executable selector is accepted. Full source boundary is recorded outside
project in `capmgr-read-policy-fixture-boundary-r1.md` for peer review.

AdmissionObservation delegates the existing LeasedCatalogGate and records whether
it actually returned owning admission. Denied roles require no return before
client SQLite construction; they report SQLite NOT_RUN without inferring the
precise failed directory/metadata syscall from NULL/Confirm0. Positive create0/
Confirm1 and real metadata observations are required. SAME-label directory/direct
file veto, actual IDs/groups/caps/NNP/own-label, platform ancestors/control pipes
and OFD lock permissions remain measured setup prerequisites. The root-writer
scope and explicitly modeled handoff do not establish real TIDL/Cynara, app_fw-
owned production directory protection or relabel revocation.

The crash supervisor owns no SQLite or inherited journal reference and creates
the durable full plan only in its coordinator child after fork. Signal blocking
keeps spawn uncertainty marked until positive PID attachment; own child inventory
is bookkeeping, while exact waitid(P_PID, WNOWAIT) after actual coordinator reap
alone establishes adopted-child ownership before signaling. A reported PID,
flock contention, SIGKILL/ESRCH or watchdog exit never proves task identity/absence.
Coordinator retains its writer unreaped through hold ACK/first checked actual
write-close, then a barrier forbids a second rule. A specific neutral assertion
and unchanged scope/plan/receipts plus independently observed live owned child
are required before exact final reap and real normal recovery/idempotent retry.
Any unknown child/adoption/extra descendant blocks automatic recovery and PASS.
Global rule plan remains exactly the accepted nine new-subject-or-new-object rows.

Host `read-policy-children-host-r3.log`5/5 PASS exercises injected ECHILD/ESRCH/
reap failures and actual isolated unprivileged subreaper adoption/kill/reap, with
bounded child alarms and exact-record cleanup. `read-policy-probe-host-tests-r2.log`
10/10 PASS uses only FakeOperations/CLI responses/owned temporary files, including
first-write failure versus post-write ACK ordering, no second row, exact CLI
result/duplicate/size/timeout validation, receipt preservation and default-entry
refusal. `read-policy-probe-host-check-r2.log` full CTest8/8 PASS. New source-only
host tests do not execute root roles, labels/groups/capability drop, SQLite,
load2 or an actual policy-journal CLI. Existing hold tests stay opt-in skips in
ordinary CTest. Native updated compile/safety-test method, protected full binary/
source/preflight and any actual policy execution are NOT_RUN/pending separate
review. No product/private primitive acceptance is inferred from this request.

Independent FIXTURE-r1 source disposition: CHANGES_REQUESTED. The crash path
recorded an unexpected descendant only as an error, then could run destructive
automatic recovery after a later empty inventory and the two known child reaps.
Final FAIL did not undo those mutations. The six-file r1 manifest and original08
`43e99f5ec098df79a8970fe30395ce1c7d021c89c6e8b7bac4c2d4e1d7f17858`
are preserved; no native root role or policy fixture was executed.

Compile-only `read-policy-fixture-native-build-r1.log` checks archive
`07b92d67538340f4bff9236a9cfa3611c37a8b317ba376b699a1c36ec7aa1477`,
exact nine source/dependency entries, 75 accepted core source entries and both
pinned Release15 static archives. A new protected root0700 scope builds the role
and affected four API objects. Build-only image
`46849eeaad62c0c336622f50656bf0f51712c4d4c1304845cf333d3475ab7872`
is root0755/singlelink/noACL/no caps; remote0/transport0. This is r1 compilation,
not a test of normal roles, context drop, SQLite, load2 or real-journal recovery.
The discovered Python orchestration defect prevents full fixture execution.

### P06-READ-POLICY-FIXTURE-r2 (accepted local source/non-policy checkpoint)

Only the probe, its test and administrative08 change from frozen r1. A sticky
recovery-eligibility record now rejects automatic recovery permanently after any
unexpected child inventory or inventory-read failure. Observed uncertain known
child ownership/adoption also permanently rejects recovery even if later known
record cleanup finishes. Later empty rosters and reaped known records cannot
restore proof about a previously untracked child. Known children can still be
cleaned up; uncertain scope/journal/rules stay retained for independent recovery.
Ordinary functional failures with uninterrupted ownership/absence proof remain
eligible for safe cleanup; an error string alone is not the recovery criterion.

`read-policy-probe-host-tests-r3.log`15/15 PASS adds tests of the actual run_crash
control flow with fork/FD/filesystem/transport/recovery all replaced by in-memory
fakes. Every inventory boundary tests an unexpected extra then empty roster and
a read error then empty roster. Adoption and observation proof loss remain
ineligible after simulated eventual known-child cleanup; assertions require zero
real-recovery calls and no REMOVED_SCOPE/PASS marker. Intact normal flow and a
functional CLI failure separately prove the intended safe recovery branch.
`read-policy-probe-old-ownership-diagnostic-r1.log` runs those five orchestrator
tests against the hash-checked frozen r1 probe: eight subcase assertion failures
reproduce the old unsafe invariant. Its diagnostic exit0 means the expected old
failures were observed, not product PASS. No actual fork/policy/role IO occurs in
these orchestrator cases. Full `read-policy-probe-host-check-r3.log` CTest8/8 PASS.
Exact r2 native preparation, non-policy safety tests and full context/policy/
real-journal execution remain NOT_RUN and require separate method/safety review.
InstalledRelease15 and all production/external prerequisite gates remain open.

Independent FIXTURE-r2 local source disposition: ACCEPTED; the r1 lifecycle finding
is closed at source level only. Original reviewed08
`10e5c70385f10253115692ad5f5102e425db72348e8e1182ead7b67989d949ca`
and frozen six-file manifest are preserved. No full policy runtime follows from
this source acceptance.

`read-policy-fixture-native-prepare-r2.log` checks the new nine-member archive
`12af1e009cdb9bdd0f03ed953a5c8ac79439ac696f709e4b655290083abb1940`,
exact source9/9, unchanged role plus core75 and both pinned static archives. The
identical compiled r1 image is copied to the new protected root0700 r2 scope,
explicitly not recompiled. Read-only `read-policy-fixture-native-preflight-r2.log`
checks method2/2, protected source/build ancestry, source9/9/image, interpreter/
watchdog hashes, initial namespaces/ext4 and no cache; remote0/transport0.

Independent NON-POLICY-METHOD-r2 pre-execution disposition: ACCEPTED for selected
children5/probe15 tests only. Method hashes are preflight
`e3bdb087f24fcbf09eb3824e6cfa17a004fde5dfddd03c0d241c0cccd6c93298`
and runner
`eb156d735d1ad7cc21448bb60d2ad88e0405b58cfa0246c02ce39bc55824dee3`.
Owner `capmgr-read-policy-fixture-execute-safety-r2.py` validates those transferred
method hashes/root0600/singlelink/noACL/no-cap protected metadata immediately
before invoking the clean environment with -B/inherited no-bytecode. Log
`read-policy-fixture-native-safety-r2.log` records installed60s watchdog/outer90s,
exact before/after preflight source9/9/image/interpreter/watchdog/provenance checks,
all20 tests PASS without skips (0.092s), NON_POLICY_SAFETY_RETURN0,
remote0/transport0. Actual isolated owned-child subreaper/adoption/kill/reap and
injected/fake failure cases run; no role image, context drop, SQLite, load2 or
actual policy-journal CLI runs. CLI/receipt strings in these unit logs come from
fake responses, not policy execution. No timeout/retained-child marker occurs.
This supersedes only r2 non-policy native-test NOT_RUN; full --run remains NOT_RUN
and UNAPPROVED pending separately frozen exact-method safety review. Final local
native/publication disposition is pending; installedRelease15/product gates stay
unchanged.

Independent final FIXTURE-r2 non-policy native/publication disposition: ACCEPTED
for the reviewed five code/test files plus administrative08 only. Accepted
final-evidence08
`1c8925fbe6c8af412f1c1e68efbcf4ef9aacf885c85137eff901546261daec1e`
is preserved alongside original source08. This supersedes only final local
native/publication pending status. Next: separately review the frozen full-runtime
plan and protected invocation before any root-writer/UID301 drop, SQLite, load2,
real-journal CLI or adopted-child policy-crash execution. No full --run acceptance,
package/install or production activation is implied by this private checkpoint.

### FIXTURE-r2 publication and FULL-METHOD-r2 runtime (overall FAIL)

Accepted local-source/non-policy checkpoint
`e0683a57601603406e2aa2d0295b7d0a1c570369` was signed off, committed/pushed0 and
exact remote main verified with a clean tree. InstalledRelease15 remains separate.
Independent FULL-METHOD-r2 pre-execution safety disposition accepted only the fixed
reviewed source/image route, clean environment and installed180s/owner210s bounds.
It did not accept a runtime outcome, rule broadening or production activation.

Owner `capmgr-read-policy-fixture-execute-full-r2.py` immediately rechecks transferred
method2/2/protected metadata, then invokes the reviewed runner. Log
`read-policy-fixture-native-full-r2.log` records source9/9/archive/image46849e...7872,
interpreter/watchdog/protected ancestry/initial namespace/ext4/no-cache checks
before and after. The full command returned1, CAPMGR_REMOTE_EXIT1 and
TRANSPORT_EXIT0; no timeout or full fixture PASS occurred. This run is preserved
as FAIL, independently confirmed NOT_ACCEPTED for the overall fixture result.

The normal root-writer/UID301 modeled-C phase completed separately: allowed readers
acquired shared OFD locks, directory/direct-file RO baselines and MAC write vetoes;
same-group denied-MAC acquired its shared lock but failed directory/direct-file
read and create returned NULL/Authorize1/Confirm0/Finish0/gate-returnfalse/client
SQLite NOT_RUN. Missing-group role failed at lock admission with later stages
NOT_RUN. Positive clients had Authorize1/Confirm1/Finish1 and actual local SQLite
validation/query. Source checked exact ID/group/capability/NNP/label context tuples,
but those raw replies were not printed in this log and are not claimed visible.
Generation1->2 local queries, EX BUSY while leases remained, unchanged DB/WAL/SHM
mode/label/dev/inode through writer close, fresh RO create/query and final physical
client closes allowing EX all passed. Two actual protected --recover calls each
returned cleanup PASS/errors[]/remaining_rules[]; then
REMOVED_SCOPE=/opt/usr/capmgr-read-policy-0d6098ed45244b98b4b3e83e27f6d801 preceded
READ_POLICY_MODELED_MATRIX_PASS_ROOT_WRITER_ONLY. The root-only receipt journal
remains intentionally; remaining_rules reports failed writes, not kernel enumeration.

The crash phase failed at its initial own-child inventory read:
/proc/self/task/3006813/children ENOENT. Frozen source places this before subreaper
setup, pipes/fork or crash UUID/journal/policy creation. Thus crash functionality
was NOT_RUN; there was no crash child/journal to clean up and no reported uncertain
normal-phase cleanup. Missing proc surface is never treated as empty. Local kernel
39b6687 sources gate it on CONFIG_PROC_CHILDREN and disable it in tizen_emul_defconfig,
but runtime ENOENT is not installed configuration or exact-build provenance proof.
The completed normal phase does not substitute for full crash/recovery acceptance.

Next development-contract proposal replaces live-roster equality with a distinct
closed ordinary-child spawn topology, exact P_PID ownership/nonexit and P_ALL
ECHILD-only expected-empty boundaries. P_ALL None is not absence/live enumeration;
unknown child events/errors and prior uncertainty remain sticky. No fallback or
full rerun is implemented/authorized by that proposal. Already validated context
replies will be printed in a separately reviewed fixture-only revision. All real
TIDL/Cynara, app_fw-owner, product, package/install, PATH/remount and ARM gates stay
open. The exact changed contract/source/method requires independent review first.

### P06-READ-POLICY-WAIT-BOUNDARY-r1 (local source/native ACCEPTED)

Owner w1:pJ; reviewer w1:pA; phase P06; R02/R13/R14 and DB-ACCESS-01.
Development contract design SHA
`114d011c5111aa8dc66896a9f171a7bb8d143e57d0fdcbee9bc88b1e36b1f09e`
was independently ACCEPTED FOR DEVELOPMENT CONTRACT ONLY. This explicitly
revises the proof; waitid does not enumerate all live children and is not a
replacement with equivalent roster semantics. Full-r2 remains overall FAIL and
its normal-phase evidence/cleanup remains separately passing as recorded above.

The own-process setup checks one task and trusted POSIX CPython HAVE_SIGACTION,
requires default SIGCHLD, explicitly resets it and verifies the subreaper.
signal.getsignal alone does not query SA_NOCLDWAIT. Local CPython29231b9
PyOS_setsig sa_flags=0 and kernel39b6687 wait source are source observations, not
exact target-image/interpreter provenance. Native retention tests remain required.
All crash setup and the first empty boundary precede crash journal/pipes/fork.

Only actual errno ECHILD from P_ALL/WEXITED/WNOHANG/WNOWAIT proves an expected
empty ordinary-child boundary. None, a child event, EINTR and unknown errors fail;
no P_ALL result is reaped/signaled. The closed trusted ordinary-SIGCHLD topology,
exclusive waiter, no automatic reaping/ptrace/nonstandard clone/PID namespaces or
escaping descendants are essential premises. Exact P_PID adoption/nonexit remains
separate before/after the contention assertion and makes no sole-live-child claim.
Post-known-reap and final empty barriers plus both known actual reaps and sticky
eligibility precede automatic recovery. Later ECHILD or known cleanup cannot erase
an earlier proof loss. Only validated ROLE_CONTEXT and actual verified known-child
nonexit/reap observations are printed; they do not infer holder identity from flock.

Host child API preflight passed before implementation. Outside-project prototype
r1 had one failing extra-live subcase because a second child inherited the earlier
parent control writer; prototype r2 closed that FD and passed4/4. These are test
harness diagnostics, not product-policy results. The project drivers close earlier
control writers and use bounded isolated children/exclusive positive-PID records.
`read-policy-wait-children-host-r1.log` and final-r2 each pass11/11; the final tests
cover actual setup/live None, retained zombie, extra live/exited children, exit
race and adoption; injected exact flags/errno/setup failures; no generic reap.
`read-policy-wait-probe-host-r1.log` and final-r2 each pass17/17. Orchestrator tests
cover all three empty boundaries, later-empty sticky failures, lost adoption,
setup-before-fork, safe ordinary-error cleanup and validated observation ordering.
Orchestrator/CLI/receipt/rule operations are fake in those tests, not policy runs.
`read-policy-wait-host-check-r1.log` records full CTest8/8 PASS; git diff --check
passes. Native wait tests and changed full fixture runtime remain NOT_RUN pending
separate frozen source/method/protected-path safety review. No role image/C++/ABI,
rule matrix, package/install, operational DB or production admission is changed.


Independent WAIT-BOUNDARY-r1 local source disposition: ACCEPTED for the exact
five-file source/contract scope. Original source-reviewed08 SHA
`eae60ed56a6ef29bb1e2ffe6596eb19612f5ae0bfc8d2ff9f5fe1fc1f111b591`
is preserved. This acceptance does not imply a full policy/crash runtime result.
Native preparation creates a new protected read-policy-wait-boundary-r1 scope:
archive `6251d45bde6f964cd4899629b8b2eac54bac42b0c660335e90bf0e061e7fa65e`,
input manifest `6c5bf82e541bf67dec359586cb5f505b3ec5949166db20d9a45b46760cf2198c`,
source9/9/unchanged core75 and static archives, copied unchanged role image
46849eea...7872 (not recompiled). Native-prepare-r1 and read-only-preflight-r1
both record remote0/transport0. No role runs during preparation/preflight.

Independent NON-POLICY-METHOD-r1 pre-execution disposition: ACCEPTED for exactly
children11+probe17 selected tests only. Method hashes are preflight
`7875da06e394a86831d3a11d90d7d4bf441c0a97c2d2fcdc8e6130ca6a76afb2`
and runner
`a6f350c12c379559642def02a49686ad7c81b7698cd3fa6008d2c3589d17cad7`.
Owner `capmgr-read-policy-wait-boundary-execute-safety-r1.py` validates transferred
method2/2/root0600/singlelink/noACL/no-cap/protected ancestry immediately before
execing the fixed clean -B/inherited-no-bytecode environment. Installed60s watchdog
and owner90s outer bounds are logged. `read-policy-wait-boundary-native-safety-r1.log`
records source9/9/image/interpreter/watchdog/namespace/ext4/no-cache pre/post checks;
all28 tests PASS without skips (0.335s), NON_POLICY_SAFETY_RETURN0,
CAPMGR_REMOTE_EXIT0 and TRANSPORT_EXIT0. The actual isolated own-child setup/live
None/retained zombie/extra-live/extra-zombie/exit-race/adoption and exact-reap tests
passed on the pinned target interpreter. Source assertions enforce retention;
individual driver stdout is captured by the tests rather than independently printed.
This does not prove exact source provenance of interpreter or kernel signal flags.
The orchestrator/CLI/receipt outputs remain fake; no role image/context drop/SQLite/
load2/actual policy-journal CLI was selected. No timeout or retained marker occurred.

This supersedes only the changed native non-policy tests' NOT_RUN status. Final
native/publication disposition is pending; full changed policy/crash invocation
remains NOT_RUN/unapproved and requires its separate exact safety review. Prior
full-r2 overall FAIL/normal scoped pass remains preserved. InstalledRelease15 and
production, real TIDL/Cynara, app_fw-owner/direct-open, package and ARM gates stay
unchanged.


Independent final WAIT-BOUNDARY-r1 local source/non-policy native/publication
verdict: ACCEPTED for the four reviewed code/test files plus accurate08 only.
Accepted final-evidence08 SHA
`67602b7f65550f0a4c71f2c05e6e451e9eaa8ff5433a679fd5e452b2c0e9e253`
is preserved alongside original source08. This supersedes only local/native
publication pending wording; changed full --run remains NOT_RUN/unapproved.
Next: separate exact full-method pre-execution review, then observed full runtime
review. No package, installedRelease15 or production scope changes follow.


### WAIT-BOUNDARY-r1 publication and full fixture runtime (ACCEPTED)

Accepted local source/native checkpoint
`77d52616433e5021bcc934fb3ca2824d6a46bbbc` was signed off, committed0/pushed0,
exact origin/main verified and worktree clean. LICENSE remained unchanged. Only
reviewed four code/test files and accurate08 were published; no package/install.

Independent FULL-METHOD-r1 pre-execution disposition: ACCEPTED for the exact
protected route after native28 non-policy prerequisite acceptance. Frozen method
is preflight7875da06...afb2 and full runner
`7a18a504c028c63f9c7978ff4e1e3605ffe30f38a408e061b1046e3888470626`.
Owner `capmgr-read-policy-wait-boundary-execute-full-r1.py` immediately verifies
transferred method2/2/root0600/singlelink/noACL/no-cap/ancestry, then fixed cleanenv
-B/inherited no-bytecode under installed180s/owner210s. Log
`read-policy-wait-boundary-native-full-r1.log` records exact source9/9/archive
6251d45b...fa65e/input6c5bf82e...2198c/copied image46849eea...7872/interpreter/
watchdog/protected ancestry/initial namespace/ext4/no-cache before and after.
FULL_MODELED_POLICY_RETURN0, CAPMGR_REMOTE_EXIT0 and TRANSPORT_EXIT0; no timeout.
A host log-count audit initially counted the literal method marker inside the
printed invocation too; corrected exact-line matching confirms1 method marker,
2 source checks,4 recovery receipts and2 scope removals without another native run.

The normal new UUID scope
`/opt/usr/capmgr-read-policy-0da8e01705d44af3ad47a80a03111569`
passed the root-writer/UID301 modeled-C matrix. Validated ROLE_CONTEXT now prints
root writer User::Shell and fixed reader UID/GID301, all-zero capabilities/NNP1,
new labels and platform-group presence/absence. C++ validation checks the full
real/effective/saved/group context; compact printed fields are not independent
raw capget/getresuid syscall output. Allowed readers acquired SH and directory/
direct-file RO baselines with MAC write veto. Same-group MAC-denied acquired SH,
then directory/direct-file denial and NULL modeled create/Authorize1/Confirm0/
Finish0/gate-returnfalse/clientSQLiteNOT_RUN. Missing-group denied at the lock;
later stages are NOT_RUN. Positive modeled create records Authorize1/Confirm1/
Finish1 with actual SQLite validation/query, never real TIDL/Cynara authority.
Generation1->2 local queries/no queryIPC, EX BUSY while reader leases remained,
sidecar ownership/mode/label and dev/inode identities through writer close,
fresh RO create/query, final physical closes and EX success all passed. Both
actual protected --recover invocations return cleanupPASS/errors[]/remaining_rules[];
REMOVED_SCOPE precedes READ_POLICY_MODELED_MATRIX_PASS_ROOT_WRITER_ONLY.

The separate coordinator-loss scope
`/opt/usr/capmgr-read-policy-b21d7476623049b2a49b6d7f3ce4fe02`
passed the revised proof. Frozen source enforces prepared default SIGCHLD/verified
subreaper and initial ECHILD before crash journal/fork, checked durable full plan
and flushed recovery argv before first completed load2 write/ACK, and the barrier
before row2. Actual OWNED_CHILD_REAP logs coordinator3014469 status-9. Exact adopted
reference3014470 nonexit is observed and logged before/after the actual protected
--assert-contended CLI, which returns EX_CONTENDED/mutations0; source additionally
requires unchanged receipts/scope. Flock does not identify holder type/liveness;
the owned P_PID observations are separate. Adopted reference actual reap status-9
is logged, then source requires post-reap and final ECHILD plus sticky eligibility
before two real --recover calls, both cleanupPASS/errors[]/remaining_rules[].
REMOVED_SCOPE precedes READ_POLICY_COORDINATOR_LOSS_RECOVERY_PASS. Both phase PASS
markers precede READ_POLICY_FIXTURE_PASS_ROOT_WRITER_MODELED_HANDOFF_ONLY. No
retained/uncertain/traceback marker is present. Root-only receipt journals remain
intentionally; remaining_rules is failed-revocation-write reporting, not independent
kernel rule enumeration. Wait proof still relies on the reviewed closed topology
and trusted retention semantics, never arbitrary descendant/sole-live enumeration.

This new runtime supersedes only revised full-runtime NOT_RUN as observed passing
fixture evidence, pending independent final disposition. Prior full-r2 remains
FAIL and its normal result remains separate. No new C++/role image/rule expansion,
real TIDL/Cynara authorization, app_fw-owned production-directory protection,
operational DB, package/install/root bootstrap, product factory/backend/broker/
mount, PATH or ARM gate closes. InstalledRelease15 is unchanged.


Independent WAIT-BOUNDARY-r1 full runtime/administrative publication disposition:
ACCEPTED for the reviewed root-writer/UID301 modeled-C and coordinator-loss
fixture only. Accepted final-evidence08 SHA
`7f9afdb22ed48fa843e525b6ca1f4a1837f36a33029cd8561df70cf7a2763b1e`
is preserved. This supersedes final runtime review-pending wording, not the prior
full-r2 FAIL or open product gates. Publish only this accurate administrative08;
source77d5261 and installedRelease15 remain unchanged. Next is separately reviewed
real TIDL/Cynara policy preflight, not automatic production selection or grant.


### P06-READ-REAL-POLICY-PREFLIGHT-r1 (source/native build pending)

Full fixture evidence-only administrative checkpoint
`81659c28291dafdf93e7589d3567e51711aef3ff` was committed/pushed0, exact remote
main verified and worktree clean before this next development scope. Installed15
and source77d5261 are separate from this build-only diagnostic.

Independent design6538a1d2c648658a8bc2dc90b9f7e200cf83040f572dc83a07afbbc4bc153da6
is ACCEPTED FOR DEVELOPMENT CONTRACT ONLY. New native-only TIDL fixture measures
fixed UID/GID301 System/User::Shell and UID/GID1 User::Shell candidates with empty
groups, exact own full ID/capability/bounding/ambient/NNP/label checks. It makes no
policy-allow assumption or inference from generic native exceptions. The normal
generated BindChannels/ValidateChannels/real MAIN Cynara code and IDs stay unchanged.
Only a fixed Cancel probe returns unsupported -6; successful gate evidence needs
one exact server body record matching the known positive-spawn client PID/token/
context/counters and the child's actual received -6. No fallback maps a failed
method to a positive result. Other overrides are inert/counted; no job, catalog,
SQLite, grant, lease, rule, new label or policy database operation is made by the
fixture. Connection principal is not per-message task identity.

Only retained MAIN is logged; no callback authority getter was added. The unchanged
generated dispatch validates both sockets and callback extension before the body.
Each client owns a private uniterated creator context before sync connect, listener
outlives proxy and context survives proxy destruction. Context/thread mismatch
fail-stops; teardown errors fail rather than count as authorization denial.
Root parent owns no SQLite/TIDL endpoint before spawn. Standard descriptors are
validated, privately duplicated and explicitly mapped before closefrom3 LAST;
fixed positive child return is immediately attached to a reserved ownership slot.
The new scope retains uncertainty from server spawn until verified normal service
drain/owned exit and own endpoint absence. Failed readiness/drain cannot remove a
scope merely because a destructor later killed the server. Explicit checked scope
cleanup precedes diagnostic completion. Fewer than two positive UID301 contexts
reports an image/availability prerequisite; UID1 denial is not assumed. Empty-group
observations cannot substitute for the later platform-group filesystem matrix.

`read-real-policy-host-check-r1.log` CTest8/8 PASS, with real platform/TIDL disabled
on this host: the new optional image is NOT compiled or run by that result. CMake
adds it to check only with actual transport/Cynara dependencies and no install or
privileged CTest invocation. clang-format and diff --check pass. Compile-only native
preparation and separate exact source/root pre-execution review are next; all new
root context/IPC runtime remains NOT_RUN/unapproved. Existing accepted scopes and
production gates are unchanged.


Independent REAL-POLICY-PREFLIGHT-r1 local source/design disposition: ACCEPTED
for exact three files only, original08 SHA
`b602157e6621a2c7e3bae01399631ec27b991ce1878d26fe1a533d02be71a72e`.
Native compile-only `read-real-policy-native-build-r1.log` is FAIL: source4/4,
accepted core75/generation4, pinned Release15 static archives, actual required
platform flags/Cynara macro and method IDs pass, but GCC14 -Werror rejects the
role-selection reference as potentially dangling. Remote1/transport0; no binary
or context/RPC execution is inferred. Archive
`2950c71f27f9db2b36fb0e391613b71b10fccbdd2c0454cddc5704a05194be00`
is preserved with this failure.

Narrow r2 copies the fixed RoleConfig value instead of retaining that reference;
no guard/policy/protocol change or compiler-warning suppression. New exact archive
and native direct compile/link follow; root invocations remain NOT_RUN/unapproved.
The compile method links pinned accepted generated/platform/adapter/catalog
archives rather than claiming a fresh complete CMake/platform rebuild.


Independent REAL-POLICY-PREFLIGHT-r2 narrow source disposition: ACCEPTED for the
same three files. Accepted r2 evidence08 SHA `bf259445be4f5ba31a462a7f177d973fc96824964979d63af86dde246df2ad45` is preserved.
The copied type is FixedRole (the preceding RoleConfig wording was a naming error),
and the GCC14 warning is not evidence that the old function returned its temporary
argument. Static role table/string lifetime and all guards remain unchanged.

`read-real-policy-native-build-r2.log` compiled/linked the exact r2 source but then
FAILED an outside-project symbol assertion using an incorrect capmgr::platform
namespace; remote1/transport0. No runtime or overall build-verification PASS is
inferred from that attempt. `read-real-policy-native-build-r3.log` checks the same
source4/4, accepted core75/generation4, four pinned accepted Release15 archives,
required actual platform options/CAPMGR_HAVE_CYNARA and IDs0..10, recompiles/links
with GCC14 and verifies actual capmgr::RequirePlatformPrivilege plus Cynara
check/PID/user/client symbols. ldd -r has no missing provider/undefined symbol;
remote0/transport0. This is direct compile/link verification, not a fresh complete
CMake build or native CTest run. Image SHA
`85a4f1de0af9572d7a8fa22ade2f591fa58033e8ec1528eb520289d4ed2a5b64`
is root:root0755/singlelink/noACL/no filecaps under protected read-real-policy-r2.
Native source archive `17f68aa44b1ca9e3a9088662ce1fcf60b683b9e95c7d91d00cb985d68cc805a5`
contains the frozen three files plus unchanged trusted_fixture header.

Read-only `read-real-policy-native-preflight-r2.log` passes exact source4/4/archive/
image/interpreter/watchdog metadata and hash checks, trusted ancestry, initial
namespaces/local ext4/no cache; remote0/transport0. Separate fixed --contexts-only
60s/90s and real-RPC 120s/150s methods are proposed for pre-execution safety review.
Both remain NOT_RUN/unapproved; no policy mutation/product/package claim follows.


Independent r2 native direct compile/link evidence disposition: ACCEPTED as scoped
above. Context-only method-r2 was CHANGES_REQUESTED because its first outer root
interpreter used inherited startup environment before later clean exec. No root
fixture ran. Outside-project outer-method-r3 now starts absolute /usr/bin/env -i
and /usr/bin/python3 -I -B before any verifier imports; existing protected method
hash/metadata checks and clean runner exec remain. Fixed source/image/preflight
bytes are unchanged. Both contexts-only and RPC still require separate safety
verdicts and remain NOT_RUN at this point.


Independent OUTER-METHOD-r3 context-only safety disposition: ACCEPTED for fixed
three-role --contexts-only, installed60s/owner90s only. Actual
`read-real-policy-native-contexts-r2.log` is FAIL: immediate transferred3/3 and
source4/4/image/provenance pre/post checks pass, but initial own task/FD-table guard
rejects at startup. Contexts-return1/remote1/transport0; no timeout, OWNED_SCOPE,
context-drop or RPC marker. Source places rejection before Run/scope creation.
No source-level inference distinguishes extra task versus descriptor yet. RPC
remains NOT_RUN/unapproved; no context repair or guard relaxation is performed.

Narrow fixture r3 adds bounded own scan-name/count/errno and own-FD readlink
failure diagnostics before the unchanged initial proof rejection. It never opens,
duplicates/closes those observed descriptors and does not derive peer credentials.
Existing exact table/single-task guard and all drop/connection/cleanup paths remain
unchanged. New native compile and separate bounded method review are required;
no runtime is implied by these diagnostics or the r2 compile result.
`read-real-policy-host-check-r2.log` existing host CTest8/8 PASS remains non-native
regression evidence, not compilation of the optional image.


Before submission/execution, diagnostic r4 refines the initial r3 draft with
separate own-task/own-FD stages, fixed bounded same-scan name storage, exact expected
versus observed count, scan FD, entry flag, separately captured readdir/closedir
errors and truncation reporting. Only after closing the scan directory, failure
reporting obtains fstat/F_GETFD/F_GETFL and bounded own-FD readlink metadata,
reporting syscall failures/races honestly. It never opens replacement data FDs,
reads FD contents, closes unknown endpoints or widens the exact allowlist.
The first diagnostic r3 draft was compiled only, not submitted/run; r4 is the
frozen complete diagnostic amendment for review. No dynamic-loader/platform
origin is inferred before actual rejected-table evidence is observed.


Frozen diagnostic-r4 original08 SHA `ce5d5bcc0ea5b13b1c73146c6ffe1329379318a39ca185189ad90c864696d2f9` is preserved.
`read-real-policy-native-build-r4.log` is compile-only PASS for the superseded r3
initial diagnostic draft, not runtime or final diagnostic source acceptance.
`read-real-policy-native-build-r5.log` compiles/links exact complete r4 source4/4
against the same checked accepted core75/generation4/four archives with actual
real Cynara flags/IDs; ldd-r clean and policy/socket symbols present, remote0/
transport0. Source archive
`ed80384547656d4420362606cf67bf73ca5b00215c366bcccc053f5241af36e9`,
image `06259d636ba3dce9f691f8e85a0818dd03bbaef8e0efbcd3532e8e31ecde68cc`.
New protected read-real-policy-r4 read-only preflight passes source4/4/archive/
image/root0755/noACL/no caps/protected ancestry/initialns/ext4/interpreter/watchdog/
no-cache, remote0/transport0. Exact method-r4 and outer-method-r4 are frozen for
separate diagnostic-context-only safety review; no rerun has been made. Prior r2
context guard failure is unchanged FAIL; RPC is still NOT_RUN/unapproved.


Independent r4 diagnostic source/direct-build/context-only safety disposition:
ACCEPTED. Actual `read-real-policy-native-contexts-r4.log` remains FAIL:
transferred3/3/source4/4/image/provenance pre/post pass, own task scan succeeds but
own-FD scan rejects observed5 versus expected3 (scan_fd4, entry flag false, both
scan/close errors0, no truncation). Std0/1/2 are present; extra FD3 is nonblocking
CLOEXEC anon_inode:inotify, FD5 writable dlog private deleted FIFO without CLOEXEC.
Contexts-return1/remote1/transport0; no timeout/scope/drop/RPC. This is actual own
metadata, not exact library-init attribution or permission to close/admit extras.
Local dlog initialization source has matching mechanisms, not installed provenance.
A separate minimal pre-drop launcher/delayed native-module loading contract is
proposed before any structural source amendment; no source/runner has changed or
been rerun to bypass the rejected table. RPC and the real filesystem matrix remain
NOT_RUN/unapproved; diagnostic failures do not close authorization/product gates.


### P06-READ-REAL-POLICY delayed-load split-r2 (scoped ACCEPTED)

Independent delayed-load development contract accepted at SHA
`53d38797c83e59b76be0b8b192ce23b7ed092a0bc8db0863e79fa7d12e155406`.
The r4 combined-image startup FAIL remains unchanged. The implementation now
separates a minimal libc/C++ launcher (unchanged OwnedChildren compiled directly)
from a fixed build-only native TIDL/Cynara shared fixture. Neither image is installed
or selected for production; no rules, authority hook or platform logging stub is
added. Initial task/std-FD validation is unchanged. Context-only never opens/loads
the module. Every client label/ID/group/capability/NNP check completes before load;
exact table admits only the one validated owned code FD during that private phase.
Unknown library descriptors are never closed or admitted across credential drop.

Root validates the fixed module ancestry, regular/root:root0755/singlelink and
ACL/cap metadata before handing its O_RDONLY/NOFOLLOW/CLOEXEC reference to the
private code-image owner. The owner rechecks pin identity/flags before and after
the pre-load proof, loads only its own FD magic path with NOW/LOCAL, resolves one
fixed noexcept C entry, closes its own code FD before entry and never dlcloses the
mapping, including symbol/entry failures. The entry contains exceptions and checks
final context as a postcondition; it never changes credentials or forks. Own-FD
metadata/loading is separate from prohibited untrusted-peer proc credentials.
This pins only the top ELF; trusted dependency/loader configuration and stable
root-owned image/no-hostile-root/update premises still require native audit.
Post-drop read/mmap/proc/SMACK failure is setup failure with no fallback or repair.

The generic private test-fixture code owner does not grant path/owner trust: the
root launcher performs those checks before transfer. Separate fixed loader test
images have no native platform dependencies or credentials/policy operations.
Eleven isolated loader tests cover entry-before-close, rejected pre-load context,
missing entry/mapping retention, bad ELF, missing/substituted/non-CLOEXEC/writable/
O_PATH code descriptors, extra alias, missing stdio and post-proof pin recheck.
These are ordinary tests, not actual root context or native RPC evidence. CTest
adds `capmgr-code-image` (nine total names); all new targets remain uninstalled.

`read-real-policy-split-host-configure-r1.log`, host-check-r1 and focused-r3 record
host configuration/full CTest9/9 and final11/11 loader tests. Host-check-r1 predates
the three added negatives; final exact host-check-r2 follows. Minimal launcher
host direct compile-r1/r2/r3 is separate from optional native-module compilation;
no host launcher/drop/RPC invocation was performed. Local WIP extraction scripts
stopped at mismatched markers before correction; these are edit-tool failures,
not native test evidence or accepted source. Native build/audit and all new-image
context/loader/RPC invocations remain pending separate exact review at this point.
InstalledRelease15, accepted modeled-policy matrix and production gates unchanged.


Independent SPLIT-r1 exact nine-file local-source/host disposition: ACCEPTED,
original08 SHA `226399ded877933a2f8193ad7b14a2fbe0a41043fe2b304b9a91e8a14452ce65`
preserved. Final host-check-r2 CTest9/9 and focused-r3 11/11 stand. The empty host
direct-compile logs alone are not a recorded argv/exit proof; no runtime claim is
based on them. The launcher validates metadata/identity, NOT a code digest; exact
bytes rely on external execution preflight and stable trusted-root/no-update
premises. Dependency closure is a separate audited prerequisite, not pinned by
that top-level file descriptor.

`read-real-policy-split-native-build-r1.log` is FAIL before extraction or compilation:
outside-project global marker replacement corrupted a Python print string,
SyntaxError/remote1/transport0. It was not a product compiler finding. Corrected
build-r2 uses the unchanged archive
`216c8d69f592c1433c4c9f3156d844e596e606125f913f60e041117383592a0b`,
source10/10 (nine reviewed files plus unchanged trusted_fixture), core75/generation4
and only the two pinned accepted PIC platform/generated archives. In a new protected
read-real-policy-split-r2 scope, GCC14 freshly compiles the minimal launcher,
native module, two fixed loader images and ordinary loader-test binary. Method IDs
remain0..10; actual real-Cynara flags/PIC/default policy and socket symbols checked.
All ELF images are root:root0755/singlelink/noACL/no caps; ldd-r is clean. Direct
ELFs have no RPATH/RUNPATH. Transitive resolved system dependencies and their
root-managed symlinks/metadata/hash are recorded. Minimal closure contains only
loader/libc/libdl/libstdc++/libgcc/libm/libpthread, no platform objects. Module
closure includes the actual platform dependencies; trusted loader/image/update
premises remain necessary. This is compile/ELF audit only, not native test or full
CMake verification: remote0/transport0.

Direct-build launcher SHA
`899cafdff86e9473e6118abf2b80b239526e51228b88694e66a0856d2e77eac2`,
module `550a1849e0f9e98e261fde232147f4cbfa82809dd780521de2af742608fcd2fb`,
ordinary test `d6fca498eb471344d71a474493672a6779d2b0ba0dbe125fd95e853ae2aa48d4`.
No context, loader test, module entry, RPC or policy execution occurred.

Narrow split-r2 adds the module's nlohmann_json header dependency explicitly to
its CMake target; direct compile already supplied that include, so direct-build
success does not independently validate CMake propagation. Other implementation/
test bytes are unchanged. Actual native CMake target build and separate frozen
root context-only/ordinary-loader-test methods are being prepared. All new runtime
remains NOT_RUN/unapproved; native direct audit does not authorize it. Historical
r2/r4 combined-image startup failures, installedRelease15 and product gates remain.


Independent split-r2 narrow source/direct native audit disposition: ACCEPTED.
Reviewed08 SHA `3d4ba858bbb7d7241d3dbf6182144f321d0daadc8bfe42685034b4fc91b156eb`
is preserved. `read-real-policy-split-native-cmake-build-r1.log` now configures a
fresh exact current171-file source snapshot, full platform optionsON, JSON source
from the trusted accepted Release15 build tree, and freshly builds only the new
minimal/native-module/ordinary-loader targets plus their generated/platform
prerequisites. It is NOT full native CTest/RPM or runtime evidence. Module's actual
compile command proves the explicit JSON header dependency propagated. Native
method IDs0..10, policy/socket/fixed-entry symbols, root0755/singlelink/noACL/no caps,
clean ldd-r, no RPATH/RUNPATH and transitive resolved dependency/hash audit pass;
final source171 unchanged, remote0/transport0. New archive
`1b3f42b83637075393ed87df61c65ebef281d6bff953e576203997497bd9125f`
contains historical accepted r2 administrative08, not this later append.

These CMake artifacts differ from the earlier direct-build artifacts:
launcher `0f7abcb9d41f7df3803ae1773ff753883e13a1c2f4214ae8584c403721dd9e66`,
native module `26f315dab592431cc0a44c9b6f2c9fbec0e718ce287b524c45c2fae107d53c2e`,
ordinary loader tests `56c7bba740d89fb9cfc8ca7c2444da456d6af0230f7fe6bd0dbbcd724ae70b84`.
They reside in protected read-real-policy-split-cmake-r2. No context, loader-test
entry or RPC image has run. Separate frozen methods bind these exact artifacts,
not the prior direct-build digests. Read-only native-preflight-r1 checks current
source171/images5/53 resolved dependencies, archive/manifest/interpreter/watchdog,
protected ancestry/initial namespaces/ext4/no cache and transferred3 methods.
Fixed context-only60s/90s and ordinary11-loader-test60s/90s invocation plans remain
NOT_RUN pending separate pre-execution safety review. Real module loading/RPC and
later filesystem matrix remain separately NOT_RUN/unapproved. No operational
policy/SQLite, package or production behavior changed.


Independent split-r2 native CMake/method-r1 disposition: ACCEPTED for the targeted
build evidence and separate context-only/ordinary-loader runs, not actual native
module/RPC loading. Reviewed08 SHA
`41acee0673fade6600b976f40713f62944f2c8ae06b65f3aac18c15fcda0e794`
is preserved. Actual owner executes the frozen outer `execute-r1.py contexts`
then `execute-r1.py loader`, each FIRST clean/isolated root startup and exact
transferred3 hashes/metadata, installed60s and owner90s; no added workload or
fallback cleanup. All source171/images5/dependency53/interpreter/watchdog/initial
namespace/ext4/no-cache checks pass before and after each independent run.

`read-real-policy-split-native-contexts-r1.log` PASS: all three fixed contexts
(system301/System, shell301/User::Shell, shell1/User::Shell) report validated exact
IDs/empty groups/all-zero caps/bounding/ambient/NNP and labels. Source requires
actual normal exclusive owned child completion. REMOVED_SCOPE=
`/opt/usr/capmgr-real-policy-Aqk9yD` precedes REAL_GATE_CONTEXT_ONLY_PASS;
CONTEXTS_ONLY_RETURN0/remote0/transport0. This minimal module-free route succeeds
without allowing or closing the previously observed platform-library FDs. It does
not load the module or measure post-drop magic-link/mapping permissions.

`read-real-policy-split-native-loader-r1.log` PASS: eleven ordinary isolated loader
experiments, no skips, each owned child outcome required by the test source;
LOADER_ONLY_RETURN0/remote0/transport0. Only tiny dummy modules/bad ELF are selected,
not actual TIDL/Cynara dependencies. Extra-alias/missing-stdio diagnostics are
expected rejected test inputs, not an authentication pass. No own context drop,
SQLite, registration or policy operation is performed by these tests.

Both runs have no timeout/retained-scope marker. Historical r2/r4 combined-image
startup FAIL remains preserved; this does not retroactively turn those runs PASS.
Final scoped runtime/publication review follows these logs. Actual delayed native
module/RPC invocation and later causal filesystem matrix remain NOT_RUN/unapproved;
installedRelease15 and all production/package/task-proof gates remain unchanged.


Independent split-r2 final scoped source/context/dummy-loader runtime/publication
verdict: ACCEPTED. Reviewed08 SHA
`17156c1ee97b9f8a84f716ab975f3ad0a6645b9d2ea16822e272b7f1debe7b32`
is preserved before this disposition/heading bookkeeping. Publish only the eight
reviewed source/build/test entries plus administrative08. These runtime outcomes
close the minimal context and ordinary loader gates; actual native module/RPC
remains NOT_RUN, with a separate fixed method submitted for review. No code,
image, policy or runtime method was changed by this administrative disposition.
InstalledRelease15 and all full-product/external prerequisites remain separate.


Accepted split source/context/dummy-loader checkpoint published as
`4b549f0270d4d8e787ee3ae34b116edb463fe071`: nine reviewed files including accurate
administrative disposition/Next, signoff/message line checks, commit0/push0 and
exact origin/main verified, tree clean before this evidence append. Source
implementation and accepted image bytes remain unchanged; installedRelease15
remains separate. No real RPC success was claimed by that publication.

Independent split-r2 RPC-method-r1 pre-execution disposition: ACCEPTED only for
fixed actual-module diagnostic, installed120s/owner150s. Actual owner invocation
is frozen `execute-rpc-r1.py rpc`, FIRST env-i/absolute Python-I-B transferred2/2
and method metadata before fixed runner exec; all source171/images5/dependency53/
interpreter/watchdog/protected ancestry/initialns/ext4/no-cache checks pass before
and after. `read-real-policy-split-native-rpc-rpc-r1.log` records actual result:
REAL_RPC_DIAGNOSTIC_RETURN0/remote0/transport0, no timeout. This completes the
bounded diagnostic, not product authorization or the required two-positive gate.

Three lower-context module/client cases execute. System UID301 (empty groups,
zero capabilities/bounding/ambient, NNP1) has one actual server Cancel body with
raw UID/GID301, socket labelSystem, PID3038149, fixed matching token, cancel_calls1
and other_calls0; actual client reply-6/exit10 correlates to the parent's owned
client PID. Its server reports cancel1/rejected_bind0. This is default real MAIN
Cynara/generated-channel permission evidence for that exact connected context,
not trusted unit identity, task/delegation authority or production activation.

User::Shell UID301 and UID1 contexts report validated final tuples and each a
native InvalidProtocolException at client-method-reply with
specific_Cynara_decision=NOT_OBSERVED. Their servers report cancel0/rejected_bind1,
no body/reply-positive record. These are availability NOT_PROVED, not separately
identified Cynara ACCESS_DENIED and not assumed UID1 policy behavior. The source
requires normal owned child completion, service drain and endpoint absence for
all three cases. Three REAL_GATE_SERVER_DRAINED observations precede checked
REMOVED_SCOPE=`/opt/usr/capmgr-real-policy-mWoOEQ`, then diagnostic completion.
Actual candidate_positives1 and NEXT_MATRIX_BLOCKED image/availability are printed;
no scope/endpoint/child uncertainty marker is present. Existing policy was untouched.

The diagnostic establishes actual delayed platform loading/RPC for these measured
cases under final client credentials without carrying root library endpoints
through Drop. It does not provide a second authorized UID301 label or platform-
group authorization, causal real-file/direct-open proof, app_fw-owned writer
protection or policy provisioning. The original two-subject real matrix remains
BLOCKED pending a reviewed alternate context/object design or image prerequisite.
No rule broadening/role repair or silent replacement of that contract is performed.
Final RPC-runtime/admin review is requested; all product/package/ARM gates remain.


Independent actual RPC diagnostic/administrative08 final verdict: ACCEPTED for
this exact narrow measured scope; accepted08 SHA
`da7008f2d13d8336cc7bbdc905b7b896d9ea8e0751c0f856e9c69c940c0697c4`
is preserved before disposition/Next bookkeeping. Diagnostic completion with one
real System UID301 positive does not close the original two-subject filesystem/
direct-read subset: it remains BLOCKED. The recommended same authorized subject
versus different fresh object-label slice is a separate development direction,
not source/policy/runtime acceptance. Actual catalog Authorize/Confirm plus local
access veto must be measured anew; Cancel's permission evidence alone is not that
protocol. Different subject and app_fw-writer policy remain later prerequisites.
Earlier loader-test summary phrase "entry-before-close" was a wording error:
reviewed source/tests require owned code-FD close BEFORE native entry. No code or
method changes are made by this correction. Publish this accurate administrative
result separately; existing source4b549f0 and installedRelease15 remain unchanged.

### P04 worker catalog lease r1 local source/host checkpoint (review pending)

Owner w1:pJ; reviewer w1:pA; R02/R10/R13/R14. Baseline administrative RPC evidence
`7894900dee2d49100d39291296f71415201aca57` was committed/pushed0 with exact
origin/main verification and clean tree before this work. It records the accepted
one-positive real diagnostic only; installedRelease15 is unchanged. Original
two-subject read-policy/direct-open subset remains BLOCKED. The independent
worker lease design SHA
`31cf85c5c8cdb5f7b02cec74e2c049ca6c839aa131dacad619df314952340ae1`
was accepted FOR DEVELOPMENT CONTRACT ONLY. Reviewer confirmed that a distinct
private connection owner, instead of broadening legacy Database, fits the contract.

The new WorkerCatalogReader encapsulates concrete CatalogReadLease plus its RO
SQLite connection and all queries/transaction. It checks borrowed directory
without duplicating invalid data FDs, acquires an independent SH before SQL,
requires actual READONLY/WAL/canonical path and same pinned generation, captures
schema/revision/published bounded registry in one RO transaction and transfers the
same lease only after actual sqlite3_close OK. Explicit close errors retain both
owners and no result; lexical/constructor cleanup physically closes first.
LeasedWorkerCatalogSnapshot is move-only/nothrow without move assignment. Creator
checks cover SQL entry/finalization/rollback/close and inherited reader destruction
fails before SQL. Closed snapshot fork references close only. General legacy
loader/Database/Statement, worker/bootstrap and production factory are unchanged.

Source scope is src/CMakeLists, catalog/read_lease hh/cc, new launcher/
worker_catalog_lease hh/cc, test/CMakeLists, test/fixtures/sqlite_lock_probe.cc,
new unit/worker_catalog_lease_test.cc,07 contract and this08. MatchDirectory is
private to the new reader; no arbitrary channel ReadAccess/FD authority getter.
The existing tests-only independent-exec SQLite probe gains a separate generation
EX observation mode. New prepare/step/open/close interposition belongs ONLY to
the adapter test executable and resets each case; no product failure accessor.

Host build-ofd uses accepted prefix /tmp/capmgr-host-deps/root/usr, Release
-O1/-DNDEBUG, platform transport/Cynara/TIDL OFF, SQLite3.45.1. Initial focused-r1
ran17:13PASS/4FAIL because the test counter also counted legacy sqlite3_close(NULL)
after fixture provisioning, not a violated physical-close invariant; preserve that
FAIL log. Counter now observes non-null connections only. Subsequent focused-r2
19/19, r3/r4 23/23 passed while coverage grew; they are not final-manifest evidence.
Final host-build/check-r2 CTest9/9 and host-focused-r5 24/24 PASS on current bytes.
Evidence files are /tmp/capmgr-evidence/worker-catalog-lease-host-*.log, with exact
commands embodied in the build/check and focused test invocations. Tests use unique
owned /tmp fixtures and injected labels, not real policy authorization. OwnedChildren
reserves before fork/spawn, pins returned positive PID before logging, requires
actual exit/reap and retains/fail-stops before scope deletion on unconfirmed cleanup.

The24 cases cover independent EX busy before/during/after physical close until
snapshot's final reference ends; real escaped-statement BUSY/finalize/retry;
synthetic close IO retry without reopen; constructor and post-open/allocation faults;
post-close allocation refusal; BUSY destructor/constructor invariant fail-stop;
invalid borrowed DB/SHM and directory/O_PATH; missing sidecar/schema/content refusal;
published/pending/other-kind and256/257 differential comparisons to legacy loader;
in-place commit with explicitly unchanged old registry/revision; inherited SQL-owner
rejection versus closed-snapshot close-only/last-reference behavior; and independent
exec WAL write/exclusive/read-snapshot checkpoint exclusion through loader success
and failure. O_PATH metadata is not a read-permission proof. Ordinary rollback IO
failure is retained as failed load, not an automatic invariant fail-stop.

Exact native current-source build/tests are pending, NOT_RUN; native preparation
will use a new protected tree and pinned source/dependencies. No root/bootstrap
run, five-FD Finish wiring, NamespaceInit/CLI nondelegation, new rules, real TIDL,
RPM/install, live invalidation or production/current-job authority is claimed.
Source/07/host review requested before any publication; original accepted hashes
will be retained before heading-only bookkeeping. Full product/P09 gates stay open.


### P04 worker catalog lease r2 JSON validation correction (review pending)

Independent r1 review returned CHANGES_REQUESTED for detail validation parity:
legacy GetPrivate parses stored detail and requires an object, while r1 only
checked its TEXT type. This is a source/content finding; host24/24 did not close
it. R2 parses those exact selected bytes as object JSON inside the same RO
transaction before registry construction; malformed/non-object/invalid UTF-8
returns no snapshot on the failed-load path. No legacy Database escape or lifetime
change. The new differential test covers not-json, array, null, invalid UTF-8 and
a valid nested object, comparing the legacy loader. For each rejection independent
EX remains BUSY through physical Close until reader retirement, then succeeds.

Frozen r2 host check-r3 CTest9/9 and focused-r6 25/25 PASS, SQLite3.45.1/injected
labels. Prior r1 logs and10-file manifest are preserved separately, not relabelled.
Native build-r1 reached exact source174 but rejected reused JSON dependency's
root:root775 source ancestry BEFORE configure/compile, remote1/transport0. It is
FAIL, not product/compiler evidence. No older JSON tree was altered. Compile-only
retry uses a fresh protected subset of the accepted JSON3.11.3 archive
SHA0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406,
including CMake/templates/headers, individually hash-verified and extracted0600
under700 directories. Current native-build-r2 still targets frozen SOURCE-r1
archive5e1b80346b96bf2c1184007026e39662b85e90c1d5b147f4182918578ec1ba0a
with no unit execution; it cannot substitute for corrected r2 build/tests.

New r2 source/07 and actual exact native outcome still await review/evidence;
root/bootstrap/policy/package remain NOT_RUN for this change. Typed five-FD report,
worker lifetime/nondelegation, live invalidation/executable/job-absence and full
production gates stay open. Parallel same-subject/object proposal is only a draft,
including separate fixed supplementary-group authorization measurement and a
separately reviewed EIGHT-rule recovery variant; no mutation authorization follows.

### P04 worker catalog lease r2 exact ordinary native evidence (ACCEPTED)

Independent local source/07 review ACCEPTED r2 at manifest
c2c8273d0d309fa20d85f4a73f095d9adc57665b160da72e8fc3e66e4a5c293e.
The malformed/non-object/invalid-UTF8 detail finding is CLOSED. Original accepted
07 SHA124ef413701fd0f65e2ed7e467026706d3f2cd7d06c15a199d070e1a8f3e486b
and original frozen08 SHAdf71ad0091a6fdb734bf0c8f1afd796aeac67b77e27c6e719a843b0e1ca33acc
are preserved before subsequent administrative bookkeeping. The other nine entries
remain byte-identical to the accepted r2 source manifest.

Native build-r2, still using historical SOURCE-r1, configured with the fresh
protected JSON subset but exceeded its300s compile watchdog, remote124/transport0.
Preserve this FAIL; a watchdog outcome is not child-absence or test proof. Before
exclusive cache reuse, build-r3 refused active cwd/exe references into the owned
scope and checked the old source/dependency hashes, then updated only the four
changed r2 source/test/document entries. It rebuilt affected objects and API/test
targets incrementally with -j1, not a fresh whole build. Source174 and JSON53 hashes,
clean ldd-r on the four initial artifacts and final hashes passed; remote0/transport0.
Corrected r2 archive SHA:
40d1ca98c109a8ea33f5ef4db9a8fcda8520df5a90150952e35b25765b6c86ee.
The exclusive protected tree is
/opt/usr/capmgr-bootstrap-build-p4h4og7o/worker-catalog-lease-r2.
Native SQLite3.50.2/GCC14.2/platform options ON are distinct from host3.45.1.

Method-r2 pre-execution review found missing pins for the executed CLI fixture
and C consumer's loaded libcapmgr. No tests ran under that revision. Method-r3
adds both exact digests BEFORE and after execution, protected root/singlelink/
noACL/no-cap metadata, exact root0700 leaves observed from owned umask077,
root-owned fixed SONAME symlink targets and the C consumer's sole trusted build
RUNPATH/actual resolved library. No chmod or permission fallback was needed.
Read-only code-audit-r3 rejected an overstrict0755 expectation with a traceback;
transport0/no remote success marker is not PASS. Corrected audit-r4 and
preflight-r3 each passed remote0/transport0. Standard installed system dependency
and trusted-root/no-concurrent-update premises remain. Fixed method SHA:
dfd6ab6a423454805a8b9240816109b07d5a68a40a9941bbb7ce0f7119742d3d;
outer executor SHA:
1da58a50588c1a43c20e06dd663b981f66a8c1057ba168ee2850a8d25d48b744.
Independent method-r3 bounded ordinary-execution disposition was ACCEPTED.

Owner executed /tmp/capmgr-worker-catalog-lease-execute-native-r3.py, whose log
prints the exact FIRST /usr/bin/env -i plus absolute Python-I-B verifier command.
It verifies transferred method hash/root0600/noACL/no-cap before immediate
preflight, then invokes installed hash-checked run_bounded.py --seconds180 with
the fixed ordinary runner, under owner210s. Native-tests-r3 records source174/
JSON53/images6/protected ancestry/initial namespaces/ext4/interpreter/watchdog/
no-cache and C-library resolution checks before and after. No timeout occurred.
CTest --show-only selection is asserted to exactly unit/adapters/C-consumer3;
--verbose/--no-tests=error records3/3 PASS, actual unit123/123 and adapters187/187
(310 GoogleTests), plus C consumer. Focused WorkerCatalogLease25/25 and existing
CoordinatedWriter24/24 pass separately; these repeat subsets, not49 additional
unique tests. CAPMGR_REQUIRE_METADATA_TESTS=1 observes real User::Shell metadata
and honestly reports user xattrs UNSUPPORTED. Direct C_CONSUMER_EXIT=0, exactly12
versioned CAPMGR_0 exports, ORDINARY_NATIVE_ONLY_PASS, remote0/transport0.

Evidence is /tmp/capmgr-evidence/worker-catalog-lease-native-{build-r1,build-r2,
build-r3,code-audit-r3,code-audit-r4,preflight-r2,preflight-r3,tests-r3}.log;
host-check-r3 CTest9/9 and focused-r6 25/25 stand separately. Scratch SQL tests
inject Fixture labels; the existing real metadata regression does not turn this
loader checkpoint into a real worker catalog authorization test. OwnedChildren
cleanup assertions remain required; no watchdog/transport exit supplies absence.
Native selected3 is not a full native CTest9, RPM or installed result.

Final exact native/administrative publication verdict is pending. No bootstrap
five-FD wiring, NamespaceInit workload/nondelegation, live invalidation/current-job
authority, operational DB, new rules, installed package or production activation
follows. InstalledRelease15 is unchanged. Separately accepted Stage A development
contract now has disjoint WIP fixed supplementary-group diagnostic sources, which
are excluded from this worker lease manifest/publication and from the native
archive. Stage B recovery-reference FD4 and eight-rule matrix remain unimplemented
and separately gated; explicit close3 is required in its future ordered mapping.

Independent final r2 ordinary-native/publication disposition: ACCEPTED for these
ten private source/test/contract/administrative paths only. The accepted08 hash
before this disposition and heading/Next bookkeeping was
381ce549d0bf531a08f2c7355bbc331735df643f046d116db535753108ef784c.
The original07 hash above is retained; only its pending-review heading changes.
This paragraph supersedes historical pending statements, preserving their evidence.
Stage A disjoint implementation files are excluded from this publication. No
overall completion or new production/bootstrap/policy/package gate is inferred.
Actual commit, push and remote equality will be reported after publication.

### Worker catalog lease publication and fixed platform-group source checkpoint

Reviewed worker lease r2 ten-file checkpoint published as
6b0aa46bb6a517693f400f8d3f75c292e2c96741. Commit/message/signoff checks passed,
normal push exited0 and origin/main ls-remote equalled that exact SHA. LICENSE
is unchanged. The disjoint platform-group fixture WIP was excluded. Installed
Release15 and bootstrap/invalidation/executable/current-job gates remain open.

P06-READ-PLATFORM-GROUP-r1 implements only Stage A of accepted development design
same-subject-object-r2 SHA:
eb7e9f72ddbc1a4797f968024ca4ab4397a387c480854d0afa98a4c87dced1ec.
Source scope: test/CMakeLists, fixtures/read_policy_context.hh, integration/
read_policy_tidl_probe.cc and read_policy_tidl_native.cc, new ordinary unit/
read_policy_context_test.cc,07 clause and this08. The fixed new System301 role has
only supplementary GID10212 after own drop. Name/GID mapping is read-only and
checked in the root coordinator before scope/spawn; post-lookup own-table guard
rejects any NSS-created task/endpoint rather than closing or admitting it. Native
entry verifies the same tuple before registration. No operational policy, group
database mutation, arbitrary label/group or new production authority is added.

Separate fixed context-only and RPC top-level modes preserve the existing three
empty-group paths and reject cross-route role names. New tuple availability uses
distinct result markers and never increments the original two-subject count.
Actual generated/default MAIN Cynara, callback validation, body/reply correlation,
code-FD close before entry and proxy/listener/context lifetime are retained. No
journal reference, catalog grant, SQLite or Stage B policy matrix is implemented.
The original different-subject/direct-read subset remains BLOCKED.

Host focused-r1 directly compiled and ran four ordinary fixed-role/group negatives
and syntax-checked the minimal launcher under C++20/-Wall/-Wextra/-Werror. These
tests check cross-route rejection, exact missing/wrong/extra/duplicate groups and
refusal of an unavailable/mismatched image group; they do not Drop or invoke NSS.
After adding the post-lookup guard, focused-r2 records the actual CMake test target
four cases and the current launcher syntax. Host check-r1 CTest10/10 adds only the
ordinary capmgr-read-policy-context target; existing nine names remain. Host
optional native launcher/module is not built by this host configuration, and
syntax-only is not module linking or runtime evidence.

Source/07 review, exact native build/dependency audit and ordinary native4-test
evidence are pending. Both new root context-only and actual RPC routes are
NOT_RUN/unapproved until separately frozen binary/method reviews. No real-positive
group tuple, permission repair, policy mutation, installed package or production
acceptance is inferred. Stage B source remains unimplemented and needs separate
non-policy surviving-reader/server lifetime plus full recovery/policy review.

### P06 platform-group r1 native preparation and context evidence

Independent exact seven-file local source/07 disposition: ACCEPTED, baseline
6b0aa46, manifest faec51b412b06b7b50e2fec3e47271e8d1095dbae4df41605d0e540254e2325b.
Original accepted07 SHA4029a1c74228a6ccfd8a87c98dcdfa50ed733c49b6d4379078ef160c8afed29a
and original08 SHA0a1ff8d2cc0984f7e4227b792c9f265ef47ff1a2a0c5927d6fad7dbebfc5370a
are preserved. Later pending statements above are chronological, not a claim that
source acceptance itself established native success or authorized RPC execution.

Owner native cache inspection found9MiB free. Only prior accepted Release15
rpmbuild-release15/BUILD was removed after trusted ancestry/initial namespaces/
ACL, pinned identity and no active cwd/exe checks with symlink-safe removal.
Two source archives/four binary RPMs/SRPM hashes match before and after; sources,
RPMs, logs, installedRelease15 and the old0777 tree are preserved. Space-r1 log
records418544KiB available, remote0/transport0; this is environment evidence only.

Exact current175 regular-file source archive SHA:
12c092d2bcf8cc3bfc4ba674dc71f2066e888a3b06a712904fc5e4b8af6181fa.
Fresh trusted JSON subset53 retains SHA206435af4b644e722508c0c95f63a188576c41f2d1f99623ebcc2c79a81fb108.
Native-build-r1 fails SDB command length before remote execution, transport1;
no compilation success is inferred. Short-transfer-map build-r2 checks inputs and
freshly compiles the native CMake target closure, then fails its outside minimal
closure classifier because standard libdl.so.2 was omitted. Preserve remote1/
transport0 as FAIL. Build-r3 corrects only that outside classifier and rechecks/
rebuilds the same targets, including generated proxy/stub and module compilation;
it is neither a second fresh whole build nor a source amendment. Target subset
build/audit PASS remote0/transport0, generated IDs0..9/Confirm10, real default
Cynara/native entry symbols, clean ldd-r, no RPATH/RUNPATH, standard runtime/dl-only
launcher startup closure and pinned53 resolved dependencies. No native CTest,
helper/context/RPC or package result follows from those build logs alone.

Final CMake artifacts at protected read-platform-group-r1/build are root:root0755,
singlelink/noACL/no filecaps: launcher
4eb5f412aa0e10fbb054a1d1b5b196f55ddcc5b5aabf558d3806736219d0af87;
module eb4dbd3e13613343b6b7d54a35220ebb02d315cf6d8b8ee6de004e21b786ce0a;
ordinary helper c60430bf244f7eb849d390ac6277861235217064a5825430358fb37a9997b24c.
Readonly preflight-r1 passed source175/JSON53/images3/dependencies53/interpreter/
watchdog/protected ancestry/initial namespaces/ext4/no-cache, remote0/transport0.
Trusted /etc/group and NSS configuration hashes are logged inputs, not lookup,
Drop or authorization evidence. No image configuration/group database was changed.

Independent METHOD-r1 pre-execution disposition: ACCEPTED for helper4 first,
context-only only after success. Owner invoked the frozen execute-r1.py helpers
and then contexts; each log prints the exact FIRST env-i absolute Python-I-B
verifier, transferred method3 root0600/hash/noACL/no-cap, clean inherited
no-bytecode environment and immediate/pre/post preflight. Installed watchdog30s/
owner60s for helpers,60s/90s for contexts. Native-helpers-r1 actual4/4 PASS and
PLATFORM_GROUP_HELPERS_ONLY_RETURN0, remote0/transport0. These are pure helper
assertions; they do not invoke NSS, Drop, module, registration or SQLite.

Native-contexts-r1 logs the exact verified System UID/GID301, groups[10212],
allcaps/bounding/ambient0, NNP1 labelSystem. Frozen source requires the preceding
getgrnam_r mapping/post-NSS task/FD check and actual owned normal child completion;
those are source-enforced conditions, not separately printed syscall replies.
REMOVED_SCOPE=/opt/usr/capmgr-real-policy-Hg0IhO precedes dedicated
REAL_GATE_PLATFORM_GROUP_CONTEXT_ONLY_PASS, context return0/remote0/transport0.
This route never opens/loads the native module. No timeout or retained marker is
recorded. Native helper/context runtime/publication review remains pending.

Separate RPC-method-r1 now freezes common preflight plus fixed RPC runner and
FIRST cleanenv outer executor under120s/150s. Readonly native-rpc-preflight-r1
passes transferred2/provenance/immediate preflight0/transport0 only. Actual
--platform-group-rpc remains NOT_RUN until separate pre-execution acceptance.
No context success implies its authorization result. Original different-subject/
direct-read subset remains BLOCKED; catalog/Authorize/Confirm, SQLite, recovery
reference, load2, Stage B, production and package gates remain open. Existing
installedRelease15 is unchanged. Raw evidence is outside Git under
/tmp/capmgr-evidence/read-platform-group-*.log.

### P06 platform-group r1 actual RPC diagnostic (scoped ACCEPTED)

Independent helper/context runtime disposition: ACCEPTED for the narrow context
prerequisite. Separate RPC-METHOD-r1 pre-execution safety disposition: ACCEPTED
for fixed --platform-group-rpc only. Owner invoked execute-rpc-r1.py with installed
120s watchdog/owner150s; native-rpc-r1 prints the exact FIRST env-i absolute
Python-I-B command, transferred2 root0600/hash/noACL/no-cap checks and immediate/
pre/post source175/JSON53/images3/dependencies53/interpreter/watchdog/NSS metadata/
initial namespaces/ext4/no-cache validation. No source/image update or context,
label, policy, NSS or permission repair occurred.

Actual verified System UID/GID301/groups[10212]/zero caps/bounding/ambient/NNP1
client PID3064944 matches the server body rawUID/GID301/socket_labelSystem/token
system301-platform:3064944, cancel_calls1/other_calls0. Client logs actual result-6;
frozen source requires exit10 and owned PID/body equality before counting the
positive. REAL_GATE_SERVER_DRAINED cancel1/rejected_bind0 and correlated positive
are followed by REMOVED_SCOPE=/opt/usr/capmgr-real-policy-3HgHvk before dedicated
platform-group diagnostic completion positives1. Source also requires actual
normal known client/server exits and own endpoint absence; those are enforced
conditions rather than separately printed wait/syscall replies. Diagnostic return0,
CAPMGR_REMOTE_EXIT0 and TRANSPORT_EXIT0; no timeout/retained marker recorded.

This is the measured default/generated real MAIN+callback Cancel availability
route for the fixed new tuple; no AuthorizeCatalog/ConfirmCatalog grant, actual
catalog C create, direct file/SQLite permission matrix or production authority
was tested. The tuple is available only as a prerequisite for a separately
reviewed same-subject/fresh-object increment. The original different-subject/
direct-read-subset gate is explicitly still BLOCKED. Stage B recovery FD4,
eight-rule scenario/recovery, reader/server surviving-reference tests, production
policy/app_fw-owned directory protection, task authority, package/install and
ARM remain separate. InstalledRelease15 is unchanged.

Exact code and07 remain frozen against the original r1 manifest; only08 has this
chronological administrative evidence appended. Final scoped runtime/08 publication
review is pending. Preserve original07/08 hashes recorded above and all failed
build/transport/audit attempts. No overall completion signal follows.

Independent final P06-READ-PLATFORM-GROUP-r1 Stage A source/runtime/administrative
publication disposition: ACCEPTED for these seven paths only. Accepted08 before
this disposition and heading/Next bookkeeping:
23e58e991c0d340133148760c75a063624d208e1758a2b29101b196d8aeed337.
Original07/08 hashes above remain preserved; only the new07 heading changes.
This final disposition supersedes historical pending/NOT_RUN statements for
Stage A. The measured new-tuple prerequisite does not close the original
DIFFERENT-subject gate or any catalog/object-policy/production gate. Next work
is separately reviewed inherited recovery-reference lifetime and actual catalog
Authorize/Confirm/object-access integration; the private worker bootstrap/lease
owner design is an independent development review. Publish only reviewed7;
commit/push/exact remote verification will be reported after publication.

### Platform-group publication and recovery-reference local primitive

Reviewed Stage A seven-file checkpoint published as
402b21b4dd8e5194de4b3aad9baa563c18a5d017, signed/co-authored English message checked
before/after, commit0/push0 and exact ls-remote main equality. Worktree was clean
and LICENSE unchanged before this next slice. InstalledRelease15 is unchanged.

P06-READ-RECOVERY-REFERENCE-r1 implements only the first metadata/lifetime helper
in accepted same-subject-object-r2 development contract. Scope is new fixture
header read_policy_recovery_reference.hh, new unit test, test/CMakeLists and07/08.
The fixed borrowed FD4 is not closed, duplicated, read or unlocked by the helper,
including constructor rejection/destruction. Only trusted expected root0600 lock
identity/access and CLOEXEC transition are checked, including no ACL; inaccessible
metadata rejects. No role/spawn/pre-load table or actual module is wired yet.

Host focused-r1/r2 each records four pure metadata tests PASS and two explicit
standalone root-only tests SKIPPED. The r2 option requires exact environment1;
no root experiment was executed on the host. These skips do not establish real
root flock retention, surviving reader/server lifetime, context/label access,
actual journal CLI or kernel-exit reference release. Full host check and source
review are pending. Native build/tests and any root experiments are NOT_RUN and
unapproved pending their exact source/method reviews. No policy/SQLite/role/module/
RPC/load2/installed/production effect follows from this local helper.

Owner pre-execution review found that r1's opt-in positive test unlinked the lock
before validating it, changing nlink1 to0 and correctly causing the product
metadata guard to reject. No root test ran and no product guard is relaxed. Test
r2 defers unlink until after validation/destruction, TEST-only close4 and actual
independent EX acquisition; only the test and chronological08 change. Preserve
r1 focused skips and its original frozen manifest as historical scope, not native
PASS. Re-run current focused/full host evidence before final source freeze.

Independent r1 source review CHANGES_REQUESTED confirms the same early-unlink
finding. Correction also verifies actual held metadata/access/flags against the
positive baseline before corrupting only expected inode, covers held nlink0 in
pure tests, and checks named dev/ino/root0600/single-link before unlink afterward.
Owned/removed/retained reference-file markers make test cleanup explicit; no
watchdog or killed-child outcome supplies test PASS. MarkCloexec documentation
now says a failed postcheck can leave CLOEXEC set but never releases/unlocks the
reference; startup fails with no promised flag rollback/non-CLOEXEC retry.
Root experiments remain NOT_RUN. Current frozen r2 evidence will follow.

Frozen r2 host evidence: focused-r3 four PASS/two honest root skips; full final
host-check-r4 CTest11/11 PASS. Earlier check-r1/r2/r3 and focused-r1/r2 remain
historical iterative evidence. Final check-r4 runs after the nlink and MarkCloexec
wording/test corrections; no concurrent source edit occurs during that run.
No native compilation/default/root result is inferred. Source re-review pending.

### P06 recovery-reference r2 exact native standalone evidence

Independent r2 local source/contract disposition: ACCEPTED, manifest
e4099e999101667f87ce54f5567a42c028396831e20f2c0df65efcf7eae11d30.
Original07 SHAdd22f65ce97116412ba9d790e7044cd6dc4f78b3daa701d6cf2f977de1af91fb
and original08 SHA704934b1c75d638ba4278c7d12c079f4dda9ea9d3794182ac53844f0b7732495
are preserved before this administrative evidence. Five-member source archive
SHA584e83d1c68beb8f3a256415f9ebbb37c89b68d288f527368f4627dab7e5da9e.

Native-build-r2 newly directly compiles the exact standalone test/header with
GCC14 C++20/-O1/-Wall/-Wextra/-Werror against actual installed GTest; source5 before
and after, clean ldd-r/no RPATH/RUNPATH, trusted standard/GTest dependency8,0/0.
This is direct target compile/link, not native CMake/fullcheck/RPM. Protected
read-recovery-reference-r2/build test image root:root0755/singlelink/noACL/no caps
SHA35edf12e594b7d9bfbaf78d2ac6650391a2feb1ba62fef439113acbea594d56d.

Outside method-r2 had a local log-path collision: the read-only preparation was
misnamed read-platform-group-native-tests-r1.log, and execute-tests-r2 would open
that already existing log with x and fail before test execution. Preserve that
misnamed log/outer-r2 and independent CHANGES_REQUESTED; neither is native test
PASS. Outer-r3 fixes only the output path to fresh read-recovery-reference-native-
tests-r3.log. Fresh read-only native-preflight-r3 passes0/0. Independent narrow
method-r2/outer-r3 pre-execution disposition: ACCEPTED. Fixed preflight SHA
eea1f38fe28ff1fea7c696c7fc25fea31785faacb0eaf7863df21e3dfb60282a;
runner a273d769b1edf4b0bdec678a9d294d19f6c8ae48cc71d6c84a23b5d180063485;
outer f011f3c52f1b01a0f9f5b8cffe03cca60a788de3b512fc541e203ac0ed46f37b.

Owner invoked execute-tests-r3.py only after that acceptance. Native-tests-r3 logs
FIRST env-i absolute Python-I-B and installed30s/owner60s; transferred2/root0600/
noACL/no-cap/digests, checked source-only preflight (no cached bytecode loader),
exact source5/archive/image1/dependency8/interpreter/watchdog/protected ancestry/
initial namespaces/ext4/no-cache before and after. Fixed opt-in environment1 and
only six reviewed compiled cases. Actual6/6 PASS with no skips in5ms; four pure
metadata cases plus two isolated direct-child tests. Frozen source requires actual
normal wait/reap0 for each child, not a kill/timeout result.

Actual owned and checked removed temporary references are
/tmp/capmgr-recovery-reference-yKJcIw and /tmp/capmgr-recovery-reference-H82US0.
No retained marker or timeout is recorded; NATIVE_REFERENCE_TEST_RETURN0,
CAPMGR_REMOTE_EXIT0 and TRANSPORT_EXIT0. The source-enforced assertions establish
independent-description EX exclusion after helper destruction or failed inode
validation, then TEST-only close4 and EX success before named-identity cleanup.
Those assertions are not separately printed raw flock/wait syscall replies.

This proves helper nonclose/unlock and the explicit TEST-only release boundary,
not inherited fixed-role mapping, post-Drop ACL availability, live reader/server
adoption/coordinator-loss recovery, library/TLS/kernel-exit reference retention,
actual durable journal CLI, SQLite/catalog policy, Stage B eight-rule integration
or production authority. No role/Drop/module/RPC/load2/group/policy mutation/RPM
or installedRelease15 change occurred. Host final CTest11/11 and focused4/two skips
remain separate evidence. Final scoped runtime/admin publication review pending;
earlier source/method errors and original acceptance hashes are retained.

Independent final P06-READ-RECOVERY-REFERENCE-r2 standalone source/native/admin
publication disposition: ACCEPTED for these five paths only. Accepted08 before
this disposition/heading bookkeeping:
43a6512e666076da5a4bf6432a5e1070c143be5aee9449c7aca475a4ac2d13bc.
Original07/08 hashes above remain preserved; only the new07 heading changes.
This supersedes historical pending statements for the metadata helper, not any
actual surviving-role or policy gate. Next increment must wire the exact stable
stdio0..2/journal4 mapping with explicit close3, fixed table and post-Drop/module
checks, then prove real surviving-reader AND server refusal/reap release under
separate non-policy source/method review. Worker loop lease owner/startup binding
has its separate accepted development contract, not source/runtime acceptance.
InstalledRelease15 and production/PATH/ARM gates are unchanged. Commit/push/exact
remote equality will be reported separately; no overall completion is implied.

### P06-READ-REFERENCE-CONTEXT-r1 local mapping/source checkpoint

Owner w1:pJ; independent reviewer w1:pA; R02/R13, DB-ACCESS-01. Baseline
f75e5067bc0e2f2d8271982cfbfe753e9b5ba288 is the published accepted standalone
recovery-reference checkpoint: commit/push0, exact origin/main verified and clean
tree before this disjoint next increment. InstalledRelease15 remains unchanged.
Original07 fa82aeca04cca32443b8b2bc8f4d69f27438f86dbaed130b1674e28c539531e8
and08 88319f70a32c2d452079a41ed4e2c669b2eb579f69ac1e38d1348e273cb28810
are preserved before this append.

Scope: test/CMakeLists.txt, shared private read_policy_context.hh, new build-only
read_policy_reference_context_probe.cc, new private read_policy_reference_spawn.py
and ordinary read_policy_reference_spawn_test.py,07/08. No product source, module
entry ABI, installed payload or rule matrix changes. Existing Stage A default
table calls stay unchanged; the separate typed witness permits only journal4,
with code3 reserved for a future separately reviewed loading route.

Host-build-r1 failed from an ambiguous duplicate Self helper/unused-function
warning; preserve FAIL, no image execution. Removing the duplicate uses the
already accepted helper. Explicit host-build-r2 compiles/links the new diagnostic
target successfully; it does not run it. Host-tests-r1 had5PASS/1ERROR because
Python negative-FD validation raises ValueError rather than the test's OSError.
Current source explicitly rejects missing stdio before the syscall and also
rejects stdio aliases of journal. Final host-tests-r2 actual7/7 PASS, with real
unprivileged fake-image spawns and exact owned waits. Host-check-r1 CTest11/11
PASS, including those tests in ordinary tool discovery and the old context/loader
tests. No privileged new image is in CTest. Existing root experiments retain
their opt-in/default-skip boundary.

The spawn helper performs stable5+ copies before final own-FD snapshot, ordered
stdio/journal4 mapping, explicit close3 and close of all other5+ descriptors.
Signal-deferred attachment precedes handshake/logging. Unknown spawn is sticky
uncertainty, not absence. Tests retain the linked scope on unproved child cleanup;
fake-image EX contention after parent close and success after exact normal reap
measure inherited-description lifetime only. They do not execute real root IDs,
post-Drop ACL or SMACK checks, platform loading, policy or actual journal CLI.

Local source review and exact native compile/method/results are pending. No root
new-image execution has occurred or is approved. Post-Drop metadata failure must
remain setup FAIL without label/ACL/NSS repair. Reader AND server coordinator-loss
survival, module/TLS retention, standalone real recovery refusal and Stage B eight
fixed rules require later frozen source/method gates. Worker owner/bootstrap has
its separate accepted development contract, not implementation/runtime evidence.
No production admission, original different-subject subset, PATH, remount, RPM,
installedRelease15 or ARM gate closes.

P06-READ-REFERENCE-CONTEXT-r1 independent source CHANGES_REQUESTED: intentional
FD3/high sentinels were CLOEXEC, so those passing cases could not distinguish
explicit-close actions from exec removal. Preserve the7/7 result as mapping/
reference evidence but not causal evidence of those closes. R2 changes only the
test and07/08: both intentional sentinels are explicitly made inheritable and
checked before spawn. Two test-only posix_spawn action filters omit exactly one
required close; each fake child now rejects its table and is actually reaped1.
The complete mapping still requires exact0,1,2,4 and normal reap0. Production
stable copies remain CLOEXEC; no mapping/guard/context relaxation. Host-tests-r3
actual9/9 PASS. Full current host check will be recorded below before freeze.

Native-build-r1 is separate compile-only preparation of the frozen r1 source10
archive b77f6f30e001506018751097d1f49d88510229f0e568e50abb30078f2203a325.
Protected new read-reference-context-r1 root700; exact source10/acceptedJSON53,
initial namespaces/ext4/trusted ancestors; direct GCC14 C++20/O1/Werror compilation,
clean ldd-r/noRPATH/RUNPATH and minimal standard-runtime closure6; remote0/transport0.
No image or Python test was executed. Its unchanged compiled inputs remain exact
current C++ code, but archived test/07/08 are historical r1, not revised r2 bytes.
The new native image has no runtime/Drop/post-Drop metadata acceptance. R2 exact
archive/method and independently reviewed native execution remain pending.

Frozen r2 final host-check-r2 CTest11/11 PASS after the inheritable-sentinel and
causal omission tests; focused host-tests-r3 actual9/9. Four implementation/build
paths are unchanged from r1; only test/07/08 differ. Re-review is required to close
the explicit-close evidence finding; the owner does not self-close it. Native
compile r1 remains historical compile-only, and no root context is executed.

Independent r2 re-review closes the original explicit-close evidence finding,
but requests one negative-fixture race fix: omit3 must not send x to a pipe whose
child correctly rejects before reading. R3 changes only that test/chronological08:
normal mode still checks its control write, while omit3 directly waits/reaps1
without a write or broad suppression of pipe errors. Preserve r2 host9/9 as prior
passing evidence with that scheduling gap. Final current host-tests-r4 actual9/9
and host-check-r3 CTest11/11 PASS. No implementation/07 guard change.

Native-build-r2 recompiles the unchanged C++ image in a new protected r2 tree
from exact r2 source10 archive a0deac1694410ae8531c704c3cb6d68425cf34a83322b56a2cd48a2fc133e1dd,
accepted JSON53, initial namespaces/ext4 and trusted code paths; direct GCC14
compile/link, minimal standard-runtime6, clean ldd-r/noRPATH/RUNPATH,0/0. Image
SHA217c1b008b1dae3d734276c963643d340ba2a2550c1eb28afae539829fa0ec3c.
No native test or image invocation occurred. R3 test bytes require their own
exact native safety evidence; root context execution remains unapproved/NOT_RUN.

Independent r3 LOCAL SOURCE disposition ACCEPTED preserved original07
01aa226c255a13fe3b921233a5ca01f62bf5d89f9708b4449a2e3162878e35e6
and08 a460c6415de9de8aa7f37a6b9a0e2e05b6f9eecfb40de679fd0be7056c50f304.
Native-build-r3 freshly directly recompiles unchanged C++ from exact source10
archive b3c22946546e9395a2d018eb64312cae4c7f04ccb2ff8172b0c1f37520afaae3,
JSON53/minimal standard6, same image217c1b008b1dae3d734276c963643d340ba2a2550c1eb28afae539829fa0ec3c,
clean ldd-r/noRPATH/RUNPATH/0/0. This is not native CMake/CTest or image execution.

Nonpolicy method-r1 read-only request failed SDB service-name/command-length
before remote execution, transport1 with no remote sentinel; transfers are not
test execution. Protected source-only verifier method-r2 shortened the first
clean/isolated command; read-only preflight-r2 source10/JSON53/image1/deps6/
interpreter/watchdog/trusted ancestry/ns/ext4/no-cache PASS0/0, explicitly exits
before tests. No nine-test or real context run occurred.

Method-r2 independent CHANGES_REQUESTED reopens only a nested-driver test cleanup
path: subprocess.run timeout kills/reaps the driver but cannot prove its child's
absence, while ordinary tearDown could delete its linked scope. R4 adds a sticky
nested invocation latch before launch, clears it only after exact successful
driver return whose source requires actual child reap/close, and prevents rmtree
on timeout/nonzero/unknown. No unknown PID signaling or automatic recovery.
Two injected tests assert deletion is never reached; TEST-only reset is justified
solely because subprocess.run was mocked and no driver/child actually launched.
Implementation/build inputs are unchanged; only test/07/chronological08 differ.
Host-tests-r5 actual11/11 and host-check-r4 CTest11/11 PASS. Historical r3 source/
host acceptance and unexecuted method remain preserved; revised source/native
method re-review required, not self-closed. Real context/Drop/ACL/module/recovery/
policy/installed/production gates remain NOT_RUN/unapproved.


P06-READ-REFERENCE-CONTEXT-r4 independent LOCAL SOURCE ACCEPTED preserves
original07 a1a97998782d814c57c277dbf168af682082b6bcdbc6f27f88dbfdf7c27069c3
and08 db3ed0a4c6d32b5f7ae9835852b1d7f344457fa6ef263745c98dd24ab9ca10cb.
Native-build-r4 is direct GCC14 compilation of exact source10 archive
248dee32209287de40561e8466f5c1ff819b8d1dfa1142fa86d54bcccd7eb3d5,
JSON53 and standard runtime6 in the protected read-reference-context-r4 tree.
Image217c1b008b1dae3d734276c963643d340ba2a2550c1eb28afae539829fa0ec3c,
clean ldd-r/noRPATH/RUNPATH, remote0/transport0; no CMake/fullcheck/RPM claim.
Nonpolicy-preflight-r3 read-only PASS0/0 exits before tests.

After separate NONPOLICY-METHOD-r3 pre-execution ACCEPTED, owner invoked
python3 /tmp/capmgr-read-reference-context-execute-nonpolicy-r3.py tests.
read-reference-context-native-nonpolicy-tests-r3.log prints the FIRST env-i
absolute Python-I-B/source-only verifier command, transferred method checks,
installed60s watchdog and owner90s outer bound. Exact source10/JSON53/image1/
standarddeps6/interpreter/watchdog/protected ancestry/initialns/ext4/no-cache
checks pass before and after. All11 mapping/fake-image tests PASS without skips
in0.191s; NATIVE_REFERENCE_MAPPING_RETURN0, remote0/transport0. The C++ image is
pinned but NOT executed. The source requires exact owned fake-child wait/reap,
parent-reference close/independent EX exclusion and post-reap release in the
positive, causal omission-child rejection, and retained scope/zero deletion in
injected nested timeout/nonzero tests. These are passing assertions, not raw
flock/wait syscall records; the nested failures are no-launch mocked cases.
No timeout or unexpected retained result is logged.

Final native/publication review is pending. This supersedes only the historical
native mapping-test NOT_RUN state. Actual module-free root context image/Drop/
post-Drop ACL remains NOT_RUN and unapproved; no NSS, real-journal recovery,
surviving/adopted reader AND server, module/TLS, SQL/policy/StageB or production
proof follows. InstalledRelease15 remains unchanged. The separate P04 isolated
worker owner API-boundary-r2 contract is ACCEPTED FOR DEVELOPMENT ONLY, not code
or bootstrap/runtime acceptance.


P06-READ-REFERENCE-CONTEXT-r4 FINAL NONPOLICY/native/publication ACCEPTED by
independent reviewer; accepted administrative08 SHA
bd738190604b6fe65555cf14a49bae4a44c311964f64109781bd0baba8ea4bb5 is
preserved before heading-only07 and this final disposition bookkeeping. All six
code/07 entries were independently rechecked before administration. The preceding
final-review-pending statement is superseded by this explicit scoped verdict.
Next: publish these seven reviewed paths, then implement the separate P04 owner
boundary and prepare an independently reviewed real reference-context method.
No real root context/Drop, module/TLS, journal recovery, policy or product gate
closes. Disjoint P04 source WIP is excluded from this publication.

## P04-WORKER-OWNER-r1 isolated owner/factory local checkpoint

Owner w1:pJ, reviewer w1:pA; baseline264d13b80a3432b9e6d4f8a353cec67ae980a328.
Accepted owner-api-boundary-r2 development plan SHA
774d1dac6c3ec85d9d85c2adcea75e7121e3a6ad53d59decfa21bdef7e89a4c4,
with the earlier bootstrap contract d7401c2c7dc4f0fadd5107e912671a9288895e68f047bed406d58be1ea70bf61.
Scope: src/CMake; read_lease.hh/.cc private borrowed report; snapshot friend;
worker_bootstrap.hh/.cc typed owner Finish; leased_worker_loop.hh/.cc; test/CMake,
leased_worker_loop_test.cc;07 and08. P06 seven-path publication264d13b is distinct,
push0/exact remote main equality/LICENSE unchanged. Its native eleven fake-image
mapping tests do not provide worker-owner bootstrap evidence.

Factory validates exact initial bootstrap BEFORE acquisition, physically closes
RO SQLite through the accepted reader, moves the SAME snapshot into a nonmovable
registry-bound loop owner, and publishes only after one-shot typed Finish. Five
actual concrete-lease FDs join six aliases/witnesses in both table checks. No
caller-supplied or unrelated snapshot can satisfy the production factory. The
private invalid namespace constructor is reachable only by the test-TU friend
and cannot satisfy ValidateCurrent; it tests actual factory refusal-before-load,
not an alternate allow policy. Step has no additional metadata/SQLite operations.

Host-build-r1 compiles the owner/adapters. Host-build-r2 FAIL is a test-only
-Werror=dangling-else around EXPECT_THROW; explicit braces correct it. Subsequent
focused-r1/r2 and fullcheck-r1 preserve FAIL because a test incorrectly required
Jobs>0 after lost-delivery cleanup. Actual existing WorkerLoop explicitly clears
those tokens when lost after confirmed child cleanup; no product change was
needed. Removed that false assertion, retaining the stronger real unreaped-child
retirement rejection and lost+Quiescent boundary. No new blanket guard relaxation.
Final host-build-r5/focused-r3 actual11/11 PASS; host-check-r2 CTest11/11 PASS.
Independent exec generation EX stays BUSY through readiness, queued child-free
Complete and delivery-loss cleanup, then succeeds after actual lease retirement.
The tests use injected label metadata, real SQLite/physical close and real owned
fork/exec/wait/reap. They do not drop IDs/SMACK or run real NamespaceInit.

Tests distinguish open-admission/no-job destruction, queued child-free Complete
with saturated output, normal drain/terminal retirement, delivery loss with an
unreaped child versus after cleanup, constructor/moved-from/inherited snapshot
rejection, inherited wrapper operations and fail-stop before inherited loop dtor.
Normal factory refusal/one-shot failure is tested with invalid namespace witnesses;
trusted test startup bypass supplies ordinary owner lifetime evidence only. No
successful real root typed Finish/table or namespace/CLI nondelegation claim.
Source/native review pending; native compilation/ordinary execution NOT_RUN for
this revision. InstalledRelease15, production admission, revision invalidation,
executable authority, crash maintenance, policy/RPM and ARM gates stay unchanged.

P04-WORKER-OWNER-r1 disposition DEFERRED pending an owner-acknowledged test
ownership amendment; no r1 test execution/publication acceptance. R1 native
compile-only archive183 bc881d2cbf201bc6d8dfeaeeb727054480d0261034cddba0c5cea1ed60b0e64a
is historical frozen source, not r2 test evidence. No native tests have executed.
Owner self-audit found a single fixture child_id could be overwritten by a later
probe while an earlier wait remained unresolved; base fixture removal could then
precede OwnedChildren destruction. R2 retains separate preallocated records for
every fixture/probe child until exact reap/release. Teardown attempts all known
records, requires no unresolved record/table entry AND a sticky uncertainty guard
before closing references/base rmtree. Unknown observation errors or Uncertain
status permanently poison deletion eligibility; later successful probes cannot
restore it. A known live child/no-budget wait can be retried without pretending
its absence; its record remains owned throughout.

The added actual live-child/zero-budget wait followed by independent successful
probe preserves the first record and rejects deletion eligibility until its exact
known kill/wait/reap/release. A separate pure injected-uncertainty test has no
actual ownership loss and tests the sticky guard after a successful probe; its
TEST-only reset is for that synthetic no-lost-child setup, not a runtime escape.
R2 also preserves the declared O_NONBLOCK lease-lock descriptor flag in startup
report checks, with a real clear/restore/poison negative. F_OFD_SETLK was already
nonblocking and this flag is not what makes OFD acquisition nonblocking.
Host-build-r6/focused-r4 12/12/check-r3 CTest11/11 precede that final flag/sticky
amendment. Final host-build-r7/focused-r5 actual14/14/check-r4 CTest11/11 PASS.
No product worker-loop behavior change; source re-review and exact-r2 native
compile/execution methods remain pending. Root successful Finish, NamespaceInit,
reference-context Drop, policies, installedRelease15 and production remain separate.

### P04-WORKER-OWNER-r2 ordinary native evidence (accepted private scope)

Independent LOCAL SOURCE/host verdict ACCEPTED for exact12 manifest
9d121daa9f96c81150d5e4333d73dd65ac535bdb238bac3c9f3fe5813fe6ae6d.
Reviewed07 remains295385ef29164051d326538c94bcea6717b9908fea9a1fede867175812ee17ae;
original reviewed08 3fc0cf2e412e5f50747ad2411754f88ca25415e28eaec593bbd6a12ff309ecd6
is preserved before this append. Historical source/native-pending text above is
superseded only within the explicit local/ordinary scope described here.

Native-build-r1 FAIL: runner_test.cc cc1plus reports Killed signal, gmake error2,
remote1/transport0. The cause is not established; neither timeout nor OOM is
claimed. No tests ran. Original183-source archivebc881d2c...e64a and log remain.
Native-build-r2 verifies protected existing worker-owner-r1 tree, initial
namespaces/ext4, original source hashes and no active owned cwd/exe before reuse.
Current archive183 SHAecc5df28d8507d9ccb73a9ef06aa85d30de2905cf6e28691b0fed9eef14f4727,
manifest0282b3cf4570c4c36eb7a19f1cffee048215ce4ffc6e6b5b4f3040d45261241f,
changes only accepted read_lease.cc/test/administrative08. Incremental -j1 builds
the affected source and completes previously unbuilt test/API targets; it is not
a fresh whole build. Six project-image hashes, clean ldd-r and final183 pass0/0.
Source12 and ordinary method-r1 were independently reviewed before execution.

Actual owner command is preserved in worker-owner-native-tests-r1.log: FIRST
/usr/bin/env -i and absolute /usr/bin/python3 -I -B verifier, protected runner
hash124ced2b182f052f8296ffe7a18eb0dc28d52ae9158b9142a3d686d707108db1,
installed180s watchdog with owner210s outer bound. Immediate pre/post checks pin
source183/JSON53/images6, CLI fixture and project library BEFORE tests, exact
SONAME links/sole build RUNPATH/C-consumer library resolution, interpreter,
watchdog, protected ancestry/initial namespaces/ext4 and no bytecode cache.
Source files and the exclusive owned build tree remain frozen throughout.

Observed selected CTest registration and actual execution exactly3/3 with verbose
--no-tests=error: unit123/123 + adapters201/201 =324 unique GoogleTests, plus the
pure C consumer. Separate focused LeasedWorkerLoop14/14, WorkerCatalogLease25/25
and CoordinatedWriter24/24 PASS repeat those subsets. Real metadata prints
User::Shell with user xattrs explicitly UNSUPPORTED; loader test labels remain
injected. Direct C_CONSUMER_EXIT0 and exactly12 capmgr_@@CAPMGR_0 exports pass.
ORDINARY_NATIVE_ONLY_PASS, CAPMGR_REMOTE_EXIT0 and TRANSPORT_EXIT0; no timeout or
retained marker is recorded. Source-enforced exact fork/exec/wait/reap and lease
assertions are not separately printed syscall evidence.

This is ordinary owner/lifetime evidence, using the test-TU Ready bypass, not
successful root typed Finish/five-FD startup or real NamespaceInit/CLI
nondelegation. Native full11, RPM/install, production admission, live revision
invalidation, executable authority and crash-maintenance proof remain unexecuted
or gated. InstalledRelease15 unchanged. Final scoped runtime/document/publication
review is pending; next action is that verdict, followed by reviewed publication
and separately designed root bootstrap integration. No reference-context Drop,
module/service/RPC, global policy or operational DB result is inferred.


P04-WORKER-OWNER-r2 FINAL ORDINARY-NATIVE/PUBLICATION ACCEPTED by independent
reviewer for the exact12-path private checkpoint. Original accepted07
295385ef29164051d326538c94bcea6717b9908fea9a1fede867175812ee17ae and
reviewed administrative08 9f0dc2af9c2e1a718f459f372c024a95d5bcdb1248a0ae5c6c23b25dda44dede
are preserved before these heading/Next/disposition edits. The preceding final
review-pending text is chronologically superseded only by this scoped verdict.
Next: publish the reviewed12 paths, then separately design/freeze successful
root typed Finish/five-FD startup and NamespaceInit/CLI nondelegation evidence.
No root workload, package/install, revision invalidation, crash maintenance or
production gate closes from the ordinary test-TU Ready bypass.

P04-WORKER-OWNER-r2 reviewed12-path publication completed as
fb9bcc10028e9ba4a48ec9f6c31878ae74b46dcf: signoff/coauthor/message line checks,
commit0/push0, exact origin/main equality, clean working tree and unchanged
LICENSEc71d239d...d0ab4. Only accepted07 heading and administrative08 disposition/
Next changed after the final verdict; temporary P06 coordinator/methods were not
included. InstalledRelease15 is unchanged. Successful root owner bootstrap and
NamespaceInit/nondelegation still need their separate reviewed increment.

### P06-READ-REFERENCE-CONTEXT-COORDINATOR-r2 own-context evidence (accepted inert scope)

Separate temporary coordinator/test source2 manifest57b67514dfa93b3884fbd7c51d11c8368b5ca7144480c0fcc15b5b06e57f53ab
is LOCAL SOURCE ACCEPTED, not project inclusion. R1 source-loader finding was
complete stat_result comparison including read-induced atime. R2 compares explicit
stable dev/ino/mode/uid/gid/nlink/size/mtime_ns/ctime_ns, ignores atime and retains
NOFOLLOW/CLOEXEC, bounded exact read, digest-before-compile and named/held checks.
Host-r4 actual11/11 FakeOperations PASS includes every held/named stable-field
negative, atime-only positive, digest rejection before execution and first-child
cleanup failure followed by later successful cleanup/final absence with zero
removal. Those mocked tests are not native context evidence.

Own-method-r1 received separate pre-execution safety acceptance. It pins the
complete old source10 closure plus coordinator/test2, explicitly executed
read_policy_children.py and reference_spawn.py, JSON53/image1/standarddeps6,
interpreter/watchdog, protected ancestry/initial namespaces/ext4 and no cache.
Actual own-native-contexts-r1 attempt FAIL is SDB service-name-too-long/transport1
before any remote sentinel/scope/role; no remote run or cleanup claim. Preserve
that log. Length-only short-outer-r2 received a new explicit safety verdict.
Protected root0600/singlelink/noACL/no-cap verifier992341d80936aebd20ea3834a952a7d884c5d3b2b5facf14df4459709abca59f
contains the original approved verifier bytes plus a newline. FIRST env-i absolute
Python-I-B short949-byte source-only launcher validates that file before compile;
original method4 and code image remain unchanged. No weaker checks or repair.

Actual own-native-contexts-r2.log records the approved short command, transferred
method4 and immediate pre/post complete preflight, installed60s watchdog/owner90s.
Fixed module-free image217c1b008b1dae3d734276c963643d340ba2a2550c1eb28afae539829fa0ec3c
runs sequential root-user-shell then system301-platform. Both exact per-role
stdout markers have status0/stderr empty; source requires exclusive actual owned
normal reap0. System final UID/GID301, sole group10212, labelSystem/allcaps0/
bounding0/ambient0/NNP1 and reference4 metadata/table are source-enforced checks,
not separately printed raw syscall replies. Post-Drop ACL accessibility succeeds
for this exact inherited reference/context; no permission/rule/FD repair occurs.

Fresh linked inert reference under root0700 scope/root0600 file is NOT a recovery
journal. OWNED_INERT_REFERENCE_CONTEXT_SCOPE and both validated role markers
precede REMOVED_REFERENCE_CONTEXT_SCOPE=/opt/usr/capmgr-bootstrap-build-p4h4og7o/reference-context-9_u4fjgv,
which precedes REFERENCE_CONTEXT_ONLY_PASS. Source enforces prepared default
SIGCHLD/subreaper/exclusive waiter, initial/final ECHILD, sticky unknown-state
refusal, pinned scope cleanup and independent final EX after parent SH close.
Those are passing conditions, not raw kernel traces or sole-child enumeration.
OWN_REFERENCE_CONTEXT_RETURN0, CAPMGR_REMOTE_EXIT0, TRANSPORT_EXIT0; no timeout,
traceback or retained scope is recorded. Actual source/dependency/image bytes
remain unchanged before/after; C++ image is the earlier direct-compile artifact,
not a newly rebuilt or installed code path.

Final measured runtime/administrative-document review is pending. Parent-held SH
and final EX do not prove a surviving reader/server keeps recovery blocked, TLS
or module lifetime, adoption or real-journal authority. No actual journal/CLI
recovery, module/RPC/SQLite, policy/load2, Stage B, package/install or production
admission occurred. Temporary coordinator/methods remain outside Git. Next P06
increment separately wires exact reference4 into delayed platform loading and
proves surviving reader AND server lifetime/refusal/reap before any new policy
fixture. The original different-subject matrix remains BLOCKED.


P06-READ-REFERENCE-CONTEXT-COORDINATOR-r2 FINAL MEASURED RUNTIME/admin-only
publication ACCEPTED by independent reviewer. Accepted administrative08
7488866b80631037ce9b515e4dc078de3f612d71e7823288b267c44098162239 is
preserved before this heading/disposition bookkeeping. The previous pending
paragraph is superseded for this inert-reference own-context scope only. Publish
administrative08 alone; temporary coordinator/test/methods remain outside Git.
Next: independently reviewed P04 no-job root typed-startup fixture and revised
P06 module-mapped reader/server survivor development contract; no extra policy or
production gate closes from these proposals or the preceding context results.


## P04-WORKER-OWNER-ROOT-STARTUP-r1 source/host review pending

Owner w1:pJ; reviewer w1:pA; phase P04, R10/R11/R13/R14 and DB-ACCESS-01.
Baseline 4c19b56e81a5f2f401efec9b2a3a469d0324d5c6. The exact temporary
root-startup development design SHA256
08bb03e8b92dd82e7fa514e2f474517259aca1b0c10827c0b46acc5f2aff4f50 was
ACCEPTED FOR DEVELOPMENT CONTRACT ONLY. Prior private owner publication
fb9bcc10028e9ba4a48ec9f6c31878ae74b46dcf remains a distinct accepted ordinary
checkpoint. No root source/runtime disposition is inferred from either.

This revision adds separate fixed no-START image/parent variants, the exact FD7
fixture topology/ancestry helper, and fixed child-record retention helpers. It
uses the actual LeasedWorkerLoop::LoadAndFinish and existing namespace capture,
spawn, session, journal and supervisor contracts. The normal parent provisions
under EX and physically retires all parent SQL/leases before spawning. Planned
measurements are typed READY/revision, independent exec EX BUSY, no-job normal
stop/reap/durable empty reservations and EX OK. Separate negatives require actual
rejected exit125 plus absent SHM, or source-enforced post-successful-close hook /
unexpected-FD gate rejection exit126. No START, job, clone/mount or policy write
is present. The new parent/image targets are EXCLUDE_FROM_ALL, uninstalled and
not CTest commands. Only the ordinary helper tests join default check.

Host evidence under /tmp/capmgr-evidence:

- worker-owner-root-path-host-build-r1.log: FAIL, requested new target before
  regenerating CMake; no rule existed then. Preserved separately from later builds.
- path configure/build-r2/r3 and path-tests-r1/r2: iterative earlier ordinary
  helper evidence; not the final ten-case bytes and not privileged path acceptance.
- worker-owner-root-image-host-configure-r1.log and image-host-build-r1.log: host compile
  of both fixed images PASS, no image invocation.
- worker-owner-root-parent-configure-r1.log and parent-build-r1.log: both parent
  variants compile PASS, before the extracted/tested retention helper amendment.
- Final worker-owner-root-parent-configure-r2.log and parent-build-r2.log:
  `cmake -S . -B build-style`, then explicit normal/fault parent plus path-test
  targets with -j2, EXIT0. Dependencies rebuild both images as needed. Host
  prefix remains /tmp/capmgr-host-deps/root/usr; no system installation.
- worker-owner-root-path-host-tests-r3.log: actual10/10 PASS (seven path/borrowed
  FD negatives/controls plus three injected ownership-reply tests). The latter
  create no child and prove earlier timeout/observe error retains deletion
  ineligibility after later exact known cleanup. Invalid borrowed ordinary/O_PATH
  descriptors remain caller-owned. Successful root ancestry validation is NOT_RUN.
- worker-owner-root-host-check-r1.log: `cmake --build build-style --target check
  -j2`, actual CTest12/12 PASS, EXIT0. This registers only the ordinary helper
  increment; neither new parent nor root image runs during check.

Final source freeze/diff/hashes will accompany the independent review request.
Native build, native ordinary tests and all new root image/parent execution remain
NOT_RUN and UNAPPROVED. No NamespaceInit/CLI nondelegation, live invalidation,
executable authority, crash-old-job absence, full native CTest, RPM/install or
production gate is claimed. InstalledRelease15 remains unchanged.

In parallel P06-READ-REFERENCE-MODULE-SURVIVOR-DESIGN-r2 at
c9331552fe993ad697b3d71247d7292580471252826a9989525990161fc221f0 was
ACCEPTED FOR DEVELOPMENT CONTRACT ONLY. Implementation must distinguish actual
body/-6 and teardown/HOLD_ACK while alive from final owned reap0, preserve old
10/20 modes, and keep supervisor output readers through kernel exit without an
SH reference in the supervisor. No survivor source/runtime or actual journal,
policy/SQLite/Authorize/Confirm/StageB authority follows from that design verdict.


## P04-WORKER-OWNER-ROOT-STARTUP-r2 source re-review pending

Reviewer r1 CHANGES_REQUESTED against exact9-file manifest
80949c7133456e15e694ff952cbe27bd70e8bbbcf77b9ed1a82ef04b8be61d74;
archive a5cbee9d67b18105ab8858ea1cc581c1a254ccef26559dd8618c3f3da9450ca4
and original host10/10/CTest12/12 remain preserved, not accepted root evidence.
Two fixture cleanup gaps were identified: transient errors in direct supervisor
Inspect/internal StopAndWait could be overwritten, and startup/drain deadline
paths could bypass the sticky timeout rule and later delete the scope.

R2 introduces a fixture-only ChildOperations decorator on the SAME owned table
passed to WorkerSupervisor. Every nonzero Observe/Reap/Kill result permanently
poisons deletion eligibility before forwarding its result, including internal
retry paths. Product OwnedChildren and WorkerSupervisor behavior is unchanged.
Startup/drain absolute deadline checks run before/after operations; the exact
existing supervisor internal bootstrap deadline message also sets the latch.
Negative startup requires no timeout/uncertainty before accepting125/126.
Actual normal release clears the caller's record immediately after supervisor
confirmation, before further fallible checks. Normal/failure cleanup both use
one tested gate; later successful absence proof never restores eligibility.

New focused cases exercise direct-table Inspect, internal StopAndWait observe,
reap and kill errors followed by terminal success, plus startup/drain expiry and
internal startup deadline followed by successful known cleanup. Injected later
absence succeeds but removal/phase-PASS calls remain zero. These tests launch no
child and do not execute a root scope; they exercise the same cleanup gate used
by the parent. The original path/borrowed-FD and timeout/later-probe cases remain.

The126 report is limited to successful non-null close hook AND the existing
FD-table gate error text; that text also covers readdir failure. It is not an
independent syscall trace or unique extra-FD/errno classification. MissingSHM125
with continued absence remains a rejected-startup observation whose positive
control is the separate normal no-START case.

Iterative parent-build-r3/path-tests-r4/check-r2 logs under /tmp/capmgr-evidence
are preserved separately; a subsequent shared CleanupScope gate refinement makes
them non-final evidence. Final frozen r2 checks will be recorded from
parent-build-r4, path-host-tests-r5 and host-check-r3. Native/source disposition
and all new root workload execution remain pending/NOT_RUN. No production,
NamespaceInit/nondelegation, policy, RPM/install or installedRelease15 change.


P04 root-startup r2 final host evidence: parent-build-r4 rebuilds both fixed
parent variants and the ordinary helper target, EXIT0. path-host-tests-r5 logs
actual14/14 PASS (path7 + child/proof7, with three internal retry seams repeated
inside one case). host-check-r3 records actual CTest12/12 PASS, EXIT0. Final source
bytes stayed unchanged during these checks; no root parent/image was invoked.
The9-file r2 manifest/diff/archive are frozen for explicit independent re-review;
author changes do not self-close the two r1 findings. Root execution remains
NOT_RUN/unapproved and old r1 evidence/frozen inputs remain preserved.


## P04-WORKER-OWNER-ROOT-STARTUP-r3 source correction (ACCEPTED)

The reviewer rechecked r2 manifest
31fbcc60ebb015e8852ebb329623a69f6310b1acf97d3d23e6fc18638bbf0a88
and CLOSED the observation-decorator finding, but returned CHANGES_REQUESTED
for the internal worker-close drain deadline. Session can throw after its5s
budget before the fixture7s budget, skipping the previous after-call check.
The overall failure could therefore still remove the scope after known cleanup.
R2 archive65a547942a4f5db6da108d554b9773bc6c240d9cb55cca8c5da97db96d321df4
and host14/14/CTest12/12 evidence remain historical, not acceptance of that path.

R3 uses one fixture-only WithinDeadline wrapper for startup, PrepareStop and
drain calls. It checks the same absolute budget on normal and exceptional exits,
classifies the exact existing Supervisor/Session deadline messages, permanently
poisons cleanup eligibility and rethrows the ORIGINAL exception. Actual normal
release still clears the caller record immediately inside the wrapped operation,
before a fallible post-check. Product Session/Supervisor are unchanged.

Two added deterministic cases exercise the internal Session deadlines and an
operation throwing after the startup/drain budget. Later simulated exact known
cleanup and successful empty-boundary verification use the SAME CleanupScope
gate; removal and phase-PASS calls remain zero, with the original functional
exception preserved. These are ordinary injected helper tests, not root scope
execution or raw syscall traces. Final /tmp/capmgr-evidence logs:
worker-owner-root-parent-build-r5.log rebuilds both parent variants/helper, EXIT0;
worker-owner-root-path-host-tests-r6.log actual16/16 PASS;
worker-owner-root-host-check-r4.log actual CTest12/12 PASS, EXIT0.

Separate native-build-r2.log is compile-only evidence for HISTORICAL r2 bytes:
source189/archive2a314020fbf2f37b1b7880f3abf5ed435192d6ac7d9b880ab55a6455d74eaa55,
manifestcaf7eb54216ac1afd6d4fddc6821471cd8cc02071e8b587db99b139ad7373d16,
JSON53, six freshly built target images and eight resolved standard/GTest
providers, remote0/transport0. No native test or root fixture was invoked; those
r2 binaries do not supply corrected-r3 evidence. The outside-project ordinary
14-test method draft was not submitted, transferred, approved or run and is
superseded by the corrected16-case scope. Exact r3 native compilation/method and
all root typed startup execution remain NOT_RUN/pending independent review.
No NamespaceInit/nondelegation, policy, production or installedRelease15 change.


P04 root-startup r3 LOCAL SOURCE/host ACCEPTED by independent reviewer against
manifest6f1b0f6489d74bf148d2cc92d0492142f3b405d7cddc1cb6c8ed020feacb9a06
and archivefdba3162903b88ba9c8ce859d8e58ead551966df8f99439307dc705e63cc7683.
Accepted07 SHA3a23e9b94a5f8adeec499c44926e8a02b02c65c065a1a9bf3de56821a5bab22b
and08 SHA9942938cb686260fd97dac517ee00c9afd34dd808c3be60584c68f94d50f610a
are preserved before this append. Remaining r2 deadline finding CLOSED; host16
and CTest12 evidence accepted separately from pending native/root methods.
No native/root/publication acceptance is inferred from that source disposition.

P06-READ-REFERENCE-SURVIVOR-CORE-r2 temporary LOCAL core ACCEPTED separately
against manifest43b304064634e58fc3ec52a8dd4ed82e25524f3b47fd32157149e268cdecc7dd,
helpercf1dc5ba64836a022919cb368a829a5fe8292cbd974ee9965bb07f1ca7ec481a and
testd6dabcb7a1697349d4578c41f2f1c21db9a49f3390673d723461a7ae9d8d390b.
Host read-reference-survivor-core-host-r2.log actual20/20 PASS. Late final full
write/ACK/EOF fail against the same absolute deadline without rollback claims;
all six pipe descriptions are nonblocking, including output writers and their
stable copies. Any core Failure must poison future integration cleanup eligibility;
EOF/retained observed bytes cannot supply authority after a failed operation.
This is outside-project protocol/core evidence, not completed survivor/adoption/
reference4/module/timer integration, native/root execution, SH/TLS lifetime or
actual policy-journal/production authority. Earlier r1 findings remain preserved.


P04 root-startup corrected-r3 native COMPILE-ONLY preparation: fresh protected
worker-owner-root-startup-r3 tree, archive
2bc433acb89290edc57b9452b56963ae831491dc068ab53637982d9d3183a8bd,
manifest e97eddd258eb49a436736fae4e4969eb714c761c417c353716030742a719301c.
Archive189 was independently compared by owner to HEAD4c19b56 plus frozen9; its
08 is the accepted source snapshot, separate from these later administrative
appends. worker-owner-root-native-build-r3.log records fresh configure with full
platform optionsON and -j1 targeted closure rebuild: both parent/worker variants,
ordinary helper and lock probe. Six image hashes, no RPATH/RUNPATH, clean ldd-r
and eight resolved protected loader/standard/GTest providers are recorded; final
source189/JSON53 and remote0/transport0. No native test or root image invoked,
no whole CTest/generated transport runtime/RPM/install result inferred.

New parent hashes623bd544ec1275c5f6e1bfd866d82c3f607b49dad26a802cf2f35563b755c141
and9c4ee53a7e2ab4ac32391b3e1af067c550a4ffd0193df8e1b839c1c8acc0d179;
helper08d29b1c7f8c73f061bd781687fbdd030bf60b8a30ef3e3d79919490b61f072a.
Worker-image and lock-probe bytes happen to equal historical r2 hashes; this run
freshly compiled their closure and does not describe copies as new compilation.

Ordinary-method-r2 is frozen for separate review: preflight
7f3569d17070483200a31a45a0352c07f281a1fdf015174ae1513c6dbabcaf19; runner
b1b84751ec7ec37ac0c1bd88e154934bf042c957128d25fd07e0a73cf62b9504;
outer1e17ef81f84b5974c72ba3d04203648851e77990043586025ab820a965f0a184.
Read-only worker-owner-root-native-ordinary-preflight-r2.log validates transferred
method2/source189/JSON53/images6/providers8/interpreter/watchdog/protected paths,
initial namespaces/ext4/no-cache, explicitly NO_TEST_OR_ROOT_IMAGE_INVOKED,
remote0/transport0. Proposed selection is ordinary helper16 only, installed30s
watchdog/owner60s; no actual child or root parent/worker is selected. Native
helper16 execution and every root typed-startup case remain NOT_RUN/unapproved
until their separate exact-method dispositions. InstalledRelease15 unchanged.


P04 root-startup ordinary-method-r2 pre-execution ACCEPTED independently for
helper16 ONLY, preserving accepted administrative08
5881524f8d8409be51f69899bd20ce5f653813eb3b517607e3ed341584e856d5.
Owner ran the exact frozen outer after host method2/outer2 hash rechecks. Actual
worker-owner-root-native-ordinary-tests-r2.log prints FIRST env-i absolute
Python-I-B command, installed30s/owner60s and transferred protected method2;
immediate pre/post source189/JSON53/images6/providers8/interpreter/watchdog/
initial namespaces/ext4/no-cache PASS. Actual16/16 (path7 + injected children9),
no skips, HELPER_TEST_RETURN0, ORDINARY_HELPER16_ONLY_PASS, remote0/transport0.
No timeout/failure marker. No kernel child/root parent/worker/lock-probe image was
selected. Borrowed-FD/temporary-file and sticky deadline/cleanup assertions are
source-enforced tests, not raw syscall traces or typed startup success.

Native ordinary-helper final disposition remains pending this actual evidence
review. Root normal/missingSHM/post-close fault execution remains NOT_RUN and
unapproved; its separate method is under preparation, with no implicit permission
from helper16. No NamespaceInit/CLI nondelegation, live invalidation, policy,
RPM/install or production claim; installedRelease15 unchanged.


Independent reviewer ACCEPTED root-startup r3 ORDINARY native helper evidence
and accurate administrative08 SHA9989229fc5c9163c4dcc1770d608fe160e724a467bf1970cd372b1d9ddfbe051.
This closes only helper16; parent/worker/probe remained uninvoked in that run.
The full root-startup gate is explicitly NOT_RUN/unapproved, separate from this
ordinary-only disposition and all earlier source/host/compile checkpoints.

ROOT-METHOD-r1 proposal is frozen outside Git: exact same preflight
7f3569d17070483200a31a45a0352c07f281a1fdf015174ae1513c6dbabcaf19, runner
7ed08164aeb9d09b54f2aed1a7b4dd2cac1d69e621c59aec17ccb909c1cfc937,
outer72b5878d62ff6c1c8e75ba1212cec7d347e90219c91028510e95342bf8db4a74,
plan54acc1590ba06ebce59680bc341d4a1257a5ba36961c0e21c335317bb71ec4a2.
Read-only worker-owner-root-native-startup-preflight-r1.log checks transferred2
and unchanged protected source189/JSON53/images6/providers8/interpreter/watchdog/
ns/ext4/no-cache PASS, explicitly NO_TEST_OR_ROOT_IMAGE_INVOKED, remote0/transport0.
Proposed future route is fixed no-argument normal parent (normal-no-start then
missingSHM) followed only after success by fixed fault parent, installed90s each
and owner210s total. Three fresh owned fixture scopes only; no START or CLI exec,
NamespaceInit/clone/mount, policy/install/production authority. This read-only
preparation does not authorize the actual root fixture. Exact method request is
now separate from the accepted ordinary16 evidence; runtime remains NOT_RUN.


## P04-WORKER-OWNER-ROOT-STARTUP-r3 measured no-START runtime (ACCEPTED)

ROOT-METHOD-r1 pre-execution ACCEPTED independently for the exact three-case
route; accepted administrative08
43a2353cf88e53fc0459c8beb4c2ffff88addb6c055b4349ebbe2e5c30564708
is preserved before this append. Owner verified method2/outer2 hashes immediately
before executing /tmp/capmgr-worker-owner-root-execute-startup-r1.py unchanged.
Complete evidence: /tmp/capmgr-evidence/worker-owner-root-native-startup-tests-r1.log.
FIRST env-i absolute Python-I-B command, installed90s per fixed parent/owner210s
are printed; transferred2 and source189/JSON53/images6/providers8/interpreter/
watchdog/protected paths/initial namespaces/ext4/no-cache pre/post checks PASS.
Both no-argument parent returns0; final ROOT_NO_START_THREE_CASES_ONLY_PASS,
CAPMGR_REMOTE_EXIT0 and TRANSPORT_EXIT0. No timeout/retained/failure marker.

Normal scope /opt/usr/capmgr-leased-bootstrap-fixture-aa1NgF: actual typed
READY revision1/NO_START, independent generation EX BUSY/owned probe reap0, then
OWNED_NORMAL_WORKER_REAP0/CONFIRM_NORMAL_EXIT/NO_RESERVATIONS and EX OK/probe
reap0. Source requires all parent provisioning SQL/SH leases physically retired
before BUSY, normal CanExitCleanly retirement and exact owned supervisor release.
Those unprinted predicates are source-enforced assertions, not extra raw syscall
traces or independently printed context/five-descriptor tuples. REMOVED scope
aa1NgF precedes normal-no-start CASE_PASS.

Missing-SHM scope m6iMLS: LEASED_STARTUP_REJECTED=Invalid worker READY, exact owned
exit125, MISSING_SHM_STILL_ABSENT, EX OK/owned probe reap0, removal before its
CASE_PASS. Source requires blocked failed startup with no reservations; this is
a rejected generation, not normal ConfirmNormalExit. Parent then reports its
final no-START fixture PASS/return0 and postcheck before the fault parent starts.

Post-close-table-fault scope IJYQcD: Invalid worker READY, owned exit126,
POST_CLOSE_FAULT_FIRED_AND_TABLE_REJECTED=source-enforced-exit126, EX OK/probe
reap0, removal before CASE_PASS/final parent PASS/return0. Dedicated worker wrapper
requires the armed non-null actual successful SQLite close followed by the common
table-gate failure text. This remains limited source-enforced attribution; that
text also covers readdir failure and is not a unique errno/extra-FD trace. No
negative case counts a timeout as success.

Runner additionally checks three matching owned/removal paths and actual absence
plus removal-before-case ordering. Fixed source requires sticky syscall/deadline
eligibility, exact record resolution and final ECHILD before pinned complete
inventory cleanup. ECHILD/topology and cleanup predicates are source-enforced,
not separate raw wait replies. Printed CMR1 values are validated provisioning
identities, not a raw SMACK/context tuple; the metadata getter is real/default.

Earlier root runtime NOT_RUN paragraphs are historical and superseded ONLY by
this exact measured no-START result; final runtime/publication disposition is
still pending independent review. No START/job/CLI execution, NamespaceInit/
clone/mount/FD nondelegation, live revision invalidation, executable authority,
crash-old-job absence, policy/service/load2, full native12/RPM/install, production
or ARM gate follows. InstalledRelease15 remains unchanged.


P04-WORKER-OWNER-ROOT-STARTUP-r3 FINAL NO-START RUNTIME/PUBLICATION ACCEPTED
independently for the exact reviewed9-path private fixture and administrative08.
Preserved original accepted07
3a23e9b94a5f8adeec499c44926e8a02b02c65c065a1a9bf3de56821a5bab22b
and final accepted08
dd73e2f9d40d79e8b6ade1238e116054a3350db2bbf4f2a17c20c9273d3b9b81
before only heading/phase-board/Next/disposition bookkeeping. All reviewed code
and development clauses stay unchanged; r1/r2 findings/failures and historical
NOT_RUN text are preserved and explicitly superseded only for this exact no-START
result. Source/host, native ordinary16, fresh targeted build and root3 results
remain distinct. Actual Git publication is reported separately after commit/push
and remote equality verification; this disposition is not that Git evidence.

Next P04 gate is separately reviewed NamespaceInit/CLI FD nondelegation and
worker/job lifecycle integration; private no-job READY/lease retirement does not
prove live revision invalidation, executable authority or crash-old-job absence.
P06 temporary survivor protocol/core is local ACCEPTED only; its complete
reference4/module/adoption/timer integration remains separate development/review.
Original different-subject/direct-read matrix and all production/PATH/physical/
ARM gates remain open. No service/policy/install/RPM change; installedRelease15
unchanged. No overall completion signal is inferred.

## P04 no-START publication and STYLE-02 development sequencing

Published only the accepted nine-path RootStartup-r3 checkpoint as
07780c7d7e920e4212e224f811685d3a4e58da6c. Owner push succeeded and actual
refs/heads/main query equalled that full commit, with local/remote ahead-behind0/0.
Outside-project proof: worker-owner-root-publication-r1.json and the publication
manifest. InstalledRelease15 and other gates remain unchanged.

STYLE-02 scope-r1 is independently ACCEPTED for development sequencing only:
new WIP presentation first, separate global skill, mechanical C/C++ presentation,
concrete component-build map and package visuals. No broad existing-file moves or
mechanical rewrite has run in this checkpoint. Global skill exact2 independently
ACCEPTED: SKILL.md f5b57149e2fd413815c6c65e87876ac8070b63543f5a5d9b37441c59a606e0e2;
build-packaging.md3073a7b82e273168c6c68b750f1018bc168b0a668c235ed31fd0d42f0666fe92.
Skill-creator quick_validate PASS; source notices/spacing/component/resource
syntax guidance preserves project authority and all deployment exclusions.
No manifest/service/socket placeholders or Watcher policy/workflow were added.
CMake3.20 test-property directory ownership, fixed outputs/private variants/
optional generation dependencies and token/include/literal proof remain explicit
requirements for later STYLE-02 implementation.

## P06-READ-REFERENCE-MODULE-SURVIVOR-r1 local implementation (review pending)

Owner w1:pJ, reviewer w1:pA; phaseP06, R01/R11/R14 private lifetime/authorization
fixture, baseline07780c7. Accepted design-r2 and temporary core-r2 hashes remain
preserved in07/history. New separate fixed reference4/code3 delayed-load launcher
and native entry implement registered-zero-activity server and post-real-Cancel
reader holds. Supervisor has no SH; separate fixed pipe copies, independent
ACK/correlation barrier, exact adoption/nonexit/normal reap, sticky proof loss,
post/final ECHILD and pinned inert cleanup are source proposal, NOT runtime claims.
No current native/root invocation is authorized or performed.

Accepted core helper cf1dc5ba64836a022919cb368a829a5fe8292cbd974ee9965bb07f1ca7ec481a
is copied byte-for-byte into the repository; its ordinary20 tests only change
the source lookup path. Existing actual default/generated Cancel implementation
moves to private read_policy_native_support.hh, retaining original C entry10/20.
Reconstruction removes only the new guard, relative context include and
maybe_unused Server annotation for the second TU; phase2-spliced raw tokens,
including directive/literal spellings and include order, match all2159 original
tokens. Proof read-reference-module-native-reconstruction-r1.json. Initial proof
script rejected multiline whitespace dump formatting before comparison; the
corrected parser reads complete raw-token records. This was a diagnostic-harness
failure, not native compilation or product failure. Full Apache source notices,
readable include/declaration/namespace spacing apply to new WIP; no identifier or
lifecycle changes are hidden in that reconstruction.

Host Python focused-r1/r2 35/35 and source-loader-expanded-r3 38/38 PASS; host-r4 39/39 PASS; check-r2 FAIL12/13 because the new slot3 test assumed the
full-discovery interpreter had a free FD3 (actual owned lock5). No unknown parent
FD was closed. The final isolated driver starts with close_fds, uses a proven
inheritable linked3, requires exact rejected child reap before success-only _exit,
and keeps a nested uncertainty latch on timeout/nonzero. Two no-launch injection
cases prove no rmtree; test-only reset follows mocked subprocess only. Final
host-r5 41/41 PASS, then final host-r6 42/42 (core20 + mapping8 +
orchestration/reports/source14) including sticky known-wait timeout coverage, no
skips. That additional no-child fault test uses a TEST-only reset.
Actual fake-image exec checks stdio/borrowed4, nonblocking opposite pipe writers,
non-CLOEXEC high/slot3 causal close omission, independent EX exclusion after parent
SH close and actual normal known child reap/release. Fault cases inject no root
module: each causal phase failure, later known/empty proof after uncertainty,
ambiguous fork/pipe allocation, wrong body types/PID, separate barriers, output
errors, atime-only load versus stable-field/digest rejection before compile.
These are host ordinary/fake evidence, not module/TLS/surviving-role runtime.
Hold/topology C++4/4 PASS. Minimal launcher g++20 -Wall/-Wextra/-Werror syntax
check with a fixed never-loaded placeholder module path PASS; command did not
link/run platform code. Host check-r1 CTest13/13 PASS preceded the slot3 addition. Check-r2 failure is
preserved above; Check-r3 13/13 PASS; final check-r4 13/13 PASS covers the additional sticky
wait case against the frozen final tests. Both final focused42 and check exits0. Optional TIDL/Cynara OFF means neither
new native module nor launcher target is compiled by that host check. Native
compile/dependency audit, actual reference entry/loading, root server/reader
coordinator-loss and context/SH lifetime remain NOT_RUN/unapproved.

New native targets are explicit-build-only EXCLUDE_FROM_ALL and uninstalled;
existing target modes/check dependencies and output paths remain. Only the pure
hold test adds a CTest name; ordinary Python tests enter existing discovery.
No policy/journal/recovery CLI/SQL/Authorize/Confirm, package/install/StageB,
production activation, original different-subject/direct-read or ARM claim.
Next: frozen source/host review, then separately reviewed exact native artifacts
and non-policy/root methods. Launchpad session/signal/FD/namespace reference map
and separate STYLE-02 mechanical/layout work continue independently, without
mixing those lifecycle/build changes into this freeze.

### Survivor-r1 review findings and revised r2 local evidence (pending)

Reviewer verified original frozen18/18/archive and returned CHANGES_REQUESTED:
stop could quit before queued disconnect, and final-path existence did not prove
WriteRecord completion. R1 remains historical source/host evidence, not accepted
runtime. R2 uses the pure drain decision in the actual GLib timer: stop with live
services continues, actual empty completes before the unchanged budget, deadline
and errors fail. Six hold/topology tests include deterministic stop-before-empty
and late/error cases; no sleep-dependent native success is inferred.

New ready/body publisher completes `.next` write/mode/fsync/close before atomic
RENAME_NOREPLACE. Ordinary wrapper tests pause actual writes after creation and
after two bytes, proving only pending exists until release; completed final is
single-link600. Rename failure retains completed pending with no final; prior
final is never overwritten. Wrappers are ONLY on this ordinary test executable;
new native image has no hook. Initial host publication build-r1 FAIL -Werror
ambiguous else around EXPECT_EQ was corrected with explicit braces; build-r2
and tests-r2 3/3 PASS. No native compiler/runtime result from this host correction.
Coordinator tests additionally exercise empty/partial pending opportunities,
bounded failed publication and final read/decode crossing its deadline. Final
Python focused host-r7 45/45 PASS, no skips. Native/full survivor stays NOT_RUN.

Shared private Service now has one writer-function parameter defaulting exactly
to legacy WriteRecord; ReferenceFactory alone selects the new atomic publisher.
This declared constructor/field/call seam is an r2 source change requiring review;
it is not claimed to be byte-identical original support. After normalizing only
that seam plus earlier declared extraction changes, reconstruction retains all
2159 original ordered preprocessing/directive/include/literal tokens. Old entry,
policy/body checks, normal factory, Client10/20 and old WriteRecord behavior stay
unchanged. Proof native-reconstruction-r2.json is distinct from r1 proof.

STYLE-02 mechanical133 is disjoint, token-equivalent local WIP under its own
review scope; current host checks include both work cohorts. This is not an
isolated native source/package build. Survivor-r2 only adds its two publication
paths/tests and declared source/docs changes; no sessions/signals/remount or
broad style bytes enter its manifest. Global skill remains separately accepted.

Final r2 combined host check-r5-style-mechanical CTest14/14 PASS: original13 plus
ordinary publication test registration. It recompiles mechanically changed
project code and selects final survivor tests; it does not compile/run the
optional platform module. Final minimal launcher syntax-r3 exit0 is printed with
its exact argv and never-loaded placeholder; real native compilation remains
NOT_RUN. The mechanical cohort is separately token-verified and excluded from
the survivor manifest. No source-only verdict has been self-converted to native,
root safety, runtime, package or publication acceptance.


### Survivor-r2 local acceptance and fresh native target preparation

P06-READ-REFERENCE-MODULE-SURVIVOR-r2 local source/host ACCEPTED by the
independent reviewer against manifest20/20 SHA7a4d088e4ef64d55f27f486516f00a072c58cef0c17fa0a13ac9077a47efbcd6.
The two r1 drain/publication findings are closed for source only. Preserve accepted
07 SHA02a52977f0750396a15d6ebcf336fe9c2ea40a8adf9160d1ca4f0c11ca5821b8 and
08 SHA28f8c6d8604a2b8d7ca345966fa99a261c9d93c9c1f0845e5880a3fa312f2dd5;
this administrative evidence follows those frozen bytes. No final native/runtime
publication acceptance is inferred from that verdict.

STYLE-02 mechanical-r1 was separately accepted for local source/host scope:
exact133 files and captured baseline07780c7 originals, phase2-spliced Clang tokens,
original literal/directive/include order and nonempty lines match after only
notice/namespace-comment normalization. __LINE__ shifts only C-consumer failure
diagnostics. Full Apache notices retain genuine SPDX/attribution; no renames,
member/lifecycle/guard changes. Initial formatter handling of the C SPDX comment
failed, all captured originals were restored and the corrected script completed;
that is a script failure, not a product test failure. Combined host CTest14/14,
testing-OFF configure/build/zero-test map, temporary host DESTDIR/public header,
capability-manager.pc Name0.1.0, exactly12 versioned exports/SONAME0 are separately
reviewed host evidence. No native133/full-check/RPM/install/publication gate follows.
Global two-file skill remains independently accepted unchanged. The concrete
STYLE-02 build-layout map-r1 is accepted for development compatibility only; no
layout changes were applied during this native preparation.

Native compile-only archive202 is an explicit disjoint union: unchanged baseline
07780c7/49 + accepted mechanical133 + accepted survivor20. Every actual byte was
checked against its cohort or git-show baseline before archive creation. Archive
SHA38832632a20fc8e5fb7ece0ba1dbd722ebacaf34a3251a126aedde716e980cc7,
manifestSHAbde3acc0e131c1489235b820e43fdc3b469d61717e9fa1ed55e3d548842ab6a6.
Its archived08 remains the accepted original; this later append is not silently
substituted into the native source. Runner signal/session work is excluded.

read-reference-module-native-build-r1.log records rechecked emulator Linux4.4.35
x86_64, free326340KiB before preparation, new protected read-reference-module-r2
scope, FIRST clean env-i absolute Python-I-B, transferred3 hashes/metadata and
installed1800s/owner1830s compile-only bounds. Fresh native configure with all
platform requirements ON, GCC14.2/SQLite3.50.2, O1/parallel1 builds the generated
platform/transport prerequisites, new and original native modules/minimal
launchers and the two ordinary helper images. Six images freshly compiled, not
copied/direct-build artifacts. source202/JSON53 match before/after, remote0 and
transport0; no test or root fixture invocation occurred.

ELF audit: all six images have no RPATH/RUNPATH and clean ldd-r, 53 resolved trusted
provider hashes retained. Both minimal launcher startup closures are standard
runtime/loader only; platform constructors remain confined to the modules. Both
modules propagate PRIVATE JSON/PIC and link actual default Cynara/socket symbols;
generated existing IDs0..9/Confirm10 are checked. Fixed C entry symbols remain.
renameat2 linkage is present, but native RENAME_NOREPLACE behavior is NOT_RUN.
This target subset is not a whole native check, package build or runtime proof.

Fresh CMake artifact hashes:
- capmgr-read-policy-tidl-probe: SHA2d60928aa92d096d2bb893bf60cfdf4c01b760d491125f00ea97fd5c35f89ece
- capmgr-reference-module-probe: SHA513b1b4421cb7fa1806d6a81c28c67963b69099f8d66939549c69733e8599aee
- capmgr-survivor-hold-tests: SHA16b45830b090415acfec473130191a501124652bd244ebd2114c5b8fb1637824
- capmgr-survivor-publication-tests: SHA08757f574bd0f41a689c987b3af9f0b5336b63de190696a39023d5a24889b00b
- libcapmgr-read-policy-native-fixture.so: SHAea564dda268bba447481fde0f0d50c6096b10db5892105783ca38fa6ad6f16c4
- libcapmgr-reference-native-fixture.so: SHA5f41125b47c88bf7c41e566d8573e5bffb6ef306830ce2afb56d007bf5f713cb

Frozen ordinary-method-r1 selects only six pure C++ hold/drain tests, three own
file-publication tests and the exact three Python modules/45 cases (total54).
C++ module/launcher pins are checked but not invoked. Read-only preflight-r1
verifies transferred method metadata/hash and source202/JSON53/images6/providers53,
resolved named paths/interpreter/watchdog/protected ancestry/initial namespaces/
ext4/no-cache. Initial read-only preflight-r1 FAILED on the Cynara socket provider
hash: the outside generator globally replaced source-count digits after inserting
provider literals, corrupting one hash containing189. Transport0 had no remote
sentinel and did not establish preflight success. A preliminary owner progress-r1
note/request incorrectly called it0/0; it was immediately retracted, retained and
corrected here. No watchdog/test was invoked. Method-r2 restores the exact digest;
AST comparison checks all six image and53 provider literals against native build
artifacts. Fresh read-only preflight-r2 PASS/remote0/transport0 explicitly exits
before watchdog/tests. Verifier now prints remote1 on a preflight exception.
All r1 scripts/logs and the preliminary progress-r1 hash6f09180f03c0ea32045a41d4831272c653bc4ffb2e79625cb35dc6e4b7542650
remain evidence, not accepted execution. Native54 is NOT_RUN pending separate
exact method-r2 safety review. Root survivor,
old/new actual module modes/runtime, policy/SQL/real journal recovery/StageB,
production/install/ARM remain separate unapproved gates. No external automatic
cleanup on timeout or lost ownership. Next: exact native build/audit and bounded
ordinary-method review; continue disjoint STYLE-02 layout and LAUNCH-SIGNAL work
under their development contracts without changing this frozen archive.


### Survivor-r2 ordinary native54 result (pending independent final review)

Reviewer accepted exact NONPOLICY-METHOD-r2 pre-execution only after its corrected
read-only preflight. read-reference-module-native-nonpolicy-tests-r2.log prints
the exact FIRST clean short verifier/installed60s/owner90s invocation, transferred
method2 metadata/hash checks and immediate pre/post source202/JSON53/images6/
providers53/interpreter/watchdog/protected ancestry/initial namespace/ext4/no-cache
checks. Hold6/6 and publication3/3 PASS, no skips; each binary return0. Python exact
three selected modules45/45 PASS, no skips in0.159s, return0. Total54 is distinct
from host CTest14 or a full native check. ORDINARY_SURVIVOR54_ONLY_PASS, remote0/
transport0; no retained/timeout/failure marker. No C++ launcher/module is invoked.

The native publication experiment now measures target RENAME_NOREPLACE support:
actual paused-empty/partial writes leave only pending until checked publication;
failed publication retains its own pending file, and existing final is not
replaced. Test-only wrappers never enter the native module. Source-enforced fake
image exact-table/known reap and inherited TEST-SH/independent EX assertions are
ordinary child/reference evidence; mocked coordinator/adoption/source-loader
faults remain fake evidence. No actual module/context/RPC/coordinator-loss or
native TLS/kernel-exit inference from these54. Original read-only r1 FAIL and
retracted preliminary report remain preserved. Final ordinary native/publication
review pending; full root survivor remains NOT_RUN/unapproved and its exact
method is prepared separately without invocation. Continue the isolated CMake
layout compatibility work and disjoint signal handling development.


### Survivor-r2 ordinary54 accepted; separate root method prepared

Independent reviewer ACCEPTED the exact local source plus targeted ordinary54
native evidence/current08 only. Preserve accepted progress-r3
SHA7a977217b7c53da43540e3c944bb2cdc1c08ac11bd44fdded8d53415d8eb4eb4
and original source/07/08/method/build hashes. This supersedes ordinary-only
pending status; it does not accept full root runtime/publication, full native
check or STYLE-02 final native/package gate.

ROOT-METHOD-r1 freezes the same source202/launcher/module/provider pins, the
unchanged accepted preflight and a separate fixed runner/verifier/outer/plan.
Read-only root-preflight-r1 performs protected transferred checks and full
preflight PASS remote0/transport0, explicitly before watchdog/source/image entry.
No full root experiment was executed. Proposed invocation is only the fixed
supervisor --run plus exact launcher: sequential registered-server and post-RPC
reader inert TEST reference experiments, installed120s/owner150s. The root plan
keeps independent ACK/correlation versus final reap, no-SH supervisor, reader-case
server retirement before coordinator loss, P_PID adoption/nonexit, sticky proof
and ECHILD/pinned cleanup conditions. No original old-mode runtime is inferred.
Full root survivor remains NOT_RUN/unapproved pending that separate exact method
review. No real journal/recovery/policy/SQL/Authorize/Confirm/StageB/install or
production authority. The isolated STYLE-02 layout source is independent; this
accepted native archive and actual repository source cohorts remain unchanged.


### Survivor-r2 full-root-r1 measured FAIL; server phase separate pass

Independent reviewer accepted exact ROOT-METHOD-r1 pre-execution only. The owner
rechecked method3/3 and outer/plan2/2 before invoking it. Actual root-tests-r1.log
prints approved FIRST clean env-i absolute Python-I-B, installed120s/owner150s,
transferred method checks and immediate source202/JSON53/images6/providers53/
provenance checks before/after. Overall ROOT_SURVIVOR_RETURN1, remote1/transport0;
no full fixture PASS or timeout. This is FAIL, not publication acceptance.

Server scope iWXvoL separately reaches SURVIVOR_BARRIER_READY=server, actual known
coordinator reap-9, exact adopted nonexit before and after neutral EX contention,
actual normal adopted reap0, then independent EX availability. Checked
REMOVED_REFERENCE_SURVIVOR_SCOPE precedes server CASE_PASS. Zero-activity Listen/
drain, reference retention/context/table and both empty barriers are source-
enforced assertions, not separately printed raw syscall details.

Reader scope iCNk4Q is explicitly RETAINED. Its unexpected-native-diagnostic error
occurs before reader barrier, coordinator-loss proof or reader CASE_PASS. The
rejected frame bytes are not printed by this source, so this log alone does not
identify the native failure stage/exception or a Cynara decision. Known-child
cleanup is the source's scoped failure handling; no printed blanket absence or
full cleanup proof is inferred. Sticky failure retains the inert scope/inventory;
no external removal, image/context/FD/policy repair or automatic recovery follows.
The overall failure does not invalidate the completed server phase's removal.

Next: bounded diagnostic refusal reporting/host regression as a narrow frozen
source revision, then separate revised native/method review before another run.
No rerun is approved by this failure. Original source20/ordinary54/build/method
hashes and accepted administrative progress-r4 remain preserved. The new layout
host work remains isolated and does not enter source202. Real journal/CLI/policy/
SQL/StageB/production/install/ARM gates remain open.


### Survivor-r3 bounded refusal observability (local review pending)

Reviewer confirmed full-root-r1 overall FAIL, server-only passing evidence and
retained reader scope. The diagnostic-classification rejection alone does not
identify a native stage. R3 changes only that refused diagnostic's error detail:
fixed mode/origin, original byte length, repr of at most256 bytes and explicit
truncation. No strict UTF-8 decode/raw controls on refusal. The original exact
origin/prefix acceptance predicate, required independent reports, sticky ownership
and cleanup gates remain unchanged. A REAL_GATE_NOT_PROVED candidate is still
refused, never an authorization positive or a specific Cynara verdict.

Four deterministic negative cases cover unsupported diagnostic, wrong-origin
known prefix, invalid UTF-8/control bytes and bounded truncation. They establish
no barrier/authorization progress and preserve escaped source bytes as data.
Focused host-r8 actual49/49 PASS with no skips (previous45 + four); final host
check-r6 diagnostic CTest14/14 PASS. No C++ source, entry, policy, FD, context,
loader, image or CMake behavior changes in this revision. New runtime remains
NOT_RUN/unapproved until exact revised frozen method review. No existing retained
scope is cleaned, and source revision cannot reopen the original full-r2 FAIL.


### Survivor-r3 local accepted; native diagnostic source/method prepared

Independent reviewer ACCEPTED the exact20-file local source/host diagnostic
amendment, preserving accepted08 SHAa39372e9de4b085744ff2f91dcdd621f1550273e866606d297b859940ca72b62.
No native rerun/publication follows from that source disposition. The original
root-r1 FAIL, separate server-only pass and retained reader iCNk4Q remain intact.
No refused-frame contents or native-stage/Cynara cause are inferred retrospectively.

Native preparation-r3 creates only a new protected source tree, with source202
archive6758a7ab93bc0c8cdb6a4397a805fd170555de1b6b892fca449a82c8fd7c8b07
and manifest3ab766bca31f8439b18984f43a366dbdd40be2b79e917f0bb663c2434f582e2c.
The exact union remains baseline49 + mechanical133 + survivor20. Only supervisor
Python/test Python/historical08 differ from original native build-r1; all199
remaining members including every C/C++/CMake compiled input match. No compilation
or image copying occurs. All six original protected r2 images and53 providers
are checked. The reused launcher retains its compiled r2 module path; that exact
path/module digest and trusted ancestry are explicitly pinned, not described as
a new r3 module or fresh compilation. No retained scope is accessed or removed.
Preparation log records remote0/transport0 and explicit no test/image/module/role.

NONPOLICY-METHOD-r3 selects only the three exact Python modules49 tests, installed
60s/owner90s. The original unchanged C++ hold6/publication3 evidence remains
separate. FIRST clean env-i absolute Python-I-B/source-only checked verifier,
transferred method metadata/digests and pre/post new/old source202/archive pins,
JSON53/reused images6/providers53/interpreter/watchdog/namespace/ext4/no-cache
checks remain. Read-only nonpolicy-preflight-r3 PASS remote0/transport0 explicitly
exits before watchdog/test/role invocation. Native49 remains NOT_RUN/unapproved
until the separate exact method disposition. Full diagnostic root rerun requires
its own later method review; no external cleanup, context/policy/FD repair,
real journal/CLI/SQL/StageB/production/package/installed result is authorized.

STYLE-02 isolated8-file build-layout local/host checkpoint is independently
ACCEPTED. It remains outside the actual repository while the frozen P06 source
union is prepared. Applying it later explicitly supersedes the two survivor
CMake paths in a new declared union, followed by actual-tree compatibility checks
and separate native optional/REQUIRE evidence. The old survivor manifests are
historical exact snapshots, not claims about future changed CMake bytes.


### Survivor-r3 exact native Python49 PASS; diagnostic root method pending

Independent reviewer ACCEPTED NONPOLICY-METHOD-r3 pre-execution only. After exact
method3/3 and outer/plan2/2 recheck, native-nonpolicy-tests-r3.log records the
approved FIRST clean env-i absolute Python-I-B, installed60s/owner90s, transferred
checks and immediate pre/post new/old source202/JSON53/reused r2 images6/providers53/
provenance checks. Exactly49 Python tests PASS with no skips in0.158s, native
Python return0, ORDINARY_DIAGNOSTIC_PYTHON49_ONLY_PASS, remote0/transport0. No
C++ launcher/module/own-context/RPC image is invoked. Ordinary real fake-image
child/pipe/TEST-lock assertions and mocked orchestrator/diagnostic faults stay
separate. Original unchanged native C++9 evidence remains historical accepted
r2 evidence; this is not a new54/58 or full CTest claim. Final native49 review is
pending; original full root FAIL and retained reader remain unaffected.

Diagnostic ROOT-METHOD-r2 separately freezes supervisor r3/source202 in the new
protected source root and explicitly selects original unchanged r2 launcher and
its compiled r2 module path. No C++ rebuild/copy or behavior/context/policy change.
Fixed server then reader cases, installed120s/owner150s, exact barriers/ownership/
reap/absence/pinned cleanup and stop-on-first-failure remain. Fresh read-only
root-preflight-r2 records transferred metadata/hash and complete preflight PASS
remote0/transport0, explicit no watchdog/test/image/role invocation. Full root
r3 runtime remains NOT_RUN/unapproved pending separate exact method review. The
new bounded refusal can reveal a future refused prefix without accepting it or
retrospectively establishing the old frame/stage. Retained iCNk4Q remains untouched.

After independent layout source acceptance, only its eight frozen CMake paths
were applied to the actual tree. The two overlapping survivor CMake entries are
explicitly superseded in the working tree; accepted survivor archives/source202
and device trees remain historical exact snapshots. Actual-tree before/after
semantic capture matches67 targets and14 CTest commands/env/cwd/order. Actual
host check is in progress; no native optional/REQUIRE/package result is inferred.


### Survivor-r3 diagnostic root-method-r2 overall FAIL; reported client stage

Independent reviewer ACCEPTED native Python49 evidence and separately exact
ROOT-METHOD-r2 pre-execution. Preserve accepted08
SHA458842b8eaa3a77da2b91bf299e7037d50e477ccbee87dbb6650a47c221fa27e.
Owner rechecked root method3/3 and outer/plan2/2 immediately before invocation.
Native-root-tests-r2.log prints approved clean startup120s/150s, transferred
checks, pre/post source202/JSON53/reused images6/providers53/provenance PASS.
ROOT_SURVIVOR_RETURN1, remote1/transport0; no timeout/full fixture PASS.

Server wnES7j again separately reaches barrier, actual known coordinator reap-9,
exact adopted nonexit before/after independent EX contention, actual adopted
normal reap0, EX available, pinned removal before server CASE_PASS. Source-enforced
zero-activity/drain/reference/context/absence conditions remain distinct from
printed markers. This second server pass does not establish a passing reader.

Reader B29oJu is explicitly RETAINED before reader barrier or CASE_PASS. The new
bounded refusal reports mode reader/origin role/original129 bytes/truncatedFalse,
with an escaped prefix containing REAL_GATE_NOT_PROVED role=system301-platform
stage=client-connect native=InvalidIOException specific_Cynara_decision=NOT_OBSERVED.
This is the actual refused frame's reported native stage/exception, not an
identified underlying syscall/errno, authenticated positive or Cynara denial.
The prefix predicate correctly remains rejecting; no ACK/body/teardown/reader
survival/cleanup proof is inferred. Neither new B29oJu nor old iCNk4Q is externally
accessed/removed/signalled or repaired. Original r2 full failure/server-only result
and original hashes/logs remain preserved. Next: independent failure evidence
review and local routing/source investigation; any changed source/method or
further invocation needs separate frozen review. No policy/SQL/real journal/
recovery/StageB/product/package/installed authority follows.

STYLE layout actual-tree check completed CTest14/14 PASS11.82s, with final target67/
CTest14 semantics equal to before application. Separate native layout source207
freezes baseline48 + mechanical133 + survivor18 + layout8, explicitly superseding
three existing CMake members and adding five; archive08 stays the historical r3
snapshot, current admin08 separate. Native layout compile/configuration and final
style/publication/package evidence remain pending, with no root workload implied.

### LAUNCH-SIGNAL-r1 isolated implementation/host evidence (review pending)

Owner w1:pJ, reviewer w1:pA; P04/R10/R11/R12, accepted Launchpad route-map-r1.
Separate behavior scope: runner.cc, cli_fixture.cc, new isolated signal driver,
test parent/fixture CMake and additive07/08 contract/evidence. No runner.hh change,
no member/lifetime/catalog/style133 changes; declared layout8 bytes are historical
snapshots and the two test CMake paths now additionally carry this separate behavior.
Frozen style native207 excludes these new signal bytes and remains independent.

Parent unsupported SIGCHLD/no-auto-reap refusal is before pipes/spawn; child empty
mask/eight defaults/group attributes are checked without changing parent state.
Original SID-inherited/PGID=PID behavior remains. Dedicated ordinary driver uses
only its own signal setup, test-target-only spawn/attribute/wait wrappers, no policy
hook; child fixture reports actual tuple/signals/high-FD metadata. Four attribute
faults and parent IGN/NOCLDWAIT require zero spawn calls plus final ECHILD. Normal
requires correlated positive PID/actual exact waitpid normal0 and observed child
mask/defaults/PGID/SID, before final ECHILD. Exec failure retains full parent state.

Direct host driver initial/r2 compilations PASS; isolated-r1 eight cases PASS;
existing Cli/Rpc focused9/9 PASS. The CMake-owned driver adds eight ordinary tests,
so final check-r2 CTest22/22 PASS11.48s (existing14 +8), not a changed native check.
A final source-presentation-only pass and matching build/check are recorded in
later final evidence, rather than claiming older binary bytes are those edits.
Full Apache notices/readable include/namespace boundaries apply to these behavior
files excluded from mechanical133; no style baseline is silently rewritten.
Local source review, native compile/test and publication remain pending. No root
workload/worker image/NamespaceInit/remount/policy/service/package/install or setsid
result follows. Parent stable disposition/exclusive waiter and trusted executable
selection remain explicit premises.


### LAUNCH-SIGNAL-r1 final frozen host bytes

Final source-presentation/check-r3 CTest22/22 PASS11.34s. After the final explicit
string_view include, the CMake-owned driver rebuilt and registered8 verbose cases
PASS with no skips in0.02s. Normal prints PID1074447/PGID1074447/SID1074393,
SID_INHERITED=true and exact OWNED_CLI_REAP_PID1074447 STATUS0. These are host
observations, not native or production authority. Both unsupported parents and
all four attribute faults print SPAWN_CALLS0; exec failure prints1 with final
source-enforced ECHILD. Full parent-state preservation/high-sentinel absence remain
source assertions. This is a seven-file local source/host review request; source
acceptance/native/package/publication pending, no new root workload. Source207
style native compilation proceeds independently from frozen pre-signal bytes.


### LAUNCH-SIGNAL-r1 local source accepted; STYLE native audit correction

Independent reviewer ACCEPTED the exact seven-file LAUNCH-SIGNAL-r1 local
source/host checkpoint, manifest e0abf3ab622762923edd054182fb73edadece978a05e1f5d1a9343824bfd4afc.
Native signal compilation/test/publication remains pending; the eight defaults and
inherited SID do not establish setsid or recertify broader RunCli containment.
Preserve the source archive and host observations as historical exact evidence.

STYLE layout source207 native-build-r1 freshly configured the protected tree with
platform transport and both REQUIRE options ON, compiled default ALL plus fixed
optional fixture targets at O1/-j1, and printed eleven image hashes. The subsequent
outside-project audit unconditionally rejected the C consumer's intentional build
RUNPATH. Preserve the complete run as FAIL, remote1/transport0, not compiler or
runtime failure. The owner wrapper's later artifacts pull also failed because the
artifact file was not created; transport0 alone was not a remote success sentinel.

Separate native-audit-r2 reuses those exact eleven compiled images without a
rebuild, source207 unchanged. Only the C consumer may have its exact sole protected
build RUNPATH; both SONAME symlinks and actual ldd-r build-library resolution are
checked, with the library digest pinned before audit. Every other inspected image
retains no RPATH/RUNPATH. Audit-r2 checks protected source207/JSON53 and all eleven
image pins before inspection, resolved provider metadata/hashes, generated method
IDs, actual default Cynara/socket/entry symbols and both minimal launcher closures.
ABI12/SONAME0 passes. Final source207/JSON53 checks and audit-only marker pass,
remote0/transport0; no test/consumer/launcher/module/root fixture is invoked.
This is fresh compilation followed by corrected audit evidence, not a second
fresh build, full native CTest, package or installed result. Exact ordinary native
method and final STYLE mechanical/layout/publication review remain separate.

P06 routing engineering direction prefers an unchanged-image separately bounded
read-only dlog collector, conditional on usable pinned CLI/provider inputs. No
collector or further root fixture is invoked by that direction. Failed reader
frames did not externally emit an exact attached-reader PID; endpoint/time and
library-reported PID can provide diagnostic correlation only. Retained iCNk4Q and
B29oJu remain untouched. Original connect errno must not be inferred from an errno
logged after library sleep/fcntl. A separately frozen collector/fixture method
requires explicit pre-execution review.


### LAUNCH-SIGNAL native target compile/audit and exact method preparation

Independent reviewer ACCEPTED STYLE source207 scoped compile/audit-r2 evidence
and administrative08 SHA850fcf11588ef52768eac4798918dca4bde949a346c561c34597e588c522b8d0,
not native tests or final publication. That hash and both native audit failures
remain historical. Separate signal native source208 is exactly baseline46 +
mechanical133 + survivor16 + layout6 + signal7; historical signal source08 is frozen,
current admin08 separate. Only the signal driver/CLI fixture target closure was
freshly compiled with O1/-j1 in its new protected tree, not default ALL/platform
modules or full native22. Build-r1 FAIL remote1/transport0 occurs in copied artifact
serialization after compilation/audit: it expected an unselected TIDL generated
directory. It is not a compiler/runtime failure or evidence TIDL was generated.
Audit-r2 reuses the exact two images without rebuilding, checks source208/JSON53,
protected images2/providers6, no RPATH/RUNPATH/clean ldd-r and final source checks,
remote0/transport0. Generated scope is explicitly empty/unselected.

Frozen native-method-r1 selects only the eight registered isolated signal cases,
FIRST clean env-i absolute Python-I-B/protected source-only verifier, installed60s/
owner90s, pre/post source208/archive/JSON53/images2/providers6/interpreter/watchdog/
initial namespaces/ext4/no-cache and exact registration/command/cwd/TIMEOUT checks.
Read-only native-preflight-r1 PASS remote0/transport0 explicitly invokes no driver
or CLI. Actual native eight-case execution remains NOT_RUN/unapproved until its
separate method disposition. No setsid, root worker, namespace/remount, context
Drop/module/RPC/policy/package or broader RunCli containment conclusion follows.

### P06 dlog candidate preparation measured refusal; collector remains unavailable

Independent reviewer ACCEPTED exact DLOG-CANDIDATE-INVENTORY-r3 preparation-only
method after provenance/budget correction and host pure8/8. R1/R2 remain preserved
and unrun. Owner rechecked all four frozen hashes, then only the approved fixed
standalone inventory ran under installed30s/owner60s with clean isolated startup.
Transferred source and pinned interpreter/watchdog/initial namespace/ext4 checks
pass. Candidate /usr/bin/dlogutil metadata reports UID/GID1901, mode0750 and a
security.capability xattr; the unchanged trusted-root/no-cap code predicate rejects
before candidate data read or ELF dependency walk. CAPMGR_REMOTE_EXIT1 and
TRANSPORT_EXIT0, no timeout. Preserve complete native-inventory-r3 log as FAIL;
this is a measured candidate prerequisite refusal, not usable-collector PASS.
No collector/help/flags/log reads/config changes or fixture rerun was executed.
No ownership/capability repair, substitute binary or package action is authorized.
Retained iCNk4Q/B29oJu remain untouched; client IO branch/errno/Cynara remains
unidentified. Next diagnostic direction is the separately reviewed new-only own
API-path/socket/lifetime observation fallback; no implementation/runtime acceptance
is inferred from this preparation failure.


### LAUNCH-SIGNAL-r1 exact ordinary native8 measured PASS (final review pending)

Independent reviewer ACCEPTED source208 target compilation/corrected audit and
exact native-method-r1 execution safety. Preserve accepted08
SHA71263a3d62e3faff7ca1ac777dd6cb964cd6ebe5c1a10f1fe3f2ee5bb78b7175.
Owner rechecked frozen method3/3 and outer/plan2/2 immediately, then only the fixed
registered eight ordinary cases ran. Native-tests-r1 prints FIRST clean startup,
installed60s/owner90s, transferred method checks and immediate pre/post source208/
JSON53/images2/providers6/interpreter/watchdog/ns/ext4/no-cache/registration8 PASS.
CTest8/8 with zero skips/failures in0.02s, NATIVE_SIGNAL_CTEST_RETURN0,
NATIVE_SIGNAL8_ONLY_PASS, remote0/transport0; no timeout/retained result.
Normal reports actual PID3299278=PGID3299278, SID3299274 inherited and exact
OWNED_CLI_REAP_PID3299278 STATUS0. Parent IGN/NOCLDWAIT and four attribute fault
cases report SPAWN_CALLS0; exec failure reports1. Full parent mask/disposition
preservation, child empty mask/eight defaults/high sentinel absence and final
ECHILD are source-enforced assertions, not additional raw syscall traces.
No setsid/session topology, root fixture, Drop/module/RPC/policy or broader RunCli
containment is re-certified. Host22 remains separate from selected native8.

Reviewer noted native-inputs-r1.json inherited source_count202 while count/files
and actual archive/method correctly208. Preserve original JSON
SHAbe11897f5cb53af89722a20c04a38fe1351e60c6630965a6939c6bb4828a17dd.
Versioned native-inputs-r2.json corrects only source_count to208 plus an explicit
correction note, SHA202e82e62c23f338249fdab9ea96194284aece767be38357c18fb55109e5a232.
No source/archive/image/method change or rerun follows. Exact runtime/admin
publication review remains pending; no checkpoint is staged/published yet.


### LAUNCH-SIGNAL-r1 final scoped review ACCEPTED; publication ordering pending

Independent reviewer ACCEPTED the exact seven-path signal checkpoint and native8
evidence, preserving accepted08 SHA6fa233259b97f61cd5c6e4c82e8849a7bda9a04a9930106921b73d9ca0ae3964.
This is not setsid/session, broader RunCli containment or a production gate.
No checkpoint is staged/published yet: shared CMake/docs require a coherent reviewed
mechanical/layout/P06 baseline; publishing a broken intermediate tree is excluded.
Native-inputs-r2 count correction and prior build/audit failures remain historical.

### P06 REFERENCE-ROUTE-OBSERVATION-r1 local diagnostic implementation (review pending)

Owner w1:pJ, reviewer w1:pA; P06/R13 private diagnostic, exact eleven paths.
Accepted development design SHAab305f5262e03a40c5a35c03b2cf3b17ed9f1f636c4e30ae36354d8f181bad27.
New-only module reports finite bounded own-name/fixed AUL path/lstat snapshots:
server Listen/terminal lifetime counters, independent client rawUID301 preconnect/
native-failure query with actual proxy target/path NOT_OBSERVED. Entire literal and
sun_path length must match before lstat; API status and immediately saved lstat
errno never identify a failed RPC syscall. No mutating target-UID diagnostic call,
alternate pathname inspection or new connection. Arbitrary bytes are bounded hex;
all returned strings freed and observation exceptions contained with failed state.
Original stage/native exception are saved before observer/teardown. The default-null
shared Client seam is reversed in reconstruction-r2 to recover exact accepted
old support SHA2681cd395a17e8224e0df945a92a444ca034e3fb8a5938f857ad65f78aee50a2,
including current formatted bytes; old Client10/20/C-entry/policy route unchanged
apart from that explicitly declared fixture-only seam.

Supervisor validates fixed origin/mode/phase/type/hex/cardinality schemas (one per
phase, at most four); informational/error frames confer no ACK/body/readiness or
ownership authority and never reset budgets. Known spawn diagnostic prints only
after the existing attached-record report; wait/adoption proof is unchanged. Existing
unsupported REAL_GATE_NOT_PROVED rejection and sticky failure remain. Reference
native module adds a required PRIVATE imported AUL dependency; it is optional on
this host and remains NOT_COMPILED/NOT_RUN here.

Host helper6/6 PASS (helper-r2), Python56/56 PASS (host-python-r2), final combined
CTest23/23 PASS11.54s (host-check-r2), all exit0. These include separately reviewed
style/layout/signal/P06 cohorts, not isolated native evidence. Initial target build
before reconfigure failed with No rule to make target; target-build-r1 is preserved,
then configure-r1 and target-build-r2/r3 succeed. Earlier host-check-r1 PASS preceded
the final schema/presentation tightening and is not substituted for final r2.
Pure C++ tests cover bounded/null/overlong/arbitrary-byte strings, no inspection on
bad literal/length, saved lstat errno and metadata-only reporting. Seven new Python
cases cover information/error without barrier/ledger progress, repeated/wrong-origin/
wrong-type/independent-query violations and lifetime counters after service removal.
They use fake/injected data, not actual AUL/path/context/RPC observations.

The eleven-file freeze explicitly supersedes current shared test/CMakeLists.txt,
test/fixtures/CMakeLists.txt and tidl/fixtures.cmake layout/signal paths plus changed
survivor supervisor/test/module/shared support and administrative docs. Prior frozen
survivor20/style/signal archives remain historical unchanged, not current-unchanged
claims. Mechanical133 remains exact. No native compile, actual AUL/provider linking,
module/root diagnostic rerun or final publication accepted yet. Both overall root
FAIL logs/server-only passes and retained reader scopes iCNk4Q/B29oJu stay preserved
and untouched. No policy/context/FD repair, collector, real journal/recovery, SQL,
Authorize/Confirm/StageB/install/production authority follows.


### P06 REFERENCE-ROUTE-OBSERVATION-r2 supervisor error-latch correction (review pending)

Preserve r1 manifest66be1b77b0b0b0d5327894ce8abda331927353e2d9fc235f0bc172521071d605,
archive328d51f4bcafe04ccceb60549bdb4c61bc8ed4574f7cfbac1e39bc1295f93414
and original08 SHA36f674534726bebc3832918df1d7517829b55b2ac9f2b4646ef05b96d90ac28a.
Independent review CHANGES_REQUESTED: valid observation-error frames were validated
but did not poison supervisor readiness/retirement. R2 preserves the validated
bounded error diagnostic then raises RouteObservationFailure through existing
experiment/RealProof exception handling; RouteRecords failure is permanent. Tests
feed a valid error before spawn followed by all valid proof frames (raw barrier
inputs valid but supervisor readiness stays false), and error after readiness in
actual RealProof output drain. The SAME retirement ledger remains uncertain after
later mocked exact reap/ECHILD/endpoint success, with zero remove/no CASE_PASS.
Ordinary informational frames still grant no proof. Native timer additionally
rechecks its original reader-server deadline after terminal observation, without
changing the intentionally different server-hold expiry rule; this optional native
branch remains uncompiled/unexecuted on host. Six pure helper tests cover text/
lstat helpers, not actual AUL allocation/status/module entry.

Final host-python-r6 actual58/58 PASS0.233s; host-check-r4 CTest23/23 PASS11.48s,
exit0. Preserve host-check-r3 FAIL: an owner command mistakenly executed an edit
generator in place of unittest, duplicating the validator; frozen r1 source was
restored and the narrow correction reapplied. Empty host-python-r3 is not a test
result. Host-python-r4 initially failed in the new test by replaying an already
recorded coordinator reap; corrected test applies only later missing proofs to
the SAME ledger, with final r6 passing. Earlier helper6/6 remains unchanged.
No source/native/root acceptance or rerun inferred; frozen r2 requested separately.
Old Client reconstruction-r2 still applies because shared support is unchanged.
Retained iCNk4Q/B29oJu untouched and prior full FAILs preserved.


### P06 route-observation-r2 local source ACCEPTED; native preparation next

Independent reviewer ACCEPTED frozen11 manifestc478885e37e063312d536156534de773fdc85271c7faebe084c25c628a69b902
and archive89b2574af440bedc09eb32233eeb7b0577a698a50ccb894272302f979a741ff2,
local source/host only. Preserve accepted08
SHA2c39d8b73e1d24ee63e6bf69c22fe2132b16c4b5ad7afd2fbf77e852448fcf80.
Native compile/provider audit and bounded ordinary method precede any new root
diagnostic request. Retained scopes/FAILs unchanged; no runtime or publication
acceptance inferred. New source union will explicitly declare source208 plus
two new helper/test files and eleven reviewed overrides (210 total), separate
from the unchanged source207 STYLE device tree.

### STYLE-02 ordinary-method-r2 preparation (execution NOT_RUN/pending review)

Accepted source207 compile/audit remains unchanged. Preparation noticed ordinary
unit/adapters exec sibling capmgr-sqlite-lock-probe, absent from the previous
eleven-image artifact list. Additional read-only probe-audit-r3 checks existing
root0700/noACL/no-cap code digest9af692bffa451e15ace1368d9f5cf950f57f6c0b3cb3484ff7c3a3297e9a6d32,
no RPATH/RUNPATH, clean ldd-r and standard closure against existing provider pins.
PASS remote0/transport0 with no test/image execution/rebuild. Artifact-r3 adds
only this image/closure to r2, giving images12/providers56. Probe-audit-r1 failed
transport1 (service name too long; no remote result); r2 reached code-metadata
assertion and failed because its expected0755 was overstrict for actual owned
umask077 root0700. No chmod/permission repair: r3 checks actual0700; prior failures
remain FAIL, not retroactive PASS.

Frozen ordinary-method-r2 selects registered unit/C-consumer/adapters3 only with
--no-tests=error/verbose and real metadata required; expected unique123+201 Google
Tests plus consumer, direct C repeat and12 CAPMGR_0 exports. BEFORE/AFTER checks
include source207/JSON53/images12/providers56, both spawned sibling images and
project library, SONAME chain/exact sole build RUNPATH/C-consumer resolution,
interpreter/watchdog/initialns/ext4/no-cache/exact registration/cwd. FIRST clean
env-i absolute Python-I-B/source-only protected verifier, installed180s/owner210s.
Read-only ordinary-preflight-r2 PASS remote0/transport0 explicitly invokes no test
or consumer/module. Initial preflight-r1 FAIL remote1/transport0 rejected the
root-managed hardlinked system readelf tool; r2 allows regular hardlinks only for
four trusted nonwritable system audit tools, retaining singlelink on all project
source/images/methods. No test invocation yet; separate pre-execution review pending.
No full native14/23, optional module/root fixture/policy/operational DB/package
or installed claim; signal/route WIP not copied into this source207 tree.


### STYLE-02 exact ordinary native3 measured PASS (final review pending)

Independent reviewer ACCEPTED exact ordinary-method-r2 safety (method3/outer-plan2),
then owner rechecked all hashes immediately before the fixed invocation. Complete
ordinary-tests-r2 log prints FIRST clean env-i absolute Python-I-B/source verifier,
installed180s/owner210s, transferred2 and pre/post source207/JSON53/images12/
providers56/project-library-resolution/provenance/exact registration checks.
Actual selected CTest3/3 PASS5.67s, unit123/123 + adapters201/201 =324 unique
GoogleTests, plus C consumer; no skips/failures. Direct C repeated exit0, exactly12
capmgr_@@CAPMGR_0 exports, STYLE_ORDINARY_CTEST3_ONLY_PASS/remote0/transport0.
Real metadata User::Shell with USER_METADATA_XATTR=UNSUPPORTED is recorded; injected
labels are not actual worker-policy authorization. Owned probe/reap/lock assertions
remain source-enforced evidence, not new syscall traces. No full native14/23 or
optional module/launcher/root fixture was selected. Prior build-r1 FAIL, audit-r2
reuse, probe audit and readelf hardlink method failures are preserved unchanged.
Final scoped native/admin/publication review requested separately; no staging yet.

### P06 route-r2 native configure prerequisite FAIL; private SDK proposal pending

Exact source210 archive5502389a3615ccc498df85225d35178d863dbe35fb9cd1186e76d6356510623b
manifestb8583e4a270a3ed9e52db4bd40b493fd6e2e720f77806e4626f8b6e82bb18741
is source208 frozen union plus accepted route-r2 eleven overrides/two new files.
Fresh protected read-reference-route-r1 compile-only attempt validates source210/
JSON53 then fails CMake REQUIRED pkg-config aul at configure, before C++ compilation
or image creation. Native-build-r1 remote1/transport0, no timeout/test/module/role
invocation. Local source acceptance unchanged, no compiled/runtime PASS.
Read-only checks show AUL RPM0.83.17-1 and pinned runtime provider present, while
/usr/include/aul, development libaul.so and aul.pc are absent. Private SDK proposal
SDK5 freezes four unmodified official00ea297ff05c7dd6ca360b3cc4239504fbd24e40 headers
plus explicit diagnostic-only pkg-config metadata requiring bundle and the absolute
installed libaul.so.0.83.17 pin. No library copy/substitution/installation or system
SDK repair; original source/version is not exact installed binary provenance.
Compile-only retry/method prerequisite review pending. Retained scopes untouched.


### P06 route-r2 exact targeted native compile/audit-r2 (review pending)

Independent reviewer ACCEPTED only the private SDK5 compile prerequisite, official
headers00ea297 and diagnostic metadata, no test/runtime. Fresh protected
read-reference-route-r2 verifies exact source210/JSON53/SDK5 and installed AUL pin
before configure with build-process-local PKG_CONFIG_PATH. Targeted O1/j1 freshly
compiles new reference launcher/module, old real-policy launcher/module, hold,
publication and route helper; no default ALL or image invocation. Native-build-r2
remote0/transport0. Seven image hashes,53 provider closures, clean ldd-r/no RPATH/
RUNPATH, real generated IDs/default Cynara/socket/entry/PIC/minimal launcher checks
pass. Printed compile_commands binds new observer to protected SDK include/aul;
old module is freshly compiled without that include and retains default-null seam.
Actual link command contains absolute pinned libaul.so.0.83.17; new module ELF
DT_NEEDED explicitly libaul.so.0, resolving to pinned installed runtime provider.
No assumed equivalence to the missing official pc. SDK headers/manifest/library
and source/JSON pins checked again afterward. Complete native-build-r1 configure
FAIL remains distinct, not retroactive PASS. No module entry/AUL API execution/
Drop/NSS/context/RPC/root survivor test occurred.

New frozen ordinary-method-r1 selects ONLY C++ RouteObservation helper6 plus exact
three Python core/spawn/supervisor modules58, total64/no skips. Current source
210/archive/JSON53/SDK5/images7/providers53/protected ancestry/interpreter/watchdog/
initialns/ext4/no-cache pre/post, FIRST clean absolute Python-I-B/source-only
protected verifier, installed60s/owner90s and fresh logs. Read-only native-ordinary-
preflight-r1 PASS remote0/transport0 explicitly no test/module/role invocation.
The exact ordinary route requires separate pre-execution disposition and remains
NOT_RUN/unapproved now. No new root diagnostic request until its prerequisites;
retained iCNk4Q/B29oJu untouched and both original full-root FAILs preserved.

STYLE native3/admin review is ACCEPTED separately, preserving progress-r7
SHAcd786017fa652db3f47b3c09c1bc0d36a46c0698520e56bd4050f787b01dd9af.
It does not waive native full/package checks or approve an unstaged index.
Prospective sequential source trees and intermediate checks are being prepared
for explicit review before mechanical/survivor SOURCE/layout/signal publication;
new route11 remains separate. No staging or commit/push yet.


### P06 route-r2 exact ordinary native64 measured PASS (final review pending)

Independent reviewer ACCEPTED scoped targeted compile/audit and ordinary-method-r1
only. Owner rechecked method3 and outer/plan2 hashes before invocation. Complete
native-ordinary-tests-r1 log prints FIRST clean env-i absolute Python-I-B checked
source verifier, installed60s/owner90s, transferred2 and immediate pre/post
source210/JSON53/SDK5/images7/providers53/provenance/no-cache checks. Actual
RouteObservation6/6 and exact three Python modules58/58 in0.143s PASS without skips;
both command returns0, ORDINARY_ROUTE_HELPER6_PYTHON58_ONLY_PASS, remote0/transport0.
Helper text/lstat cases are ordinary injected metadata evidence; fake-image own-child
reference/mapping assertions and mocked orchestration/adoption/route-error cases
remain distinct from native platform module/AUL/Drop/RPC evidence. No launcher or
module entry, role/context/NSS query, SQL or policy operation was selected.
Preserved accepted preparation08 SHAfd804b6c6fe951defefb39dde2779df3332d0400dec79dab8f7b3fbf5cf74b5f
before this append. Earlier configure FAIL and both full survivor FAILs/retained
iCNk4Q and B29oJu remain unchanged. Final ordinary evidence review is requested;
new root diagnostic invocation remains NOT_RUN/unapproved pending its exact method.

Prospective publication series host checks now PASS in four filesystem-only trees:
mechanical133 CTest12/12; frozen survivor-r3 SOURCE20 CTest14/14; layout8 CTest14/14;
signal7 CTest22/22. Each tree starts from published07780c7 plus explicit ordered
frozen overrides, with original overlapping CMake/docs preserved in its tree map.
No index/staging/commit/device mutation is implied by those checks. Source-only
survivor publication requires explicit disposition preserving overall root FAIL,
server-only evidence and retained readers; route11 is excluded from this series.
Native STYLE3 acceptance remains scoped, not full native/package waiver.


### P06 route-r2 ROOT-METHOD-r1 frozen read-only preflight (execution pending)

New exact root method3/outer-plan2 binds accepted source210/SDK5 and freshly compiled
route launcher8ece8582...9bc9/module dce96ddc...c2e59. Delta from previously approved
survivor-root-r2 method is only protected root/source-preflight/image/hash/log
binding; sequential server then reader, marker ordering and sticky cleanup proof
remain unchanged. FIRST clean env-i absolute Python-I-B/source-only checked verifier,
installed120s/owner150s, immediate pre/post checks and fresh exclusive logs. Exact
read-only root-preflight-r1 validates transferred2/source210/JSON53/SDK5/images7/
providers53/provenance/no-cache and prints NO_TEST_OR_MODULE_OR_ROLE_INVOKED,
remote0/transport0. It exits before watchdog/role/module invocation; no root result.
Ordinary64 final evidence review and separate exact root safety review are pending.
Retained iCNk4Q/B29oJu untouched; no cleanup/repair or policy expansion.


### P06 route-r2 root diagnostic measured FAIL (final evidence review pending)

Independent reviewer ACCEPTED ordinary64 evidence and exact ROOT-METHOD-r1
pre-execution safety only. Owner rechecked exact method3/outer-plan2 hashes then
ran the unchanged bounded route. Complete native-root-tests-r1 log prints FIRST
clean env-i absolute Python-I-B, installed120s/owner150s, transferred2 and immediate
pre/post source210/JSON53/SDK5/images7/providers53/provenance checks.
ROOT_SURVIVOR_RETURN1, remote1/transport0; no timeout/full fixture PASS.

Server sbEzCW separately completes printed barrier/coordinator reap-9/adopted
nonexit/EX contention/nonexit/actual normal reap0/EX release/removal before server
CASE_PASS. New bounded AUL server Listen/terminal query records match the expected
MAIN path and successful named socket metadata, with zero lifetime service/cancel/
other/rejection counters. Source-enforced initial/final ownership/barrier assertions
remain distinct from individually printed observations.

Reader Vmiui5 is RETAINED before its barrier. Known attached report records reader
3314926/server3314925. Server Listen, reader preconnect and native-failure queries
all report the same expected representable95-byte MAIN path with successful lstat,
matching metadata dev20/inode15414846/root UID/GID0; own registration-name reports
match expected. Client query explicitly independent/rawUID301, actual proxy target
and retained path NOT_OBSERVED. Saved lstat errno0 is not a failed RPC errno. Actual
129-byte untruncated REAL_GATE_NOT_PROVED preserves client-connect/InvalidIOException/
specific_Cynara_decision NOT_OBSERVED; there is no reader body/ACK/teardown/lifetime
proof, nor final reader-server drain sample. Matching independent path APIs/socket
metadata do not identify internal prepare/connect/send/receive/credential failure.

Overall full route remains FAIL/NOT_ACCEPTED; server-only evidence is separate.
Fresh retained Vmiui5 plus historical iCNk4Q/B29oJu are untouched externally; no
cleanup/repair/rerun follows. Source/provider guidance remains distinct from exact
installed binary provenance. No policy/SQL/real recovery/StageB/product result.


### Prospective publication series administrative boundary (review pending)

Four exact filesystem-only source trees based on published07780c7 use frozen
mechanical133 -> survivor-r3 SOURCE20 -> layout8 -> signal7 overrides. The original
versions of overlapping CMake/contracts are selected, not current route11 WIP.
Each prospective tree passed its ordinary host check12/14/14/22; its complete
path/hash/mode and CTest command/env/cwd maps are frozen separately. Source-only
survivor publication is explicitly requested with overall root FAIL, two retained
readers and scoped server-only evidence preserved; it is not runtime acceptance.
Final admin candidate07 is signal7's exact contract plus this boundary bookkeeping,
so route11 development clauses are excluded. Candidate08 retains all chronology
and accurately records route11 as separate accepted source/build/ordinary evidence
with root-method review pending; route implementation is not part of these four
source commits. No native full/package waiver, staged index or publication result
is inferred. Exact final disposition/commit/push/remote evidence follows separately.


### Ordered source publication disposition ACCEPTED

Independent reviewer ACCEPTED the four exact sequential source trees and this
admin-only candidate, preserving07 SHA9836d3c1318c975d054c04e3460321ba6d88d4f32bdfdf1450462c79bfffd2b6
and08 SHA70d5bc87563deb1771bbac6b68a708011a53ab1a3488f952783efa6f8a8a2f25
before this permitted status append. Survivor-r3 publication is SOURCE-ONLY,
explicitly overall runtime FAIL with server-only evidence and retained readers.
Host checks12/14/14/22 were sequential reconfigure/affected rebuilds in one reused
build directory, not four fresh native builds. Route11/session/spec changes are
excluded from the four source commits. Actual owner commit/push/ref checks follow
separately; historical pending paragraphs retain their chronological meaning.


### Ordered publication owner result and package presentation acceptance

Owner verified each selected index tree against frozen path/hash maps with Git
canonical file modes, preserving the route WIP worktree. Commits pushed on main:
mechanical133 06779a532b5be22fb04c0fb2217de1cc009a4405; survivor-r3 SOURCE20
c73945a00c953b5841852eadccbe7e784c683df9; layout8
8e1fa44958b75a93b1ee180e0f393b0baff5995d; signal7
c1fa923d843fc827a2825e06d8dfe43cfc96fdb8; accepted admin2
243211e47f7944e9b3023cc013dda3b82e60465d; separate package spec presentation1
38ce9ba36111a7bc7e92c4d3e16e73baebbc1b23. Every push exit0 and exact remote
ref equality verified; source-only survivor runtime remains FAIL. Original first
publication-verifier attempt refused archive physical0664 versus Git canonical
100644 before any commit, despite equal bytes/executable bits. R2 canonical
Git-mode comparison fixed only that outside-project verifier. GitHub initially
rejected survivor push with fatal commit_refs; same reviewed commit retry succeeded
without history rewrite. Logs/maps/failures remain external evidence.

Independent reviewer ACCEPTED separate spec SHA
b559690ce1a16e99df6ae66960b44446bdf6f3aeabf703aabe4c1002a5750e54: only
command continuations/blank separation,109 joined logical lines/tokens unchanged,
host/native/native-prefix rpmspec parses0/0 and expanded arguments equal. Existing
pc Name/filename/template and LICENSE unchanged. Release15 unchanged; no rebuilt
RPM/install/package activation follows from source publication.

P06 root diagnostic is independently ACCEPTED AS ACCURATE FAILURE EVIDENCE ONLY,
not full runtime: preserve accepted08 SHA102741de349b60379798ef95ad80225b1aa1c3e63218583ce897ebae91dddd93
before bookkeeping. Server-only sbEzCW passes; retained Vmiui5 has no reader proof.
Observed socket49600 decodes S_IFSOCK|0700/root UID/GID0, not a failed syscall errno.
A separate accepted development contract permits fixed early root-server-only
creation-mask transition, before module load, with unchanged real authorization.
No new source/native/runtime result is inferred by design acceptance.


### P06 socket-creation-r1 local implementation/host evidence (review pending)

Independent reviewer ACCEPTED development contract e4d6173578dd3bed6ecc33f938a5a79e29ef77a48cdfba780cce16408759323c,
not new native/root execution. New standard-runtime helper changes only validated
server-hold/reader-server creation mask after complete pre-dlopen one-task/context/
reference validation. Unexpected prior mask rejects; no socket repair or threaded
query-toggle. Both native observer and Python supervisor reject wrong named
root/socket0777/API/literal prerequisites before readiness; informational success
alone supplies no barrier/authentication. Source-owned record opens request0600
and mkdtemp/private directory creation0700, with provider creation side effects
explicitly part of the process-wide000 server premise. Shared original-mode
support/entry unchanged; no generated policy/FD/lease/lifetime weakening.

Ordinary host helper11/11 PASS16ms (existing route6 + actual isolated creation4 +
pure prerequisite1), Python61/61 PASS0.234s, full host CTest23/23 PASS11.34s, launcher
-fsyntax-only exit0. Optional native module/AUL is not compiled by those host
checks; pure helpers/protocol and trusted startup bypass remain distinct from
actual root/module authorization. Initial helper build-r1 used stale build tree
and failed No rule to make target before compilation; configure actual build-style
and build-r2 passed. Final catch/cleanup presentation change rebuilt as r3 before
these recorded results; earlier successful binary is not substituted for final
source bytes. No corrected native compile/test/root invocation yet.

Frozen review union14 includes accepted route11 plus changed launcher and two new
standard-only helper/test files; delta10 against captured actual originals includes
contracts/evidence. Published source-only survivor/layout/signal/spec checkpoints
remain unchanged; new current07/08 include separate administrative publication
records without silently staging route WIP. All historical failures/retained
scopes and original acceptance hashes remain external immutable evidence.

### P06 socket-creation-r2 ordinary wait-budget correction (review pending)

Reviewer CHANGES_REQUESTED r1 only for ExactReap accepting a positive wait after
its absolute end. Frozen r1 union14/manifest99f27a259282548ac8307b50d4bd826ea726afb9f30efabc2e66dabc0e150047
and archive5ab8aa0016789e3bb5c9cb53353bb9a566c2df37955678f26d2580878c5a3cee
remain historical source/host evidence. Production launcher/native/Python gate
bytes are unchanged. Corrected SAME test helper checks before and after wait;
late actual reap fails without another wait/signal/removal. Three deterministic
Test-TU clock/wait cases reject expiry before retry and late final positive reap,
and accept on-time completion. They launch no child and make no kernel claim.

Final helper build-r4 return0; helper-r4 actual14/14 no skips PASS17ms. Full host
check-r2 CTest23/23 PASS11.47s. Python61 remains unchanged and runs within that
check; prior independent Python61/syntax0 evidence remains separate. Session
SETSID candidate is held outside the worktree during this fullhost rerun; the
accepted signal baseline is preserved. Frozen socket r2 changes only the test
and07/chronological08 from r1. Corrected native compilation/tests/root remain
NOT_RUN/unapproved; prepared source212-r1 was never transferred/executed and
will be superseded before any future compile. All retained reader scopes untouched.

### P06 socket-creation-r2 accepted source / native compile / ordinary method

Reviewer accepted exact union14 manifest2467835020b2cfbdabb7d669afc107bab3ce0f3b9d9f73022e4b51c3a236f49d
and archive df98bf6e98dba962882588a2530cc68bee78532d61b89f5b311ae446e3b5e245
for local source/host only; r1 late-positive-reap finding closed by explicit verdict.
Original r2 snapshot07/08 bytes/hashes retained before this administrative append.

Fresh protected native read-reference-socket-r2 compile/audit: source212 is frozen
historical source210 overridden by accepted socket14, unchanged202 plus existing
delta8/new2. Archivec58209928e7290f08ebfbcc77ed6eab96b230fa8c8c6241e2487e3beae55cc99
manifest5912575bc59a9cf1efac1733f1fd10266881414f6c13bddeb16d230bd5e6889b.
This is not a whole current-HEAD/native build; LAUNCH-SESSION is excluded. SDK5
four official headers remain exact; diagnostic pc changes only the protected
include-prefix to fresh tree, not library/SDK/system installation or LD path.
Targeted native O1/j1 configure/compile/link/audit remote0/transport0. All7 image
hashes and53 providers/closures recorded; IDs/default Cynara/entries/PIC/JSON,
old/new module linkage, minimal launcher standard-only closure, AUL imported
absolute link/NEEDED resolution and clean ldd-r/no RPATH/RUNPATH checked. Compiler
commands identify early-mask launcher and new ordinary test inputs. This proves
target compilation/linking, not module entry, socket creation or authorization.
Installed1800s/owner1830s compile-only bounds recorded. No test/role executed.

Prepared ordinary-method-r1 and read-only preflight: transferred3 methods checked,
source212/JSON53/SDK5/images7/providers53/interpreter/watchdog/namespaces/ext4/
no-cache PASS, explicit NO_TEST_OR_MODULE_OR_ROLE_INVOKED/remote0/transport0.
Pending selection is exact helper14 plus three Python modules61 =75/no skips,
FIRST clean Python-I-B checked source-only verifier/installed60s/owner90s and
immediate before/after preflight. Test execution remains NOT_RUN/unapproved until
separate exact method disposition. Root survivor method is not requested here.
All retained iCNk4Q/B29oJu/Vmiui5 remain untouched; previous full FAILs stand.

### P06 socket-creation-r2 ordinary75 result / root method pending

Targeted compile/audit and exact ordinary-method-r1 separately ACCEPTED by
reviewer; preserve accepted08 SHA1d81d7855ef4ffa4470f8c079a808ceb9908514afafec20e3c023d706e78fa45
before this append. Exact method3/outer-plan2 hashes rechecked before owner run.
Native ordinary-tests-r1: helper14/14 and Python61/61 (0.128s), no skips, both
returns0, ORDINARY_SOCKET_HELPER14_PYTHON61_ONLY_PASS, remote0/transport0. Approved
FIRST clean startup/installed60s/owner90s and immediate pre/post212/JSON53/SDK5/
images7/providers53/provenance pass. Four actual ordinary owned-child socket tests
measure077->0700/early000->0777/private creation/parent-reader preservation and
unexpected mask refusal; exact reaps/cleanup are source-enforced assertions,
not printed syscall traces. Three fake clock/wait tests launch no child. Python
fake-image/protocol and helper pure metadata remain separate from platform role
execution. No launcher/module/AUL/RPC/Drop/root survivor was selected.

Root-method-r1 preparation: same fixed sequential server then reader inert-reference
experiment, exact fresh socket-r2 launcher/module and preflight/source212 bindings;
FIRST clean source-only verifier/installed120s/owner150s, immediate full pre/post,
no workload/cleanup predicate broadening. Accepted early process-wide server000
creation premise and negative pre-READY named0777 gate are the source amendment.
Read-only root-preflight-r1 checks transferred2/provenance PASS remote0/transport0,
explicit NO_TEST_OR_MODULE_OR_ROLE_INVOKED before watchdog/role invocation. Full
root runtime remains NOT_RUN/UNAPPROVED pending separate exact pre-execution review.
All three historical retained readers and prior overall FAIL/server-only evidence
remain unchanged; no automatic cleanup/repair/policy/SQL/real-journal authority.

### P06 socket-creation-r2 approved root method / measured two-case result

Reviewer accepted ordinary75 evidence/admin prefix d5bcb5c555125fa3fb2cdf06a76f1766d5ada4bdb54039eff75926db2e5ab3df
and exact root-method-r1 pre-execution separately. Owner rechecked method3/outer-
plan2 hashes immediately before fixed invocation. Complete native-root-tests-r1
records FIRST clean env-i absolute Python-I-B/source-only verifier, installed120s/
owner150s and full212/JSON53/SDK5/images7/providers53/provenance checks before/after.
ROOT_SURVIVOR_RETURN0, ROOT_SURVIVOR_TWO_CASES_ONLY_PASS, remote0/transport0; no
timeout/retained/failure marker in this new run. Final runtime/publication review
is pending; prior three overall FAILs and retained scopes remain unchanged.

Server scope o0zK5l: Listen and terminal counters created/rejected/services/cancel/
other all0, named root0:0 socket49663 (=S_IFSOCK|0777), complete expected95-byte
literal. Printed independent barrier, actual coordinator reap-9, adopted role
3328421 nonexit before/EX contention/nonexit after, actual normal adopted reap0,
EX available then REMOVED before server CASE_PASS.

Reader scope5No9sh: spawned reader3328524/server3328523 diagnostics; Listen and
independent rawUID301 preconnect samples match the expected95-byte literal and
named socket dev20/ino15474556/root0:0/mode49663/lstat0/errno0. Actual proxy retained
target/path remains NOT_OBSERVED. Native body PID3328524/token system301-platform:
3328524/raw UID/GID301/System/cancel1/other0 and actual client reply-6 printed.
Server terminal counts created1/rejected0/services0/cancel1/other0. Source requires
checked server drain/normal reap/endpoint absence and reader teardown/context/ref
validation plus independent HOLD_ACK/coordinator body correlation before barrier;
those conditions are not independently printed raw syscall tuples. Then printed
barrier/coordinator reap-9/adopted nonexit/EX contention/nonexit/normal reap0/EX
available/REMOVED before reader CASE_PASS. Both phases precede full fixture PASS.

Five bounded route JSON records, one body/reply, two owned/removal pairs and two
known adoption sequences were audited from complete log; local audit JSON is
extra owner analysis, not separate device execution. Initial/default SIGCHLD/
subreaper/table/drop/pre-dlopen exact077->000/code3 closure/borrowed4 retention,
post-known-reap/final ECHILD/sticky eligibility and pinned inventory checks remain
source-enforced, not individually printed syscall evidence. Server000 is process-
wide including trusted pinned provider constructors/creation effects, not socket-
local or production policy. Named observations are not retained socket identity,
Cynara raw decision or the historical failed connect/send/receive syscall/errno.

The new measured fixed fixture now traverses actual real generated/default MAIN/
callback/Cancel route and post-teardown reader lifetime under the accepted setup.
It does not prove arbitrary descendants/native TLS behavior, actual journal/CLI
recovery, SQL/Authorize/Confirm/StageB/direct-read subset or production admission.
iCNk4Q/B29oJu/Vmiui5 untouched externally; their FAIL evidence is preserved.
No original-mode/namespace/remount/policy/load2/install/package/ARM authority.
LAUNCH-SESSION isolated candidate is source/host accepted separately and still
unapplied to current worktree/device source212/index; native support remains open.

### P06 socket-creation-r2 final scoped runtime/publication ACCEPTED

Independent reviewer w1:pA accepted the complete measured two-case log, exact
method3/outer-plan2, reviewed13 code/07 hashes and administrative08
d55cb3346194b4cee920ce8c9bea04387f3e50a3cf3c92e0b90a191ae37869d7.
Prospective14 and complete212-file tree were independently reconstructed from
HEAD38ce9ba plus those exact overrides; LAUNCH-SESSION remained excluded.
Historical NOT_RUN/UNAPPROVED paragraphs above describe their pre-execution
states; the later fixed inert-reference runtime is now ACCEPTED. Earlier full
FAILs/server-only results and retained iCNk4Q/B29oJu/Vmiui5 remain unchanged.

Accepted scope is measured registered-server and post-RPC reader survival/release
under the fixed ownership/topology and early process-wide server000 premise,
with original exceptions, default policy and informational-observation limits.
No historical failing syscall/errno, arbitrary TLS/descendants, real journal/CLI
recovery, SQL/Authorize/Confirm/StageB/direct-read subset, product admission,
package/install or ARM acceptance follows. No further root run is requested.

Next: publish only the reviewed14 paths after exact selected-tree verification,
record actual commit/push/remote equality, then separately apply the accepted
LAUNCH-SESSION-r2 four code/build overrides and append-merge its contract/evidence
into current documents. Actual-tree host checks and a frozen native target/method
remain required; no session native test or privileged workload is approved.

### LAUNCH-SESSION-r1 isolated local source/host candidate (review pending)

Accepted development contract49c4b61635a4ae068e9c08bec9e2f727047e1968f5bf0de873ef01cfa058f63d
implemented in a separate six-path candidate outside the working tree while
P06 socket-r2 native/root method review proceeds. Exact RunCli diff only requests
SETSID instead of SETPGROUP, removes setpgroup and adds its scoped comment; no
new post-fork C++/signal handler, product hook, public header or cleanup refactor.
Driver wrappers remain uninstalled/test-only. CMake retains target/sibling/root
outputs and TIMEOUT8: replace obsolete group fault with init fault, add fixed
invalid-executable/spawn/timeout/cancel/two notifier-failure cases,14 total.

Fresh isolated host tree uses reviewed historical source212/socket-r2 plus these
four code/build overrides, optional native transport/module OFF. Final driver
build-r6 return0; registered verbose tests-r6 actual14/14 no skips PASS0.20s.
Normal PID1118529=PGID=SID differs from parent1118525, exact normal reap0. Fixed
timeout1118542 and cancel1118545 each show same owned SID/PGID, actual kill negative
PID/SIGKILL return0 before exact leader reap signal9; application entry remains
NOT_PROVED for spawn-return readiness. Parent state/eight defaults/high-FD/flags/
ECHILD are source-enforced assertions, not extra syscall traces. Cancel helper
JOINED with positivePID1, two error cases JOINED with positivePID0; actual spawn
ENOENT2/ENOEXEC8 and injected EIO5 are distinct. Source method does not claim a
measured kernel setsid failure. Existing Cli/Rpc focused9/9 PASS2195ms, including
the separate actual-descendant fixture regression. Final full hostcheck-r4
CTest29/29 PASS11.68s; no optional module/AUL/root workload selected.

Historical driver11/12 passes and hostcheck-r1 26/26 remain earlier candidates.
Adding notifier error cases initially omitted their driver allowlist: tests-r4
failed2 before Run/spawn; hostcheck-r2 failed2/29. Logs preserved; corrected fixed
allowlist rebuilt/reran as r5, then final finite notifier report/comment rebuilt
r6 before this final evidence. No failed evidence rewritten as pass.

Frozen six-path candidate is not the current worktree/index or a published/native
result. Actual worktree remains accepted signal baseline and P06 route/socket WIP;
index empty/main remote38ce9ba unchanged. Source originals captured immediately
before candidate freeze, including current P06 administrative prefix; no P06
code/native212 mutation. Source acceptance, controlled application/final actual-
tree checks, exact native compile/provider/ordinary method and publication ordering
remain pending. No worker/NamespaceInit/remount/root/policy/install/production gate.

Initial unsubmitted local session-r1 freeze misstated parent SID1118524; actual
verbose normal record is1118525. Revision r2 corrects only that administrative
number and records this correction, preserving r1 bytes/hash; code/tests unchanged.

### P06 published checkpoint / LAUNCH-SESSION-r2 controlled application

Reviewed P06 fourteen-path private checkpoint committed and pushed as
37e3dc4ecf9c2d3c75a26ee745b634bdcac776fa; actual remote main equals that commit.
Index/complete212-file map matched reviewed scope plus permitted append-only
final disposition, and the worktree was clean before session application.
Accepted runtime08 d55cb3346194b4cee920ce8c9bea04387f3e50a3cf3c92e0b90a191ae37869d7
and committed administrative08 cf49afb39ec290e88739321216203a1cb8368b9a8b56a5f700e3ad04a6c533fa
remain preserved. No session code was included in that publication.

Reviewer accepted exact isolated LAUNCH-SESSION-r2 six-path local source/host
candidate manifest af7227229fe893faa33caeed2bfef7b37ca0fab096ac90a1a5cf6fc5956c2334
and archive8709c3ca82deb4e1d71391bf1b15e38055da3219fb90c0c31136075ad81ee2de.
Controlled actual-tree application copied only its exact four code/build files,
then appended its accepted contract/evidence suffixes to current P06 documents;
newer P06 chronology was not replaced with older isolated candidate snapshots.
Original session pending statements above are historical; source/host candidate
is ACCEPTED, while actual-tree checks/native compile/runtime/publication remain
separate pending gates. P06 source/native212/root results are unchanged.

### LAUNCH-SESSION actual-tree host selection (r3 review pending)

After accepted P06 publication37e3dc4, controlled application retained exact
accepted session four code/build hashes and appended only accepted document
suffixes. Actual host build-style driver rebuild return0, verbose fixed14/14
no skips PASS0.20s, then full check29/29 PASS12.16s. Captured actual CTest JSON
contains exactly29 registrations and fixed14 with original root-build cwd/
TIMEOUT8. This is affected rebuild/reconfiguration of the existing host tree,
not a fresh independent build or optional platform/module/runtime proof.
Published P06 implementation and all three earlier retained scopes are unchanged.

Separate native preparation uses historical accepted socket212 archive plus
exact isolated session6 overrides; documents there are historical candidate
snapshots, not this current administrative tree. Fresh target compilation/audit
only is in progress, with existing private official headers SDK4 unchanged and
a prefix-only pc for the new protected build. No driver/CLI/invalid executable/
CTest or privileged fixture is invoked by compilation; native tests remain
NOT_RUN/unapproved until exact artifacts/provider/generated-invalid-file method
review. Native source/preparation outcomes will be recorded separately.

### LAUNCH-SESSION-r3 application accepted / targeted native preparation

Reviewer accepted exact actual-tree6 manifest0a44141db92da0a9289c74f05a4300667057e92f61a5bbe5e09031ccef915527
and administrative08 3abd36114a87550c180dec25d92e7e5c6c9b6fc64246232450b0b0024852af8b.
Four code/build files plus07 equal accepted isolated r2; newer P06 publication
chronology is preserved in08. Actual host14/29 remains affected-build evidence.

Fresh protected launch-session-r2 native target compile/audit: historical socket212
with exact isolated session6 overrides, archive77514e5360355e557ac22d06396d76a9f8c1cd4f1f619e25d2fa46a04529b847,
manifest6db0671344bbe3e0caf95719cf0ad28822749cbbee53718f35881fddf2c40df1.
Native administrative source documents are the older isolated candidate snapshots,
not the newer actual-tree08 selection. Fresh configure with platform optionsON,
O1/j1 target closure compiled runner/driver/CLI/adapters/catalog. Actual C++20
compile and wrapper link commands printed; driver63c568e9c966cea34d93b2e89dd9ec1a563a39645de806fcb478760e73917c76
and CLI91b7377f2648a02e623bd17c81fac1dff4b4e6830b9032c0bbf35f0e782fe821
hashed. Clean ldd-r/no RPATH/RUNPATH, six resolved standard/SQLite providers pinned.
POSIX_SPAWN_SETSID compiles without fallback; runtime semantics remain NOT_RUN.
No TIDL generation/module target or full native build/check was selected.

Private SDK official headers4 unchanged; pc changes only protected include prefix
to launch-session-r2, same installed AUL pin, no library copy/install/LD path.
SDK5 is configure-only metadata; selected ordinary driver closure does not invoke
AUL/platform module. Existing generated invalid-worker-image is exact0700/root/
singlelink/26 bytes/hash1f9c66a9af66d0ee9c034df3a45fa25cd5ff1cf6f297dd165766b91ce8651cd2,
read without invocation. Source212/JSON53/SDK/AUL and provider hashes rechecked;
compile/audit remote0/transport0, no test/driver/CLI/invalid-image execution.

Prepared ordinary native method-r1 pins images2 plus that fixed invalid file,
providers6 and SDK5 before/after, exact registered14/root cwd/TIMEOUT8. FIRST
clean env-i absolute Python-I-B/source-only protected verifier, installed120s/
owner150s, fixed verbose CTest14/no-tests-error/stop-on-failure. Read-only native
preflight-r1 prints full provenance and exits before watchdog/tests, remote0/
transport0. Native14 remains NOT_RUN/unapproved until separate method verdict.
No arbitrary-descendant/unknown-wait/watchdog absence, root worker/NamespaceInit/
remount/policy/module/RPC/operational DB/package/install/production gate follows.
All P06 earlier retained scopes remain untouched.

### LAUNCH-SESSION-r2 approved native method / measured14 result

Reviewer accepted scoped fresh target compile/audit and preparation08
d58467c03769b1cb1c2c54663021076e666c40fd5fadf3006de2e4e9c83592d2,
then exact ordinary-method-r1 pre-execution separately. Owner immediately
rechecked method3/outer-plan2 before the fixed invocation. Complete native-tests-r1
SHA d41d2483fdfab0f3f9a2bdc90a113f5a3e9b6037aaccbcb1c5974c5e2e3bc35f
prints FIRST clean env-i absolute Python-I-B, installed120s/owner150s, transferred
method/protected source-only verifier and immediate pre/post212/JSON53/SDK5/
images2/invalid1/providers6/interpreter/watchdog/ns/ext4/no-cache checks. Fixed
registered14/14 no skips PASS0.20s, NATIVE_SESSION_CTEST_RETURN0,
NATIVE_SESSION14_ONLY_PASS, remote0/transport0. No timeout/retained marker.

Actual normal child3334707=PGID=SID differs from parent3334703, normal owned reap0.
Timeout3334720 and cancellation3334723 each show PID=PGID=SID at real spawn return
with application entry NOT_PROVED, negative-PID SIGKILL return0 before exact
leader signal9 reap. Cancellation notifier joins positive1; its exec/injected
spawn failure cases join positive0/0. Actual ENOENT2/ENOEXEC8 remain distinct
from injected parent-call EIO5 with no real spawn. Parent IGN/NOCLDWAIT and four
attribute faults show zero spawn. Parent full signal/SID/PGID preservation, empty
child mask/eight defaults/high-FD exclusion/exact attributes/final ECHILD are
source-enforced driver assertions, not additional raw syscall traces. Owner log
audit JSON parses these records only; it is not another native execution.

Measured native SETSID supports this fixed ordinary RunCli route without fallback.
It does not recertify arbitrary/escaping descendants, unknown waits, abrupt owner/
watchdog containment, worker/NamespaceInit/remount/module/RPC/policy/operationalDB/
package/install/product authority. No kernel setsid failure was injected or observed.
Earlier isolated host failures/correction hashes and all P06 historical FAILs/
retained scopes remain preserved. Final exact6 publication review is pending;
index remains empty and P06 published implementation is unchanged.

### LAUNCH-SESSION final scoped native/publication ACCEPTED

Reviewer w1:pA accepted fixed native14 complete log/method3/outer-plan2 and exact
actual6 publication tree212 from HEAD37e3dc4; index empty. Accepted08
a4c7a61afd61a4c1ec6bf8c96b0941ff73ec8568b1db7490f7e4d6b21c85ade2
and accepted07 01a053dabca5e07f378bedb2f06882f7f8ff383e925f2fcd4263a394b5075822
are preserved before this final status append. Historical pending/NOT_RUN
paragraphs above describe earlier candidate/pre-execution states; later exact
native SETSID14 evidence is now ACCEPTED for the fixed ordinary RunCli route.
No further native/root run is requested; all earlier host failures/hashes and
P06 retained scopes remain preserved. Broad descendant/watchdog/worker/namespace/
remount/policy/install/product limits are unchanged.

Next: publish only these reviewed6 paths and record actual commit/push/remote
equality. Continue the accepted P06 same-subject/object development contract
with a separate pure fixed eight-rule plan/schema checkpoint before durable
journal/real Authorize-Confirm/catalog integration. No policy/runtime execution
or original different-subject/direct-read-subset acceptance follows from that
development direction.

### LAUNCH-SESSION published / next pure P06 plan review

Exact reviewed6 session checkpoint committed/pushed as
c189386349a0e7fe302eaadb8919e6d4caae9b3a; actual remote main equality verified.
The selected index/full212 map matched the reviewed publication tree plus
permitted append-only final07/08 disposition; worktree clean immediately after
push. Published07 fb3a42155213ee319454dccb69b1d35e0c2e9dec35b8c689233375d767ec88b9
and08 64116214326919414337f0946bf2f2e1cc0fb52b599f273ba35d72ae6f67a1fb
are preserved before this owner report. No unrelated WIP was staged.

Continued authorized P06 same-subject/object development as temporary pure
value/schema source2 outside Git/current imports/CMake/native methods. Fixed
version2 system301-platform-objects-v1, fixed System301/groups10212/User::Shell
principals, exact eight old-subject/fresh-object permissions, fixed paths/labels,
bounded JSON/duplicate/type/uint64 rejection and independent returned references.
Host final13/13 PASS0.004s; earlier12 PASS retained. No filesystem/lock/load2/
label/credential/SQL/RPC operation is implemented or executed. Numbers alone are
not metadata provenance/freshness/lock authority. Temporary frozen source manifest
542ab7bb9835eff960830b9424c5376738173213b73b1c8f480579bb35f18f12 and
archive5006d7624b1c4ef9a632b7846b713bd7036ed61b3d0577e007d4325d26d26573
sent to independent reviewer as P06-READ-OBJECT-PLAN-r1. Source review pending;
no native/root/policy/publication request. Durable journal and actual catalog
Authorize/Confirm/local-access integration remain separately frozen future work.
Original different-subject/direct-read-subset gate BLOCKED; retained P06 readers
and all historical failures remain untouched. Overall product is incomplete.

### P06 object-plan OP-01 / frozen temporary r2 awaiting review

Reviewer returned CHANGES_REQUESTED on r1 decoder error normalization: bounded
JSON can still raise parser RecursionError or integer-conversion ValueError.
No runtime/policy issue was inferred. Exact r1 source2/hash/archive/host evidence
remain unchanged. Temporary r2 preserves explicit InvalidPlan and normalizes
UnicodeError/RecursionError/ValueError only in the parsing block, with causes
retained; no byte/schema relaxation, BaseException catch or global parser change.

Two new bounded negatives exercise nested32767-byte JSON and integer4301-byte
token with host limits1000/4300. Initial host-r3 failed the expected recursion
cause assertion because1050 nesting levels parsed successfully; log retained.
Final deeper bounded input and host-r4 yield15/15 PASS0.005s/no skips. Tests have
explicit deterministic parser-error fallback only under high/disabled settings;
both real parser-error branches ran here. Frozen r2 manifest
73007b786b9beb700f3e6b1b53f4ccec7755a49b0114e23aca2662f65459ccae and
safe2 archivecf7de271b3828ddf2ad8a80dc86016091361c30238f6a1570a3789743bb92612
sent for independent re-review; OP-01 remains open until reviewer disposition.
Temporary source has no project/CMake/CLI/native/root/policy integration.
All original blocked/retained-scope/product limits remain unchanged.

### P06 object-plan r2 accepted / temporary journal mechanics r1 review

Independent reviewer accepted exact temporary source2/host15 object-plan-r2;
OP-01 CLOSED. Prior pending paragraphs are historical. Accepted manifest
73007b786b9beb700f3e6b1b53f4ccec7755a49b0114e23aca2662f65459ccae
and archivecf7de271b3828ddf2ad8a80dc86016091361c30238f6a1570a3789743bb92612
remain preserved. No integration, native/root/policy/publication follows.

Continued separate temporary journal mechanics with exact accepted plan helper,
mandatory unselected trusted operations, durable fixed plan/receipt publication,
real host flock exclusion and retry, fake eight-rule callbacks and empty known
host-scope cleanup. Final host-r3 actual25/25 PASS0; earlier18/24 logs retained.
Tests include partial/zero write, sync/close faults, unsafe metadata/substitution,
receipt retry and same-process alias lifetime. No child/global policy/CLI/SQL/RPC
or native/root image runs. Close-fault mocks physically close then report EIO,
not unresolved FD ownership. No root backend/inventory or runnable recovery CLI
is implemented; future integration must separately bind these authorities and
prove owned-child absence. Temporary3 manifest
21466a185895b2a1efe199b2c2f7c93d849fddeaff4293fe906f557877d88801
and archive1fa65d71b3a07f8fec07190be28f19c2098a833932358cc99ef2a263b2708e0c
frozen for P06-READ-OBJECT-JOURNAL-r1 independent local source/fault review.
Source review pending; no project import/CMake/staging/publication. Original
blocked different-subject gate, retained readers and product limits unchanged.

### P06 temporary object-journal r1 accepted / inventory value boundary r1

Independent reviewer accepted temporary journal3 mechanics and host25 only,
manifest21466a185895b2a1efe199b2c2f7c93d849fddeaff4293fe906f557877d88801
and archive1fa65d71b3a07f8fec07190be28f19c2098a833932358cc99ef2a263b2708e0c.
Prior pending wording is historical. Mandatory Operations remains unselected;
no runnable root backend/CLI/project/native/policy/publication acceptance follows.

Next temporary inventory2 is pure value validation plus sticky two-role cleanup
facts. Fixed eleven-key metadata/explicit absence and bottom-up path order,
strict types/modes/owner/groups/labels/transmutation/ACL/capability/identity,
wrong PID/nonzero reap/conflicts and permanent uncertainty are host tested.
Host16/16 PASS0 with no files/children/policy launched. Observations are fabricated;
this does not prove scanner completeness, provenance or kernel wait/absence.
Source manifest/archive and evidence are frozen outside Git under
/tmp/capmgr-read-object-inventory-r1 for independent local contract/source review.
Actual trusted inventory scanner/delete adapter, complete coordinator ledger,
root backend/source-pinned CLI and execution methods remain separate future work.
Accepted plan/journal bytes unchanged; original gate BLOCKED and retained readers
untouched. No project/CMake import/staging/publication in this increment.

### P06 inventory value r1 accepted / temporary held-object scanner r1

Independent reviewer accepted exact inventory2 pure metadata/ledger source and
host16 only; manifest03ea6875dba2fe3647e510690d1c9eae47a97c44bdcccb1afc5b50f7b14bdcae
and archived4b96e91a1d70b8d891d20a790a3e1e26b889066bbb2e6c1fe557729ea1e00cc
remain unchanged. Prior pending statements are historical; no cleanup authority
or kernel facts were inferred from supplied values.

Continued temporary scanner3 with exact accepted inventory helper, mandatory
unselected root trust/metadata backend, held directory/O_PATH object binding,
bounded complete fixed inventory, SAME sticky ledger before/during bottom-up
fixed unlink/rmdir, final parent fsync/absence/checked close. Host14/14 PASS0 uses
actual own scratch trees and kernel file metadata with explicit virtual logical
root/groups/labels/ACL values and supplied no-spawn/absence facts; no child,
SQLite data FD, root/native image, policy/RPC/SQL or target metadata proof.
Unexpected entries before first mutation, substitution/type/label, enum/metadata/
unlink errors, poison during removal and close-error all-owned-close cases covered.
Close mocks physically close then EIO; no unresolved-FD cleanup model. Partial
removal on later failure stays failed/poisoned, not rolled back or called clean.
Frozen scanner source/evidence under /tmp/capmgr-read-object-scanner-r1 pending
independent review. Root xattr/ancestry backend, deadline/coordinator wait adapter,
journal/CLI integration and native methods remain separate; original gates and
retained readers unchanged. No staging/publication in this increment.

### P06 scanner SC-01/SC-02 / temporary r2 awaiting review

Independent reviewer requested changes to local scanner-r1: eligibility could be
poisoned during validation or terminal completion without blocking mutation/return;
parent CURRENT held/named comparison did not pin the initial safe authority or
reread backend trust. Exact r1 source3/archive/host14 remains preserved as historical.
No target/native failure was inferred and no root backend was selected.

Temporary r2 retains accepted inventory bytes. SAME ledger now gates after complete
validation, immediately before and after every mutation, through final enumeration/
absence/fsync and after all successful owned closes. Initial parent authority
binding is retained; named+held samples must match before/after mandatory backend
trust revalidation. Own removal size/time/nlink changes do not alter that binding.
Final host22/22 PASS0 covers prior14 plus callback poison at mutation and terminal
root removal/fsync/enumeration/last close, same-inode parent chmod, backend rejection
and parent change during trust. Close attempts all descriptors; actual removal is
not rolled back and terminal poison prevents success even if root is already gone.
Frozen temporary scanner-r2 source3/evidence sent for explicit re-review; SC-01/SC-02
remain open until reviewer disposition. No CLI/journal/project/native/root/policy
integration/publication or retained-reader access. Original product gates unchanged.

### P06 scanner-r2 accepted / temporary guarded integration-r1

Independent reviewer accepted exact temporary scanner3/r2 and host22; SC-01 and
SC-02 CLOSED. Manifestbd4bd1d329b21dffbb32ab50d892c6ec6fc2f43b73df8cb302947a4864aa7f12
and archive65869e4bc1a60e534ad58ab7dbc0ab986e40240e3dd60055edbf596b48bdc0bd
preserved; pending paragraphs are historical. Root backend/topology/native gates
remain separate.

Continued temporary six-file host integration with four accepted helpers unchanged
and new guarded composition/tests. SAME retirement ledger gates before/after
recovery, including absent scope; every scanner path closes. Scanner failure still
attempts all8 fake revokes but cannot return guarded success. Receipts remain
non-authoritative; late close poison can reject after mechanics receipt publication.
Host9/9 PASS0 includes real owned direct-child SH contention after parent owner
close, exact bounded waitpid normal0 and release, independently for two fixed slots.
These are same inert host code, not target reader/server/FD4/adoption/module/TLS.
Actual host xattrs use own pinned FD proc links; labels/groups/root remain virtual.
No child-absence inference from EX; remaining no-spawn/endpoint facts are supplied
for this closed test topology. Mocked no-launch retention resets only a proven
no-child harness flag, never actual ownership uncertainty. Root/native/policy/SQL/
CLI/project integration/publication NOT_RUN. New temporary integration freeze/evidence
sent for independent local source/lifetime review; no staging and target retained
scopes untouched. Original different-subject/product gates remain BLOCKED/open.

### P06 host integration-r1 accepted / metadata prerequisite-r1 review

Independent reviewer accepted exact temporary integration6 and host9 only;
manifest354d255e3caecd05a0c75dd496c574d0f95bff77864239bc32bcc7a592b63013
and archiveeb3f3f246937498e92ecfda064b855f0cef28925b4bf7da79e085008e3bc73ee
preserved. Two inert direct children, virtual fixture labels/groups and all prior
limits remain distinct from target reader/server/adoption/TLS/root evidence.

Continued separate temporary metadata3 prerequisite with exact inventory helper,
raw held FD ownership/type/access/CLOEXEC/xattrs, explicit root User::Shell/IDs and
creator/self namespace witness, fixed ancestor binding and bounded exact ext4
mount-description checks. Root Backend is prospective and remains unselected by
all accepted defaults; no actual root callback/PID1 capture or target metadata run.
Host22/22 PASS0 uses own pins plus injected SMACK/root context/ancestor/namespace
facts and pure mount parsing; one own ACL/capability absence case uses real host
xattrs. No policy/SQL/RPC/native or child run in this metadata checkpoint. Root
namespace capture is privileged-only future reviewed preparation; subsequent
verification never reopens PID1. Errors reject, no permissions/policy repair.
Frozen temporary metadata source/evidence sent for independent source/fault review.
Root integration/full topology/deadlines/target Python and O_PATH/xattr/proc behavior,
CLI/provisioning/methods and publication remain pending separate gates. Existing
accepted helpers, original blocked gate and retained target readers unchanged.

### P06 metadata-r1 accepted / execution boundary-r1 source review

Independent reviewer accepted exact temporary metadata3 prerequisite and host22;
manifest340a2c9a62af615c3dff033cae958e22f598bdebd2a2201f5cd2641393915828
and archive7308040f278706be847f3854518ede0127a055c1d1577b1df23ee7ec7e3f7dd2
preserved. Root context/namespace/ancestor/SMACK observations remain injected;
prospective RootBackend unselected and no operational root capture occurred.

Next temporary execution3 applies SAME sticky ledger, creator-before-callback,
mandatory unselected topology and one fixed30s absolute budget before/after normal
metadata callbacks. Exceptions preserve original errors, late successes reject;
known owned cleanup remains attemptable after expiry but cannot finish normally.
Constructor/clock/creator errors poison; same poisoned ledger cannot restart via
new guard. Host18/18 PASS0 uses fake clocks/topology/close/metadata callbacks only,
not actual OS resource retirement or target timing. No selected root backend,
actual own FD/task verifier, journal/scanner/runtime wiring, CLI/policy/native/root
or project/publication operation follows. Frozen execution source/evidence pending
independent source/fault review; accepted helpers/gates and retained readers unchanged.

### P06 execution-r1 accepted / own topology-r1 source review

Independent reviewer accepted exact temporary execution3 and host18 only;
manifest656165a61277be2145e4e07662e1c2b9a98d4b829576ca5522e5d5e3d0c06aa4
and archive3bb9d1cb928d355da8aac501c0b2b95e3be7935dd5772a9ccf839aa1974bb587
preserved. Known cleanup callback result remains no normal completion/absence proof;
actual bookkeeping/full topology/runtime gates remain separate.

Next temporary topology4 copies inventory/execution exactly, adds bounded own task/
FD verifier against explicit owned registry and host tests. Creator-first, single
own task, raw signature/flags and stable registry checks account only one live own
proc FD enumeration-directory handle with post-close EBADF; no unknown FD closure,
admission fallback or OFD-identity claim. Host15/15 PASS0 includes actual own-table/
alias/extra procdir/status flag/substitution/thread rejection and same ExecutionGuard
sticky failure; post-scan EIO/live observation are injected. Physical fd absence is
not separately observed closedir return/errno. Actual scanner ownership-registry
hooks, metadata/root/journal/CLI composition and target interpreter/procfs behavior
remain next separate review. No child, root backend, policy/native or project/run
selection/publication. Frozen topology source/evidence pending independent review;
all accepted scopes, original gates and retained target readers unchanged.

### P06 topology-r1 accepted / registry-r1 source review

Independent reviewer accepted exact temporary topology4 and host15 only;
manifestdb5bb9e5efb7b1ff975dc4cebe863a9fe55fb446cb8a509215a19fa3260dcda7
and archive171e5aef539a805470bebef4a359eafbbe91ffdc5a8f561a6267b1e5e4e33455
preserved. Own metadata signatures and post-enumeration EBADF are not OFD identity
or raw closedir-return proof. Target runtime/topology selection remains separate.

Next temporary registry5 copies inventory/execution/topology exactly and adds a
preallocated own-FD owner plus host fault tests. Reservation precedes opening;
actual returned FD is recorded before pin/post-budget work. Successful close is
recorded before post-clock failure. UNKNOWN close outcomes are never retried;
cleanup attempts every independently known descriptor while preserving first error.
RESERVED/UNKNOWN records and SAME ledger poison prevent completion; later empty
snapshots never repair eligibility. Borrowed stdio is never closed. Private trusted
scalar opener/registry, exclusive stable FD table and guard bound to this registry
remain premises, not hostile-caller isolation or OFD/absence authority.

Host-r1 FAIL1: the pin-fault test rejected topology's transient enumeration FD
before the opener; its expected OPENED state was therefore wrong. Corrected test
injects only after actual owned OPENED recording. Host-r2 PASS14 preceded final
first-error cleanup adjustment; final host-r3 PASS14/14, no skips, includes actual
own O_PATH pin/close cases, fake late budgets, constructor/creator/guard faults,
unknown close/no-retry and all-known close attempts after a clock error. Mock close
errors do not establish unresolved-FD cleanup. No child or target scope involved.
Frozen registry manifest5c67a1c401aa768951c965ea0de2b34846847b7578b72fc6151143e0ab8aeda8
and archivece1b05c43a170328c6882bff5257a5797b0b707533c4d99601e4cad10e5f9ade
sent for independent local source/fault review. No scanner/journal/root/backend/CLI
integration, native/policy run, project publication or product completion follows.
All earlier accepted scopes, original blocked subset and retained readers unchanged.

### P06 registry-r1 RG-01 / registry-r2 revised review

Independent reviewer requested RG-01: duplicate returned-number detection made only
the new record UNKNOWN, leaving an existing LIVE owned record eligible for close
on the same ambiguous number. R1 manifest/archive and host14 evidence remain
historical; stdio-only duplicate coverage did not exercise this owned-alias path.

Temporary r2 changes only registry/test. Before raising, every matching owned
record is invalidated UNKNOWN; borrowed stdio remains BORROWED. New causal LIVE
alias plus distinct LIVE descriptor verifies no ambiguous-number close, actual
independent retirement, both records retained, and poisoned completion after
harness-only known mock-resource retirement and later empty snapshot. Final host-r5
15/15 PASS0 includes explicit borrowed-state assertion; host-r4 PASS15 is earlier.
Frozen manifest7f2f8ba886958a6fdc02bd2659ffc875a8bda4ab9b3dd153cb61a8aaf5d44c9e
and archiveb50240c00aabaaaf8954fd91fa49a06b193d9164140c4f042190c36b369d4eb3
submitted for independent re-review; RG-01 not self-closed. Three accepted helper
copies unchanged. No scanner/journal/root/CLI/native/policy/project/publication
selection; retained target readers and original blocked gate untouched.

### P06 registry-r2 accepted / local runtime-r1 composition review

Independent reviewer accepted exact temporary registry5 and host15; RG-01 CLOSED
for manifest7f2f8ba886958a6fdc02bd2659ffc875a8bda4ab9b3dd153cb61a8aaf5d44c9e
and archiveb50240c00aabaaaf8954fd91fa49a06b193d9164140c4f042190c36b369d4eb3.
Scope-only historical r1 host-r3 wording clarified against final r2 host-r5; no
accepted source bytes changed. Private opener/exclusive table premises and no
unresolved-resource absence authority remain explicit.

Next temporary runtime6 composes exact accepted inventory/execution/topology/
registry copies with new private one-shot LocalRuntime/tests. One budget begins
before borrowed pin capture; actual owned registry supplies topology. Callback
runs under SAME guard, all known cleanup attempts follow error/expiry, then actual
registry completion precedes result. Original functional exception wins while
cleanup failure remains separately recorded. No descriptor/child created during
factory construction; no destructor/retry/unknown adoption. Trusted callback must
register each scalar descriptor and return no live resources; Python privacy is
not hostile no-escape enforcement. Local completion is no durable recovery receipt,
OFD/child/job absence, scanner/backend or namespace authority.

Final host-r2 12/12 PASS0 includes actual own pin/close lifetime, two-close cleanup
with injected post-close error, late callback/final close, RESERVED/reentry/extra
FD poison, inherited and constructor clock faults. Host-r1 PASS9 is earlier subset.
No target/root/metadata/child/policy operation. Frozen runtime6/evidence sent for
independent local source review; scanner/journal/root/backend/CLI/project/native/
policy/publication integration remains unselected. Retained readers and historical
failures/original blocked subset unchanged.

### P06 runtime-r1 accepted / scan-lifetime-r1 local inspection review

Independent reviewer accepted temporary runtime6 and host12 for manifest
8046bf3112f18d4af7262203105827c47ea2bc53fb2adb6e3deafecc539e6bf4,
archive8eafb51ba2e8e56d71cfa6edcc0e83a3fc9764ac453777437e39ab80e32dba10.
No resource-escape/OFD/receipt/child authority follows; trusted scalar callback
premises remain. Prior nine-case host subset stays separate.

Next temporary scan-lifetime9 supplies optional fixed owned descriptor transitions
in a private scanner variant and a read-only inspect function over SAME runtime.
Parent plus11 fixed objects reserve slots3..14; actual open is registered before
scanner map assignment/pin/post-budget. Partial constructor failures retire known
registry pins even when scanner never received the return. Explicit scan close
and outer runtime attempt every independently known cleanup; UNKNOWN never retry.
Original functional exception survives cleanup errors, retained separately.
Value copies return only after all known retirement and successful runtime finish.
Selected inspection never invokes deletion; exported legacy delete budget/journal/
root composition remains separate. Six accepted helper/test copies unchanged.

Final host-r2 29/29 PASS0 = new inspection7 + unchanged legacy scanner22; r1 PASS29
is earlier diagnostic-retention variant. Actual own scratch pin/close cases and
mocked time/signature/error/virtual root labels remain distinct. Legacy22 own-tree
mutations are not new inspection deletion authority. No native/root/backend
capture/policy/journal/CLI/project/publication selection; frozen source/evidence
sent for independent review. All earlier failures/retained readers/original
blocked gate preserved.

### P06 scan-lifetime SL-01 / revised r2 source review

Independent review requested SL-01a/b on r1: pre-owner-close creator/identity hook
errors could evade retained diagnostic; outer runtime cleanup overwrote an earlier
scanner cleanup error. Prior r1 manifest/archive/host29 remain historical, not
accepted. R2 catches the entire close hook while creator validation remains first;
forwarding and revised runtime cleanup preserve only the first diagnostic.
Original functional exception remains original and every known cleanup is attempted.

R2 freezes10: prior9 plus exact accepted runtime12-test file. Hook/runtime/lifecycle
test changed; scanner variant unchanged from reviewed r1, six accepted helper/test
copies exact. Runtime helper revision explicitly supersedes its accepted byte-copy
claim only in this temporary composition; earlier runtime-r1 snapshot preserved.
New one-time creator error and DISTINCT scanner/orphan close error cases assert
original/first exception identity, known actual retirement attempts, UNKNOWN no
retry, failed return and SAME poison. Final host-r4 43/43 PASS0 = new inspection9,
legacy scanner22, runtime12; earlier r3 PASS31 remains distinct. Frozen manifest
79ed44936384f2d4cdaf159ab6480e619fbab4a6c504358f899cb22ae87b70ab and archive
000d7f9bcbaf7885dcb98ec831ee06133bcdd01aa84297609418f33cf035c00a
sent for re-review; SL-01 not self-closed. No selected deletion/root/backend/journal/
CLI/native/policy/project/publication work; retained target readers untouched.

### P06 scan-lifetime-r2 accepted / retirement-r1 local composition review

Independent reviewer accepted temporary scan-lifetime10 and host43; SL-01a/b CLOSED
for manifest79ed44936384f2d4cdaf159ab6480e619fbab4a6c504358f899cb22ae87b70ab,
archive000d7f9bcbaf7885dcb98ec831ee06133bcdd01aa84297609418f33cf035c00a.
First cleanup diagnostic and original functional identity are separate, not a
complete error trace/unknown-resource authority. Selected inspection stays no-delete.

Next temporary retirement12 changes only scanner per-action seam and adds fixed
retire_scope/tests; other9 accepted copies exact. SAME runtime guard brackets each
unlink/rmdir/fsync before and after actual operation, preserving existing inventory/
eligibility gates. Expiry/poison halts further mutations and rejects completion;
actual partial/full removal remains actual (no rollback/retained-path fiction).
Known registered pins retire despite expiry; first diagnostic remains retained,
UNKNOWN never retry. Default-null legacy actions and no-delete inspection unchanged.

Final host-r2 51/51 PASS0 = retirement8 + inspection9 + scanner22 + runtime12.
Actual own scratch12 pins/deletion/close distinct from virtual metadata/no-child
facts and fake clocks/error callbacks. Historical host-r1 FAIL1 assumed first
removal was SHM; corrected test counts six remaining fixed regular objects without
inventing order. Late first unlink/root rmdir/parent fsync and final close error
cannot return success even after actual removal. Frozen source/evidence sent for
independent local review. No root capture/backend/provisioning/journal/CLI/native/
policy/project/publication integration; retained readers/original gate unchanged.

### P06 retirement-r1 accepted / journal-reader-r1 local lifetime review

Independent reviewer accepted temporary retirement12 and host51 for manifest
0b495f542229de55a336c6c8ce97fef77b6b41e56422530b34d3a643777411b1,
archive016c02077a93e61cca2461a17ccb1c0a6184d0d255c0e5675eb701c4572dfdff.
Boundary classification remains separate from physical partial/full removal;
virtual authority/supplied no-child facts confer no operational root authority.

Next temporary journal-reader11 copies nine accepted helpers/tests exactly and
adds registered immutable-plan SH reader/tests. NEW runtime registers directory/
lock/plan slots3/4/5; exact named/held metadata, fixed paths, bounded READONLY plan
read/schema/key and journal/parent/lock identity revalidate under SAME guard.
Actual SH|NB stays through validation until known physical closes; no unlock or
lease/value-resource escape. Descriptive snapshot only; no producer install,
recover, receipt/policy/delete callbacks selected by the reader.

Final host-r2 46/46 PASS0 = reader9 + runtime12 + journal25; historical r1 PASS45
is earlier subset. Own inert journal provisioned before separate read budget,
producer physically closed. Actual same-process independent EX contention during
read and EX availability after closes are not exec/child/TLS/adoption evidence.
Late full read/SH, zero/corrupt/substituted plan and reported close/read faults
reject result, retain first diagnostic/original exception and attempt all known
closes. Host trust/path/UID/GID remain supplied own-scratch premises; root SMACK/
namespace/provenance/operational capture unselected. Frozen source/evidence sent
for independent local review; full producer/recovery registration, journal/CLI/
policy/target methods and publication remain separate. Retained readers and
original blocked different-subject gate unchanged.

### P06 journal-reader-r1 accepted / registered producer-r1 local review

Independent reviewer accepted temporary journal-reader11 and host46 for manifest
7fe0f6a98ef3d438d768745c840cad66d9f36e2981f6edec967f0bf62d9ccba8,
archivecea5f87ce15d909244821dec76a54db1e86cd5374121a2726b9e2b34dd471df9.
Registered SH read lifetime remains no role/TLS/receipt/child-absence authority.

Next temporary producer13 copies those11 exactly, adds fixed one-call producer/
tests. SAME runtime registers parent/journal/SH lock/pending plan/completed readback
slots3..7. Durable bounded write/fsync/checked physical close/no-overwrite link/
unlink/directory+parent fsync and readback equality precede descriptive future argv
and all8 fake callbacks. SH held throughout, known descriptors retire before value
return; no unlock/recovery/revoke/receipt or automatic artifact cleanup selected.
Partial write/link and callback failures retain actual pending/complete artifacts,
stop normal work and reject result; nlink2 late-link artifacts are not repaired.

Final host-r2 55/55 PASS0 = producer9 + reader9 + runtime12 + journal25. Earlier
host-r1 PASS54 predates registered readback and first explicit-close diagnostic
amendments; preserved as historical. Ordinary own scratch publication/modes/actual
closes and same-process EX contention distinguished from virtual trust/fake writes,
mock clocks/close errors. False write-progress readback rejects before callbacks;
reported physical temp-close EIO still attempts other known closes without retry.
Frozen source/evidence sent for independent local review. No root/backend capture/
recovery/CLI/native/policy/project/publication or target role selection. Original
blocked subset and retained readers unchanged.

### P06 producer-r1 accepted / registered receipt-r1 local review

Independent reviewer accepted producer13/host55 for manifest
96892138b87b9dfd1af74e131f20d04dca151eb186598cb881635306bafc3d20 and archive
02573317202423b704baad3e30c5ec0272edbaaf4b0215c51a95d13a1cbb6686.
SH retirement/value result remains no transferable role lease or recovery authority.

Full recovery composition raised a concrete contract question: accepted normal
guard poison prevents continuing callbacks, while cleanup_call is FD/reap only.
Independent development direction now permits separate exact8 RevokeSweep best
effort across ordinary scope/revoke failures ONLY while original authority/budget
remain valid; expiry/clock/authority loss stops new policy callbacks, late actual
callback is attempted-late/unknown, fixed remaining rows retained for later admission.
No budget renewal or cleanup_call policy reuse. Design only; source/runtime pending.

Independent temporary receipt15 copies producer13 exactly and adds nested registered
publisher/tests. Supplied EX/full provenance remain caller premises, not helper
inference. Caller directory3/lock7, stale/prior receipt4/5 and pending6 register
actual transitions; trusted named/held0600 singlelink/attributes before removal,
bounded write/fsync/physical close/replace/fsync guarded before/after. Late mutations
preserve actual artifacts but reject completion; first close diagnostic and all
known retirement retained. Receipt bytes descriptive and never authority; helper
has no recovery/revoke/scope delete or plan rewrite. Future diagnostic receipt
following failed sweep requires separate admitted guard/budget, no bypass implied.

Host-r1 62/62 PASS0 = receipt7 + producer9 + reader9 + runtime12 + journal25; own
scratch EX driver/artifacts distinct from virtual trust/no-child/provenance facts.
Frozen source/evidence pending local review; separate sweep implementation proceeds
under design-only direction. No root/backend/CLI/native/policy/project/publication
selected, retained readers and blocked subset unchanged.

### P06 receipt-r1 accepted / fixed RevokeSweep-r1 local fault review

Independent reviewer accepted temporary receipt15/host62 for manifest
10404e27d8ba8f520725ff539351777d08cc37494b300b400a6615cbb2aa3d02,
archivef191f72d6ce37f9a6b809acb48786a7918178e473cbe4dfdbb3ee317441f9259.
Caller EX/full provenance are premises; poisoned/expired recovery cannot publish
through this ordinary guard or infer authority from physically published bytes.

Next temporary sweep5 implements separate accepted development contract, with
inventory/execution/plan copies exact. Clean original admission preallocates exact8
private fixed------ rows and captures original budget; mandatory independent
Authority remains unselected. Creator/topology/time/authority checks before/after
attempts; ordinary scope/row exceptions poison but permit later fixed best effort
ONLY within independent authority/budget. Expiry/clock/EX/provenance/unknown
retirement loss starts no further callback; late attempted rows UNKNOWN/not retried,
remaining NOT_ATTEMPTED retained for later independent admission. Final report
construction budget checked. No cleanup_call policy reuse/budget renewal/ledger
clear, no receipt or authoritative final recovery result. Diagnostic-only caller
retirement-error hook preserves first error without performing/authorizing cleanup.

Final host-r5 19/19 PASS0 uses fake clock/topology/authority/callbacks/close-report
objects only, NO FD/EX/child/root/policy operation. Historical r1 FAIL1 miscounted
admission checks, corrected with causal row-state expiry; r2 ERROR16 misplaced
retirement field in slotted Row, fixed; r3 PASS16/r4 PASS18 earlier subsets. Source/
fault evidence frozen and sent for independent review. Concrete exact-plan/EX/
namespace/provider verifier, actual known cleanup and complete journal/receipt
composition remain separate; no native/project/publication selection. Retained
readers/original blocked subset unchanged.

### P06 RevokeSweep RS-01 / revised r2 local fault review

Independent review requested RS-01 on frozen r1: generic shared ExecutionFailure
could mean on-time authority/retirement loss, so exception type alone mislabeled
ATTEMPTED_LATE_UNKNOWN. R1 manifest/archive/host19 remain historical, not accepted.
R2 changes only sweep/test: dedicated SweepDeadline from actual sweep clock expiry
selects LATE; all other boundary failures UNKNOWN. Original identity/stop/poison/
no-retry behavior preserved. Accepted copies3 unchanged and no resource selection.

Final host-r7 21/21 PASS0 includes on-time authority and retirement ExecutionFailure
objects/clock0, identity, one attempt/no later callback and UNKNOWN, plus explicit
true last-row expiry SweepDeadline/LATE control. Host-r6 PASS21 predates added
control assertion. Frozen manifest6effdab8d43173dd10aa262fc723d9317b95ba1c74fc8d6813dbe4b7f6280188
and archive4fd3a5344cfd88af9f482abe9be6f1311e511731fd9d71a1a3ae57beaa4d6eab
sent for independent re-review; RS-01 not self-closed. Fake clock/authority/error
objects only; no EX/FD/child/root/backend/journal/CLI/policy/native/publication.
Retained target readers and original blocked subset unchanged.

### P06 RevokeSweep-r2 accepted / registered recovery-admission-r1 local review

Independent reviewer accepted sweep5/host21, RS-01 CLOSED for manifest
6effdab8d43173dd10aa262fc723d9317b95ba1c74fc8d6813dbe4b7f6280188,
archive4fd3a5344cfd88af9f482abe9be6f1311e511731fd9d71a1a3ae57beaa4d6eab.
Dedicated local clock expiry alone means LATE; generic authority/retirement failure
remains UNKNOWN. Fake admission remained no EX/resource/provenance authority.

Next temporary recovery-admission13 copies eleven accepted journal/runtime/sweep
helpers/tests exactly, adds local EX/immutable-plan binding and tests. NEW runtime
owns directory/EX lock/plan3..5; private captured exact bytes and registered LIVE
pins are rechecked around every fixed revoke through OwnTopology, held/named
journal/lock/plan/parent metadata and mandatory Provenance (default UNSELECTED).
Actual local read/EX lifetime bound to exact8 ordered------ fake callbacks; ordinary
scope/row errors retain poison and later attempts only within independent original
budget/authority. No scope deletion or receipt selected. Metadata identity is not
OFD proof; exclusive table/no-unlock/no-conversion remains prerequisite.

Final host-r3 72/72 PASS0 = admission14 + sweep21 + runtime12 + journal25; r1 PASS67
and r2 PASS71 earlier subsets. Ordinary own EX/plan/pins/proc checks distinct from
fake provenance/revokes/supplied scope error. Actual EX record retirement/unknown
known-test alias/change of admitted bytes stops after first callback; runtime never
closes unknown alias, harness alone retires its known mock resource. First final
close and original scope object retained; secondary diagnostic failure cannot mask
original functional error. Mock EX contention/error objects are not role/child/
root/policy evidence. Frozen source/evidence pending independent local review.
Concrete root capture/backend/provider/namespace/retirement admission, scanner
scope deletion and receipt/CLI/full recovery composition remain separate. No native/
policy/project publication or retained-reader access; original gate unchanged.

### P06 recovery-admission-r1 accepted / full LOCAL composition-r1 review

Independent reviewer accepted temporary admission13/host72 for manifest
6722176c48128330fada90b79873c0ca4bb1b51950451910052b8daee46cfb77,
archive aed3a03f0dc9b690a22bf4226eca0790857eed3a687e106b9e90783ee1b89af8.
EX/immutable plan/owned pins now bind fixed revokes locally; provenance remains
UNSELECTED/mock, no root/child/policy/CLI/project publication authority.

Next temporary recovery-composition23: twenty accepted copies exact, new local
composition/test plus fixed receipt recovery slots20..22 (standalone4..6 retained).
One original NEW runtime/guard budget owns journal3..5, scanner8..19 and receipt20..22.
Initial/repeated supplied known retirement facts and sticky OwnTopology failures
bind independent sweep authority; transient observation cannot be repaired later.
Scope deletion uses prior bounded scanner, closes all known scanner pins/orphans
without releasing journal EX, retains original scope/first close diagnostic and
unknown records. Ordinary scope/row failure allows fixed best effort only within
independent original authority/budget; unknown/late failures stop new callbacks.
Observed absent scope creates no opener/reservation. Full durable plan retained.

Only clean phases may publish explicitly BLOCKED/authority=NONE diagnostic receipt
through ordinary guard, stating final known retirement pending. No receipt through
poison/expiry, no renewed diagnostic budget or authoritative success receipt.
Physical publication followed by late/final-close failure remains non-authoritative;
LOCAL_PHASES_COMPLETED returned only after runtime known closes/finish, not root
recovery/policy enumeration. No root metadata/provider/namespace/backend selected.

Final host-r6 actual134/134 OK/no skips/exit0: composition16 + admission14 + receipt7
+ retirement8 + inspection9 + scanner22 + sweep21 + runtime12 + journal25. Actual
own scratch/EX/preads/pins/deletion/receipt/proc facts remain separate from virtual
metadata/fake callbacks/clock/error/supplied no-child facts. New cases include
ordinary unlink failure then8; failed row then8; late actual unlink/no callbacks;
transient topology/changed absence refusal; scanner close-then-error/all15 closes;
original scope plus distinct close object; already absent scope; stale/prior fixed
receipt slots; late replace and final-close failure retaining only BLOCKED diagnostic;
creator-query cleanup failure retained through later known closes. Historical r1
PASS12 subset, r2 FAILED1/133 test-harness root-before-children removal error, fixed
harness r3/r4/r5 PASS133 remain preserved, never relabeled.

Frozen manifest698814edf9971acaa7f8189a1b42d62b66999cbc6befef5024dce59baa1591c9,
archive4c9225c812922e1e2ba4283c39837513ee9328e43cd54f254c010458279f268a,
full logSHA c8b7397586aa81e342ec92823bd28ed7b58c19ea5f14f0be9fc42c6006076d71
and exact scope/diff/evidence sent for independent local review, pending disposition.
No native/root/policy/CLI/project publication run, no operational provenance or
hostile-race/OFD/child absence/hard-preemption proof. Retained iCNk4Q/B29oJu/Vmiui5
untouched, original different-subject gate BLOCKED, published HEAD unchanged.

### P06 recovery-composition-r1 RC-01/RC-02 / revised LOCAL r2 review

Independent reviewer returned CHANGES_REQUESTED for frozen r1/host134: creator
query was outside StickyTopology latch; failed actual absence observation poisoned
common ledger but left prior True flags intact, allowing independent sweep checks
to mistake restored facts for valid admission. R1 source/archive/log preserved.
RC-01/RC-02 remain pending explicit reviewer confirmation, not author self-closed.

R2 temporary24 changes inventory variant/composition/test, adds exact accepted
inventory16 test file. Other21 r1 members unchanged, receipt20..22/default4..6
unchanged. Inventory explicitly revised: retirement_failure captures FIRST role
attach/no-spawn/reap/absence validation error via existing require/poison semantics;
ordinary scope/revoke poison remains separate. Later successful known observations
or different errors cannot erase/replace that reason. Known record-release behavior
unchanged. RecoveryAuthority rejects retained retirement reason before current facts.
Topology creator validation enters SAME first-error try while remaining before
record/underlying observation. Restored query/original empty topology cannot repair.

Final host-r2 153/153 OK/no skips/exit0 = composition19 + inventory16 + admission14
+ receipt7 + retirement8 + inspection9 + scanner22 + sweep21 + runtime12 + journal25.
RC causal cases: one-time owner creator error at actual scanner guard observation;
actual confirm_absence(False,True) after successful initial admission with/without
later successful confirm. All retain exact first error/poison, five known closes,
zero revokes/receipt/local result. Subsequent success/different absence error still
cannot repair. Ordinary scope unlink EIO ->8 and unknown inventory ->8 controls
assert no invented retirement error. Host-r1 PASS153 precedes extra explicit
control/first-error assertions. Host facts remain scratch/EX/pins and fake provenance/
virtual labels/supplied child absence; no kernel-child/operational/root authority.

Frozen manifest2a87413b5a1ee023f5df420a7965c34e200d939f4b01908fba1b73b872505eb6,
archive5568b7537d77df26961a900bc117ecb5ee01da866ecdce7df5be07d440351794,
logSHA9e0b841526c1c93940ddd4bfd55f285f3a3746d494a079f06bcf7eebd80327ad
plus scope/diff/evidence sent for independent re-review. Diagnostic BLOCKED/authority
NONE receipt stays clean-only and non-authoritative before final known retirement.
No native/root/backend/CLI/policy/project publication, retained target scopes untouched,
original different-subject gate BLOCKED, HEAD unchanged.

## AMD-PLUGIN-r1 — functional source/host checkpoint ACCEPTED

Owned optional AMD worker initializes/checks/imports the existing Action DB and
reconciles every five seconds. Existing generations use guarded SH (AMD-01);
missing DB initialization uses EX. Failed initial source readiness retains the
worker and retries a fresh service (AMD-02), with stop/join before fini/unload.
Default fixed configuration is disabled; real app_fw/layout/label provisioning and
actual public-create authorization remain prerequisites. Periodic fallback is not
SYNC-01. CAPMGR stream dlog, public named error/kind enums, API pc location, RPM
module/config payload and concise AGENTS updates are included. Source notice-only
changes are separately identified; no invented attribution or dummy service/socket.

Host `/tmp/capmgr-amd-host-r1`: final focused20/20 (11 module +2 config +7 import),
including recorded missing-source failure then actual retry/import after5033ms;
full CTest29/29 PASS17.31s. C/C++ default enum width/value checks and pure C consumer
pass. testing-OFF configure/build/install-staging PASS; ELF12 CAPMGR_0 exports and
SONAME0, unchanged pc template bytes/Name/filename. Host/Tizen rpmspec parse PASS.
Evidence `/tmp/capmgr-evidence/amd-plugin-host-focused-r2.log`,
`amd-plugin-host-check-r2.log`, `amd-plugin-host-off-*-r1.log`,
`amd-plugin-exports-r1.txt`, and `amd-plugin-*-spec-r2.txt`. No RPM built/installed.
Earlier focused20 passed before the retry test had causal synchronization; final
test waits for actual startup failure. Initial fullcheck failed from a test edit
and integer-to-enum C++ arguments; fixed with CAPMGR_KIND_ALL before final check.

Platform option configuration rejects missing host pkgconfig amd. Local official
header syntax stops at missing generated tzplatform_variables.h (initially also
missing tizen_error include); no substituted headers. Dlog implementation syntax
with actual local dlog/common headers PASS. Optional module link/AMD loading/native
activation NOT_RUN; no privileged/device/policy work. SpecRelease16 is source-only;
installedRelease15 unchanged. Existing retained target readers remain untouched.

Recovery-composition-r2 is explicitly DEFERRED/UNREVIEWED, not resumed or accepted;
RC-01/RC-02 remain OPEN. Prior153-test/frozen24 report is pending owner evidence,
not reviewer-verified closure. Preserve all preceding history. No publication yet;
request ordinary feature-sized peer review before scoped commits.

Independent reviewer accepted the stable AMD feature/source/host/docs/instruction
checkpoint and closed AMD-01/AMD-02. Prior review-pending wording above records the
historical request state. Notice-only commit a95f027 is separate; feature publication
follows this disposition. Next: actual native AMD dependency/build validation and
connected public-create authorization integration, with privileged methods reviewed
when required. Native activation/SYNC-01 and deferred RC-01/RC-02 remain open.

## API-HANDLE-VOID-r1 — source/host ACCEPTED

Public client/results handles are void* typedefs following Action. Private C++
Client/SearchResults owners live in the implementation's anonymous namespace;
incoming boundaries static_cast before typed access/delete. No pointer discovery,
registry or changed ownership model. Existing valid-live-handle/null/creator/BUSY/IO
output/callback contracts retained. C _Generic and C++ is_same assert exact aliases.

Affected host build PASS; verbose CTest unit123 + adapters214 + pure C consumer,
3/3 PASS14.41s/no skips, covers query/result survival, asynchronous callback/cancel,
destroy BUSY/IO, creator and lease lifecycle. Evidence
`/tmp/capmgr-evidence/api-handle-void-host-build-r1.log`,
`api-handle-void-host-tests-r1.log`; exports-r1 still12 CAPMGR_0.
No native/device/privileged work or expanded arbitrary-pointer validation claim.
RC-01/RC-02 remain deferred/unreviewed; historical retained scopes untouched.

Independent reviewer accepted exact seven-path source/docs/instruction scope and
selected host evidence; scoped incremental publication authorized. Next remains
actual platform AMD prerequisites and connected public-create admission.
