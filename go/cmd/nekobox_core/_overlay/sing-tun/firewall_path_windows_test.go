// overlay-base: github.com/sagernet/sing-tun v0.8.12-0.20260727151122-3a09076491df
package tun

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"golang.org/x/sys/windows"
)

// Only file/path operations. Never adds a firewall rule or creates a TUN.
func TestFirewallExecutablePath(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "application directory with spaces")
	if err := os.Mkdir(dir, 0700); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(dir, "test program.exe")
	if err := os.WriteFile(path, []byte("fixture"), 0600); err != nil {
		t.Fatal(err)
	}
	got, err := firewallExecutablePath(path)
	if err != nil {
		t.Fatalf("long path: %q %v", got, err)
	}
	// TempDir itself can contain RUNNER~1. Compare file identity first, then
	// require both spellings to expand to the same canonical full path.
	original, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	expanded, err := os.Stat(got)
	if err != nil || !os.SameFile(original, expanded) {
		t.Fatalf("expanded path identifies a different file: %q %v", got, err)
	}
	canonical := got
	p, err := windows.UTF16PtrFromString(path)
	if err != nil {
		t.Fatal(err)
	}
	buffer := make([]uint16, 32768)
	n, err := windows.GetShortPathName(p, &buffer[0], uint32(len(buffer)))
	if err != nil || n == 0 || int(n) >= len(buffer) {
		t.Fatalf("short path length %d: %v", n, err)
	}
	short := windows.UTF16ToString(buffer[:n])
	got, err = firewallExecutablePath(short)
	if err != nil || !strings.EqualFold(got, canonical) {
		t.Fatalf("short path %q expanded to %q: %v", short, got, err)
	}
}
