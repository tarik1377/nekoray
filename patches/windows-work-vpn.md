# Windows work VPN discovery

Core-only, opt-in integration for sing-box v1.13.18. Add the top-level boolean
`"windows_work_vpn": true` to each relevant core configuration (main proxy and
standalone external TUN). Also set `route.auto_detect_interface: true` and leave
`route.default_interface` unset. The option defaults to off. The core consumes
it before sing-box parsing and registers a separate discovery manager per box.
No GUI, LocalDns, DataStore or legacy-transformer changes are needed here.

The core adds one `windows_work_vpn` route condition and a reserved direct
outbound `greenrhythm-work-vpn`. The rule follows leading sniff/DNS interception/
reject/route-options guards and precedes ordinary proxy or process selection.
The existing Internet final, DNS servers and DNS rules are retained. Reject rules
later in the configuration retain their existing position; integration must put
mandatory guards before the corporate routing rule.

## DNS and routing policy

* Read active adapters, their DNS servers and DNS suffixes using
  `GetAdaptersAddresses`, and IPv4/IPv6 routes using `GetIpForwardTable2`.
* Recognize OpenVPN/TAP-Windows adapters; exclude `neko-tun`, `sing-tun`,
  `v2RayTun`, and configured TUN interface names, including custom names.
  Generic unidentified Wintun/Hyper-V/Ethernet adapters are not inferred as VPNs.
* Bind corporate DNS sockets to the numeric VPN interface. The DNS server must
  itself have a specific route on that VPN. Use UDP with TCP retry under a total
  900 ms budget; query different VPN resolvers concurrently.
* For suffixes advertised by those adapters, use their DNS directly. Also read
  plain split-DNS NRPT `Name`/`GenericDNSServers` rules from the effective policy
  registry hive (group-policy hive takes precedence). Honour suffix vs exact-name
  matching and exclusions to non-VPN resolvers. Ignore global `.` policies.
  DNSSEC/IPsec/DirectAccess NRPT policies are not reimplemented as plain DNS.
* For an unknown name, query the ordinary configured DNS first. Consult VPN DNS
  only after NXDOMAIN or NODATA; never after an ordinary timeout, refusal,
  response-rule rejection, or successful public answer. For NODATA, also check
  the other address family through ordinary DNS to avoid probing IPv4-only or
  IPv6-only public names. A fallback answer is accepted only if all its address
  records are in specific VPN routes. No corporate domains/IPs are compiled in.
* Fake-IP names are classified through the real configured final DNS, retaining
  fake-IP responses for public names. If final is itself fake-IP, the first real
  configured transport is used; configure a real final DNS for unambiguous policy.
* HTTP/SOCKS domain destinations also resolve through the normal DNS router
  before the corporate route condition, with a two-second total limit. Public
  addresses keep the configured outbound and original proxy domain destination.
  An OS-scoped DNS failure stays on the direct path so its name is not sent to a
  remote proxy resolver.
* Use specific OS VPN prefixes for IP rules and interface binding, including
  public prefixes. Exclude default `/0` and redirect-gateway `/1` routes.
  Longest prefix, then route+adapter metric, determines the interface. Specific
  ordinary LAN routes override VPN aggregates; proxy capture routes are ignored.

Snapshots refresh on demand at most every two seconds and on network reset.
Corporate DNS cache entries live for at most 30 seconds (bounded by DNS TTL);
an OS adapter/route/DNS/NRPT change invalidates them. Caches are per box and bounded.
Existing established connections are not migrated. Discovery only reads OS state;
it does not edit adapters, DNS, routes, NRPT, hosts, proxy settings or firewalls.

## Verification

Always prepare the overlay before testing/building. Do not edit the module cache.

```bash
cd go/cmd/nekobox_core
MODFILE=$(bash _overlay/prepare.sh)
CORE_TAGS="with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api"
go test -modfile "$MODFILE" -tags "$CORE_TAGS" ./...
go test -modfile "$MODFILE" -tags "$CORE_TAGS" github.com/sagernet/sing-box/common/workvpn github.com/sagernet/sing-box/dns github.com/sagernet/sing-box/route/rule -run TestWorkVPN
go test -modfile "$MODFILE" -tags "$CORE_TAGS" github.com/sagernet/sing-box/route -run TestPrivateDestinationInterface
go build -modfile "$MODFILE" -tags "$CORE_TAGS" .
```

For an explicitly authorized native table observation, set
`GREENRHYTHM_WORK_VPN_READONLY_TEST=1` and optionally
`GREENRHYTHM_WORK_VPN_TEST_ADDRESS` before running
`TestWorkVPNLiveDiscoveryReadOnly`. It prints counts and the selected interface
index, not domains/DNS addresses. Normal tests use synthetic data and localhost
listeners only; they never start a TUN. Core configuration tests construct but
do not start any TUN. The loopback startup test has no TUN/auto-route.

## Limits

The mechanism needs specific routes and a reachable VPN DNS. Another TUN's
firewall can deny these DNS sockets; discovery does not bypass that protection.
Without an OS namespace, public DNS must first report the name absent. A public
wildcard/sinkhole/positive answer deliberately prevents corporate fallback, and
the initial unknown query therefore reaches the ordinary DNS. Servers answering
with a CNAME but no address records cannot be classified by the fallback.

Ordinary configured System/Local DNS retains its existing Windows behaviour;
this feature does not promise that a separately configured OS DNS policy will
avoid corporate servers. The guarantee is that this feature adds no corporate
probe for successful ordinary names and does not redirect general IP traffic.
Unidentified VPN drivers and complex NRPT security policies require a separate
supported integration. Real TUN startup, reconnect under traffic, and GUI option
integration must be checked separately without disturbing active user sessions.

Primary API references: [route table](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getipforwardtable2),
[effective NRPT](https://learn.microsoft.com/en-us/powershell/module/dnsclient/get-dnsclientnrptpolicy),
[OpenVPN NRPT implementation](https://github.com/OpenVPN/openvpn/blob/master/src/openvpnserv/interactive.c).
