package main

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func rollbackFixture(t *testing.T) (string, string, *installationRecord, installOptions) {
	t.Helper()
	archive, install, old := installFixture(t)
	entries := map[string]string{"GreenRhythm/newasset.dat": "new asset"}
	for _, name := range []string{"greenrhythm.exe", "greenrhythm_core.exe", "updater.exe", "xray.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Widgets.dll", "Qt6Svg.dll", "platforms/qwindows.dll", "wintun.dll"} {
		value := "old-" + name
		if existing, ok := old[name]; ok {
			value = existing
		}
		path := filepath.Join(install, filepath.FromSlash(name))
		_ = os.MkdirAll(filepath.Dir(path), 0755)
		if err := os.WriteFile(path, []byte(value), 0644); err != nil {
			t.Fatal(err)
		}
		entries["GreenRhythm/"+name] = "new-" + name
	}
	record := &installationRecord{Schema: 1, ConfigSchema: 1, Platform: "windows-x64", Version: "v1.9.2.2", Provenance: "explicit-verified-local", SourceCommit: strings.Repeat("a", 40), GoToolchain: "go1.26.9"}
	for name := range entries {
		rel := strings.TrimPrefix(name, "GreenRhythm/")
		if rel == "newasset.dat" {
			continue
		}
		file, err := checkedRuntimeFile(install, rel)
		if err != nil {
			t.Fatal(err)
		}
		record.Files = append(record.Files, file)
	}
	if err := writeRecord(filepath.Join(install, installationReceipt), record); err != nil {
		t.Fatal(err)
	}
	unknown := []byte(`{"new_profile_option":{"opaque":42},"private":"fixture only"}`)
	_ = os.WriteFile(filepath.Join(install, "config/profiles/future.json"), unknown, 0644)
	entries["GreenRhythm/config/profiles/future.json"] = "must not replace private future data"
	makeZip(t, archive, entries)
	options := fixtureOptions()
	options.record = func(_ string, payload string, files []string) (*installationRecord, error) {
		next := *record
		next.Version = "v1.9.2.3"
		next.Files = nil
		for _, rel := range files {
			if !rollbackRuntimePath(filepath.ToSlash(rel)) {
				continue
			}
			file, err := checkedRuntimeFile(payload, filepath.ToSlash(rel))
			if err != nil {
				return nil, err
			}
			next.Files = append(next.Files, file)
		}
		return &next, validateRecord(&next)
	}
	return archive, install, record, options
}
func assertPrivateFuture(t *testing.T, install string) {
	t.Helper()
	assertConfig(t, install)
	got, err := os.ReadFile(filepath.Join(install, "config/profiles/future.json"))
	if err != nil || string(got) != `{"new_profile_option":{"opaque":42},"private":"fixture only"}` {
		t.Fatalf("unknown future profile changed: %q %v", got, err)
	}
}
func TestSnapshotPersistsAfterVerifiedInstallAndContainsOnlyRuntime(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, old, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	saved, err := loadPrevious(install)
	if err != nil || saved.Version != old.Version {
		t.Fatalf("%v %v", saved, err)
	}
	if !previousVersionInfo(install).Available {
		t.Fatal("saved version not offered")
	}
	err = filepath.Walk(filepath.Join(install, previousDirectory), func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		rel, _ := filepath.Rel(filepath.Join(install, previousDirectory), path)
		if rel != "manifest.json" && !rollbackRuntimePath(filepath.ToSlash(strings.TrimPrefix(rel, "files"+string(os.PathSeparator)))) {
			return errors.New("private or unexpected file saved")
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	assertPrivateFuture(t, install)
	backups, _ := filepath.Glob(filepath.Join(install, ".greenrhythm-backup-*"))
	if len(backups) != 0 {
		t.Fatal(backups)
	}
}
func TestRestorePreviousRemovesNewRuntimeAndPreservesUnknownProfiles(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, old, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	if err := restorePrevious(install, fixtureOptions()); err != nil {
		t.Fatal(err)
	}
	if err := verifyRuntimeFiles(install, old); err != nil {
		t.Fatal(err)
	}
	if Exist(filepath.Join(install, "newasset.dat")) {
		t.Fatal("new-only runtime remained after restore")
	}
	assertPrivateFuture(t, install)
	if previousVersionInfo(install).Available {
		t.Fatal("current version offered as previous")
	}
}
func TestRestoreRejectsChecksumCorruptionBeforeMutation(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, _, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	oldGUI, _ := os.ReadFile(filepath.Join(install, "greenrhythm.exe"))
	_ = os.WriteFile(filepath.Join(install, previousDirectory, "files/greenrhythm_core.exe"), []byte("corrupt"), 0644)
	if previousVersionInfo(install).Available {
		t.Fatal("corruption accepted by inspection")
	}
	if err := restorePrevious(install, fixtureOptions()); err == nil {
		t.Fatal("corruption accepted by installer")
	}
	got, _ := os.ReadFile(filepath.Join(install, "greenrhythm.exe"))
	if string(got) != string(oldGUI) {
		t.Fatal("working GUI changed")
	}
	assertPrivateFuture(t, install)
}
func TestRestoreRestartFailureRollsForwardAndRestartsWorkingVersion(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, _, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	var current installationRecord
	if err := readJSONFile(filepath.Join(install, installationReceipt), &current); err != nil {
		t.Fatal(err)
	}
	restore := fixtureOptions()
	starts := 0
	restore.start = func(string) error {
		starts++
		if starts == 1 {
			return errors.New("old GUI loader failure")
		}
		return nil
	}
	if err := restorePrevious(install, restore); err == nil {
		t.Fatal("failed rollback announced as successful")
	}
	if starts != 2 {
		t.Fatal("working version not restarted", starts)
	}
	if err := verifyRuntimeFiles(install, &current); err != nil {
		t.Fatal(err)
	}
	assertPrivateFuture(t, install)
}
func TestRestoreEveryReplaceFailureRollsForward(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	for fail := 1; fail <= 26; fail++ {
		t.Run(string(rune('A'+fail)), func(t *testing.T) {
			archive, install, _, options := rollbackFixture(t)
			if err := installUpdate(archive, install, options); err != nil {
				t.Fatal(err)
			}
			var current installationRecord
			_ = readJSONFile(filepath.Join(install, installationReceipt), &current)
			restore := fixtureOptions()
			calls := 0
			restore.rename = func(from, to string) error {
				calls++
				if calls == fail {
					return errors.New("locked fixture")
				}
				return os.Rename(from, to)
			}
			err := restorePrevious(install, restore)
			if fail <= 25 && err == nil {
				t.Fatal("rename failure ignored", fail)
			}
			if err != nil {
				if err := verifyRuntimeFiles(install, &current); err != nil {
					t.Fatal(err)
				}
				assertPrivateFuture(t, install)
			}
		})
	}
}
func TestRecordRejectsUnverifiedLegacyForeignPlatformAndPrivatePaths(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	_, _, record, _ := rollbackFixture(t)
	for _, mutate := range []func(*installationRecord){
		func(r *installationRecord) { r.Provenance = "unknown" },
		func(r *installationRecord) { r.Version = "v1.9.2.1" },
		func(r *installationRecord) { r.GoToolchain = "go1.26.1" },
		func(r *installationRecord) { r.Platform = "linux-x64" },
		func(r *installationRecord) {
			r.Files = append(r.Files, runtimeFile{Path: "config/profiles/1.json", SHA256: strings.Repeat("a", 64), Size: 1})
		},
		func(r *installationRecord) { r.Provenance = "signed-release"; r.Release = nil },
	} {
		data, _ := json.Marshal(record)
		var candidate installationRecord
		_ = json.Unmarshal(data, &candidate)
		mutate(&candidate)
		if validateRecord(&candidate) == nil {
			t.Fatal("unsafe saved record accepted", candidate)
		}
	}
}

func snapshotBytes(t *testing.T, root string) map[string]string {
	t.Helper()
	result := make(map[string]string)
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		rel, err := filepath.Rel(root, path)
		if err != nil {
			return err
		}
		bytes, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		result[rel] = string(bytes)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	return result
}
func nextUpdateFixture(t *testing.T, archive, install string, options installOptions) installOptions {
	t.Helper()
	var current installationRecord
	if err := readJSONFile(filepath.Join(install, installationReceipt), &current); err != nil {
		t.Fatal(err)
	}
	entries := make(map[string]string)
	for _, file := range current.Files {
		entries["GreenRhythm/"+file.Path] = "version-C-" + file.Path
	}
	makeZip(t, archive, entries)
	previousBuilder := options.record
	options.record = func(archive, payload string, files []string) (*installationRecord, error) {
		record, err := previousBuilder(archive, payload, files)
		if err == nil {
			record.Version = "v1.9.2.4"
		}
		return record, err
	}
	return options
}
func TestFailedThirdUpdateKeepsEarlierPreviousVersionByteExact(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, _, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	originalPrevious := snapshotBytes(t, filepath.Join(install, previousDirectory))
	beforeInfo := previousVersionInfo(install)
	if !beforeInfo.Available || beforeInfo.Version != "v1.9.2.2" {
		t.Fatal(beforeInfo)
	}
	var current installationRecord
	_ = readJSONFile(filepath.Join(install, installationReceipt), &current)
	next := nextUpdateFixture(t, archive, install, options)
	next.start = func(string) error { return errors.New("version C immediate loader failure") }
	if err := installUpdate(archive, install, next); err == nil {
		t.Fatal("failed C accepted")
	}
	if err := verifyRuntimeFiles(install, &current); err != nil {
		t.Fatal("A current was not restored", err)
	}
	afterPrevious := snapshotBytes(t, filepath.Join(install, previousDirectory))
	if len(afterPrevious) != len(originalPrevious) {
		t.Fatal("B previous file set changed")
	}
	for path, want := range originalPrevious {
		if afterPrevious[path] != want {
			t.Fatalf("B previous changed after C failure: %s", path)
		}
	}
	afterInfo := previousVersionInfo(install)
	if !afterInfo.Available || afterInfo.Version != beforeInfo.Version {
		t.Fatal("B previous is unavailable after C failure", afterInfo)
	}
	assertPrivateFuture(t, install)
}

func TestDisplacedPreviousSnapshotIsRetainedUntilSuccessfulStartup(t *testing.T) {
	if !runtimeWindows() {
		t.Skip("Windows rollback")
	}
	archive, install, _, options := rollbackFixture(t)
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	before := snapshotBytes(t, filepath.Join(install, previousDirectory))
	next := nextUpdateFixture(t, archive, install, options)
	checked := false
	next.start = func(string) error {
		retired, _ := filepath.Glob(filepath.Join(install, ".greenrhythm-retired-*"))
		if len(retired) != 1 {
			t.Errorf("previous B was discarded before C startup: %v", retired)
			return errors.New("missing B")
		}
		bytes := snapshotBytes(t, retired[0])
		if len(bytes) != len(before) {
			t.Error("retired B file set changed before C startup")
		}
		for path, want := range before {
			if bytes[path] != want {
				t.Errorf("retired B changed before C startup: %s", path)
			}
		}
		checked = true
		return nil
	}
	if err := installUpdate(archive, install, next); err != nil {
		t.Fatal(err)
	}
	retired, _ := filepath.Glob(filepath.Join(install, ".greenrhythm-retired-*"))
	if !checked || len(retired) != 0 {
		t.Fatal("retired B was not discarded after successful C startup", retired)
	}
	info := previousVersionInfo(install)
	if !info.Available || info.Version != "v1.9.2.3" {
		t.Fatal("A is not offered as previous after successful C startup", info)
	}
	assertPrivateFuture(t, install)
}
