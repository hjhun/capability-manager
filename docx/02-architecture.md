# Capability Manager architecture and internal responsibilities

Document status: **preimplementation design**, reflecting the latest agreements through 2026-09-27. The directory and module names below propose a division of work; they are not existing or working code. Requirement IDs refer to [R01–R18](01-requirements.md), public signatures and payloads to [API contracts](03-api-contracts.md), development order to the [implementation plan](04-implementation-plan.md), and open items to the [decision record](07-decisions-and-open-items.md). Where the earlier `docs/design-discussion.md` conflicts, follow the latest agreements and open items in this document set.

## 1. Boundaries and principles

- The public C API uses the `capmgr_` prefix; implementation is C++20. Build with CMake and deploy RPM packages. Refer to Tizen Watcher's directory structure, packaging, and TIDL generation without copying its C++17 setting. [R01]
- Find Skill, App Skill, CLI, and Action in one catalog. Enumeration, search, and detail use a **local read-only SQLite connection in the Agent process**. Do not use per-query IPC or load the whole catalog into RAM. [R02, R09]
- `client_create` checks privileges and prepares local read-only DB access. It does not implicitly start a service, import all Actions, mount resources, or launch a CLI. Return a defined error if the catalog or DB schema is not ready. [R02, R07, R14]
- Package metadata parsers write Skill/App Skill/CLI information directly to the DB. This path must work in MIC without AMD or the launcher. Import Actions from the existing Action DB without reparsing Action metadata. [R03, R04]
- Put maintenance in a **CapMgr AMD module without changing AMD core**. The CLI launcher is a separate systemd service running as `app_fw`. Use TIDL where interprocess communication is needed. [R01, R10]
- Do not expose installation, registration, DB writes, index rebuild, or an internal mount writer through the public API. Public `remount_resources(client, destination_path)` is an explicit resource-access request that also handles the first mount. [R07, R13]

## 2. Components

| Component | Responsibility | Outside its responsibility |
|---|---|---|
| `libcapmgr` | C ABI, read-only DB queries, common JSON projection, explicit remount requests, CLI/Action execution and cancellation, callback lifetime | Package registration, Action metadata parsing, catalog maintenance |
| Shared private catalog layer | Input normalization, ownership and ID validation, DB/FTS transactions, migrations, generation and recovery state | A public writer API for the Agent |
| CapMgr metadata parser plugin | Skill/App Skill/CLI metadata, repeated keys and semicolons, direct DB updates for package install/update/remove | Requiring AMD or launcher startup for registration |
| CapMgr AMD module | DB preparation and maintenance, startup Action DB import, Action resynchronization, changed events after CapMgr commit, coordination of internal resource access | AMD core changes or writes to the source Action DB |
| CLI launcher | Run allowed CLIs as `app_fw`; collect stdout/stderr; enforce limits, timeout, cancellation, and child cleanup | Shell command interpretation or Skill-body processing |
| Resource access helper | Verify target namespace and destination; explicitly remount with required privileges | Giving broad mount privileges to the Agent |
| Existing Action service and DB | Source definitions, execution, subscriptions, Entity and provider information | Skill/App Skill/CLI registration |
| Verification tools | Integration, smoke, and performance scenarios with reproducible evidence | Deleting operational DBs or silently elevating privileges |

A shared DB library can reuse common code, but keep private writer exports and installed headers separate from the public C ABI. Parsers and the AMD module use the same DB rules; as separate processes they coordinate through DB transactions. [R03, R13, R14]

## 3. Proposed directories and build

```text
capability-manager/
  CMakeLists.txt
  cmake/Modules/
  packaging/                 RPM spec, manifest, launcher unit, required policy
  tidl/                      Internal control, execution, change-event definitions and generation
  src/
    api/                     Public capmgr.h, capmgr_error.h, C ABI implementation
    common/                  JSON, error conversion, path validation, limits
    catalog/                 Private DB/FTS, migration, generation handling
    pkgmgr-plugin/           Metadata parser callbacks and MIC shared writer
    amd-module/              CapMgr module, Action import, maintenance
    launcher/                CLI process lifetime and output handling
    resource-access/         Namespace-aware helper/adapter
  test/
    unit/                    GoogleTest, GoogleMock, fixtures, mocks
    integration/             Cross-process and real SQLite integration tests
    fixtures/                Skill/App Skill/CLI/Action packages and failing inputs
  tools/
    integration/             Integration scenario runner
    smoke/                   Fast emulator acceptance tests
    perf/                    Latency, CPU, RSS, I/O, WAL measurement
  docx/                      This development document set
```

