# Windows socket runtime repair

The failing local core was built with Go 1.26.1. Live logs showed repeated
`fatal error: fault` failures during concurrent socket reads, followed by GUI
core restarts and a pending profile stop. This is consistent with the Windows
overlapped-I/O memory lifetime defect tracked in
[golang/go#77975](https://github.com/golang/go/issues/77975). A matching stack is
evidence of a likely cause, not a complete proof that every observed crash has
the same origin.

The release build now pins Go 1.26.9 in `.go-version` for both CI and local
`libs/build_go.sh` builds. It contains the upstream socket lifetime fixes and
later maintenance fixes. The installed global Go SDK is not replaced.
`TestWindowsPendingAcceptCancellation` exercises pending socket cancellation
and garbage collection on loopback. It does not start a TUN or alter routes.

Vision also had a separately reproduced framing bug: UUID and five-byte padding
headers were interpreted as complete within one `Read`, although outer TLS can
split them anywhere. The version-pinned sing-vmess overlay retains parser state
across reads, handles end/direct transitions, and owns buffered payload bytes.
Regression tests include every boundary, bytewise input, multiple large frames,
and verified TLS 1.3 streams through both standard TLS and uTLS.

The Vision TLS buffer lookup retains a real pointer while locating fields,
uses `unsafe.Add`, and checks the expected buffer types before enabling the
optimization. It fails explicitly for incompatible layouts; certificate and
REALITY verification remain enabled.
