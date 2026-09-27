# Decisions and open implementation gates

The latest user requirements in [01](01-requirements.md) take precedence over
historical `docs/design-discussion.md`. Technical choices are resolved with source
inspection, tests, and peer review. They do not each require user permission.

## Preserved decisions

C++20; `capmgr_` C ABI; RPM; TIDL; Watcher conventions. Create authorizes and opens
the read-only catalog, without mounting resources. Explicit remount takes the
caller's destination. Skills are directories, not loaded bodies. Skill/App Skill/
CLI parsers write directly through the private catalog, including MIC without AMD.
Action metadata is imported from its DB, never reparsed. Action execution remains
with its existing API. Local discovery never performs per-query IPC.

Action projection retains schemas, Entity closure, requiresConfirmation,
providerAppIds and defaultProviderAppId. Executor type/details/pluginPath/
autoDispose and provider packageId/displayName/enabled are private. Nested schema
type is preserved. Skill/CLI cross-owner collisions reject installation; same-owner
updates work; App Skills from different apps may share names.

CLI receives `--json` and the complete JSON-RPC request as one argv element; no
shell assembly. Both output streams can carry valid responses. Search uses English
FTS5/BM25, exact ID/name preference, at most five relevant results, no embeddings
or minimum-result padding. Cancellation attempts owned CLI cleanup or unsubscribes
Action subscriptions; ordinary Action cancellation remains unsupported. Actual
ARMv7l builds occur only after P08, at P09.

## Gates

| ID | Evidence/decision needed | Affected phase |
|---|---|---|
| PATH-01 | User confirmation of App Skill source/exposed postfix spelling | P05 paths/fixtures |
| API-01 | C ABI, allocation, filters, callbacks, threads, destroy lifetime | P01 onward |
| SCHEMA-01 | Canonical IDs, descriptor version, metadata references, Entity conversion | P02/P03 |
| DB-ACCESS-01 | Platform DB/WAL/SHM preparation and actual DAC/SMACK access | P03/P05 |
| SYNC-01 | All Action writers, including provider/default changes, post-commit events and reconnect recovery | P03 |
| EVENT-01 | Changed callback registration, payload/revision, missed-event behavior | P03/P06 |
| INSTALL-01 | Post-parser failures, uninstall without UNDO, online/MIC finalization | P02/P07 |
| DB-01 | Schema/FTS migration, multiple writers, WAL/checkpoint/recovery | P02/P07 |
| SEARCH-01 | Weights, normalization, relevance corpus and measured cost | P03/P08 |
| RPC-01 | Public IDs vs positive Action int IDs, cancellation, events, retries | P04/P06 |
| CLI-01 | Per-stream framing, mixed logs, conflicting replies, drain bounds | P04 |
| MOUNT-01 | CLI read/execute exposure; namespaces, privilege and rollback | P05 |
| PRIV-01 | Real peer privilege and consistent file access policies | P05/P07 |
| ENV-01 | Native compiler/CMake/RPM/TIDL/SQLite/testing dependencies | P01/P07 |
| TEST-01 | Actual AUL Google Test/Mock precedent; creator name if identifiable | P01 |
| ARM-01 | Official 32-bit profile, ABI and available runtime target | P09 only |

### PATH-01

Source is under logical `res/skills/`; exposed App Skills are under
`<destination>/app-skills/<package-id>/`. Different apps must coexist. The earlier
`<app-id>/<skill-name>/` suggestion was followed by a postfix correction;
`<skill-name>-<app-id>-skill/` is only a possible interpretation. The implementation
owner requested the exact choice on 2026-09-27. No answer is assumed. Catalog scope
and path adapters may proceed independently.

## P00 proposed development contract (revision 4, accepted for development on 2026-09-27)

These are accepted implementation choices, not retroactive user agreements.
P00-ABI-r4 acceptance and its original contract/header hashes are recorded in
[08](08-progress.md). This publication edit changes only the review-status label.

- **API-01:** Opaque client and search-result handles; fixed-width error/kind values.
  Create takes only an output handle. Foreach uses a kind filter (zero means all)
  and borrowed summary JSON; false stops. Search returns an opaque results object;
  its count/item accessor functions are explicitly additional to the nine core
  functions. Get allocates a UTF-8 JSON string released with standard `free()`.
  A per-client changed callback setter adds one function. Public headers document
  all symbols; ABI is version 0 until integration acceptance. Exact proposed C
  declarations, errors, callback signatures and accessor ownership are in
  `src/api/capmgr.h` (part of P00-ABI-r4 review, not yet installed).
- **Lifetime:** Caller serializes calls on one handle. Result callbacks are serialized
  on one owned dispatcher and are borrowed for callback duration. Cancel is allowed
  from callbacks, as an explicit exception to caller serialization. Destroy from
  a callback or while any callback is active returns BUSY immediately, leaves the
  handle open, and performs no partial destruction. It never waits for blocked
  user code. Otherwise destroy closes admission and callback dispatch atomically,
  cancels supported owned requests, and joins bounded internal cleanup; no
  callback after successful return. It never joins while holding a lock needed by
  callback-to-cancel. Cleanup failure returns IO with the handle retained but
  admission closed, allowing destroy retry; it never frees live worker state.
  Different handles may operate concurrently. Foreach callback must not re-enter
  the same client while its SQLite statement is active.