This tree describes responsibilities. Verify actual target and RPM subpackage names, AMD module ABI, TIDL generator outputs, pkg-config dependencies, and device installation paths against existing platform source during the corresponding phase. Do not document nonexistent targets or GBS profiles as successful commands.

Use `.h` for public headers and `.hh/.cc` for internal C++ headers/sources. Do not hand-edit generated TIDL proxies/stubs. Separate runtime/devel/test RPM outputs, install public headers explicitly, and avoid installing private writer headers with a `*.h` glob. Register GoogleTest/GoogleMock tests in CTest and run them from RPM `%check`. [R01, R16]

## 4. Data ownership and identity

| Kind | Source | Ownership and conflict rule | Public information |
|---|---|---|---|
| Skill | Package metadata referring to Agent Skills resources | Reject installation when another package owns the item; allow same-package updates | Name, description, source, Skill directory path and availability |
| App Skill | Skill resources associated with an app | **Allow the same name in different apps**, using app identity as scope | App scope, name, description, path and availability |
| CLI | Package CLI descriptor and RPK `bin` executable | Reject another package's ownership of an existing item; allow same-package updates | Executable descriptor and input/output contract |
| Action | Existing Action DB | Action side owns definitions and identity; CapDB contains a query projection | Common description, schemas, related Entity, provider app IDs and default app ID |

Normalize repeated metadata keys and semicolon-separated entries through one collection path. Define handling for duplicate paths and IDs, empty entries, and invalid files; do not partially register the first entries of a package and report success. Do not assume each pkgmgr callback contains the entire package list. Verify package-level collection and publication boundaries from actual callback flow. [R05, R06]

An App Skill's identity scope is distinct from its human-readable directory name. The exact source/destination rule for the user's final `-appId-skill` suffix remains **PATH-01 open**. Do not treat an earlier path proposal as fixed or reject same-named Skills across apps. [R06, R07]

## 5. Registration and Action synchronization

### 5.1 Skill/App Skill/CLI registration

1. The parser plugin collects references from manifest metadata.
2. It determines the package root and app scope, then validates paths, document format, ownership, and supported versions.
3. The shared private writer prepares package-level changes; catalog and FTS participate in the same DB transaction.
4. It transitions to a valid generation under the finalized publication contract. Readers see only published valid state.
5. Online notification and offline outcome handling are separate. MIC DB writes must not require an AMD connection.

Do not equate a successful `INSTALL/UPGRADE/UNINSTALL` plugin callback with completion of the whole package operation. Later file-permission, link, or repository steps can fail. [R03, R14]

### 5.2 Action import and change events

The required order is: [R04]

```text
Existing Action DB commit
  → Action change notification
  → CapMgr AMD module reads source DB state
  → CapDB Action projection and FTS commit in one transaction
  → CapMgr client changed event
  → Agent queries its local DB again
```

- Import the Action DB on module startup, never from `client_create`.
- Do not reparse Action metadata or write to the source DB. The existing Action side needs **integration that notifies after commit**; do not count that integration as already implemented.
- Notification arrival and DB publication occur at different times. Never notify clients before the CapDB commit.
- Handle duplicate notifications, rapid updates, crash after commit, and missed events during reconnection with idempotent resynchronization. Do not assume a generation field exists in event payload; define a source comparison/requery method if it does not.
- An existing SQLite read transaction retains its old snapshot. Keep read transactions short so a query after a changed event opens a fresh snapshot.

### 5.3 Public Action projection

Hide the top-level internal executor `type`, `details`, `pluginPath`, `autoDispose`, and similar settings from public JSON. Preserve JSON Schema `type` inside `inputSchema`, `outputSchema`, `eventSchema`, and Entity data schemas. Do not recursively remove fields named `type`. [R08]

Return **only provider app IDs and the default app ID** as provider information; do not add `enabled`, `packageId`, or `displayName`. Include transitive Entity dependencies needed through inheritance and field references, not just direct references. Do not mask a failed Entity resolution as an ordinary empty schema.

Separate the internal source Action JSON from its public projection. Do not overwrite a source executor `type: tidl/plugin/appControl` with the Capability kind or transform the source DB into a new schema.

