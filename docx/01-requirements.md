# Product requirements

R01–R18 in this document are stable IDs linking implementation, tests, and phase completion. Implementation examples are in the API contract; consult the decision record for unresolved items.

## R01 — Language, deployment, and communication

- Implement in C++20 with a public C API prefixed `capmgr_`.
- Build and deploy Capability Manager itself as RPM packages.
- Refer to the actual tizen-watcher project for structure, CMake, RPM, parser, and TIDL generation.
- Use TIDL for interprocess communication. Do not design a separate custom UDS protocol.
- Provide a Capability module inside AMD. Do not assume AMD core changes are required.

## R02 — Shared catalog and local queries

- Distinguish Skill, App Skill, CLI, and Action in search and detail queries.
- Public clients are read-only. Do not use IPC for every enumeration, search, or detail query.
- Initialization prepares queries; it does not load every specification or Skill body into RAM.
- `create` performs privilege checks and prepares DB access. It does not automatically mount Skill resources.
- The basic API scope contains nine functions: create, destroy, foreach, search, results_free, get, execute, cancel, and remount_resources. The final event-registration interface is decided under EVENT-01.

## R03 — Metadata parsers and offline installation

- Skill, App Skill, and CLI parsers write installation, update, and removal information directly to the Capability DB.
- Use the same private DB rules for normal installation and MIC image creation.
- MIC must initialize the DB without AMD or a TIDL service.
- Parsers must not write through the public client API.

## R04 — Action DB as source and synchronization

- Do not reparse Action installation metadata. The existing Action DB is the source.
- The Capability module in AMD imports Action information into the Capability DB during initialization.
- After initialization, a change event starts synchronization immediately. The order is source DB commit → Capability module import and FTS commit → Capability client change notification.
- Clients respond to events by querying the local DB when needed; they do not update it directly.
- Do not assume existing events cover every change. Additional integration, reconnection, and missed-event recovery are SYNC-01 verification work.

## R05 — Registration keys and multiple entries

| Kind | Key / source |
|---|---|
| Skill | `http://tizen.org/metadata/capability/skill` |
| App Skill | `http://tizen.org/metadata/capability/app-skill` |
| CLI | `http://tizen.org/metadata/capability/cli` |
| Action | Existing Action DB and registration path |

Support both repeated declarations of the same key and semicolon-separated entries in one value, including a mixture of the two. Process every entry and normalize duplicate declarations of the same entry to one. Finalize the exact individual value format under SCHEMA-01.

## R06 — Ownership and duplicates

- Ordinary Skill and CLI: reject installation if another package already owns the same item. Do not silently skip registration while reporting installation success.
- Allow an update of the same kind and item by its existing owner package.
- Different items of the same resource kind may be registered.
- App Skill: allow the same name in different apps. Do not apply the global ordinary Skill/CLI conflict rule.
- Action: follow existing Action identity and registration rules; allow multiple providers of the same Action.
- Define the precise identifier namespace across kinds under SCHEMA-01.

## R07 — Skill and resource exposure

- Provide Skills as Agent Skills directories so the agent can read `SKILL.md` and related files. Do not add a separate Skill-body loading API.
- Provide App Skills as TPK/WGT resources. Use app identification under the package's logical `res/skills/` directory.
- Do not fix the exact directory string for the app ID and Skill suffix from the user's final wording before PATH-01 is resolved.
- The agent destination root is passed to `capmgr_client_remount_resources(client, destination_path)`. This function also performs the first mount.
- Expose ordinary Skills under `<destination>/skills/` and App Skills under `<destination>/app-skills/<package-id>/`. The per-app naming below the latter depends on PATH-01.
- `create` prepares only DB access. Metadata searches and queries work before mounting.
- Return an absolute path to an actual Skill directory only after access is prepared. Do not overwrite a shared DB path with one client's destination path.
- Do not guarantee that old paths remain valid after update or removal. Refresh through a new query and an explicit remount when needed.
- Specify the scope of direct agent access to CLI resources under MOUNT-01 alongside the launcher's role. Do not silently drop the initial CLI read/execute requirement.

## R08 — Public Action information

- Provide common information useful to the agent, `inputSchema`/`outputSchema`, related Entity information, `eventSchema` when applicable, and `requiresConfirmation`.
- Public provider fields are `providerAppIds` and `defaultProviderAppId`.
- Do not add provider package ID, display name, or `enabled`; the previous proposal to return active state was withdrawn.
- Exclude the source Action's top-level executor `type` and internal execution settings such as `details`, `pluginPath`, and `autoDispose` from public detail.
- Preserve data `type` inside input, output, and Entity schemas. Do not recursively strip every `type` key.
- Execute through the existing Action API. Query information does not guarantee execution-time success.

## R09 — Search