- **SCHEMA-01:** Descriptor version 1. Kind-specific canonical keys use percent-
  encoded UTF-8 segments: `skill:<key>`, `cli:<key>`,
  `app-skill:<app-id>:<key>`, `action:<original-action-name>`. Required descriptor
  `key` is stable identity; `name` is mutable display text. Segments are case-sensitive
  UTF-8 bytes with no implicit Unicode normalization. Encode every byte except
  ASCII alphanumeric, dot, underscore and hyphen using uppercase percent hex;
  `%` becomes `%25`, `:` becomes `%3A`. Distinct bytes stay distinct. Reject NUL,
  invalid UTF-8 and empty segments. Display-name changes retain identity. Package is private ownership data. Descriptor
  fields are version/key/name/desc plus kind-specific resource or executable and
  schemas. Metadata values are package-relative descriptor paths, repeated and
  semicolon-separated; normalize identical paths, reject empty/escaping entries.
  App scope comes from installer metadata, not an untrusted descriptor app ID.
- **Projection:** Common id/name/desc/kind; Skill path is null and available=false
  until explicit preparation. Source paths/executables are private. Action uses
  an allowlist; related Entity records form an `entities` map keyed by typeName,
  with base and transitive field references. Missing/invalid Entity fails the
  complete import transaction. Default provider is null if unset. All registered
  provider app IDs remain visible; enabled is not a public field.
- **RPC-01:** JSON-RPC 2.0 `tools/call`, params.name is the canonical capability ID,
  params.arguments is an object. IDs are nonempty strings or signed 64-bit integer
  JSON values, preserved exactly; reject null, fractional IDs, notifications and
  batches. A client-local monotonically allocated uint64 request token is separate
  from the JSON ID and Action int mapping. Tokens start at 1, never wrap or reuse
  within a client lifetime; exhaustion returns LIMIT before admission. Integer
  IDs compare mathematically within signed int64 (including -0 == 0); string IDs
  compare exact UTF-8 bytes and never equal numeric IDs. Reject duplicate in-flight JSON IDs,
  never automatically retry a side-effecting call.
- **CLI-01:** Each stream contains either whitespace or exactly one complete JSON
  response with the matching ID. Collect each independently, validate after bounded
  drain; reject mixed logs/trailing documents, invalid ID and conflicting dual
  responses. Identical dual replies produce one terminal result. A valid native
  response remains intact even on nonzero process exit; transport failure metadata
  records exit/signal where no valid response exists. Initial limits stay 64 KiB
  input, 1 MiB combined output, 30 seconds, 2/client and 4/global, no queue.
- **EVENT-01:** Setter installs/replaces or removes a borrowed changed callback.
  Replacement/removal returns BUSY without changes while any callback is active,
  including callback-context calls. On OK, no future callback uses the old
  user_data, which the caller may release; on BUSY it must remain alive.
  Payload is a uint64 catalog revision; callback means a committed generation is
  available, not that an old SQLite read transaction has advanced. Reconnection
  compares revision and resynchronizes. Events follow catalog+FTS commit.
- **INSTALL-01:** Parser staging must not publish as successful installation.
  A private finalizer must receive authoritative backend success/failure; CLEAN
  ignores plugin failures and uninstall lacks UNDO in observed installer code.
  Until that finalizer is verified, the production parser must fail before any
  staging or catalog change for install/update/uninstall (vital plugin failure
  propagates). A successful package operation with hidden registration is not
  acceptable. Existing pending records remain hidden and preserve the previous
  published generation; only authoritative success publishes or failure discards.
  Crash recovery never infers success. Offline harness explicitly supplies final
  outcome and needs no AMD; MIC integration is still gated on obtaining that
  authoritative outcome. No-UNDO uninstall cannot delete published records early.
- **SYNC-01:** Existing Action provider/default setters lack a comprehensive
  post-commit feed. Reading SQLite WAL events can trigger resync but is not proof
  of the agreed writer-notification contract. Source integration is an explicit
  gate; this repository will not silently edit unrelated Action source.

## Decision record format

Record gate/requirements, source evidence, selected behavior, rejected alternatives,
ABI/error/security/memory consequences, review request and revision, tests and
remaining restrictions. Unexecuted work is NOT_RUN or BLOCKED, never PASS.

## Platform identity investigation (P06-PEER-r2, accepted private scope)

Socket SO_PEERCRED/SO_PEERSEC identify the connector, not the process sending each
request after fork or FD transfer. The private Peer helper pins that connection
principal and rejects disconnected, zombie or reaped connectors. Its proc/starttime
checks do not prove absence of an initial PID-reuse race. Its namespace FD pins an
object, not an authorized current-sender mount target. Do not use it alone for
RemountResources; production create/remount remain fail-closed.

The generated TIDL `-e` getters describe the callback channel. Request authorization
must obtain the MAIN channel, check all internal API results, verify both channels'
kernel identities, and bind checks to MAIN. Sender/instance strings and default
extension values are never identity proof. A repo-owned generation adapter now
binds both channels and revalidates the actual MAIN FD before parcel decoding.
Host/native unit tests and a native positive TIDL fixture exercise this boundary;
production create, initial PID binding proof and the full policy matrix remain
open. This does not turn stream credentials into per-message identity.

Cynara checks must include system UIDs and derive client/user from the verified
socket. Only an explicit allowed result permits access; denied, unresolved,
unavailable and failed credential extraction fail closed. This is connection
authorization, not permission to act in an arbitrary sender's namespace.

The target 4.4 kernel exposes SO_PASSCRED/SCM_CREDENTIALS and SO_PASSSEC/SCM_SECURITY
on an actual AF_UNIX SOCK_SEQPACKET connection. The private packet/ticket code is
a security experiment, not a production IPC direction: R01 continues to require
TIDL and forbids a separate custom UDS protocol. A TIDL protocol-2 file_desc can
carry a process-directory FD; that is being investigated as a compliant means
to pin a process object, not as proof of the sender of each stream request.
No resource transport or mount policy has been accepted. Delegation semantics,
trusted procfs provenance, packet truncation, ancillary ambiguity, replay, disconnect,
PID lifecycle, namespace access and rollback remain required negative tests.
SCM_SECURITY under Smack carries the sending socket's label; it does not prove
the current task label after exec/relabel while retaining that socket. Current
task-label validation and its race strategy remain production authorization gates.
Neither socket-label comparison nor Cynara's socket-derived identity alone closes
this gate. No private packet/ticket acceptance enables remount.