## 6. Local DB, WAL, and failure recovery

### 6.1 Connections and writers

The Agent connection is read-only; only parsers and the AMD module have private writer roles. Use SQLite WAL without assuming unlimited progress by simultaneous writers. Bound busy retries, keep transactions short, and translate errors explicitly. Do not retain long-lived read transactions during enumeration or search. [R02, R14]

Commit catalog rows and FTS rows together so removal/update leaves no ghost search hit. On failure retain the previous valid generation or return to pending. Distinguish DB schema version from descriptor version. A read-only client must not attempt migration and must clearly reject unsupported DB versions.

### 6.2 Recovery contract and unresolved point

Target invariants are hidden pending data, retained valid generation, package-level publication, failure restoration, and idempotent repeat handling. The success/failure boundary that guarantees them remains an implementation gate.

- Existing app-installers enter `CLEAN` after preceding steps succeed, but cleanup itself can fail. Parser `clean()` ignores plugin results. Therefore `CLEAN` is not automatically final success.
- An uninstall undo path does not invoke plugin `UNDO`. Immediately and permanently deleting previous state in the delete callback and waiting only for UNDO is insufficient.
- MIC/offline cannot depend on service events. An upper-layer backend result check exists, but no finalization hook carrying it to CapMgr has yet been confirmed.
- Determine online and MIC completion, pending-state resolution, and post-crash reconciliation separately before fixing the publication method. Do not claim immediate recovery is already guaranteed in every case.

Maintenance resynchronization is a recovery mechanism, not permission to leave pending state forever or publish success arbitrarily. Even if previous valid metadata is kept, package files may be moving or disappearing, so execution-time availability requires separate checks. [R14]

### 6.3 WAL/SHM and file lifetime

Manage owner, mode, and SMACK policy for the DB, `-wal`, `-shm`, and parent directory, including recreated sidecars after checkpoint. Do not solve read-only connection failures by granting Agent DB write access or using `immutable` on an active WAL DB.

Do not delete the main DB/WAL/SHM while open or back up only the DB file. Follow SQLite consistency rules for migration, checkpoint, backup, and restore under concurrent access. Test AMD module restart, concurrent parsers, open Agent connections, empty/corrupt DBs, and disk-full conditions.

## 7. Resource paths and privileges

Return Skill directory paths for the Agent to read instead of exposing a body-loading API. The same base path must resolve `SKILL.md` and related scripts/references/assets. Metadata queries work before mounting, but must not present an unprepared path as usable. Each client can choose a destination; separate registered source references from client-specific exposed paths rather than overwriting a shared DB absolute path. [R07]

`remount_resources(client, destination_path)` explicitly performs both first mount and later reconciliation. Verify that the destination belongs to the caller's namespace and check source package ownership and read privileges. Do not assume a mount in the server namespace appears in the Agent namespace. Determine the helper's required mount privileges and caller-verification method from platform evidence before implementation.

A returned path may become invalid after package update/removal/remount. Define changed events, fresh detail queries, and Agent file-open failures as one lifetime contract. Specify the relation between client destruction and mount lifetime; do not assume automatic unmount. Do not freeze actual source/destination directories before PATH-01 is settled.

Initially restrict public callers to holders of the platform privilege. Check real TIDL caller credentials through Cynara for service requests, never a caller-supplied app ID. Local DB queries do not automatically receive server-side TIDL checks, so verify that DB and resource DAC/SMACK policy agrees with allowed callers. Do not add hidden mounts in `client_create` or per-query IPC to solve this. Distinguish a platform certificate-level check from a runtime privilege grant. [R13]

## 8. CLI/Action execution and lifetime

The CLI launcher selects the executable from its descriptor and owning package and runs it under the systemd `app_fw` context. Pass **`--json` followed by the entire JSON-RPC 2.0 request in one argv element**. Do not assemble a shell command or reinterpret spaces, quotes, and Unicode inside JSON as shell quoting. [R10, R12]

Collect and parse both stdout and stderr. Preserve native error numbers, strings, and data. Finalize transport-error and retained-diagnostic behavior for invalid/empty JSON, multiple JSON documents, conflicting responses across streams, and over-limit output. Never buffer malformed output without a bound.

A synchronous `execute` error means validation, privilege, or acceptance failed. After acceptance, deliver exit status, signal, and native result/error in the callback payload. Do not promise cancellation of an ordinary one-shot Action. Handle CLI process cancellation and Action subscription termination according to kind. [R11, R12]

