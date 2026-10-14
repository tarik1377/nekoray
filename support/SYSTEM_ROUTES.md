# System stack and default routes

Based on v1.9.2 (ffb497c1c64f2cbb6fbc671d44cf450b589b040d).

Stored TUN indexes are 0=gVisor, 1=System, 2=Mixed. The GUI already has
these labels in the correct order. The supplied settings and code default
now use 1. Existing saved selections remain unchanged. The core preserves
every explicit selection, including gVisor; an absent stack uses System.

The exact dependency is sing-box v1.13.18 with sing-tun
v0.8.12-0.20260727151122-3a09076491df. Its NewStack("system") creates
*tun.System on Windows. Mixed is a different type with gVisor UDP. Tests
instantiate these actual types after configuration transformation without
calling Start, opening adapters, changing firewall, proxy or routes.
System requires an additional address in the first subnet (the supplied
172.19.0.1/28 and fdfe:dcba:9876::1/126 satisfy it), and cannot use
includeAllNetworks. Errors remain explicit; no other stack is substituted.
Live TUN startup and traffic were deliberately not tested while the user's
VPN sessions were active. No latency/performance claims are made.

The RU preset no longer includes geosite:category-ads-all. A one-shot startup
migration removes only that exact non-comment line from block_domain in saved
schemes, retaining every other selector, custom JSON, DNS setting and unknown
field. The loaded active scheme is updated after an atomic commit, preventing
its next save from restoring the rule. A failure leaves the completion marker
unset so the next startup retries. Explicit custom JSON/geosite rule_set
entries are not rewritten.

## Offline user-config patch

Build the EXCLUDE_FROM_ALL target system_routes_patch. It has no GUI or core
startup. It uses Qt Core from the normal build dependencies.

While the GUI is running, create a reviewable plan outside the installation:

    system_routes_patch.exe --plan C:/GreenRhythm PLAN.json

This reads config/groups/greenrhythm.json (falling back to nekobox.json) and
all config/routes_box files. It writes only PLAN.json. The plan contains
relative filenames, SHA256 fingerprints and the narrow changed fields;
it does not copy server profiles or the entire settings object.

After the GUI has been safely closed, regenerate and review the plan. Settings
may have changed during shutdown, so an earlier plan will correctly fail.
The parent task owns applying the patch:

    system_routes_patch.exe --check-closed
    system_routes_patch.exe --plan C:/GreenRhythm PLAN.json
    system_routes_patch.exe --apply PLAN.json

Apply refuses existing GreenRhythm/nekoray/nekobox GUI processes, acquires the
same RunGuard as GreenRhythm, and recomputes the complete manifest before any
write. Thus the normal GUI cannot start and overwrite changes during apply.
Do not deliberately start a GUI with -many while applying: that flag bypasses
the application's own single-instance guard. Process checks also run before
each write. Renaming a GUI binary bypasses the name-based check; the normal
application's RunGuard remains the primary protection.

Original bytes are backed up under ROOT/system-routes-backup-TIMESTAMP.
Each replacement is atomic; settings (vpn_impl=1, routing_ads_removed=true)
are committed last. A later I/O failure can leave some route files already
migrated, but cannot mark the migration complete before all routes succeed.
Inspect the error and backups, regenerate the plan, and retry. Unrelated
user settings and routing fields are retained.

## Checks

    cmake --build BUILD --target default_routes_test system_routes_patch_test system_routes_patch
    ctest --test-dir BUILD -R "^(default_routes_test|system_routes_patch_test)$" --output-on-failure

Go tests and build must use _overlay/prepare.sh's modfile and the release tags
with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api.
In a worktree the relative libneko replacement may need to be changed in the
generated go.overlay.mod to C:/libneko; committed go.mod is unchanged.

The offline tool tests perform successful writes only in QTemporaryDir.
They cover process refusal, the real GUI RunGuard, stale/modified manifests,
exact backups and preservation of unrelated fields. Migration tests also
cover invalid/missing JSON, idempotency and Windows replacement failure with
a locked file.
