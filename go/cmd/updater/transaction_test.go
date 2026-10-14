package main

import (
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func installFixture(t *testing.T) (string, string, map[string]string) {
	t.Helper()
	root := t.TempDir()
	install := filepath.Join(root, "app")
	if err := os.MkdirAll(install, 0755); err != nil {
		t.Fatal(err)
	}
	suffix := ""
	if runtime.GOOS == "windows" {
		suffix = ".exe"
	}
	old := map[string]string{}
	entries := map[string]string{}
	for _, base := range []string{"greenrhythm", "greenrhythm_core", "updater", "xray", "Qt6Core.dll"} {
		name := base
		if !strings.Contains(base, ".") {
			name += suffix
		}
		old[name] = "old-" + base
		entries["GreenRhythm/"+name] = "new-" + base
		if err := os.WriteFile(filepath.Join(install, name), []byte(old[name]), 0644); err != nil {
			t.Fatal(err)
		}
	}
	for _, rel := range []string{"config/groups/nekobox.json", "config/profiles/1.json", "config/routes_box/Default"} {
		name := filepath.Join(install, rel)
		os.MkdirAll(filepath.Dir(name), 0755)
		os.WriteFile(name, []byte("private fixture"), 0644)
		entries["GreenRhythm/"+rel] = "bundled template"
	}
	entries["GreenRhythm/newasset.dat"] = "new asset"
	zip := filepath.Join(root, "package.zip")
	makeZip(t, zip, entries)
	return zip, install, old
}
func assertOriginals(t *testing.T, install string, old map[string]string) {
	t.Helper()
	for rel, want := range old {
		got, err := os.ReadFile(filepath.Join(install, rel))
		if err != nil || string(got) != want {
			t.Fatalf("%s: %q %v", rel, got, err)
		}
	}
	if Exist(filepath.Join(install, "newasset.dat")) {
		t.Fatal("new-only file survived rollback")
	}
	assertConfig(t, install)
}
func assertConfig(t *testing.T, install string) {
	t.Helper()
	for _, rel := range []string{"config/groups/nekobox.json", "config/profiles/1.json", "config/routes_box/Default"} {
		got, err := os.ReadFile(filepath.Join(install, rel))
		if err != nil || string(got) != "private fixture" {
			t.Fatalf("user config changed: %s %q %v", rel, got, err)
		}
	}
}
func fixtureOptions() installOptions {
	return installOptions{verify: func(string) error { return nil }, start: func(string) error { return nil }, rename: os.Rename}
}
func TestInstallReplacesUpdaterAndPreservesConfig(t *testing.T) {
	archive, install, _ := installFixture(t)
	var stages []string
	options := fixtureOptions()
	options.report = func(s string) { stages = append(stages, s) }
	if err := installUpdate(archive, install, options); err != nil {
		t.Fatal(err)
	}
	suffix := ""
	if runtime.GOOS == "windows" {
		suffix = ".exe"
	}
	got, _ := os.ReadFile(filepath.Join(install, "updater"+suffix))
	if string(got) != "new-updater" {
		t.Fatalf("updater was not replaced: %q", got)
	}
	assertConfig(t, install)
	if Exist(archive) {
		t.Fatal("successful package not cleaned")
	}
	if len(stages) < 6 {
		t.Fatal(stages)
	}
}
func TestInstallRejectsVerificationBeforeMutation(t *testing.T) {
	archive, install, old := installFixture(t)
	options := fixtureOptions()
	options.verify = func(string) error { return errors.New("fixture signature mismatch") }
	if err := installUpdate(archive, install, options); err == nil {
		t.Fatal("verification failure ignored")
	}
	assertOriginals(t, install, old)
	if !Exist(archive) {
		t.Fatal("package lost on failure")
	}
}
func TestInstallRollsBackEveryRenameFailure(t *testing.T) {
	// Five replacements take two renames each; one newly created asset takes one.
	for failAt := 1; failAt <= 11; failAt++ {
		t.Run(string(rune('A'+failAt)), func(t *testing.T) {
			archive, install, old := installFixture(t)
			options := fixtureOptions()
			calls := 0
			options.rename = func(src, dst string) error {
				calls++
				if calls == failAt {
					return errors.New("fixture locked file")
				}
				return os.Rename(src, dst)
			}
			if err := installUpdate(archive, install, options); err == nil {
				t.Fatalf("failure at %d ignored", failAt)
			}
			assertOriginals(t, install, old)
			if !Exist(archive) {
				t.Fatal("package lost before retry")
			}
			if err := installUpdate(archive, install, fixtureOptions()); err != nil {
				t.Fatalf("retry failed: %v", err)
			}
		})
	}
}
func TestInstallRestartFailureRestoresOriginals(t *testing.T) {
	archive, install, old := installFixture(t)
	options := fixtureOptions()
	options.start = func(string) error { return errors.New("fixture process start failed") }
	if err := installUpdate(archive, install, options); err == nil {
		t.Fatal("failed restart accepted")
	}
	assertOriginals(t, install, old)
	if !Exist(archive) {
		t.Fatal("package lost")
	}
}
func TestInstallRollbackFailureRetainsBackup(t *testing.T) {
	archive, install, _ := installFixture(t)
	options := fixtureOptions()
	options.start = func(string) error { return errors.New("fixture start failure") }
	options.rename = func(src, dst string) error {
		if strings.Contains(src, ".greenrhythm-backup-") {
			return errors.New("fixture restore locked")
		}
		return os.Rename(src, dst)
	}
	err := installUpdate(archive, install, options)
	if err == nil || !strings.Contains(err.Error(), "резервные файлы сохранены") {
		t.Fatal(err)
	}
	backups, _ := filepath.Glob(filepath.Join(install, ".greenrhythm-backup-*"))
	if len(backups) != 1 {
		t.Fatal(backups)
	}
	files, _ := os.ReadDir(backups[0])
	if len(files) != 5 {
		t.Fatalf("backup missing: %v", files)
	}
}
func TestCopyUpdaterReportsFailureAndUsesUniqueHelpers(t *testing.T) {
	if _, err := copyUpdater(filepath.Join(t.TempDir(), "missing")); err == nil {
		t.Fatal("copy error ignored")
	}
	src := filepath.Join(t.TempDir(), "updater.exe")
	os.WriteFile(src, []byte("fixture executable"), 0644)
	a, err := copyUpdater(src)
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(a)
	b, err := copyUpdater(src)
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(b)
	if a == b || filepath.Dir(a) == filepath.Dir(src) {
		t.Fatal("helper not isolated")
	}
}
