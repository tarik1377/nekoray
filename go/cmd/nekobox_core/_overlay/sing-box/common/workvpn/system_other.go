// overlay-base: github.com/sagernet/sing-box v1.13.18
//go:build !windows

package workvpn

import "errors"

func readSystemSnapshot() (Snapshot, error) {
	return Snapshot{}, errors.New("work VPN discovery is only supported on Windows")
}
