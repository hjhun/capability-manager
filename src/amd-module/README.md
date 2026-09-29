# AMD catalog module

`CAPMGR_BUILD_AMD_MODULE=ON` builds `libamd-mod-capability-manager.so` against
`amd`, `tizen-core`, `dlog`, and `libtzplatform-config`. AMD discovers this shared module under
`${AMD_MODULES_DIR}/mod` and calls `AMD_MOD_INIT`/`AMD_MOD_FINI`; it needs no new
systemd service or socket. The Tizen RPM owns it in `amd-mod-capability-manager`.

The shipped `/etc/capmgr/amd.json` is disabled. This is an activation prerequisite,
not a successful import. Enabled configuration has exactly these fields:

```json
{
  "enabled": true,
  "directoryLabel": "YOUR_PROVISIONED_DIRECTORY_LABEL",
  "fileLabel": "YOUR_PROVISIONED_CATALOG_LABEL",
  "lockLabel": "YOUR_PROVISIONED_LOCK_LABEL"
}
```

An administrator must provision actual labels and access rules first; the example
labels are explanatory, not shipped policy. Configuration and `/etc/capmgr`
ancestors must be root-owned, nonwritable by others, without ACL/capability extras;
the file must be root:root 0644, single-link and at most 8192 bytes. The module
never accepts caller paths, commands or mode bits from configuration.

AMD must run as the resolved `app_fw` UID and primary GID. The fixed layout is:

- Root-owned nonwritable `/opt/usr/capmgr` and trusted parent ancestry.
- `/opt/usr/capmgr/catalog`: app_fw:app_fw 0700 with configured directory label.
- `/opt/usr/capmgr/generation.lock`: app_fw:app_fw 0600 with configured lock label.
- Catalog DB/WAL/SHM: app_fw:app_fw 0600 with configured file label.

The existing coordinated writer verifies labels, identities, modes, ACL absence,
O_PATH support and generation ownership. Only an observed missing main DB permits
exclusive initialization. Existing current-schema startup uses shared generation
ownership, so readers do not block ordinary restart/import. An existing unsupported
schema requires explicit exclusive maintenance; arbitrary admission failures never
trigger repair. No chmod, chown, chsmack or policy operation is performed.

The dedicated tizen-core task owns creation, SQLite, reconciliation and destruction.
AMD main idle hands off to owner startup idle; a timer uses exactly 5000 milliseconds.
There is no production std-thread fallback. INIT/FINI and the handoff main idle are
serialized on AMD main context, and only this module's core refcount is balanced.
It reads the existing `.tizen_action.db` under `tzplatform_mkpath(TZ_SYS_DB, ...)`;
no Action installation metadata is reparsed. It initializes/checks catalog schema
and `quick_check`, then publishes the Action snapshot and FTS transaction together.
Errors keep the prior committed snapshot. Startup source readiness is retried every
five seconds with a fresh service; running services reconcile every five seconds.
This periodic fallback is **not** the comprehensive post-commit writer feed required
by SYNC-01. That integration remains open. Stop interrupts the periodic wait and
cancels pending main/startup/retry sources and posts an owner shutdown idle.
That callback physically closes and destroys SQLite before acknowledging stop;
only then does fini quit/destroy the task and release its core acquisition.
Synchronous SQLite/IO or stalled dispatch can delay fini. Failed owner stop posting,
source cancellation, physical close or task retirement cannot safely unload code
and therefore fail-stop; optional startup exceptions instead log unavailable. Optional failure is logged and does not abort AMD startup.

Diagnostics use `LOG(LEVEL) << ...`, `LOG_TAG=CAPMGR`, basename/function/line and
real dlog in the platform build. Host tests use stderr with the same tag. Revision
logs follow commit; they are not client change notifications. The module does not
select the development-only fixed-principal TIDL read service. Public C `create`
continues to deny until an actual authorization/file-policy admission channel is
connected. The next launcher/service checkpoint must implement a runnable endpoint
before adding its service/socket payload under `packaging/`; no placeholder units,
permissive socket modes or Action manifest domain are installed here.