`client_destroy` cleans up owned CLI jobs and Action subscriptions and must prevent callbacks into released user data afterward. Avoid duplicate terminal results, dangling callbacks, and surviving child processes during completion/cancellation/destruction races. Keep uncancelable ordinary Action execution distinct from the end of client callback lifetime.

Do not block the server's main event loop on long DB reads, output waits, or process termination. Give writer coordination, jobs, and callback dispatch explicit owners. Finalize thread count and callback execution context under the API contract and measurement; do not assume a thread per request.

## 9. Initial resource limits

These are **user-accepted initial settings before measurement**, not demonstrated performance figures or an RSS bound. [R09, R15]

| Item | Initial setting | Contract |
|---|---:|---|
| Search returns | At most 5 | English FTS5/BM25, no embeddings; do not pad with irrelevant results |
| Concurrent CLI runs | 2 per client / 4 overall | Launcher atomically enforces limits; specify how excess requests are handled |
| One-shot CLI timeout | 30 seconds by default | Descriptor/platform adjustment; do not reuse as Action subscription lifetime |
| argv JSON size | 64 KiB | Verify length and encoding basis and OS exec limit |
| Combined output | 1 MiB | Across stdout and stderr; no unbounded collection after limit |
| SQLite page cache | 1 MiB per connection | SQLite cache setting, not process RSS limit |

Separate exact identifier matching from English normalization/Porter stemming. Test name/keyword/description weighting and relevance exclusion using representative queries; do not treat BM25 as a probability.

Memory measurement includes JSON parsing, result arrays, Entity bundles, per-job output, thread stacks, allocators, and mmap/shared pages in addition to SQLite cache. Verify that over-limit handling does not confuse native and transport errors.

## 10. 32-bit design and verification

Design for ARMv7l 32-bit compatibility from the beginning. Do not store pointers in integers or assume `long` is always 64-bit. Check length-to-allocation conversions and addition overflow, and specify widths at TIDL boundaries. **Perform the actual ARMv7l build only in the final P09 phase after P08 functional completion.** Do not present earlier x86_64 results as ARM success. [R18]

Integration, smoke, and performance tools are development deliverables. Include emulator-native build, RPM creation, installation, and integration verification in the plan without assuming compiler/development dependencies are already installed. Record read-only observations and future execution gates in the [verification plan](05-verification.md). [R16, R17]

## 11. Actual reference sources

Use `TIZEN_SOURCE_ROOT` for the root of a Tizen platform source tree and `ACTION_SOURCE_ROOT` for the root of the existing Tizen Action source tree; resolve each variable in the implementation environment before following these references.

- Watcher structure: `${TIZEN_SOURCE_ROOT}/platform/core/appfw/tizen-watcher/src/CMakeLists.txt:1`; root `CMakeLists.txt:11` uses C++17; `packaging/tizen-watcher.spec:67` pre-generates TIDL and `:185` separates devel output.
- TIDL: Watcher `tidl/prebuild.sh:3`; `tidl/tizen_watcher.tidl:1` declares `http://tizen.org/privilege/internal/default/platform`.
- Parser: Watcher `src/tizen-watcher-plugin/tizen-watcher-plugin.info:1`, `CMakeLists.txt:28`, and `watcher_metadata_plugin.cc:52` onward. This precedent does not guarantee install success means DB publication is final.
- Action source/projection: `${ACTION_SOURCE_ROOT}/src/api/api_stub.cc:317` returns source JSON; `src/common/action_schema.hh:80` onward describes Entity dependencies/resolution; `action_schema.cc:54` has execution type.
- Rollback limits: `${TIZEN_SOURCE_ROOT}/platform/core/appfw/app-installers/src/common/step/pkgmgr/step_run_parser_plugins.cc:135` and `:188`; `src/common/installer/app_installer.cc:310`; `src/pkg_initdb/init_pkg_db.cc:67` checks backend exit.
- GoogleTest/GoogleMock build precedent: `${TIZEN_SOURCE_ROOT}/platform/core/appfw/aul-1/CMakeLists.txt:43`, `test/unit_tests/CMakeLists.txt:43`, `packaging/aul.spec:187`. Trace application and execution evidence in the verification plan.

These references describe inspected source structure, not a successful Capability Manager build, deployment, or device run.
