# Windows recovery from v1.9.2

The repair is based on the exact v1.9.2 source, with its existing Qt and geodata
dependencies. The GUI, core and updater must be delivered together. The new GUI
expects the verified updater bootstrap produced by the new core.

Use `-DNKR_VERSION=v1.9.2.1` for a local repair build, and the same version in the
Go core linker flag. A numeric fourth component is deliberate: the existing
updater recognises it and will not offer the older v1.9.2 as an upgrade.
`nekoray_version.txt` remains the default when no CMake version is supplied.

## Changes

* Subscription URLs are retained when importing into the current group. An
  invalid/empty subscription response never clears the existing servers.
  Clipboard BOMs and percent-encoded query values are handled correctly.
* The REALITY PQV verifier survives import, storage, export and config building.
  The native core verifies ML-DSA-65 signatures without an external Xray process;
  explicit Xray selection and XHTTP support remain available.
* System constructs sing-tun's System implementation. Explicit stack choices
  are not silently substituted; diagnostics report the same rule as the core.
* Windows configurations discover work VPN routes and scoped/negative DNS
  fallback automatically. No corporate service names or addresses are bundled.
  See [work VPN policy and limits](../patches/windows-work-vpn.md).
* The RU preset uses `https://1.1.1.1/dns-query` on the direct DNS path and on
  the proxy DNS path. Explicit direct domains retain direct DNS; unmatched
  names use the proxy DNS path. A blocked local UDP resolver can no longer
  prevent otherwise reachable VPN destinations from resolving.
* Existing RU schemes receive that DNS change only when they match the complete
  inherited tuple: direct `77.88.8.8`, remote Cloudflare DoH, both IPv4-only
  strategies, simple/routed DNS, proxy traffic final, proxy or bypass DNS final,
  and the `domain:ru`, `domain:su`, `geoip:ru` signature. Custom DNS choices,
  DNS objects, scoped DNS/hosts, routes and unknown fields are retained. Atomic
  scheme commits synchronize active memory before the one-shot
  `routing_dns_repaired` settings marker is committed. Failed writes retry on
  the next startup. The earlier QUIC migration no longer changes the DNS final.
* The exact inherited `geosite:category-ads-all` selector is removed from saved
  schemes. Other selectors and settings are retained.
* Logs, routes and purchase are accessible from the sidebar. The updater shows
  download/install stages, checks errors and rolls back failed replacements.

## Validation

The full Windows Qt suite contains 33 test programs. The live import test accepts
a URL file and a private export directory: keep these outside the public tree.
It downloads through the actual Qt HTTP/parser/store path and builds native
REALITY, internal System and external System TUN configurations. Config building
does not start an adapter or change a proxy setting.

Go tests use `_overlay/prepare.sh` and the release tags from `libs/build_go.sh`.
Test the native TLS verifier, work VPN manager, DNS wrapper, route matcher and
core integration, including their targeted race tests. Updater tests include
exclusive Windows file locks, replacement failures, rollback, restart failure
and retry. Do not treat a mocked or constructor-only TUN as a live packet test.
`default_routes_test` also exercises the DNS tuple restrictions, preservation
of scoped/custom settings, active-state mismatches, locked scheme/marker files
and retries after atomic write failures.

The existing `TestLiveRuleSetReloadsWithoutRestart` exposed an upstream
`LocalRuleSet` reload race. The overlay now snapshots rule slices under the
publisher's mutex and protects cleanup/close writes. The test must pass under
`-race`, alongside the added work VPN tests.

For live checks, use a loopback proxy core with the app-generated config and the
same config-directory working directory as the GUI. Bundled `.srs` files live
there; starting the diagnostic core from the install root incorrectly triggers
remote rule-set downloads. Do not include user settings in a portable package.

## Local application

Preserve existing settings, profiles and routing files. Make byte-for-byte
backups before replacing the three executables. The GUI and its cores must be
closed, and the application's RunGuard must cover replacement and config writes.
Do not close an unrelated OpenVPN or third-party client to install these files.

Regenerate the offline System/ads plan after shutdown; its hashes reject stale
settings. See [the offline tool](SYSTEM_ROUTES.md). A restored connection and
corporate names need a separate live test when the user can switch clients.
Another active TUN can deny VPN DNS sockets; this repair does not override its
firewall or install a corporate certificate authority.
DNS defaults do not repair a disconnected corporate VPN. Confirm its tunnels,
routes and DNS reachability before treating corporate resource access as fixed.