## P04 namespace cleanup design (revision 2, accepted for development)

R10 keeps the public TIDL launcher service running as app_fw. Process groups alone
cannot contain a CLI that calls setsid. A separate, minimal root setup broker is
the selected development direction for PID/mount namespace creation; this expands
the trusted surface and is not enabled by the existing primitive acceptances.
Both public and broker request interfaces remain TIDL. Internal anonymous pipes
used between a parent and its child are fork coordination, not a public protocol.

The broker's future operations are START/CANCEL/STATUS for registered CLI IDs and
owned job IDs. No caller-selected executable, UID, environment, namespace or file
FD is accepted over that interface. The broker independently resolves the trusted
catalog and verifies the executable/resources with the eventual app_fw credentials;
a root-opened descriptor must not bypass those permissions. START is admitted only
for the independently authenticated launcher unit and its verified original-client
request context. UID app_fw or label System alone cannot authenticate the launcher:
the CLI itself has that UID. Actual unit/process/label provenance, socket policy,
registration trust and original client authorization remain integration gates.
Broker FDs must never reach jobs. A fixed image policy must provision this boundary
before deployment; no matching policy is assumed or added by these experiments.

The namespace-init creator must be single-threaded for its whole spawning
lifetime. A dedicated thread inside a multithreaded TIDL listener does not satisfy
this constraint; the proposed front-end/worker separation below requires its own
review before implementation. The spawning process owns each namespace-init child. A small non-exec PID1 supervisor creates private mount
propagation and a procfs mounted from its active PID namespace. It closes inherited
FDs except defined control/stdio pipes and sets a safe cwd. Privileged mount/SMACK
setup and bounding-set reduction occur while required capabilities, including
CAP_SETPCAP, are still held. Then remove supplementary groups, set app_fw real/
effective/saved gid and uid, clear remaining ambient/permitted/effective/inheritable
capabilities, set no_new_privs, and verify IDs/capabilities/label. Every step fails
closed and exec must not regain privilege. Expected SMACK label and permissions
must be verified against the image, not guessed. It sets PDEATHSIG only after credential/label
changes, checks the held parent-control channel and waits for GO. Only after a
successful setup handshake does it fork/exec the CLI; PID1 remains the supervisor
and reaper rather than executing potentially relabeling CLI code itself. Exec
status combines a close-on-exec error pipe with child status; EOF alone is not
proof of successful exec. Mount propagation and all namespace setup are confined
to this job, never the launcher or caller's namespace.

On completion/cancel/timeout, the broker signals its unreaped direct PID1 child
and polls waitid(WEXITED|WNOWAIT). This retains the numeric child PID until explicit
reap and prevents a subsequent kill from targeting a reused number. PID1 exit
kills namespace descendants independently of session/process-group membership.
Only observed init exit plus reap confirms complete cleanup. A bounded wait that
expires reports CLEANUP_PENDING and keeps the owned job record/capacity reserved;
it does not claim descendants are gone or admit unlimited replacements. A reaper
continues inspection. CLEANUP_PENDING is a broker-private job/STATUS state, not a
new public C error. The existing public cleanup-failure contract (IO, retained
handle, closed admission and retry) remains unchanged. Before final waitpid/reap
releases the numeric PID, mark the job terminal and forbid further signaling;
a late CANCEL must never signal a reaped/reused PID. Systemd control-group cleanup and cgroup resource limits
are additional controls, not a replacement for these semantics. Linux D-state
work can delay kernel namespace teardown indefinitely; API wait can be bounded,
physical cleanup completion cannot be promised unconditionally.

Evidence: exact-release-string kernel ref39b6687fdb8e0493d9cc61423b272638ae0e9de2
has kernel/pid_namespace.c:184-263 namespace kill/reap and unbounded wait,
kernel/exit.c:992 onward WNOWAIT, fs/namespace.c:2007-2045 private propagation,
fs/proc/root.c:100-117 procfs namespace selection, and kernel/cred.c:441-450 plus
Smack exec transition code clearing PDEATHSIG. Exact image build provenance remains
unproven. Required tests include setsid descendants, parent death before/after GO,
FD/cap/UID/label isolation, host proc invisibility, no propagated mounts, setup/exec
failure, simulated cleanup-pending state and spoofed app_fw broker callers.
This contract permits isolated implementation/tests, not production enablement.


## BROKER-01 front-end/worker refinement (revision 2, accepted for development)

The rpc-port listener can create Cynara and message-sending threads even when TIDL
is generated without `-t`. NamespaceInit must therefore never run after clone from
that listener process. The proposed root broker consists of a TIDL admission front
end and a separately exec'd, fixed-image single-threaded spawn worker. This is a
refinement of the development-only root broker above, not production authorization.

The front end starts only the package-owned worker at a fixed absolute executable
path, with a fixed environment, using posix_spawn and explicit file actions. No
client selects that path, arguments, UID, label, capabilities or inherited FDs.
Only fixed anonymous control/status and workload-output pipes cross this internal
boundary. They coordinate parent/child ownership and do not add a public endpoint
or custom UDS protocol. All unrelated descriptors, including TIDL MAIN, callback,
listener, policy and catalog descriptors, must be explicitly excluded from worker
exec; upstream accepted-socket CLOEXEC helpers cannot be relied upon. The worker
must independently validate its image/configuration and fixed descriptor layout,
reject unexpected open FDs, remain single-threaded, and keep all untrusted jobs
from inheriting control channels. Root-owned worker/unit/catalog provenance remains
an image provisioning gate. This refinement does not permit arbitrary root exec.

