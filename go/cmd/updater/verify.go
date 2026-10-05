package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	releaseauth "greenrhythm_release"
	"io"
	"os"
)

// verifyArchive checks the signed manifest and actual bytes in the installer process.
// Its trust comes from the key embedded in the binary, never from a checksum sidecar.
func verifyArchive(path string) error {
	return verifyArchiveWith(path, releaseauth.Platform(), releaseauth.Verify)
}

// A checksum alone is insufficient: verify the pinned key before opening the archive.
func verifyArchiveWith(path, platform string, verify func(*releaseauth.Manifest, string) error) error {
	sidecar, err := os.Open(path + ".manifest.json")
	if err != nil {
		return errors.New("нет подписанного манифеста — скачайте обновление заново через программу")
	}
	defer sidecar.Close()
	stat, err := sidecar.Stat()
	if err != nil || !stat.Mode().IsRegular() || stat.Size() > 64*1024 {
		return errors.New("манифест выпуска повреждён")
	}
	var m releaseauth.Manifest
	d := json.NewDecoder(io.LimitReader(sidecar, 64*1024))
	d.DisallowUnknownFields()
	if err = d.Decode(&m); err != nil {
		return err
	}
	if err = d.Decode(new(interface{})); err != io.EOF {
		return errors.New("лишние данные в манифесте")
	}
	if err = verify(&m, platform); err != nil {
		return err
	}
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	stat, err = f.Stat()
	if err != nil || !stat.Mode().IsRegular() || stat.Size() != m.SizeBytes {
		return errors.New("размер пакета не совпадает с подписанным выпуском")
	}
	h := sha256.New()
	n, err := io.Copy(h, io.LimitReader(f, m.SizeBytes+1))
	if err != nil {
		return err
	}
	got := hex.EncodeToString(h.Sum(nil))
	if n != m.SizeBytes || got != m.SHA256 {
		return errors.New("контрольная сумма пакета не сошлась — обновление отменено")
	}
	return nil
}
