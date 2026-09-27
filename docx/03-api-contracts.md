# Public C API and JSON contracts

[Requirements](01-requirements.md) define product behavior. The concrete development
choices in [07](07-decisions-and-open-items.md) revision P00-ABI-r4 were accepted;
implementation and test evidence remain separate in [08](08-progress.md). Example JSON is
illustrative, not evidence of an installed API or successful product test.

## Flow and API boundary

Platform startup prepares access to the live DB and its WAL/SHM files. Client
create checks authorization and opens the catalog read-only; it neither mounts
resources nor imports Actions nor loads all definitions into RAM. Foreach/search/
get query the local committed catalog without IPC. Explicit
`capmgr_client_remount_resources(client, destination_path)` performs first mount
and subsequent refresh. Get may then return an actually accessible Skill directory.
The agent reads SKILL.md and related resources; CapMgr does not load or execute
Skill bodies. CLI and Action execution use TIDL and JSON-RPC 2.0.

DB access cannot depend on remount, because remount requires a successfully created
client. A missing/inaccessible DB is a create failure, not an empty handle or an
excuse for an implicit mount. Clients do not create or migrate databases.

| Core function | Behavior and failure boundary |
|---|---|
| capmgr_client_create | Authorize and open read-only DB; output null on failure |
| capmgr_client_destroy | Close admission, clean owned CLI/subscriptions and callback lifetime; cannot promise ordinary Action termination |
| capmgr_client_foreach_capability | Local filtered enumeration; borrowed callback data and explicit stop rule |
| capmgr_client_search_capabilities | Local English weighted FTS search, at most five relevant results |
| capmgr_search_results_free | Release the complete results allocation, including internal elements |
| capmgr_client_get_capability | Return projected detail JSON with client-specific resource readiness |
| capmgr_client_execute | Admit asynchronous CLI/Action request; separate admission errors from later responses |
| capmgr_client_cancel | Attempt owned CLI termination or unsubscribe Action; ordinary Action cancellation unsupported |
| capmgr_client_remount_resources | Explicit helper request with destination in the caller's namespace |

These nine functions are the core feature surface. Search-result accessors and
changed-event registration are additional functions, not hidden inside a claimed
nine-symbol ABI. The exact accepted declarations must match installed headers.

## Ownership, threads and 32-bit ABI

Opaque handles keep DB state, execution mapping and mount state private. Foreach
and asynchronous callback JSON is borrowed during the callback only; callers copy
it to retain it. Search results have one owning free function. Get strings use
standard free(). Exact signatures and callback/reentrancy rules are reviewed under
API-01 before installing the header; see the accepted development contract in 07.

No C++ types or exceptions cross the C ABI. A pure C consumer must compile and link.
Pointer/size_t/long widths are not assumed to be 64 bits. Check allocation arithmetic,
length narrowing, TIDL integer conversion, alignment and output bounds. JSON-RPC
IDs must not round through double or be confused with Action's positive int IDs.
Actual ARMv7l compilation remains P09-only; host success is not ARM evidence.

## Identity, ownership and registration

Skill/CLI cross-package ownership collisions fail installation; silently skipping
registration is prohibited. Same-owner updates are allowed. App Skills from
different apps may share names. Action definitions are not duplicated for each
provider. Canonical key encoding and kind namespaces are SCHEMA-01 decisions.
Package ownership is private catalog data and must not appear as an Action provider
packageId field.

Metadata keys are `http://tizen.org/metadata/capability/skill`, `.../app-skill`, and
`.../cli`. Repeated keys and semicolon lists use one collector; mixed declarations
are supported and identical references normalized. Per-callback metadata must not
be mistaken for the entire package manifest. Validate package paths, versions and
ownership before publication. Parsers use the private writer directly, including
MIC with no AMD/TIDL service. Authoritative package completion and rollback,
including failure after parser callbacks and uninstall without UNDO, remain
INSTALL-01 gates. DB commit alone is not installation success.

## Search

English only, no embeddings. SQLite FTS5 with Porter normalization and weighted
BM25 ranks name/keywords/description. Exact ID/name matching is separate from
stemming. Return zero to five relevant results, with no filler to reach three.
Weights, case normalization, tie ordering and relevance acceptance require an
English corpus with expected IDs. BM25 is not a probability. Check UNINDEXED
column positions when assigning weights. Queries never trigger helper calls,
Action-source rereads or catalog import.