Internal bounded START/CANCEL/STATUS records identify registered CLI IDs and owned
job tokens. START carries the bounded request and the front end's accepted request
context; it does not carry an executable path, namespace, UID, capability set or
file descriptor chosen by the caller. The worker resolves the private trusted
catalog and checks the fixed app_fw execution context before spawning. It owns
OwnedChildren and the unreaped PID1 children; the front end never signals a numeric
worker-reported PID. Status/output flow control must not prevent cancel or cleanup.
Invalid/truncated records and front-end loss close admission and trigger cleanup
of every owned job. The front end must retain the direct worker process until exit
is observed and it is reaped; restarting a worker cannot forget pending jobs.

The worker holds a trusted parent proc anchor opened by the front end before spawn
and a control pipe whose only writer remains in that front end. It checks anchored
parent liveness and control EOF independently. The worker uses a long-lived creating
thread's PDEATHSIG only as an additional mechanism, not as the sole parent-loss
proof; the TIDL front end's thread lifetime and posix_spawn behavior must be tested.
On parent loss it denies new work, kills owned namespace inits and keeps retrying
bounded cleanup while retaining uncertain jobs. Killing the worker also kills its
PID1 children through their verified creator-death setup; D-state completion may
still be delayed. Systemd control-group cleanup is additional containment. Tests
must cover front-end crash during spawn/READY/GO and output backpressure, worker
crash, retained control writers, descriptor leakage and cleanup-pending restart.

Abnormal worker exit or loss of trustworthy status blocks every START and retains
all global job reservations. Reaping the worker, delivering PDEATHSIG/SIGKILL or
checking a recorded numeric PID does not prove old namespace inits or descendants
have exited. The root front end owns an exclusive, root-only persistent recovery
journal, fsynced before worker/job admission, recording the worker generation and
outstanding/uncertain reservations. A missing, corrupt or unclean journal fails
closed on front-end restart; a new front end reconstructs uncertainty and must not
start a replacement worker merely because its predecessor was reaped. Clear the
journal only after confirmed normal cleanup, or through a separately authenticated
external supervisor which proves every old job cgroup/namespace init is gone.
The supervisor proof and durable journal update must themselves tolerate crashes.
On an image without that trusted external proof, keep the broker unavailable until
a controlled image/service reset that establishes old-job absence; restarting the
unit alone is not such a reset. No automatic capacity release or recovery from an
uncertain record is allowed. Required fixtures include worker SIGKILL with live and
simulated stuck children, front-end restart reading the uncertainty record, and
refusal of new jobs until independent cleanup proof. Persistent recovery storage,
external supervision and proof of full old-job absence remain production gates.

For the trusted launcher ONLY, admission may use a conditional connection-principal
proof: verify the actual bound AF_UNIX MAIN socket with raw SO_PEERCRED, reject
POLLHUP/POLLRDHUP/POLLERR/POLLNVAL or poll failure before and after the anchored proc,
exact active unit MainPID, cgroup, executable and policy snapshots, and recheck
immediately before START. Require the launcher to keep every client MAIN endpoint
reference inside its own process, with no fork inheritance, SCM_RIGHTS/in-flight
transfer, daemon/helper duplicate or exposure to untrusted code. This nondelegation
property belongs to the image-owned launcher TCB and must be demonstrated across
its entire lifecycle. UID app_fw, label System or membership in the same cgroup is
insufficient. Callback and MAIN bindings must match, but authority is MAIN.

The conditional argument uses the 4.4.35-string source ref39b6687: last client file
release shuts down the opposite Unix endpoint before exit releases the numeric PID.
If the old PID has already been reused during the proc lookup, post-snapshot MAIN
poll must observe HUP/RDHUP under the strict no-other-reference premise. Holding a
socket's struct pid alone does not prevent numeric reuse. Retained endpoint holders
invalidate the proof, and unread parcel bytes do not cancel HUP. The target image's
exact kernel/rpc-port provenance is not established; forced numeric reuse tests,
buffered-after-exit rejection and retained-endpoint boundary fixtures remain gates.
This proves identity only at admission and permits neither public untrusted-client
identity claims nor resource remount authorization.

Production enablement additionally requires original-client authorization, distinct
job policy preventing an app_fw CLI from writing the catalog or invoking the broker,
resource limits including process-count containment, trusted registration, and the
full native failure matrix. The current image lacks a pids cgroup controller. No
root broker service, spawn worker, production CLI route or new policy is enabled by
this document. This refinement must receive explicit peer acceptance before code.

### Worker-loop lookup boundary (development revision 1)

The single-threaded worker loop cannot perform a synchronous SQLite/filesystem
lookup while jobs are active without risking cancellation starvation. Its private
engine therefore accepts an immutable snapshot of at most256 exact registered CLI
IDs and executable paths, prepared before admission and copied into the engine.
Lookup has no I/O, callbacks, locks or allocation. This is an internal bounded
lookup mechanism, not a public catalog cache or authority supplied by a client.
The native fixture independently loads its one entry from its private fixture DB
before any child exists. Production must establish the image-owned catalog's
provenance, a bounded subset selection strategy and registration invalidation
before reusing a worker generation; those paths are not implemented or enabled.
Potentially blocking snapshot preparation must stay outside a loop with live jobs.
No public create/query behavior or trusted registered-executable requirement changes.

### Private worker result collection (development revision 2, accepted)

A collector is prepared with the exact original request and client token before
worker admission. Under coordinator serialization, START uses those unchanged
bytes, the returned worker token is bound without allocation, and only then may
Step route events. The collector accepts only validated WorkerSession events;
that session fsyncs ConfirmJobGone before exposing Complete. The coordinator
consumes session-validated retired-token State/Rejected/ENOENT cancellation
acknowledgements without forwarding them to a sealed result collector. They
produce no terminal or reopened state.

