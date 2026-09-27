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
| P01 | IN_PROGRESS | C++/SQLite sources and proposed public header; build/tests pending |
| P02 | IN_PROGRESS | Private catalog staging/publication implementation; tests pending |
| P03 | IN_PROGRESS | Local search implementation started; Action integration pending |
| P04 | NOT_RUN | CLI engine pending |
| P05 | BLOCKED (paths) | PATH-01 user question pending; independent work continues |
| P06 | NOT_RUN | TIDL/AMD/RPM integration pending |
| P07 | NOT_RUN | Emulator toolchain preparation pending |
| P08 | NOT_RUN | Performance/stability and physical-device tests pending |
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
| P00-PUBLISH-r2 | Docs + reviewed contract header only | PENDING | Changes sent for re-review |

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
