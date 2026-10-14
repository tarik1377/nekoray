//go:build windows

package main

import (
	"encoding/json"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
)

func exclusiveFile(t *testing.T, path string, directory bool) syscall.Handle {
	t.Helper()
	name, _ := syscall.UTF16PtrFromString(path)
	flags := uint32(0)
	if directory {
		flags = 0x02000000
	}
	handle, err := syscall.CreateFile(name, syscall.GENERIC_READ, 0, nil, syscall.OPEN_EXISTING, flags, 0)
	if err != nil {
		t.Fatal(err)
	}
	return handle
}
func TestRestoreWindowsTargetAndSnapshotFileLocksLeaveWorkingFiles(t *testing.T) {
	for _, snapshot := range []bool{false, true} {
		t.Run(map[bool]string{false: "installed-DLL", true: "snapshot-DLL"}[snapshot], func(t *testing.T) {
			archive, install, _, options := rollbackFixture(t)
			if err := installUpdate(archive, install, options); err != nil {
				t.Fatal(err)
			}
			var current installationRecord
			_ = readJSONFile(filepath.Join(install, installationReceipt), &current)
			path := filepath.Join(install, "Qt6Core.dll")
			if snapshot {
				path = filepath.Join(install, previousDirectory, "files/Qt6Core.dll")
			}
			handle := exclusiveFile(t, path, false)
			err := restorePrevious(install, fixtureOptions())
			syscall.CloseHandle(handle)
			if err == nil {
				t.Fatal("locked runtime accepted")
			}
			if err := verifyRuntimeFiles(install, &current); err != nil {
				t.Fatal(err)
			}
			assertPrivateFuture(t, install)
			if err := restorePrevious(install, fixtureOptions()); err != nil {
				t.Fatal("retry after unlocking failed", err)
			}
		})
	}
}
func TestPublishingLockedPreviousSnapshotRollsBackNewInstall(t *testing.T) {
	archive, install, _, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	var current installationRecord
	_ = readJSONFile(filepath.Join(install, installationReceipt), &current)
	entries := map[string]string{}
	for _, file := range current.Files {
		entries["GreenRhythm/"+file.Path] = "next-" + file.Path
	}
	makeZip(t, archive, entries)
	handle := exclusiveFile(t, filepath.Join(install, previousDirectory), true)
	err := installUpdate(archive, install, options)
	syscall.CloseHandle(handle)
	if err == nil {
		t.Fatal("locked previous snapshot was overwritten")
	}
	if err := verifyRuntimeFiles(install, &current); err != nil {
		t.Fatal(err)
	}
	saved, err := loadPrevious(install)
	if err != nil || saved.Version != "v1.9.2.2" {
		t.Fatalf("previous snapshot lost: %v %v", saved, err)
	}
	assertPrivateFuture(t, install)
}
func TestRestoreFailureWithLockedEmergencyBackupRetainsRecoveryBytes(t *testing.T) {
	archive, install, _, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	var handle syscall.Handle
	restore := fixtureOptions()
	restore.start = func(string) error {
		backups, _ := filepath.Glob(filepath.Join(install, ".greenrhythm-backup-*"))
		if len(backups) != 1 {
			t.Fatal(backups)
		}
		handle = exclusiveFile(t, filepath.Join(backups[0], "greenrhythm.exe"), false)
		return errors.New("old-version loader failed")
	}
	err := restorePrevious(install, restore)
	if handle != 0 {
		syscall.CloseHandle(handle)
	}
	if err == nil || !strings.Contains(err.Error(), "резервные файлы сохранены") {
		t.Fatal("locked recovery not reported", err)
	}
	backups, _ := filepath.Glob(filepath.Join(install, ".greenrhythm-backup-*"))
	if len(backups) != 1 || !Exist(filepath.Join(backups[0], "greenrhythm.exe")) {
		t.Fatal("working GUI recovery bytes lost", backups)
	}
	assertPrivateFuture(t, install)
}
func TestExplicitLocalBootstrapChecksRealPEAndToolchainWithoutCopyingConfig(t *testing.T) {
	_, source, record, _ := rollbackFixture(t)
	seedGeoDataAssets(t, source, record)
	fixtureSource := filepath.Join(t.TempDir(), "main.go")
	if err := os.WriteFile(fixtureSource, []byte("package main\nimport \"fmt\"\nfunc main(){fmt.Println(\"sing-box: fixture GreenRhythm: v1.9.2.2\")}\n"), 0600); err != nil {
		t.Fatal(err)
	}
	exe := filepath.Join(t.TempDir(), "fixture.exe")
	if output, err := exec.Command("go", "build", "-trimpath", "-o", exe, fixtureSource).CombinedOutput(); err != nil {
		t.Fatalf("fixture compile: %v %s", err, output)
	}
	bytes, err := os.ReadFile(exe)
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"greenrhythm.exe", "greenrhythm_core.exe", "updater.exe"} {
		if err := os.WriteFile(filepath.Join(source, name), bytes, 0755); err != nil {
			t.Fatal(err)
		}
	}
	receipt := struct {
		Version      string              `json:"version"`
		SourceCommit string              `json:"source_commit"`
		GoToolchain  string              `json:"go_toolchain"`
		Qt           string              `json:"qt"`
		Files        []map[string]string `json:"files"`
	}{Version: record.Version, SourceCommit: record.SourceCommit, GoToolchain: "go1.26.9", Qt: "6.7.2"}
	for _, file := range record.Files {
		actual, err := checkedRuntimeFile(source, file.Path)
		if err != nil {
			t.Fatal(err)
		}
		receipt.Files = append(receipt.Files, map[string]string{"file": file.Path, "sha256": actual.SHA256})
	}
	receipt.Files = append(receipt.Files, map[string]string{"file": "config/profiles/future.json", "sha256": strings.Repeat("a", 64)})
	data, _ := json.Marshal(receipt)
	path := filepath.Join(t.TempDir(), "receipt.json")
	_ = os.WriteFile(path, data, 0600)
	target := t.TempDir()
	if err := registerPreviousLocal(target, source, path); err != nil {
		t.Fatal(err)
	}
	saved, err := loadPrevious(target)
	if err != nil || saved.Provenance != "explicit-verified-local" {
		t.Fatal(saved, err)
	}
	if Exist(filepath.Join(target, previousDirectory, "files/config/profiles/future.json")) || Exist(filepath.Join(target, previousDirectory, "files/config/private.json")) || Exist(filepath.Join(target, previousDirectory, "files/config/geoip-ru.srs")) {
		t.Fatal("private configuration copied")
	}
	for _, name := range reservedGeoDataNames {
		bytes, err := os.ReadFile(filepath.Join(target, previousDirectory, "files", filepath.FromSlash(name)))
		if err != nil || string(bytes) != "old-"+name {
			t.Fatalf("verified reserved asset omitted from local receipt/snapshot: %s %q %v", name, bytes, err)
		}
	}
	receipt.GoToolchain = "go1.26.1"
	data, _ = json.Marshal(receipt)
	_ = os.WriteFile(path, data, 0600)
	if err := registerPreviousLocal(target, source, path); err == nil {
		t.Fatal("unpatched toolchain accepted")
	}
}
