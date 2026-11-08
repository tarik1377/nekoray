package robloxauto

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
)

var hexKey = regexp.MustCompile(`^[0-9a-f]{64}$`)

type disk struct {
	Schema  int              `json:"schema"`
	Scope   string           `json:"scope"`
	Network string           `json:"network"`
	Updated time.Time        `json:"updated"`
	Entries map[string]Entry `json:"entries"`
}
type Store struct {
	Directory, Scope string
	AllowHost        func(string) bool
	mu               sync.Mutex
	last             time.Time
}

func (s *Store) path(network string) string {
	return filepath.Join(s.Directory, s.Scope[:16]+"-"+network[:16]+".json")
}
func (s *Store) Load(network string) map[string]Entry {
	if !hexKey.MatchString(s.Scope) || !hexKey.MatchString(network) {
		return nil
	}
	info, err := os.Lstat(s.path(network))
	if err != nil || !info.Mode().IsRegular() || info.Size() > 65536 {
		return nil
	}
	data, err := os.ReadFile(s.path(network))
	if err != nil {
		return nil
	}
	var saved disk
	if json.Unmarshal(data, &saved) != nil || saved.Schema != 1 || saved.Scope != s.Scope || saved.Network != network || len(saved.Entries) > 128 {
		return nil
	}
	result := make(map[string]Entry)
	now := time.Now()
	for key, value := range saved.Entries {
		if len(key) > 300 || !(strings.HasPrefix(key, "tcp:") || strings.HasPrefix(key, "udp:")) || (value.Path != Direct && value.Path != Proxy) || !value.Verified || !value.Expires.After(now) || value.Expires.After(now.Add(10*time.Minute)) {
			continue
		}
		allowed := s.AllowHost
		if allowed == nil {
			allowed = KnownHost
		}
		if strings.HasPrefix(key, "tcp:") && !allowed(strings.TrimPrefix(key, "tcp:")) {
			continue
		}
		result[key] = value
	}
	return result
}
func (e *Engine) Restore(network string, entries map[string]Entry) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.network != network || len(entries) > 128 {
		return
	}
	for k, v := range entries {
		e.putLocked(k, v)
	}
}
func (s *Store) Save(network string, entries map[string]Entry, force bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !hexKey.MatchString(s.Scope) || !hexKey.MatchString(network) || len(entries) > 128 {
		return errors.New("invalid Roblox cache scope")
	}
	if !force && time.Since(s.last) < 2*time.Second {
		return nil
	}
	if info, err := os.Lstat(s.Directory); err == nil && (!info.IsDir() || info.Mode()&os.ModeSymlink != 0) {
		return errors.New("Roblox cache directory is not a regular directory")
	}
	if err := os.MkdirAll(s.Directory, 0700); err != nil {
		return err
	}
	value := disk{1, s.Scope, network, time.Now().UTC().Truncate(time.Millisecond), entries}
	data, err := json.Marshal(value)
	if err != nil {
		return err
	}
	if len(data) > 65536 {
		return errors.New("Roblox cache too large")
	}
	if err = atomicFile(s.path(network), data); err != nil {
		return err
	}
	if err = atomicFile(filepath.Join(s.Directory, "status.json"), data); err != nil {
		return err
	}
	s.last = time.Now()
	return nil
}
func atomicFile(path string, data []byte) error {
	file, err := os.CreateTemp(filepath.Dir(path), ".roblox-auto-*.tmp")
	if err != nil {
		return err
	}
	name := file.Name()
	defer os.Remove(name)
	if err = file.Chmod(0600); err == nil {
		_, err = file.Write(data)
	}
	if err == nil {
		err = file.Sync()
	}
	closeErr := file.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	return os.Rename(name, path)
}