## Public detail

Common fields are id/name/desc/kind; do not introduce both desc and description
without a migration decision. Versioning and required/optional fields belong to
SCHEMA-01. Skill/App Skill detail describes a directory. Before mount, metadata
still works but an inaccessible path must not be represented as ready. Per-client
destination roots never overwrite a shared absolute path in the catalog. Returned
paths can become invalid after package updates/removal; requery and explicitly
remount as necessary. Source and exposed App Skill postfix spelling remains
PATH-01; do not revert to the old hierarchy implicitly.

Action DB is the original source. Import definition, Entity closure and providers
into CapDB; never reparse Action metadata. Public fields include inputSchema,
outputSchema, relevant Entity records, eventSchema when applicable,
requiresConfirmation, providerAppIds and defaultProviderAppId. Exclude top-level
executor type/details/pluginPath/autoDispose and provider packageId/displayName/
enabled. Preserve nested JSON Schema type/properties. Resolve base and field
references transitively without infinite expansion; unknown Entity fails rather
than becoming an empty successful schema. Accepted SCHEMA-01 uses an `entities` map keyed by typeName and null for
an unset default provider. Conversion/integration tests remain required.

Illustrative Action projection using the accepted entities map:

```json
{"id":"action:media.search","name":"media.search","desc":"Search media",
 "kind":"action","inputSchema":{"type":"object","properties":{
 "query":{"type":"string"}},"required":["query"]},
 "outputSchema":{"type":"array","items":{"type":"string"}},
 "entities":{},"requiresConfirmation":false,
 "providerAppIds":["org.example.player"],"defaultProviderAppId":null}
```

## Execution, framing and errors

For an Action subscription, a successful `result.subscription=true`
acknowledgement uses `is_event=false` and retains the request token. Subsequent
native top-level `event` messages use `is_event=true`; an `event.closed` message
also ends that request's lifetime. An ordinary result/error or a pre-acknowledgement
failure ends the request once. Preserve native `result.isError` and `event.isError`.
Transport failure after an acknowledgement ends the request with one synthetic
error. This requires a private completion flag independent of `is_event`.
Once completion is queued, cancel returns NOT_FOUND even if its callback has not
run yet. A terminal-marked successful Action subscription acknowledgement is a
protocol error; the same field in an ordinary CLI result remains native tool data.
Unsupported Action runtimes must reject subscription admission: the inspected
1.3.27 target has no subscription/cancel API. Protocol fixture tests do not enable
production subscriptions or prove native callback shutdown.

Only registered CLI/Action capabilities are executable. The precise tools/call
subset and canonical ID mapping are fixed by accepted RPC-01 in 07; JSON-RPC does not imply a full
MCP implementation. A representative request is:

```json
{"jsonrpc":"2.0","id":"request-42","method":"tools/call",
 "params":{"name":"cli:status","arguments":{"verbose":false}}}
```

The launcher receives exactly registered-executable, `--json`, complete-request
as argv entries. It runs as app_fw, without a shell. Reject input above the request
limit or OS argv capacity explicitly; never truncate it. Registered executable
binding is server-validated, not caller-selected arbitrary code.

Stdout and stderr are separate bounded streams, each assembled across partial
reads. Either may carry a valid response, so stream identity is not success/error.
Preserve valid native result/error/code/message/data. Conflicting final replies
must not be resolved by arbitrarily choosing one. Accepted CLI-01 in 07 fixes one complete response per stream, no mixed logs,
matching IDs, identical dual-response deduplication and conflict rejection.
Exit/drain behavior and limits still need implementation validation.
Examples of valid response envelopes:

```json
{"jsonrpc":"2.0","id":"request-42","result":{"ready":true}}
```

```json
{"jsonrpc":"2.0","id":"request-42","error":{"code":-32042,
 "message":"Native tool failure","data":{"reason":"fixture"}}}
```

Admission errors return synchronously without a later execution result. After
admission, execution failure, malformed reply, timeout or missing reply become
CapMgr transport errors with the confirmed cause. A valid native error remains a
native error. RPC IDs, Action internal IDs and ownership tokens are distinct.
Do not automatically retry side effects after lost replies.

