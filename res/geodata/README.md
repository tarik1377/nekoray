# Offline RU GeoIP bootstrap

`geoip-ru-bootstrap.srs` is a reviewed snapshot of the complete
[Loyalsoldier RU CIDR list](https://raw.githubusercontent.com/Loyalsoldier/geoip/release/text/ru.txt),
converted with the pinned sing-box `common/srs.Write` implementation. Its
source commit, original-text SHA256, SRS SHA256 and prefix count are recorded
in `geoip-ru-bootstrap.json`. The data license is CC BY-SA 4.0, included here;
the list incorporates GeoLite2 data created by MaxMind.

The source uses GeoLite2 Country CSV data. This differs from the older
SagerNet set, whose generator groups addresses by `RegisteredCountry`.
Country datasets can contain mistakes; this list is not a promise about the
physical location or reachability of every address.

Windows and macOS packaging both run `tools/geodata_bootstrap.py`. It checks
the reviewed manifest, full-file hash, SRS version, compressed content and
license before copying the reserved files. Packaging does not fetch an
unreviewed binary and does not overwrite an existing `geoip-ru.srs`.
The license and manifest also ship as `GEODATA-LICENSE.txt` and
`GEOIP-RU-BOOTSTRAP.txt` beside the application. Earlier updaters accepted only
SRS files inside `config/`; these ordinary runtime text files ensure the first
upgrade also delivers the data license and provenance.

The core manager owns its separate writable cache. It seeds that cache from
this reserved bootstrap and downloads the complete RU **text** list for later
updates. Existing users receive the new bootstrap filename during an upgrade;
user-maintained local rule-set files retain their names and contents.

Verify packaging with `python3 tools/geodata_bootstrap_test.py`. The core
tests additionally parse the real bootstrap with the runtime SRS reader.
