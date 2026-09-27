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
