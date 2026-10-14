//go:build windows

package main

import (
	"os"
	"path/filepath"
	"syscall"
	"testing"
)

func TestInstallWindowsLockedFileLeavesOriginals(t *testing.T) {
	archive, install, old := installFixture(t)
	name, _ := syscall.UTF16PtrFromString(filepath.Join(install, "Qt6Core.dll"))
	handle, err := syscall.CreateFile(name, syscall.GENERIC_READ, 0, nil, syscall.OPEN_EXISTING, 0, 0)
	if err != nil {
		t.Fatal(err)
	}
	if err := installUpdate(archive, install, fixtureOptions()); err == nil {
		syscall.CloseHandle(handle)
		t.Fatal("locked DLL accepted")
	}
	syscall.CloseHandle(handle)
	assertOriginals(t, install, old)
	if err := installUpdate(archive, install, fixtureOptions()); err != nil {
		t.Fatal(err)
	}
}
func TestStartApplicationReportsInvalidExecutable(t *testing.T) {
	install := t.TempDir()
	os.WriteFile(filepath.Join(install, "greenrhythm.exe"), []byte("invalid executable"), 0644)
	if err := startApplication(install); err == nil {
		t.Fatal("invalid executable accepted")
	}
}

func TestWindowsConcurrentInstallerIsRejected(t *testing.T) {
	archive, install, _ := installFixture(t)
	entered, release := make(chan struct{}), make(chan struct{})
	done := make(chan error, 1)
	options := fixtureOptions()
	options.verify = func(string) error { close(entered); <-release; return nil }
	go func() { done <- installUpdate(archive, install, options) }()
	<-entered
	if err := installUpdate(archive, install, fixtureOptions()); err == nil {
		t.Error("overlapping installer accepted")
	}
	close(release)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}
