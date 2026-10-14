package grpc_server

import (
	"archive/zip"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"time"
)

type updateProgress struct {
	Stage string `json:"stage"`
	Bytes int64  `json:"bytes"`
	Total int64  `json:"total"`
	Error string `json:"error,omitempty"`
	path  string
	last  time.Time
}

func (p *updateProgress) save(stage string) {
	p.Stage = stage
	data, _ := json.Marshal(p)
	// The GUI tolerates a missing/partial status; never let progress I/O fail an update.
	if os.WriteFile(p.path+".tmp", data, 0600) == nil {
		_ = os.Rename(p.path+".tmp", p.path)
	}
	p.last = time.Now()
}
func (p *updateProgress) Write(b []byte) (int, error) {
	p.Bytes += int64(len(b))
	if time.Since(p.last) >= 200*time.Millisecond {
		p.save("download")
	}
	return len(b), nil
}

// Bootstrap the freshly verified updater instead of relying on an installed
// updater from an older release. No other package paths are extracted here.
func prepareWindowsUpdater(archive string) error {
	r, err := zip.OpenReader(archive)
	if err != nil {
		return err
	}
	defer r.Close()
	for _, entry := range r.File {
		if entry.Name != "GreenRhythm/updater.exe" {
			continue
		}
		if !entry.Mode().IsRegular() || entry.UncompressedSize64 == 0 || entry.UncompressedSize64 > 32<<20 {
			return errors.New("неверный updater.exe в пакете")
		}
		src, err := entry.Open()
		if err != nil {
			return err
		}
		defer src.Close()
		target := filepath.Join(filepath.Dir(archive), "updater.next.exe")
		f, err := os.Create(target + ".partial")
		if err != nil {
			return err
		}
		_, err = io.Copy(f, io.LimitReader(src, 32<<20+1))
		syncErr := f.Sync()
		closeErr := f.Close()
		if err == nil {
			err = syncErr
		}
		if err == nil {
			err = closeErr
		}
		if err == nil {
			err = os.Rename(target+".partial", target)
		}
		if err != nil {
			_ = os.Remove(target + ".partial")
		}
		return err
	}
	return errors.New("в пакете нет updater.exe; скачайте полный выпуск заново")
}