- Use local SQLite FTS5/BM25 for English search, without an embedding model.
- Prioritizing exact ID/name matches and weighting name, keywords, and description is the default development direction.
- Keep English normalization/Porter processing separate from exact identifier matches.
- Return at most five relevant items; typically three to five if enough are relevant. One, two, or zero results are also valid. Do not add irrelevant items to fill the count.
- Verify weights and relevance exclusion against representative queries and expected results. Do not interpret BM25 as a probability.

## R10 — CLI

- Provide a CLI through an executable under an RPK's `bin/` and a JSON descriptor.
- Run a separate launcher systemd service as `app_fw`, using TIDL for communication.
- Construct argv from the registered executable, `--json`, and the entire JSON-RPC request string as one argument.
- Do not assemble a shell command. Reject inputs that exceed the OS argv limit with a clear error.
- Both stdout and stderr are paths for JSON responses. Do not infer success or failure from the stream alone.
- Collect the streams separately, reassemble partial reads within each stream, and preserve the valid response's result or error.

## R11 — Execution lifetime and cancellation

- CLI cancellation attempts to end the calling client's job and child processes. Do not affect another caller's work.
- Action subscriptions can be canceled. Do not pretend that unsupported cancellation of an ordinary Action succeeded.
- On client destruction, clean up its CLI jobs and subscriptions. Do not promise that an uncancelable Action has stopped.
- Distinguish accepting a cancellation request from actual termination. Deliver a normal call's terminal result only once; subscription events are separate.
- Request IDs, callback lifetime, and duplicate execution on retry follow API-01/RPC-01.

## R12 — Errors

- Execution requests and responses use JSON-RPC 2.0.
- Preserve valid tool results/errors and native errors.
- Express launch failure, malformed response, no response, and timeout as Capability Manager errors that include the confirmed cause.
- Distinguish C API failures before acceptance from execution results after acceptance.
- Do not arbitrarily choose a successful result between conflicting terminal responses on stdout and stderr.

## R13 — Privileges

- Restrict public interface use through a platform privilege.
- Use TIDL/Cynara and actual peer information; do not rely solely on library-side checks.
- Ensure actual DAC/SMACK access control for the local DB, WAL, SHM, and resource files.
- Limit shared DB writes to parsers and the Capability module.
- Restrict mounts to a verified namespace, destination, and registered resources. Do not transfer helper privileges to the agent.

## R14 — DB maintenance

- Use SQLite3 WAL for the Capability DB and update catalog data and FTS in the same publication transaction.
- Give parsers and the module shared private code and interprocess coordination for writes and migrations.
- Use the Capability module's startup integrity check and recovery as the default recovery path.
- Do not treat a DB commit as successful package installation. Verify later installer failures, uninstall undo, MIC completion, and publication under INSTALL-01 against actual source.
- Verify access to read-only WAL sidecars and their recreation lifetime. Do not work around a changing DB with the immutable option.

## R15 — Resource limits

These are user-accepted initial settings, not performance figures measured on a device.

| Item | Initial setting |
|---|---|
| Search results | At most 5 |
| Concurrent CLI runs | 2 per client, 4 total |
| One-shot CLI runtime | 30 seconds by default; adjustable by descriptor and platform policy |
| CLI request JSON | Start validation at 64 KiB |
| Combined CLI output | Start validation at 1 MiB across stdout and stderr |
| DB page cache | Start validation at 1 MiB per connection |

Do not use an unbounded queue or buffer. Do not truncate over-limit JSON and return it as a valid response. The DB page cache setting is not an overall RSS limit; measure the whole memory budget.

## R16 — Build-stage unit tests

- Inspect the source of aul-1's creator-related Google Test/Google Mock configuration and CMake integration as precedent.
- Establish a framework early to test parser, DB, query, execution state, and privilege adapters using mocks/fakes.
- Separately verify compilation and linking of a pure C consumer against the public header of the C++20 implementation.
- Connect tests to the build, CTest, and RPM check stages. Verify the exact tool names and options against the current reference source.

## R17 — Target-environment verification tools

- The user reports being able to develop inside the connected emulator. Distinguish that report from read-only environment observations in the verification document.
- Build tools that support development, RPM installation, and integration verification inside the emulator as implementation deliverables.
- Provide smoke and integration tools for emulator and real devices, reproducible fixture packages, and procedures for collecting logs and measurements.
- Include performance smoke checks for search quality, latency, memory, and concurrency.
- Report build success, mock test success, RPM creation, and target execution success separately.

## R18 — 32-bit ARMv7l

- Consider 32-bit lengths, pointers, integer ranges, alignment, and serialization when designing the code and C ABI.
- Perform the actual ARMv7l build only in the final phase, after all functional development and primary-environment verification.
- Record build environment, target triple, ABI, and generated RPM/ELF architecture.
- Do not mark a failed final build complete. If no compatible environment exists, record the blocker and defer final completion.
