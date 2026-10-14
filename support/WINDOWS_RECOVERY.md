# Windows recovery from v1.9.2

The repair is based on the exact v1.9.2 source, with its existing Qt and geodata
dependencies. The GUI, core and updater must be delivered together. The new GUI
expects the verified updater bootstrap produced by the new core.

Use `-DNKR_VERSION=v1.9.2.3` for a local repair build, and the same version in the
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
* Existing schemes keep their routing preferences across updates. Routes shows
  two choices: apply the reviewed standard routes or keep the current rules.
  The safe update repairs only the complete inherited RU DNS tuple and adds
  missing compatible guards/media defaults; custom DNS, scoped DNS/hosts and
  explicit process choices survive. A separately acknowledged full reset uses
  the bundled preset for this scheme's route/public DNS fields. Both modes show
  change counts and create a byte-exact route backup. See [route review and
  recovery](ROUTE_DEFAULTS.md). Opening the app does not run the former automatic
  DNS/media/QUIC/game migrations. The requested inherited ads removal remains
  its separate one-shot repair; explicit TUN stack choices are unchanged.
* The exact inherited `geosite:category-ads-all` selector is removed from saved
  schemes. Other selectors and settings are retained.
* Logs, routes and purchase are accessible from the sidebar. The updater shows
  download/install stages, checks errors and rolls back failed replacements.
* Browser help compares a site's system and GreenRhythm proxy paths with bounded
  requests and strict certificate checks. It reports secure-DNS conflicts and
  distinguishes an HTTP response from a successful account login. The explicit
  browser-connect button enables the application's system proxy mode. See
  [browser diagnostics](BROWSER_CONNECTIVITY.md).
* A compact settings action restores a verified previous Windows runtime while
  retaining current user files. A failed startup restores the current runtime
  and its previous-version snapshot. See [rollback and compatibility limits](../docs/windows-version-rollback.md).
* Settings starts with route review, GeoIP status/update and program recovery
  cards. Version recovery names the verified previous version and confirms the
  required application restart. Route recovery is a separate action. GeoIP
  refresh requires a running connection and has a 30-second request cooldown;
  status polling and opening these cards never change routes or reconnect.
* Explorer reports the same numeric product version as the application, core
  and package, including the fourth component used for repair builds.

## Validation

The full Windows Qt suite contains 45 test programs. The live import test accepts
a URL file and a private export directory: keep these outside the public tree.
It downloads through the actual Qt HTTP/parser/store path and builds native
REALITY, internal System and external System TUN configurations. Config building
does not start an adapter or change a proxy setting. `--stored-profile-file`
accepts a private copy of the currently saved profile, so diagnostics also cover
the user's actual configuration without downloading or replacing their profiles.

The follow-up repair bounds Start/Stop RPCs, queues connection requests during
stopping, and recovers only the application's own core. Loopback tests reproduce
the old indefinite Stop and verify deadlines, cancellation and core replacement.
Build Go binaries with the exact `.go-version` toolchain; see
[Windows socket runtime and Vision repairs](../patches/windows-runtime.md).

Go tests use `_overlay/prepare.sh` and the release tags from `libs/build_go.sh`.
Test the native TLS verifier, work VPN manager, DNS wrapper, route matcher and
core integration, including their targeted race tests. Updater tests include
exclusive Windows file locks, replacement failures, rollback, restart failure
and retry. Do not treat a mocked or constructor-only TUN as a live packet test.
`default_routes_test` also exercises the DNS tuple restrictions, preservation
of scoped/custom settings, active-state mismatches, locked scheme/marker files
and retries after atomic write failures.
`route_defaults_policy_test` covers consent revisions, custom process/DNS
preservation, backup restoration, stale previews, locked route/settings files
and rollback. `route_defaults_review_test` runs the shared review widget and
the generated route dialog offscreen, including full-reset acknowledgement.

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
