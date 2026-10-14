package main

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
)

type installedFile struct {
	rel                 string
	original, installed bool
}
type installTransaction struct {
	payload, install, backup string
	files                    []string
	changed                  []installedFile
	rename                   func(string, string) error
	removeOnly               map[string]bool
}

func newInstallTransaction(payload, install string, rename func(string, string) error) (*installTransaction, error) {
	backups, err := filepath.Glob(filepath.Join(install, ".greenrhythm-backup-*"))
	if err != nil {
		return nil, err
	}
	for _, backup := range backups {
		entries, err := os.ReadDir(backup)
		if err != nil || len(entries) > 0 {
			return nil, fmt.Errorf("осталась резервная копия незавершённой установки: %s. Восстановите её перед повтором обновления", backup)
		}
	}
	tx := &installTransaction{payload: payload, install: install, rename: rename}
	err = filepath.WalkDir(payload, func(name string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(payload, name)
		if err != nil {
			return err
		}
		parts := strings.Split(filepath.ToSlash(rel), "/")
		if strings.EqualFold(parts[0], "config") && (entry.IsDir() && len(parts) > 1 || !entry.IsDir() && (len(parts) != 2 || !strings.HasSuffix(strings.ToLower(rel), ".srs"))) {
			if entry.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		if entry.IsDir() {
			return nil
		}
		if entry.Type()&os.ModeSymlink != 0 && runtimeWindows() {
			return errors.New("ссылки в пакете Windows не поддерживаются")
		}
		if strings.HasPrefix(strings.ToLower(parts[0]), ".greenrhythm-") || strings.EqualFold(parts[0], "updater.old") {
			return errors.New("служебный путь в пакете обновления")
		}
		for parent := filepath.Join(install, rel); ; parent = filepath.Dir(parent) {
			info, e := os.Lstat(parent)
			if e == nil && (info.Mode()&os.ModeSymlink != 0 || info.IsDir() && parent == filepath.Join(install, rel)) {
				return fmt.Errorf("небезопасный путь установки: %s", rel)
			}
			if parent == filepath.Clean(install) || parent == filepath.Dir(parent) {
				break
			}
		}
		tx.files = append(tx.files, rel)
		return nil
	})
	if err != nil {
		return nil, err
	}
	tx.backup, err = os.MkdirTemp(install, ".greenrhythm-backup-")
	return tx, err
}
func (tx *installTransaction) apply() error {
	for _, rel := range tx.files {
		dst := filepath.Join(tx.install, rel)
		if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
			return err
		}
		tx.changed = append(tx.changed, installedFile{rel: rel})
		item := &tx.changed[len(tx.changed)-1]
		if _, err := os.Lstat(dst); err == nil {
			saved := filepath.Join(tx.backup, rel)
			if err = os.MkdirAll(filepath.Dir(saved), 0755); err != nil {
				return err
			}
			if err = tx.rename(dst, saved); err != nil {
				return fmt.Errorf("не удалось сохранить %s: %w", rel, err)
			}
			item.original = true
		} else if !os.IsNotExist(err) {
			return err
		}
		if tx.removeOnly[rel] {
			continue
		}
		if err := tx.rename(filepath.Join(tx.payload, rel), dst); err != nil {
			return fmt.Errorf("не удалось установить %s: %w", rel, err)
		}
		item.installed = true
	}
	return nil
}
func (tx *installTransaction) rollback() error {
	var failures []error
	for i := len(tx.changed) - 1; i >= 0; i-- {
		item := tx.changed[i]
		dst := filepath.Join(tx.install, item.rel)
		if item.installed {
			if err := os.Remove(dst); err != nil && !os.IsNotExist(err) {
				failures = append(failures, err)
				continue
			}
		}
		if item.original {
			if err := tx.rename(filepath.Join(tx.backup, item.rel), dst); err != nil {
				failures = append(failures, err)
			}
		}
	}
	if len(failures) > 0 {
		return fmt.Errorf("не удалось полностью восстановить установку; резервные файлы сохранены в %s: %w", tx.backup, errors.Join(failures...))
	}
	tx.discard()
	return nil
}
func (tx *installTransaction) discard() { _ = os.RemoveAll(tx.backup) }
