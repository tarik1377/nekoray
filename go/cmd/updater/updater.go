package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

type installOptions struct {
	verify func(string) error
	start  func(string) error
	rename func(string, string) error
	wait   time.Duration
	report func(string)
	record func(string, string, []string) (*installationRecord, error)
}

func Updater() error {
	packagePath := FindExist([]string{"greenrhythm.zip", "greenrhythm.tar.gz"})
	if len(os.Args) == 2 {
		packagePath = os.Args[1]
	}
	if packagePath == "" {
		return errors.New("пакет обновления не найден — загрузите его через программу")
	}
	return installUpdate(packagePath, ".", installOptions{verify: verifyArchive, start: startApplication,
		rename: os.Rename, wait: 30 * time.Second, report: reportProgress, record: signedInstallationRecord})
}

// Retain originals until the GUI starts. User settings, profiles and routes are
// never replaced by files bundled in an application update.
func installUpdate(archive, install string, options installOptions) error {
	unlock, err := lockInstallation(install)
	if err != nil {
		return err
	}
	defer unlock()
	report := options.report
	if report == nil {
		report = func(string) {}
	}
	report("Проверка подписи и целостности пакета…")
	if err := options.verify(archive); err != nil {
		return err
	}
	staging, err := os.MkdirTemp(install, ".greenrhythm-update-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(staging)
	report("Распаковка и проверка файлов…")
	switch {
	case strings.HasSuffix(archive, ".zip"):
		err = extractZip(archive, staging)
	case strings.HasSuffix(archive, ".tar.gz"):
		err = extractTarGz(archive, staging)
	default:
		return errors.New("формат пакета обновления не поддерживается")
	}
	if err != nil {
		return err
	}
	payload := FindExist([]string{filepath.Join(staging, "GreenRhythm"), filepath.Join(staging, "greenrhythm"), filepath.Join(staging, "nekoray")})
	if payload == "" {
		return errors.New("в пакете нет папки GreenRhythm")
	}
	suffix := ""
	if runtime.GOOS == "windows" {
		suffix = ".exe"
	}
	required := []string{"greenrhythm", "greenrhythm_core"}
	if runtimeWindows() {
		required = append(required, "updater", "xray")
	}
	for _, name := range required {
		info, e := os.Stat(filepath.Join(payload, name+suffix))
		if e != nil || !info.Mode().IsRegular() {
			return fmt.Errorf("в пакете отсутствует обязательный файл %s%s", name, suffix)
		}
	}
	tx, err := newInstallTransaction(payload, install, options.rename)
	if err != nil {
		return err
	}
	previousCandidate := ""
	if runtimeWindows() && options.record != nil {
		next, err := options.record(archive, payload, tx.files)
		if err != nil {
			tx.discard()
			return err
		}
		if err = writeRecord(filepath.Join(payload, installationReceipt), next); err != nil {
			tx.discard()
			return err
		}
		tx.files = append(tx.files, installationReceipt)
		var previous installationRecord
		err = readJSONFile(filepath.Join(install, installationReceipt), &previous)
		if err == nil {
			previousCandidate, err = prepareSnapshot(install, install, &previous)
			if err != nil {
				tx.discard()
				return err
			}
			defer os.RemoveAll(previousCandidate)
		} else if !os.IsNotExist(err) {
			tx.discard()
			return err
		}
	}
	report("Ожидание завершения прежней программы…")
	if err = waitForFiles(tx.files, install, options.wait); err != nil {
		tx.discard()
		return err
	}
	report("3/4 · Установка обновления — рабочие файлы сохранены для отката…")
	if err = tx.apply(); err != nil {
		report("Восстановление прежней установки…")
		return errors.Join(err, tx.rollback())
	}
	if previousCandidate != "" {
		report("Сохранение проверенной предыдущей версии для отката…")
		if err = publishSnapshot(install, previousCandidate); err != nil {
			return errors.Join(err, tx.rollback())
		}
	}
	report("4/4 · Запуск GreenRhythm…")
	if err = options.start(install); err != nil {
		report("Запуск не удался. Восстановление прежней установки…")
		rollbackErr := tx.rollback()
		if rollbackErr == nil {
			return fmt.Errorf("новая программа не запустилась; прежняя установка восстановлена: %w", err)
		}
		return errors.Join(err, rollbackErr)
	}
	tx.discard()
	for _, p := range []string{archive, archive + ".sha256", archive + ".manifest.json"} {
		_ = os.Remove(p)
	}
	report("Обновление установлено. GreenRhythm запущен.")
	return nil
}

func waitForFiles(files []string, install string, deadline time.Duration) error {
	end := time.Now().Add(deadline)
	for {
		busy := ""
		for _, rel := range files {
			name := filepath.Join(install, rel)
			if !Exist(name) {
				continue
			}
			f, err := os.OpenFile(name, os.O_WRONLY, 0)
			if err != nil {
				busy = rel
				break
			}
			_ = f.Close()
		}
		if busy == "" {
			return nil
		}
		if !time.Now().Before(end) {
			return fmt.Errorf("файл %s занят или недоступен; прежняя установка сохранена. Закройте GreenRhythm и повторите установку", busy)
		}
		time.Sleep(100 * time.Millisecond)
	}
}
func Exist(path string) bool { _, err := os.Stat(path); return err == nil }
func FindExist(paths []string) string {
	for _, p := range paths {
		if Exist(p) {
			return p
		}
	}
	return ""
}
