package main

import (
	"archive/zip"
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"
)

var reservedGeoDataNames = []string{
	"config/geoip-ru-bootstrap.srs",
	"config/geoip-ru-bootstrap.json",
	"config/LICENSE-CC-BY-SA-4.0.txt",
}

func addGeoDataPackageEntries(t *testing.T, archive string) {
	t.Helper()
	r, err := zip.OpenReader(archive)
	if err != nil {
		t.Fatal(err)
	}
	entries := map[string]string{}
	for _, f := range r.File {
		reader, err := f.Open()
		if err != nil {
			t.Fatal(err)
		}
		content, err := io.ReadAll(reader)
		reader.Close()
		if err != nil {
			t.Fatal(err)
		}
		entries[f.Name] = string(content)
	}
	r.Close()
	for _, name := range reservedGeoDataNames {
		entries["GreenRhythm/"+name] = "new-" + name
	}
	for _, name := range []string{"config/private.json", "config/geoip-ru-bootstrap.json.bak", "config/licenses/LICENSE-CC-BY-SA-4.0.txt"} {
		entries["GreenRhythm/"+name] = "untrusted bundled config"
	}
	makeZip(t, archive, entries)
}

func seedGeoDataAssets(t *testing.T, install string, record *installationRecord) {
	t.Helper()
	for _, name := range reservedGeoDataNames {
		if err := os.WriteFile(filepath.Join(install, filepath.FromSlash(name)), []byte("old-"+name), 0644); err != nil {
			t.Fatal(err)
		}
		if record != nil {
			file, err := checkedRuntimeFile(install, name)
			if err != nil {
				t.Fatal(err)
			}
			record.Files = append(record.Files, file)
		}
	}
	for _, name := range []string{"config/private.json", "config/geoip-ru.srs"} {
		if err := os.WriteFile(filepath.Join(install, filepath.FromSlash(name)), []byte("user-managed-"+name), 0644); err != nil {
			t.Fatal(err)
		}
	}
	if record != nil {
		if err := writeRecord(filepath.Join(install, installationReceipt), record); err != nil {
			t.Fatal(err)
		}
	}
}

func checkGeoDataAssets(t *testing.T, install, prefix string) {
	t.Helper()
	for _, name := range reservedGeoDataNames {
		content, err := os.ReadFile(filepath.Join(install, filepath.FromSlash(name)))
		if err != nil || string(content) != prefix+name {
			t.Fatalf("%s: %q %v", name, content, err)
		}
	}
	for _, name := range []string{"config/private.json", "config/geoip-ru.srs"} {
		content, err := os.ReadFile(filepath.Join(install, filepath.FromSlash(name)))
		if err != nil || string(content) != "user-managed-"+name {
			t.Fatalf("user file changed: %s %q %v", name, content, err)
		}
	}
	for _, name := range []string{"config/geoip-ru-bootstrap.json.bak", "config/licenses/LICENSE-CC-BY-SA-4.0.txt"} {
		if Exist(filepath.Join(install, filepath.FromSlash(name))) {
			t.Fatalf("unexpected bundled config installed: %s", name)
		}
	}
}

func TestInstallGeoDataMetadataAndPreserveOtherConfig(t *testing.T) {
	archive, install, _ := installFixture(t)
	seedGeoDataAssets(t, install, nil)
	addGeoDataPackageEntries(t, archive)
	if err := installUpdate(archive, install, fixtureOptions()); err != nil {
		t.Fatal(err)
	}
	checkGeoDataAssets(t, install, "new-")
	assertConfig(t, install)
}

func TestFailedInstallRestoresGeoDataMetadata(t *testing.T) {
	archive, install, old := installFixture(t)
	seedGeoDataAssets(t, install, nil)
	addGeoDataPackageEntries(t, archive)
	options := fixtureOptions()
	options.start = func(string) error { return errors.New("fixture startup failure") }
	if err := installUpdate(archive, install, options); err == nil {
		t.Fatal("startup failure ignored")
	}
	checkGeoDataAssets(t, install, "old-")
	assertOriginals(t, install, old)
}

func TestGeoDataSnapshotRestoreAndFailedRestore(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	for _, failStart := range []bool{false, true} {
		t.Run(map[bool]string{false: "restore", true: "failed-restore"}[failStart], func(t *testing.T) {
			archive, install, old, options := rollbackFixture(t)
			seedGeoDataAssets(t, install, old)
			addGeoDataPackageEntries(t, archive)
			if err := installUpdate(archive, install, options); err != nil {
				t.Fatal(err)
			}
			checkGeoDataAssets(t, install, "new-")
			saved, err := loadPrevious(install)
			if err != nil || verifyRuntimeFiles(filepath.Join(install, previousDirectory, "files"), old) != nil {
				t.Fatalf("snapshot incomplete: %v %v", saved, err)
			}
			for _, name := range []string{"config/private.json", "config/geoip-ru.srs", "config/profiles/future.json"} {
				if Exist(filepath.Join(install, previousDirectory, "files", filepath.FromSlash(name))) {
					t.Fatalf("private/custom file entered snapshot: %s", name)
				}
			}
			restore := fixtureOptions()
			if failStart {
				restore.start = func(string) error { return errors.New("fixture restore startup failure") }
			}
			err = restorePrevious(install, restore)
			if failStart {
				if err == nil {
					t.Fatal("failed restore accepted")
				}
				checkGeoDataAssets(t, install, "new-")
			} else {
				if err != nil {
					t.Fatal(err)
				}
				checkGeoDataAssets(t, install, "old-")
			}
			assertPrivateFuture(t, install)
		})
	}
}

func TestReservedGeoDataAssetsBoundBySnapshotChecksums(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, old, options := rollbackFixture(t)
	seedGeoDataAssets(t, install, old)
	addGeoDataPackageEntries(t, archive)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(install, previousDirectory, "files/config/geoip-ru-bootstrap.json")
	if err := os.WriteFile(path, []byte("tampered metadata"), 0644); err != nil {
		t.Fatal(err)
	}
	if previousVersionInfo(install).Available {
		t.Fatal("corrupt metadata accepted")
	}
	if err := restorePrevious(install, fixtureOptions()); err == nil {
		t.Fatal("corrupt metadata restored")
	}
	checkGeoDataAssets(t, install, "new-")
	assertPrivateFuture(t, install)
}

func TestRollbackToPreGeoDataVersionRemovesNewReservedAssets(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, old, options := rollbackFixture(t)
	legacy := filepath.Join(install, "config/geoip-ru.srs")
	if err := os.WriteFile(legacy, []byte("custom legacy rule set"), 0644); err != nil {
		t.Fatal(err)
	}
	addGeoDataPackageEntries(t, archive)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	if err := restorePrevious(install, fixtureOptions()); err != nil {
		t.Fatal(err)
	}
	for _, name := range reservedGeoDataNames {
		if Exist(filepath.Join(install, filepath.FromSlash(name))) {
			t.Fatalf("new-only reserved asset survived old-version restore: %s", name)
		}
	}
	if err := verifyRuntimeFiles(install, old); err != nil {
		t.Fatal(err)
	}
	if content, err := os.ReadFile(legacy); err != nil || string(content) != "custom legacy rule set" {
		t.Fatalf("legacy set changed: %q %v", content, err)
	}
	assertPrivateFuture(t, install)
}
