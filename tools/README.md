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
