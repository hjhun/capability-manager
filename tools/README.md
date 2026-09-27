# Verification tools

These tools exercise the implemented catalog/adapter fixtures. They do not claim
real installer finalization, Action writer notifications, platform privileges,
resource mounts or TIDL runtime integration. Every report currently labels those
platform checks NOT_RUN. A successful fixture run is not P07/P08 completion.

`verify.py` requires Python 3 on the controlling host and SDB for device runs.
Every SDB operation requires an explicit online serial; multiple connected devices
never cause automatic selection. It records commands, elapsed times, transport
and remote exit status, and a JSON summary. Logs may contain device data: keep the
output under ignored `artifacts/verification/<run-id>/` or an external temporary
directory, never commit raw logs.

```sh
python3 tools/verify.py preflight --target sdb --serial emulator-26101 \
  --sdb "$TIZEN_STUDIO_ROOT/tools/sdb" --output artifacts/verification/preflight
python3 tools/verify.py foundation --target host --build-dir build \
  --output artifacts/verification/host
python3 tools/verify.py smoke --target sdb --serial emulator-26101 \
  --sdb "$TIZEN_STUDIO_ROOT/tools/sdb" --output artifacts/verification/smoke
python3 tools/verify.py integration --target host --build-dir build \
  --output artifacts/verification/adapter-fixtures
python3 tools/verify.py perf --target host --build-dir build \
  --rows 1000 --iterations 100 --output artifacts/verification/perf
```

Replace the example serial with an actual `sdb devices` serial. `preflight` probes
the environment; a failed command is not proof a package is absent. `foundation`
runs CTest with empty/missing test trees treated as failure. `smoke` runs installed
foundation tests and the C consumer. `integration` runs parser/Action/CLI fixture
tests, not production platform transactions. SDB executable paths default to
`/usr/libexec/capmgr`; override using `--remote-bin-dir` for an owned build scope.

`catalog_benchmark.cc` creates a unique temporary SQLite catalog, seeds the selected
number of rows, measures warm queries and open/query/close operations, and removes
only its own directory. Reopen timings retain the OS cache; they are not cold
storage measurements. Peak RSS covers the whole process including catalog seeding.
Measurements include row/iteration counts, pointer width, DB size and cleanup.
No production database or system cache is modified. The benchmark target is
`capmgr-catalog-benchmark`, built by the CMake check target and included in the
tests RPM. The tests package also contains verify.py and its watchdog.

To build inside a selected x86_64 Tizen emulator, provide the project archive with
one `capability-manager-0.1.0/` root and the pinned upstream nlohmann/json v3.11.3
archive with one `json-3.11.3/` root. The latter SHA256 must be
`0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406`.
Archives accept only regular files/directories without absolute/escaping paths,
with 128 MiB compressed/512 MiB expanded and 100,000-member limits. Reports record
both input hashes and sizes.

```sh
git archive --format=tar --prefix=capability-manager-0.1.0/ HEAD | gzip -n > /tmp/capmgr-source.tar.gz
python3 tools/verify.py native-build --target sdb --serial emulator-26101 \
  --sdb "$TIZEN_STUDIO_ROOT/tools/sdb" --source-archive /tmp/capmgr-source.tar.gz \
  --json-source /tmp/capmgr-json-v3.11.3.tar.gz --cleanup \
  --output artifacts/verification/native-build
```

Native build uses an exclusive UUID directory under `/opt/usr`, a marker file,
TIDL-required CMake configuration and one compiler job. Cleanup removes only its
marked scope only after a successful transport and one observed completion sentinel.
Transport loss or a missing sentinel preserves the scope because remote completion
is unknown. The watchdog kills remaining process-group descendants even if the
leader exits normally, keeping its PID unreaped until cleanup. Commands that
deliberately escape the process group need separate cgroup supervision; this
build runner does not claim that production launcher isolation. A timeout preserves the scope and reports
failure; inspect and stop remaining owned processes before manual cleanup. The Python process-group watchdog deadline precedes the controlling SDB timeout. Failed transfers can also
leave the recorded scope for diagnosis. No existing operational DB is opened.