Stdout/stderr remain separate with a combined1MiB ceiling. Output, Accepted,
worker exit, EOF or State never proves completion. Only a matching durable
Complete permits one result. Its worker failure overrides provisional JSON;
signal termination produces a synthetic confirmed signal failure. An ordinary
nonzero exit with a valid native envelope remains native, including result.isError.
Synthetic errors retain actual worker failure, exit code, signal and system errno.
Overflow requests cancellation but cannot emit a result before Complete.

Dual-response comparison is conservative and lossless: object ordering, whitespace
and decoded string escapes may differ; numeric lexemes must match except integer
-0/0 normalization. Thus1.0 versus1e0 is rejected as conflict even though it may
denote the same number; different high-precision decimals cannot collapse through
floating-point rounding. One original native envelope is retained byte-for-byte.
Decoded duplicate keys, depth above128, mismatched IDs and a second malformed
non-whitespace stream fail validation. Numeric values outside the JSON library's
finite range are rejected as invalid response, never silently normalized to success.

On session loss the collector retains cleanup uncertainty and produces no result.
It supplies no reset, external absence proof or backend return path. Current
Dispatcher framed work synthesizes a terminal when its function returns/throws,
so this collector must not be wired there while cleanup is uncertain. A separately
reviewed retained-cleanup coordinator must preserve bounded public destroy/retry
and exactly-once delivery before any production backend integration. This private
collector does not enable execution, authorize registration or alter the C ABI.

Terminal construction has a separate pending state: the matching confirmed
Complete is retained before any allocating parse/comparison/synthetic-error work.
If that work throws, no further worker event is admitted, Complete() stays false,
and the coordinator retains result-delivery ownership and retries materialization
without requesting new cleanup evidence. Only a fully materialized, nothrow-movable
RunResult seals delivery. Later session loss cannot erase the already confirmed
absence proof. Unexpected comparison range errors become synthetic transport
errors; allocation/length failures remain pending for retry. Admission validation
matches WorkerCommand's depth64 and nonempty cli: suffix checks.

### Bounded dispatcher close foundation (development revision 1, accepted)

Private Close returns DONE, BUSY or IO_PENDING. An active result/changed/foreach
callback yields BUSY without changing admission or dispatch. Otherwise Close
atomically closes admission and callback dispatch, requests cooperative worker
shutdown and waits at most its selected budget (100ms public default; private
budgets clamped to0..1000ms). Scheduler/syscall delays are not real-time guarantees.
IO_PENDING maps to the existing public IO error, retaining the handle, jobs,
process-wide capacity and borrowed callback data for destroy retry. A closing
handle rejects execute before parsing/catalog lookup or side-effect-free backend
Admit. No public declaration, symbol or numeric error changes.

Terminal queueing, callback completion and worker thread exit are separate states.
A terminal token rejects cancel immediately, but releases capacity only after
callback completion and worker join; destroy may suppress queued callbacks while
retaining ownership until join. Callback-active covers only the callback itself,
never the post-callback join. Join occurs outside the callback/cancel mutex only
after a thread-exit future is ready: set_value_at_thread_exit readiness follows
thread-local destruction (C++ futures.promise), whereas a work-body-done flag does
not. Work captures are destroyed before registering completion. Dispatcher-thread
TLS is covered by the same exit readiness rule. There is no detach fallback;
unresolved object destruction is a private programming error and fail-stops.
Private backend object destructors must be nonblocking; their execution/cleanup
belongs in the tracked worker or the future retained coordinator, not delete.

This checkpoint preserves legacy mock/Action framed-work terminal synthesis.
Thread return/exception proves neither managed CLI child absence nor durable
WorkerSession Complete. WorkerResult must remain disconnected from that path
until a distinct retained-owner admission/coordinator path is reviewed. Managed
cleanup ownership, proof-plus-thread lifetime, nonblocking backend cancellation,
terminal retry and journal uncertainty are the next implementation scope.

### Managed cleanup ownership (development revision 1, accepted in r3)

ExecuteManaged is a distinct private Dispatcher path; legacy framed mock/Action
work keeps its existing missing-terminal/exception behavior. A managed owner and
client/process capacity are retained before a one-use client-token binding and
tracked Run thread creation. Only that successful creation permits asynchronous
START; later return/exception never causes synchronous token0 or proves cleanup.
No production ExecutionBackend/public execute route uses this seam yet.

ManagedOperation owns exactly one coordination thread behind nonvirtual Run.
Its subclass only implements Coordinate, using no untracked threads or native
callbacks. All WorkerSession/BrokerJournal operations, exception cleanup and
session destruction occur there; the Session destructor can fsync and must never
be deferred to Close/RetireJob. Concrete objects left after coordination have only
nonblocking destructors. Run and coordinator exit have distinct thread-exit
futures, both covering C++ TLS destruction; only observed exit plus join permits
coordinator quiescence. A subclass cannot self-assert that state with a bool.

PollCleanup observes one immutable atomic publication with cleanup state, terminal
and version under acquire/release ordering. Initial, uncertain, confirmed and
terminal-publication storage is allocated before admission; publishing confirmed
Complete allocates nothing. A materialized terminal is attached exactly once to
a preallocated publication before its atomic visibility. Allocation or a test
fault during materialization/publication preserves confirmed proof for retry,
without accepting a second Complete. Unconfirmed session loss remains uncertain;
it cannot later be converted to proof from Run return, worker exit, EOF or reap.
Confirmed per-job proof stays monotonic despite later generation loss.

