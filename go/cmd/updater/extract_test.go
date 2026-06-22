package main

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func makeZip(t *testing.T, path string, entries map[string]string) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	zw := zip.NewWriter(f)
	for name, body := range entries {
		w, err := zw.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := w.Write([]byte(body)); err != nil {
			t.Fatal(err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
}

// A Zip-Slip entry (../escape.txt) must be rejected and nothing written outside dst.
func TestExtractZipRejectsTraversal(t *testing.T) {
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "evil.zip")
	makeZip(t, zipPath, map[string]string{"../escape.txt": "pwned"})

	dst := filepath.Join(dir, "out")
	if err := extractZip(zipPath, dst); err == nil {
		t.Fatal("expected error for path-traversal entry, got nil")
	}
	if _, err := os.Stat(filepath.Join(dir, "escape.txt")); err == nil {
		t.Fatal("traversal entry was written outside the destination directory")
	}
}

// A normal entry extracts to the expected location with its contents.
func TestExtractZipNormal(t *testing.T) {
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "ok.zip")
	makeZip(t, zipPath, map[string]string{"GreenRhythm/app.txt": "hello"})

	dst := filepath.Join(dir, "out")
	if err := extractZip(zipPath, dst); err != nil {
		t.Fatalf("normal extract failed: %v", err)
	}
	b, err := os.ReadFile(filepath.Join(dst, "GreenRhythm", "app.txt"))
	if err != nil || string(b) != "hello" {
		t.Fatalf("expected extracted file 'hello', got %q (err %v)", b, err)
	}
}

func TestSafeJoin(t *testing.T) {
	dst := filepath.Join(t.TempDir(), "out")
	if safeJoin(dst, "../x") != "" {
		t.Error("../x should be rejected")
	}
	if safeJoin(dst, "a/b.txt") == "" {
		t.Error("a/b.txt should be allowed")
	}
}

func makeTarGz(t *testing.T, path string, write func(*tar.Writer)) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	gz := gzip.NewWriter(f)
	tw := tar.NewWriter(gz)
	write(tw)
	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}
	if err := gz.Close(); err != nil {
		t.Fatal(err)
	}
}

// A relative symlink whose target stays inside dst is preserved (Linux Qt .so chains).
func TestExtractTarGzSymlinkContainedKept(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("symlink creation requires privileges on Windows")
	}
	dir := t.TempDir()
	tgz := filepath.Join(dir, "ok.tar.gz")
	makeTarGz(t, tgz, func(tw *tar.Writer) {
		tw.WriteHeader(&tar.Header{Name: "usr/lib/", Typeflag: tar.TypeDir, Mode: 0755})
		body := []byte("lib")
		tw.WriteHeader(&tar.Header{Name: "usr/lib/libc.so.6", Typeflag: tar.TypeReg, Mode: 0644, Size: int64(len(body))})
		tw.Write(body)
		tw.WriteHeader(&tar.Header{Name: "usr/lib/libc.so", Typeflag: tar.TypeSymlink, Linkname: "libc.so.6"})
	})
	dst := filepath.Join(dir, "out")
	if err := extractTarGz(tgz, dst); err != nil {
		t.Fatalf("extract failed: %v", err)
	}
	link := filepath.Join(dst, "usr", "lib", "libc.so")
	fi, err := os.Lstat(link)
	if err != nil {
		t.Fatalf("symlink not created: %v", err)
	}
	if fi.Mode()&os.ModeSymlink == 0 {
		t.Errorf("expected a symlink, got mode %v", fi.Mode())
	}
	if tgt, err := os.Readlink(link); err != nil || tgt != "libc.so.6" {
		t.Errorf("readlink got %q (err %v), want libc.so.6", tgt, err)
	}
}

// A symlink whose target escapes dst must be silently skipped (not created, no error).
func TestExtractTarGzSymlinkEscapingSkipped(t *testing.T) {
	dir := t.TempDir()
	tgz := filepath.Join(dir, "evil.tar.gz")
	makeTarGz(t, tgz, func(tw *tar.Writer) {
		tw.WriteHeader(&tar.Header{Name: "evil.link", Typeflag: tar.TypeSymlink, Linkname: "../../../etc/passwd"})
	})
	dst := filepath.Join(dir, "out")
	if err := extractTarGz(tgz, dst); err != nil {
		t.Fatalf("escaping symlink should be skipped, not error: %v", err)
	}
	if _, err := os.Lstat(filepath.Join(dst, "evil.link")); err == nil {
		t.Fatal("escaping symlink should not have been created")
	}
}

// A regular entry with a traversal path must be rejected and nothing written outside dst.
func TestExtractTarGzRejectsTraversal(t *testing.T) {
	dir := t.TempDir()
	tgz := filepath.Join(dir, "evil2.tar.gz")
	makeTarGz(t, tgz, func(tw *tar.Writer) {
		body := []byte("x")
		tw.WriteHeader(&tar.Header{Name: "../escape.txt", Typeflag: tar.TypeReg, Mode: 0644, Size: int64(len(body))})
		tw.Write(body)
	})
	dst := filepath.Join(dir, "out")
	if err := extractTarGz(tgz, dst); err == nil {
		t.Fatal("expected error for traversal entry")
	}
	if _, err := os.Stat(filepath.Join(dir, "escape.txt")); err == nil {
		t.Fatal("traversal entry written outside dst")
	}
}
