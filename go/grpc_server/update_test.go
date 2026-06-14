package grpc_server

import "testing"

func TestShouldUpdate(t *testing.T) {
	cases := []struct {
		latest, cur string
		want        bool
	}{
		{"v1.2.0", "v1.1.1", true},               // normal upgrade
		{"v1.1.1", "v1.1.1", false},              // same version
		{"v1.1.1", "v1.2.0", false},              // current is newer
		{"v1.2.0", "4.0.1-2024-12-12", true},     // legacy upstream straggler -> rescue
		{"v1.2.0", "v1.2.0", false},              // equal
		{"v1.2.10", "v1.2.9", true},              // numeric, not lexical
		{"v1.10.0", "v1.9.0", true},              // numeric minor
		{"v2.0.0", "v1.99.99", true},             // major wins
		{"garbage", "v1.1.1", false},             // unparseable latest -> no junk prompt
		{"v1.2.0", "", true},                     // empty current -> rescue
	}
	for _, c := range cases {
		if got := shouldUpdate(c.latest, c.cur); got != c.want {
			t.Errorf("shouldUpdate(%q,%q)=%v want %v", c.latest, c.cur, got, c.want)
		}
	}
}

func TestParseVerRejectsLegacy(t *testing.T) {
	if _, ok := parseVer("4.0.1-2024-12-12"); ok {
		t.Error("legacy upstream version must NOT parse as clean vX.Y.Z")
	}
	if _, ok := parseVer("v1.2.0"); !ok {
		t.Error("clean vX.Y.Z must parse")
	}
}