PollCleanup uses only in-memory publication and try-lock/observed-exit joining,
never the mutex protecting Session/Journal/fsync. RequestCancel is a nonvirtual
atomic flag retained for Coordinate to process; dispatch serializes acceptance
against terminal queueing, then sets it outside the callback mutex. A queued
terminal returns NOT_FOUND. Poll and Close cannot wait for a stalled fsync.
Out-of-order concurrent snapshots cannot downgrade a newer publication or an
already observed quiescent owner. Invalid terminal data poisons admission and
retains ownership instead of synthesizing a managed failure without proof.

Only a matching WorkerSession Complete after ConfirmJobGone fsync may supply
WorkerResult's proof. A coordinator must bind its worker token before Step and
consume entitled retired-token State before collector routing. The private test
adapter exercises this boundary using real Session/Journal, including blocked and
failed fsync; it is not a deployed worker/backend. Terminal queueing, callback
delivery/suppression, durable proof, owner quiescence and Run-thread join remain
separate. Capacity/owner release requires proof plus both thread lifetimes;
normal delivery additionally requires callback completion. Destroy may suppress a
pending/queued terminal after those cleanup conditions, preserving BUSY/IO retry
semantics. Retired owner destruction runs outside the Dispatcher mutex.

No source activation, worker-generation recovery/reset, trusted absence override,
production authorization, catalog lease or resource-control claim is introduced.
A production coordinator and public managed admission factory remain integration
work, including registering every callback/thread and retaining lost generations.

### Injected managed C API route (development revision 1, accepted in r2)

The private ExecutionBackend may supply PrepareManagedCli after side-effect-free
Admit, only for a catalog-bound CLI entry. Its default nullptr preserves legacy
CLI and Action behavior. Prepare copies the registered entry and original parsed
request; it cannot create a Session/Journal, reservation, thread, child or native
callback, or an object whose pre-admission destructor performs I/O. Factory or
Dispatcher admission failure returns token0 before START. Once Run is admitted,
later faults retain its asynchronous token and cleanup ownership.

The C API routes this optional owner through ExecuteManaged with the exact parsed
request ID and existing C callback/data. Closing-handle rejection precedes both
Admit and Prepare. A callback-origin cancellation is a retained request, never
successful termination proof. OK means the retained request won the atomic end-
marker race, not that Coordinate observed or processed it: Coordinate may return
just before Run publishes that marker. If the marker was already visible and no
terminal was queued, cancel returns IO.
Terminal-queued tokens still return NOT_FOUND. Close always requests shutdown but
cannot infer absence from either cancellation result.

The test-only owner creates and destroys real Session/Journal exclusively inside
Coordinate, binds the worker token before Step, and feeds WorkerResult only the
validated Complete exposed after journal fsync. Its fixture transport sends
provisional native stdout before that proof. Delayed/failed fsync exercises public
execute/destroy/cancel and retained borrowed data, while successful completion
preserves original native bytes, high-precision content and result.isError even
with nonzero exit. Action never calls the managed CLI factory.

This is an injected test/development route. PlatformAccessGate still fails closed;
no factory is installed in production and no trusted executable/worker authority
comes from request fields. Unexpected asynchronous failure before START remains
conservatively retained, not converted into unreviewed no-child proof. A production
prepared-admission/allocation strategy, original-client authorization, registered
launcher integration, generation lifecycle and resource controls remain gates.


### Read-only catalog admission (development revision 2, accepted; implementation pending)

R02/R13/DB-ACCESS-01 vertical slice; production create remains denied until the
complete provisioned image/identity matrix is accepted. No CLI START, remount,
request-selected path or resource authority. All filesystem policy is fixed
trusted internal configuration, never an application/IPC argument.

