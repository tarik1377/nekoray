# RU GeoIP updates

The native core uses a managed `.geodata/geoip-ru.srs` for implicit
`geoip:ru` selectors. Explicit `route.rule_set` definitions retain their tags,
paths and contents. The legacy `geoip-ru.srs`, custom profiles and routing
schemes are not overwritten.

Offline startup reads a valid managed cache, then its last-good backup, then
the reserved `geoip-ru-bootstrap.srs` shipped with the application. Older
packages may use the legacy file as a seed when the reserved asset is absent.
No download occurs before `box.Start()` succeeds. The complete reviewed
bootstrap, source digest/commit and license are under `res/geodata/`.

## Source and attribution

Updates use the complete public RU CIDR text from
[Loyalsoldier/geoip](https://raw.githubusercontent.com/Loyalsoldier/geoip/release/text/ru.txt).
Its [generator configuration](https://github.com/Loyalsoldier/geoip/blob/master/config.json)
uses MaxMind GeoLite2 Country CSV data. Its
[workflow](https://github.com/Loyalsoldier/geoip/blob/master/.github/workflows/build.yml)
updates weekly; the client checks daily. This dataset incorporates GeoLite2
data created by [MaxMind](https://www.maxmind.com/) and is distributed under
[CC BY-SA 4.0](https://github.com/Loyalsoldier/geoip/blob/master/LICENSE).
The included license and attribution travel with the packaged bootstrap.
Converting its CIDRs into SRS changes the representation, not its coverage.

The previous SagerNet generator uses registered-country metadata. Country
datasets can differ, and neither one promises the physical location or
reachability of every address. No individual user's destination is added as
an exception. DB/DAT modification dates do not prove runtime SRS freshness.

The text publisher does not provide a separate text SHA256 file. HTTPS
certificate verification authenticates the transport; ETag avoids redundant
transfers. The locally recorded SRS SHA256 identifies the installed content,
and is not presented as a publisher signature.

## Runtime contract

`geodata_update: {"enabled": true}` is an app-only root config option, removed
before strict sing-box parsing. A present option starts the manager after a
successful connection; `enabled:false` disables the daily schedule while
retaining manual refresh. URL-test configurations omit this option. Schemes
without managed implicit RU rules publish `state:unused` and do not download.

The manager first waits three seconds, then checks due work every two seconds.
Downloads use the explicit `proxy` outbound, never environment/system HTTP
proxy settings or subscription insecure options. HTTPS and same-host redirects
are enforced, with a 30-second total deadline and an 8 MiB response limit.
Text parsing bounds line and prefix counts, rejects malformed/private/
catch-all networks, and generates deterministic SRS with the pinned writer.
Production responses must contain at least 1,000 canonical prefixes; a drop
below half the previous coverage is rejected for review.

The writable controls are relative to the core's config working directory:

* `.geodata/settings.json`: `{"enabled":false}` written atomically by the UI.
  Startup config is authoritative; a file left by an old process is ignored
  until its content or modification time changes.
* `.geodata/refresh.request`: a fresh UUID and newline, at most 128 bytes.
  New requests are polled and deduplicated; attempts are at least 30 seconds
  apart, including when automatic checks are disabled.
* `.geodata/status.json`: `state`, `checked_at`, `updated_at`, `applied_at`,
  `next_retry`, `sha256`, `prefix_count`, `source`, `error`, plus retry and
  conditional-request bookkeeping. Zero dates are omitted. `checked_at` means
  a successful source/cache validation, while `last_attempt_at` also records
  unsuccessful attempts. Error messages exclude response bodies/credentials.

Failures keep the previous data and back off from five minutes to six hours,
with jitter. A new file is flushed and renamed from the same directory, with
a separately retained last-good SRS. The local rule-set watcher publishes its
new immutable rules under the existing snapshot lock. Application is confirmed
by its callback and the actual IP-set fingerprint. A failed confirmation
restores the previous file and confirms that restore; if active rules cannot
be confirmed, status is `unconfirmed` and `applied_at` is cleared. Identical
downloads and HTTP 304 responses also verify the live IP set.

The tunnel is not restarted. New connections see new rules; existing TCP/UDP
sessions retain their chosen outbound. Instance cancellation stops and joins
the updater before close. A ten-second cleanup bound prevents a stuck Stop
RPC; an unfinished old writer blocks the next core instance until it exits.

## Verification

Run the core's `TestGeoData*` tests with the release build tags and overlay
modfile, then repeat with `-race` and CGO enabled. Tests use temporary files,
bounded fake HTTP responses and a real local binary rule-set watcher. They
cover offline recovery, the reviewed bootstrap, malformed text/SRS and
decompression/count bounds, atomic publish failures, live reload, rollback
confirmation, same-content/304 recovery, manual throttling, scheduling,
settings changes and cancellation. They do not start a TUN or change OS DNS,
proxy, routes or the user's installation.