Native prerequisites: C++20 compiler including cpp/cc1, CMake, make, RPM tools,
diffutils, Python 3, SQLite FTS5 development files, GTest/GMock and TIDL. Use packages matching
the target repositories. ARMv7 builds are prohibited before P08 completion, even
though the runner itself is portable; ARM build and ARM execution are separate.

Test the collector failure paths with:

```sh
python3 -m unittest discover -s test/tools -p '*_test.py'
```

## Native TIDL connection fixture

With matching rpc-port, bundle, dlog, GLib and Cynara development packages,
configure with `-DCAPMGR_BUILD_TIDL_TRANSPORT=ON -DCAPMGR_REQUIRE_CYNARA=ON`
and `-DCAPMGR_REQUIRE_TIDL=ON` in addition to the existing dependency options.
The `check` target compiles generated proxy/stub code and the `capmgr-tidl-probe`
fixture. It does not run a privileged service connection during ordinary CTest.

On the explicitly selected native fixture image as root with real RPC/Cynara
services, run:

```sh
python3 test/integration/run_tidl_probe.py --binary build-native/capmgr-tidl-probe
```

The Tizen tests RPM installs the binary and driver under `/usr/libexec/capmgr/`.
It also installs `capmgr-peer-tests`; set `CAPMGR_REQUIRE_PEER_TESTS=1` to require
socket label support without a skip. The IPC fixture creates a unique daemon
endpoint and owned temporary readiness/log directory, checks that the server's
verified MAIN principal is the client PID, and confirms remount remains unsupported.
A child drops to system UID 1 and makes 32 connections that must be denied by the
explicit policy check. A subsequent authorized connection must still succeed;
retained rejected ports/services must be zero and FDs must return below the warmed
connection count. No privilege policy is changed by this fixture.
The driver bounds client/server waits and cleans up only its own server process.
It prints captured server output before removing temporary files, so redirect the
command's output to your evidence directory. This is connection authorization
validation, not production catalog/CLI/mount integration or a full policy matrix.

The local generator adapter rejects unknown TIDL layouts instead of generating
an unchecked binding. MAIN and callback channels must be distinct sockets of the
same live process/namespace. Every dispatch rechecks the actual channels and
Cynara on MAIN. Callback extension getters are not used as authority. Initial
PID binding and current-task-label gates described in docx/07 remain open.

## Offline package transaction harness

`capmgr-package-tool` is an administrative tool in the `offline-tools` RPM. It
uses the same private catalog/parser code without AMD, TIDL or a public client.
It has no default database, environment-selected path, installer hook or inferred
outcome. The explicit `--offline` flag and an absolute image DB path are required.
Use only an owned offline image/fixture directory with trusted ancestors and
exclusive administrative control of the DB, sidecars, manifests and resources.
The regular-file/symlink checks are input hygiene, not protection against a
malicious process concurrently replacing paths in that directory.

A version-1 manifest replaces the **complete** capability set of one package.
Repeated metadata keys and semicolon entries are supported; App Skill entries
also require `appId`. CLI uses the capability/cli key. Action keys are rejected.
An empty metadata array intentionally stages replacement with an empty set.
Operation IDs must be unique across the image's catalog lifetime.

```json
{"version":1,"operation":"image-build-42-package-1","owner":"org.example.pkg",
 "mode":"replace","root":"/owned/image/opt/usr/apps/org.example.pkg",
 "metadata":[{"key":"http://tizen.org/metadata/capability/skill",
              "value":"skill.json;other-skill.json"}]}
```