A JSON-RPC error envelope and a valid `result` containing `isError: true` are
both native tool outcomes, not interchangeable transport failures. Preserve the
original envelope and payload, including unknown native fields.

| Input/outcome | Adapter treatment | Required fixture |
|---|---|---|
| Valid result, including result.isError=true | Preserve result unchanged | Tool-level failure in a successful RPC envelope |
| Valid JSON-RPC error | Preserve code/message/data and ID | Native error from either stream, including nonzero exit |
| Invalid/absent/conflicting response | CapMgr transport error with confirmed cause | Bad JSON, timeout, mismatched ID, conflicting dual replies |
| Pre-admission failure | Synchronous C error, token zero, no callback | Permission/validation/limit rejection |
| Existing Action result/event envelope | Explicit ID and envelope adapter; retain native payload | One terminal result vs subscription event |

## Cancellation and lifetime

CLI cancellation attempts to terminate only the owner's execution and descendants.
Action subscriptions may be unsubscribed; ordinary Actions currently cannot be
cancelled. Agent-owned Skill processing is outside CapMgr cancellation. Acceptance
of cancellation is distinct from completed termination. An ordinary call has one
terminal result even when completion races cancellation; subscription events are
separate. Destroy must end callback/user-data lifetime and supported owned work
without claiming that uncancellable Action effects ceased. Callback context,
reentrancy, ID reuse and destroy bounds require API-01/RPC-01 tests.

## Catalog change events

Action source commit -> module snapshot/import -> CapDB catalog+FTS commit ->
client changed notification -> fresh local query. Never emit before commit.
Provider/default changes must be covered, not just package events. Startup,
reconnect, missed/duplicate events and quick successive writes need idempotent
resync. Do not invent source generation fields. EVENT-01 defines registration,
payload and revision in accepted r4: an additional callback setter, uint64
committed revision and BUSY on active-callback replacement/removal. Nine core
functions alone do not provide this feature; platform event integration remains open.
Readers keep short transactions so a new query observes a new snapshot.

## Privilege and resources

Server TIDL/Cynara checks use real peer credentials. Caller-supplied application
IDs are not authentication. Certificate level and runtime privilege are different.
Local DB/WAL/SHM and files require actual DAC/SMACK policies; a library-only check
is insufficient. Read-only mounting does not erase source DAC/SMACK restrictions.
The helper validates caller namespace, registered sources and destination paths;
it does not give mount privilege to the agent. Its privilege/account design is
separate from the launcher's app_fw account. CLI direct read/execute exposure is
part of MOUNT-01 and must not be silently removed.

Keep the live DB and sidecars together; do not copy just the DB, grant writers to
agents, use immutable on a changing DB, or remove open WAL files. Check file-policy
recreation after checkpoint. Verify actual SKILL.md/resource reads in the agent
namespace, two clients with different roots, and failed-remount restoration.

## Initial resource settings and gates

At most five search results; CLI concurrency 2/client and 4/global; default 30 s;
request 64 KiB; combined output 1 MiB; SQLite cache 1 MiB/connection. These are
user-accepted initial settings, not measured optima or process RSS limits. No
unbounded queues/buffers. Measure JSON, Entity bundles, thread stacks and combined
cache/RSS separately, with boundary tests below/at/above each limit.

API-01, SCHEMA-01, DB-ACCESS-01, SEARCH-01, PATH-01, RPC-01, CLI-01 and EVENT-01 are
tracked in 07. Completion evidence includes C ABI, zero-query-IPC discovery,
correct Action projection, explicit path preparation, faithful RPC/error/cancel
semantics and commit-before-notify integration.

## Source references

Paths are relative to ACTION_SOURCE_ROOT (the existing Tizen Action checkout):
`src/api/api_stub.cc` (positive integer IDs and raw schema access),
`src/common/action_schema.hh/.cc` (executor vs schema fields),
`src/common/entity_schema.cc` (typeName/base/dataSchema),
`src/common/action_executor.hh` and `src/action/tidl_executor.cc` (ordinary cancel
unsupported, subscription cancellation, result/event envelopes),
`src/action/action_request_handler.cc` (provider/default setters and package events),
`src/pkgmgr_plugin_parser/sqlite_db.cc` and `src/action/sqlite_db.cc` (DB/FTS).
These are source observations, not completed CapMgr integrations.
