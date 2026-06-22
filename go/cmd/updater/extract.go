package main

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// errUnsafePath is returned when an archive entry would be written outside the
// destination directory (Zip-Slip / path traversal).
var errUnsafePath = errors.New("unsafe path in archive (path traversal)")

// safeJoin returns dst/name only if the cleaned result stays within dst, else "".
func safeJoin(dst, name string) string {
	target := filepath.Join(dst, name)
	cleanDst := filepath.Clean(dst)
	rel, err := filepath.Rel(cleanDst, target)
	if err != nil || rel == ".." || strings.HasPrefix(rel, ".."+string(os.PathSeparator)) {
		return ""
	}
	return target
}

// withinDst reports whether path resolves inside dst.
func withinDst(dst, path string) bool {
	rel, err := filepath.Rel(filepath.Clean(dst), filepath.Clean(path))
	if err != nil {
		return false
	}
	return rel != ".." && !strings.HasPrefix(rel, ".."+string(os.PathSeparator))
}

func writeFile(target string, r io.Reader, mode os.FileMode) error {
	out, err := os.OpenFile(target, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	_, err = io.Copy(out, r)
	cerr := out.Close()
	if err != nil {
		return err
	}
	return cerr
}

// extractZip extracts a .zip into dst, rejecting any entry that escapes dst and
// skipping symlinks (the Windows payload is plain files + dirs).
func extractZip(src, dst string) error {
	r, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer r.Close()
	for _, f := range r.File {
		target := safeJoin(dst, f.Name)
		if target == "" {
			return errUnsafePath
		}
		if f.Mode()&os.ModeSymlink != 0 {
			continue
		}
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(target, 0755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
			return err
		}
		mode := f.Mode().Perm()
		if mode == 0 {
			mode = 0644
		}
		rc, err := f.Open()
		if err != nil {
			return err
		}
		err = writeFile(target, rc, mode)
		rc.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

// extractTarGz extracts a .tar.gz into dst, rejecting any entry that escapes dst.
// Regular files preserve their mode (Linux exec bits); symlinks are preserved only
// when their resolved target stays within dst.
func extractTarGz(src, dst string) error {
	f, err := os.Open(src)
	if err != nil {
		return err
	}
	defer f.Close()
	gz, err := gzip.NewReader(f)
	if err != nil {
		return err
	}
	defer gz.Close()
	tr := tar.NewReader(gz)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		target := safeJoin(dst, hdr.Name)
		if target == "" {
			return errUnsafePath
		}
		switch hdr.Typeflag {
		case tar.TypeDir:
			if err := os.MkdirAll(target, 0755); err != nil {
				return err
			}
		case tar.TypeReg:
			if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
				return err
			}
			mode := os.FileMode(hdr.Mode).Perm()
			if mode == 0 {
				mode = 0644
			}
			if err := writeFile(target, tr, mode); err != nil {
				return err
			}
		case tar.TypeSymlink:
			resolved := hdr.Linkname
			if !filepath.IsAbs(resolved) {
				resolved = filepath.Join(filepath.Dir(target), hdr.Linkname)
			}
			if !withinDst(dst, resolved) {
				continue // skip symlink that would escape dst
			}
			if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
				return err
			}
			_ = os.Remove(target)
			if err := os.Symlink(hdr.Linkname, target); err != nil {
				return err
			}
		}
	}
	return nil
}