```sh
/usr/libexec/capmgr/capmgr-package-tool --offline /owned/image/catalog.db stage /owned/package.json
/usr/libexec/capmgr/capmgr-package-tool --offline /owned/image/catalog.db status image-build-42-package-1
/usr/libexec/capmgr/capmgr-package-tool --offline /owned/image/catalog.db finalize image-build-42-package-1 success
```

`stage` validates all descriptors before opening/creating the DB and durably
records hidden pending rows. Stage replay must have identical owner/payload.
`finalize ... success` atomically replaces that owner's rows and FTS and advances
the revision. `finalize ... failure` discards pending rows and preserves the
published generation. Repeating the same final outcome is idempotent; changing an
already recorded outcome conflicts. A crash or unknown result leaves pending data
hidden; `status` uses read-only access and never infers success. Exit0 means the
requested catalog operation succeeded, exit1 has a JSON error on stderr, and
exit2 indicates invalid command syntax. Success emits one JSON object on stdout.

Removal uses `{"version":1,"operation":"remove-42","owner":"org.example.pkg",
"mode":"remove"}` without root or metadata, allowing already-removed package
resources. Published rows remain until explicit success; failure preserves them.
The caller is the authority for the supplied outcome. This harness does not prove
MIC/online installer finalization or make a partial non-undoable uninstall safe.
Do not wire it to CLEAN, POST, backend exit0 or package DB presence as a substitute
for an authoritative finalizer. Production parsing remains fail-closed.

Run its separate-process fixture tests without platform services:

```sh
python3 test/integration/package_tool_test.py build/capmgr-package-tool
# Installed tests RPM:
python3 /usr/libexec/capmgr/package_tool_test.py /usr/libexec/capmgr/capmgr-package-tool
```


## Private PID namespace setup fixture

The tests RPM includes `capmgr-namespace-probe`, a development fixture with no
setuid bit or file capabilities. Run it explicitly as root on the selected test
image with app_fw and the reviewed System label policy:

```sh
python3 /usr/libexec/capmgr/run_bounded.py --seconds 30 -- /usr/libexec/capmgr/capmgr-namespace-probe
```

It requires CAP_SYS_ADMIN and credential/SMACK setup permissions and fails on
missing prerequisites. Ordinary CTest builds it but does not count its privileged
execution as a unit-test pass. All job mounts are private and the original proc
mount stays unchanged. The fixture checks UID/group/capability/no_new_privs/FD
isolation, setsid descendant cleanup, failed exec (including app_fw permission
denial), and parent death before setup, before GO, and after exec. Parent-death
cases use subreaper adoption to observe the init child without sending it a signal.

The routine supports a single-threaded trusted creator only. Before clone the
creator opens its own proc directory and mount namespace from image-trusted procfs.
The child rejects a shared mount namespace before any mount, anchors creator
liveness to that proc object and bounds the GO wait to five seconds. The fixture
also deliberately omits CLONE_NEWNS and retains a GO writer after creator death;
one case delays init until after the creator has died, exercising the missed
PDEATHSIG window. No supplied application FD is accepted as either anchor.

NamespaceInit is a private, trusted setup routine; it is not an authenticated
broker or a sandbox against filesystem/network/service access. No arbitrary path,
UID or FD interface is exposed to applications. Production CLI remains gated on
trusted registration, original caller authorization, launcher-unit authentication,
image SMACK policy and cgroup limits. D-state teardown has no unconditional bound;
OwnedChildren reserves pending jobs and preserves signal errors for retry. Its
fail-stop destructor cannot substitute for a long-lived broker/reaper. Installed
fixture success does not enable public execution or resource remount.


## Explicit SQLite WAL DAC/SMACK fixture

The tests RPM includes `db_access_probe.py` without setuid or file capabilities.
On an explicitly selected disposable development image, run as root:

```sh
python3 /usr/libexec/capmgr/run_bounded.py --seconds 180 -- python3 /usr/libexec/capmgr/db_access_probe.py --run
```

