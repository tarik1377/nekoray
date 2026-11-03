package main

import (
	"path/filepath"
	"reflect"
	"testing"
)

func TestDetachedCacheUpdaterPreservesInstallAndData(t *testing.T) {
	root := t.TempDir()
	exe := filepath.Join(root, "data", "updater.next.exe")
	install, data := filepath.Join(root, "Program Files", "GreenRhythm"), filepath.Join(root, "App Data", "GreenRhythm")
	archive := filepath.Join(data, "greenrhythm.zip")
	options, err := parseLaunchOptions(exe, []string{"--install-dir", install, "--data-dir", data, archive})
	if err != nil || options.install != install || options.data != data || !reflect.DeepEqual(options.action, []string{archive}) {
		t.Fatal(options, err)
	}
	child, err := parseLaunchOptions(filepath.Join(root, "temp", "greenrhythm-updater-123.exe"), options.forwarded())
	if err != nil || !reflect.DeepEqual(child, options) {
		t.Fatalf("bootstrap changed destinations: %+v %v", child, err)
	}
	if !reflect.DeepEqual(child.applicationArgs(), []string{"-appdata", data}) {
		t.Fatal(child.applicationArgs())
	}
}
func TestRollbackAndPortableLaunchArguments(t *testing.T) {
	exe := filepath.Join(t.TempDir(), "updater.exe")
	data := t.TempDir()
	options, err := parseLaunchOptions(exe, []string{"--data-dir", data, "--rollback"})
	if err != nil || !reflect.DeepEqual(options.applicationArgs(), []string{"-appdata", data}) || !reflect.DeepEqual(options.action, []string{"--rollback"}) {
		t.Fatal(options, err)
	}
	portable, err := parseLaunchOptions(exe, []string{"--portable"})
	if err != nil || !reflect.DeepEqual(portable.applicationArgs(), []string{"-portable"}) {
		t.Fatal(portable, err)
	}
	legacy, err := parseLaunchOptions(exe, nil)
	if err != nil || len(legacy.applicationArgs()) != 0 || legacy.install != filepath.Dir(exe) {
		t.Fatal(legacy, err)
	}
}
func TestAmbiguousLaunchArgumentsRejected(t *testing.T) {
	exe, data := filepath.Join(t.TempDir(), "updater.exe"), t.TempDir()
	for _, args := range [][]string{{"--install-dir"}, {"--data-dir", "relative"}, {"--data-dir", data, "--portable"}, {"--data-dir", data, "--data-dir", data}, {"--rollback", "another.zip"}, {"--unknown"}} {
		if _, err := parseLaunchOptions(exe, args); err == nil {
			t.Fatalf("accepted %q", args)
		}
	}
}
