# sing-box and sing-quic are managed via go.mod (the exact version lives there,
# not here — hardcoding it once already left the binary reporting a stale one).
# libneko is the exception: go.mod `replace`s it to a sibling directory, so it
# must be cloned separately.
