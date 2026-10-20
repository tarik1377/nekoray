# Domain requests and country-IP routes

Browsers send hostnames in HTTP CONNECT and SOCKS requests. The legacy config
transformer removed `inbound.domain_strategy` for sing-box 1.13 without adding a
replacement. An unresolved hostname could therefore miss `geoip:ru` and use the
VPN fallback even when the current country database contained its Russian IP.

The runtime transformer now inserts a non-terminal `resolve` action immediately
before the first destination-IP matcher. This covers legacy `geoip`, IP CIDRs,
private destinations, standard `geoip-*` rule sets (including the managed RU
set), inline IP sets, and IP matches inside logical rules. Source-only selectors
do not trigger destination resolution. A preceding unconditional explicit
resolver is retained, including its server and address-family strategy.

The original rule order, terminal actions and DNS configuration stay intact.
Earlier explicit VPN and block rules still win. The generated action does not
name a public resolver or force IPv4: it uses the configured split-DNS rules and
`dns.strategy`. It neither changes OS routes nor writes saved profiles. Country
classification still depends on the installed GeoIP data; a Russian domain name
alone does not imply a Russian destination IP.

Regression tests exercise actual sing-box routing through loopback HTTP and
SOCKS clients with controlled DNS answers. They cover multiple RU hostnames,
IPv4 literals, IPv6, foreign addresses using the VPN fallback, explicit VPN and
block choices, and a private office zone resolved only by its own DNS server.
No test starts TUN, imports a subscription, or changes the installed client.