This writes temporary SMACK load2 rules only for fresh UUID labels. Existing
System/current-label subjects gain access only to the new fixture DB label; new
reader labels gain only needed ancestor traversal. It does not install production
policy. It requires the platform-owned procfs/PID1 context, the same mount and PID
namespaces as PID1, root-owned ancestors without group/other write or POSIX ACLs,
local ext4 backing for /opt/usr, app_fw, priv_platform and symlink-safe rmtree.
A hostile root or mount administrator is outside this explicit fixture's model.
Failed preflight leaves policy unchanged. Ordinary CTest runs unprivileged recovery
tests, not this root policy operation.

The writer uses app_fw and System. Two SQLite generations exercise committed WAL
queries using mode=ro while sidecars exist, then last-writer close/removal and
sidecar recreation with inherited ownership/mode/label. Read denial is checked
separately for missing DAC group, denied SMACK label and an app_fw-UID process with
a denied label. Allowed readers cannot write; additional DAC-writable objects
isolate the MAC write denial. No immutable option or reader DB write grant is used.
The fixture does not prove that a reader can recover absent WAL/SHM without a writer,
that production privilege registration works, or that an actual privileged app has
end-to-end access to the future catalog.

Before the first policy write, the tool fsyncs a root-only recovery journal outside
the app_fw-owned tree and prints an absolute recovery command. Every fixed fork role
holds an inherited shared lock until exit. Normal completion reports cleanup,
remaining children and remaining rules. If a watchdog/SIGKILL/disconnection prevents
completion, preserve that output and execute the printed command, for example:

```sh
python3 /usr/libexec/capmgr/db_access_probe.py --recover /opt/usr/capmgr-db-recovery-<printed-UUID>
```

Recovery reruns the parent trust checks and refuses while any fixture role retains
the lock. Do not delete the lock, journal or owned tree to bypass refusal. Once all
roles have exited, it checks scope identity, safely removes the tree, attempts every
rule revocation and records exact remaining rules/errors for retry. Use a watchdog
of at least180 seconds, longer than normal role and cleanup budgets. A blocked
cleanup is a failed fixture, not a completed policy test. Root-only JSON receipts
and inactive kernel UUID label names intentionally remain; raw receipts/logs must
not enter Git. No production DB, existing policy pair or user data is modified.

## Explicit worker-loop namespace fixture

Release9 tests include `capmgr-worker-probe`, with no setuid/file capabilities or
service activation. On the selected development emulator as root:

```sh
python3 /usr/libexec/capmgr/run_bounded.py --seconds 60 -- /usr/libexec/capmgr/capmgr-worker-probe --run-root-fixture
```

The fixture creates a root-only, uniquely named catalog under `/opt/usr`, reads one registered
CLI into a bounded immutable snapshot before admission, and runs its own fixed
workload as app_fw301/System in new PID and mount namespaces. It checks normal
separate stdout/stderr, setsid-descendant cleanup, cancellation and output queue
pressure with the actual NamespaceInit/OwnedChildren path. Complete records follow
exclusive WNOWAIT/reap; output alone is never a completed native result. The GO
writer remains open until cleanup. The worker has four slots, independent priority
CANCEL input and a fixed transport queue; no active-job SQLite read is performed.

Success requires checked scope cleanup before the PASS/zero verdict. Early
no-child failures remove the scope; cleanup errors report the retained exact path
and return nonzero. Unconfirmed child cleanup retains the scope and fails stop;
a watchdog or abnormal process exit is not proof that every child has exited.
Preserve the printed path/evidence and establish absence before deleting residue.
The `--fail-after-scope` and `--fail-cleanup` options are failure-injection fixtures;
the latter intentionally retains a root-only scope and returns1 without spawning.

This does not install a broker, launch an application-selected executable, provision
SMACK/cgroups, prove original-client authorization or enable public CLI execution.
Production snapshot source/invalidation, fixed worker exec/FD layout, frontend
journal/status integration and uncertain-worker recovery remain separate gates.