A root-owned lock file lives in a root-owned ancestor outside the catalog writer's
writable directory. Verify lock type, link count, dev/ino, exact mode/owner/group,
ACL absence and SMACK label, plus trusted ancestry/mount. A writer cannot replace
its directory entry. Service and client use independently opened O_RDONLY OFD
F_RDLCK descriptions, not a shared/duplicated description. Exclusive maintenance
uses trusted O_RDWR F_WRLCK and a finite acquisition deadline; no blocking SETLKW.
Unsupported OFD locking denies. The target preflight proved support and that
O_RDONLY cannot obtain F_WRLCK. Leases exclude cooperative replacement, migration
and name/inode/policy-changing lifecycle, not ordinary in-place WAL commits,
auto-checkpoint or WAL reset (SQLite's own locking remains authoritative there).
Fork inherits OFD descriptions even with O_CLOEXEC; descendants can retain them.
An untrusted process can deny maintenance by retaining read locks; this is an
availability limit, not permission to reclaim or override a live lock.

The service holds its shared lease/pinned generation through descriptor issuance
and client validation. The SAME TIDL connection must remain live through local
lock acquisition and DB identity validation; premature HUP rejects before handle
publication. Successful deliberate proxy teardown occurs during create. The client acquires an independent shared lease before
opening, validates bounded receipt identities against fixed local configuration,
and retains its lease until AFTER SQLite closes, including destroy-IO retry.
The server TIDL connection need only remain live through this handoff; closing it
intentionally after validation avoids adding an untracked proxy destructor to
public bounded destroy. A receipt is descriptive, not a bearer authority; no
service-supplied arbitrary DB path is trusted. Client read permission remains
kernel-enforced in its own context. File checks compare identity/mode/owner/label,
never size/mtime that may change during valid in-place commits. Replacement or
policy mismatch poisons local access before returning query data. Admission checks
before/after SQLite opening keep output NULL on failure. The lifetime lease is a
file-generation lease and does not freeze catalog data or represent a revision lease.

All relevant writer connections must set SQLITE_FCNTL_PERSIST_WAL or a verified
fixture keeper must prevent last-close sidecar deletion. Check the file-control
return and test last writer close -> fresh RO open, as well as a live commit ->
newer local query. Require existing readable DB/WAL/SHM; no immutable workaround.
Ordinary Database writer connections set and check SQLITE_FCNTL_PERSIST_WAL,
but do not yet cooperate with the external OFD generation lease. Only explicitly
cooperating fixture maintainers can participate in the first checkpoint.
Production replacement/migration and sidecar recreation remain disabled until wired.

Authorization: generated TIDL binds and revalidates actual distinct MAIN/callback
FDs; real socket-derived Cynara runs on MAIN including system UIDs. Fixed UID,
GID and socket-label rules are additional constraints. Neither socket peer labels
nor the initial proc lookup become current-message/task-label or mount authority.
New connections after relabel must be used for label tests. Service returns only
a bounded identity descriptor for its provisioned generation, and never reads
catalog values or opens privileged data on the caller's behalf. Client local
DAC/SMACK-readable principals must be a subset of those authorized by this gate;
a broader direct-open policy would invalidate production authorization regardless
of successful create tests. Existing inherited endpoints and already-open SQLite
handles after fork/relabel require separate tests. Client creator-TGID rejection
is defense in depth only, not a hostile-process security guarantee.

Native matrix: actual known registered Cynara-positive principal plus UUID-label
negative, same UID/GID/groups, fresh sockets. Also injected-Allow policy native
case isolates explicit socket-label rejection without changing operational Cynara
grants. File/namespace/lease grant has no production bypass in either fixture case.
Test direct SQLite open for denied principal, RO write denial, generation replacement,
unsafe lock/sidecars, lost/swapped channels, failure before handle publication,
client destroy-IO retention and cooperative exclusive-lock admission after close.
Global fixture SMACK rules may touch only new UUID subjects/objects, using the
previously accepted root0700 durable journal/inherited recovery lock, child-exit
proof, safe cleanup and per-rule revocation; exact new fixture must receive source
review before any such native mutation. No production-policy inference.

Planned implementation increment: first implement local CatalogReadLease and private
AccessGate lease ownership with host/native filesystem tests; then fixed TIDL
service/proxy + recoverable policy fixture connects the same path for the vertical
slice. Both are required before the read-only admission slice is called verified.
A root-owned stable pathname and trusted writer/cooperating maintainer are explicit
provisioning premises: stat checks are not hostile-writer ABA prevention.


Local read-lease implementation scope: private AccessGate can provide owned
ReadAccess, with the default adapting existing path-only injected tests. A
CatalogReadLease checks independently opened whole-file OFD RDLCK, fixed
DB/WAL/SHM identity and exact owner/group/mode/label/ACL policy. Opened checks
SQLite READONLY/WAL and its reported path before any dispatcher thread or C
handle is published. Local foreach/search/get recheck generation/policy without
IPC; failures poison lease access. Admission storage outlives SQLite, including
client destroy IO retry. File ancestor/mount and writer cooperation remain trusted
provisioning prerequisites; these checks do not independently establish them.

All C client operations reject an inherited creator-TGID mismatch before touching
Dispatcher or SQLite. Output pointers/tokens retain their failure defaults. The
child must exec or exit to release inherited descriptors; destroy does not attempt
to join vanished parent threads or close a potentially inconsistent SQLite object.
This is defensive library behavior, not hostile-child revocation of cached data,
already-open descriptors or OFD lease availability. Parent behavior is unchanged.
Production gate, TIDL handoff, current-label behavior and DAC/SMACK policy matrix
are not enabled or proved by these local tests. Label reads use an explicit test
substitution in host/native unit tests, not a production permissive fallback.


### Catalog receipt and handoff seam (development revision 2, accepted locally)

A checked local lease can describe its five pinned objects (lock, directory,
DB, WAL, SHM) as fixed165-byte ASCII: CMR1: followed by each device/inode pair
in that order, each integer encoded as16 lowercase hexadecimal digits. This
bounded descriptor contains no path or executable and is not a bearer grant.
A local lease rejects any different/noncanonical descriptor and stays poisoned.
The held file objects and cooperative generation lock underpin identity matching;
no hostile rename/ABA or authorization guarantee comes from the string alone.

LeasedCatalogGate forbids its path-only fallback. It receives the descriptor from
a trusted private CatalogAdmissionChannel, acquires its independent local lease,
and carries a borrowed channel reference only through create. Opened validates
local READONLY/WAL, fixed pathname and descriptor, then checks known connection
loss and calls ConfirmCatalog on the SAME proxy before Finish and publication.
The concrete channel owns a fresh bounded one-use nonce scoped to its service
instance, descriptor and finite expiry. Confirm must revalidate both channels,
policy, nonce/expiry and held issuer lease, then consume once. Only its successful
reply proves the overlapping-lease handoff; reconnect or lost reply fails create.
Finish completes native teardown during create before handle publication. Failure
at any point leaves the output NULL; the caller-owned channel retains failure
cleanup responsibility. Finish must complete native callback/proxy teardown; no
channel/native callback state belongs to a published client or its destructor.
After success, query/destroy use only the retained local lease and no channel IPC.

The current channel is a test seam, not a concrete TIDL transport. Its tests prove
call ordering, overlapping independent locks, malformed/mismatched descriptors,
connection loss at every observed validation point, loss after SQLite open and
confirmation denial despite locally live checks and Finish failure. They cannot
establish actual native socket identity, listener quiescence or real policy.
Rpc-port1.21.17 has four split sockets; read-FD polling cannot prove the MAIN write
half survives. It is not an alternative to confirmation. The concrete create-only
proxy should use a new private thread-default GMainContext, never iterated or
shared with another thread, and destroy its proxy while its listener is alive,
then pop/unref that context before publication. Exact native split-socket,
disconnect/replay/lost-reply and teardown stress remain separate review gates.
The design does not enable production create or change the public C ABI.

### Per-instance catalog grant (development revision 1, accepted privately)

CMG1 adds a separate unpredictable 256-bit getrandom nonce to the descriptive
CMR1 receipt: CMG1:<64 lowercase hex nonce>:<165-byte CMR1 descriptor>, exactly
235 bytes. CatalogReadGrant belongs to one already-authorized service instance;
there is no global nonce lookup. One issuance per instance, one consume only.
A service-owned shared budget caps pending grants at64. Issue failure, confirmation,
expiry, disconnect revocation and destruction release the budget and issuer lease
exactly once. A failed nonce/identity check retires that instance's grant.

The steady-clock5s limit is a confirmation deadline, rechecked after metadata
validation. It is not hard wall-clock lease reclamation: a delayed service-context
expiry callback retains the lease and its budget slot. The concrete service must
schedule idle expiry and remove its timer before destruction; busy/stalled context
availability remains an explicit limitation. Expire, Confirm and Revoke serialize.
No service timer or real TIDL transport is wired by the primitive checkpoint.

Confirmation matches the per-instance nonce and rechecks its retained descriptor
and local lease. The protocol requires the client to acquire/check its independent
lease before Confirm; the primitive alone cannot prove remote compliance. A
successful validation can release the issuer lease before reply serialization
because the compliant client already holds its own. Lost reply still fails create.
Generated MAIN/callback and real policy checks must precede both Issue and Confirm;
this private class neither authenticates callers nor enables production create.

### TIDL catalog handoff adapter (development revision 2, fixture accepted)

Private ConfirmCatalog is appended as method10; existing generated methods0..9
retain their wire numbers, checked in both generated headers. The optional
CAPMGR_BUILD_TIDL_TRANSPORT adapter is not selected by PlatformAccessGate and has
no installed production factory/service. A provisioned endpoint/policy remains
trusted configuration, never a public caller argument.

TidlReadChannel constructs and pushes a new private GMainContext before proxy
construction/ConnectSync. One creator thread owns it and never iterates or shares
it. Listener outlives the proxy; Authorize parses CMG1, Confirm matches CMR1 and
uses the retained nonce on that same proxy, and Finish disconnects/destroys proxy
before popping/unrefing the context. Any error closes the channel; it cannot
reconnect/reuse. CheckSameLive reports only known lifecycle loss, not split-socket
liveness. Failed create owns cleanup in its caller, and no proxy survives C-handle
publication. This is a deliberately narrow context-ownership premise, not a claim
that GLib source destruction joins arbitrary cross-thread callbacks.

TidlReadService relies on the generated pre-parcel MAIN/callback/Cynara checks
and adds a fixed raw UID/GID/socket-label rule. Each ServiceBase owns one grant
and a100ms expiry source attached to its dispatch context. Its creator thread
serializes all methods/timer/destruction; OnTerminate destroys/unrefs the source
before revoking the grant, and destruction repeats that idempotently. No I/O or
join occurs on termination beyond owned-descriptor close. Delayed context dispatch
retains grants/budget past the5s confirmation deadline; Confirm still denies.

The explicit root fixture creates only a protected uniquely named catalog under
/opt/usr, checks its own executable ancestry and scope dev/inode before cleanup,
and owns/reaps each spawned server/client. The fixture uses a unique registered
d:: daemon endpoint, drains the server on client failure and checks endpoint
removal after normal Stub destruction. It uses actual socket-derived Cynara
and real file labels for a root/User::Shell positive route, with a deliberately
mismatched UID rule negative. It adds repeated create/local query/destroy, raw
wrong-instance/replayed/expired nonce, malformed/oversized response, stalled
context and dropped confirmation reply cases. Split-socket fixture observes four
sockets through the public rpc-port C surface, shuts down each non-read half and
requires confirmation failure. It never uses this instrumentation as authority.
No new SMACK rules, operational catalog changes or root worker jobs are involved.

The exact r2 native root fixture passed all eight cases; evidence is recorded
in08. This scoped fixture acceptance does not close image endpoint/ancestor provisioning,
original-peer/delegation/relabel behavior, same-UID/different-label/direct-open
matrix, ordinary writer OFD maintenance cooperation or production admission gates.


### Writer persistent WAL prerequisite (development revision 2, accepted locally)

Every Database::kWriter connection requires SQLITE_FCNTL_PERSIST_WAL on main
with value1 after verified WAL mode, before Catalog schema/publication work.
An unsupported or failed file control rejects construction and closes the handle;
the diagnostic includes the operation, numeric return and sqlite3_errstr. The
setting is per connection, not a cached result or persisted journal-mode pragma.
READONLY/query_only, synchronous=FULL and ordinary SQLite checkpoint/reset behavior
are unchanged. No production failure-injection hook is added.

Local tests publish real catalog/FTS data, close all writers without keepers, then
verify existing DB/WAL/SHM identities and fresh RO queries. Independent/reopened
writers, both close orders, last-reader close, in-place commits and a TRUNCATE
checkpoint are covered. Test-executable-only linker interposition injects NOTFOUND
and IOERR at the setter; ordinary tests call real SQLite. Native runtime evidence
must be recorded separately from host results.

This does not promise sidecar creation for an empty/lazy connection, arbitrary
crash/recovery RO availability, modes/labels, hostile-path safety or coordination
of raw external SQLite/Python writers. Image provisioning, trusted pathname and
generation, external OFD cooperation for replacement/migration/sidecar recreation,
and actual DAC/SMACK direct-read subset plus real-label authorization remain open.
Normal in-place WAL commits/checkpoints do not require an exclusive generation
lease; persistence neither freezes file contents nor prohibits checkpointing.
