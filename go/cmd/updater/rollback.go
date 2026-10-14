package main

import (
	"context"
	"crypto/sha256"
	"debug/buildinfo"
	"debug/pe"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	releaseauth "greenrhythm_release"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

const installationReceipt = ".greenrhythm-installation.json"
const previousDirectory = ".greenrhythm-previous"

type runtimeFile struct {
	Path   string `json:"path"`
	SHA256 string `json:"sha256"`
	Size   int64  `json:"size"`
}
type installationRecord struct {
	Schema       int                   `json:"schema"`
	ConfigSchema int                   `json:"config_schema"`
	Platform     string                `json:"platform"`
	Version      string                `json:"version"`
	Provenance   string                `json:"provenance"`
	SourceCommit string                `json:"source_commit,omitempty"`
	GoToolchain  string                `json:"go_toolchain,omitempty"`
	CoreVersion  string                `json:"core_version,omitempty"`
	Release      *releaseauth.Manifest `json:"release,omitempty"`
	Files        []runtimeFile         `json:"files"`
}
type rollbackInfo struct {
	Available  bool   `json:"available"`
	Version    string `json:"version,omitempty"`
	Provenance string `json:"provenance,omitempty"`
	Platform   string `json:"platform,omitempty"`
	Files      int    `json:"files,omitempty"`
	Error      string `json:"error,omitempty"`
}

var digestPattern = regexp.MustCompile(`^[a-f0-9]{64}$`)
var commitPattern = regexp.MustCompile(`^[a-f0-9]{40}$`)
var rollbackVersionPattern = regexp.MustCompile(`^v?\d+\.\d+\.\d+(?:\.\d+)?$`)

func supportedRollbackVersion(version string) bool {
	if !rollbackVersionPattern.MatchString(version) {
		return false
	}
	parts := strings.Split(strings.TrimPrefix(version, "v"), ".")
	minimum := []int{1, 9, 2, 2}
	for i, want := range minimum {
		n := 0
		if i < len(parts) {
			var err error
			n, err = strconv.Atoi(parts[i])
			if err != nil {
				return false
			}
		}
		if n != want {
			return n > want
		}
	}
	return true
}

// Only packaged Windows runtime assets are eligible. No settings, subscriptions,
// profiles, routes, caches or arbitrary local files enter a saved version.
func rollbackRuntimePath(rel string) bool {
	if rel == "" || filepath.IsAbs(rel) || strings.Contains(rel, "\\") || strings.Contains(rel, ":") || filepath.ToSlash(filepath.Clean(rel)) != rel {
		return false
	}
	parts := strings.Split(rel, "/")
	if len(parts) > 2 {
		return false
	}
	if len(parts) == 2 {
		switch strings.ToLower(parts[0]) {
		case "generic", "iconengines", "imageformats", "networkinformation", "platforms", "styles", "tls":
		default:
			return false
		}
		return strings.EqualFold(filepath.Ext(rel), ".dll")
	}
	if strings.HasPrefix(parts[0], ".") {
		return false
	}
	switch strings.ToLower(filepath.Ext(rel)) {
	case ".exe", ".dll", ".dat", ".db", ".png", ".txt", ".md":
		return true
	}
	return false
}
func readJSONFile(path string, target interface{}) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil || !info.Mode().IsRegular() || info.Size() > 1024*1024 {
		return errors.New("манифест сохранённой версии повреждён")
	}
	decoder := json.NewDecoder(io.LimitReader(f, 1024*1024))
	decoder.DisallowUnknownFields()
	if err = decoder.Decode(target); err != nil {
		return err
	}
	if err = decoder.Decode(new(interface{})); err != io.EOF {
		return errors.New("лишние данные в манифесте сохранённой версии")
	}
	return nil
}
func validateRecord(record *installationRecord) error {
	if !runtimeWindows() || record.Schema != 1 || record.ConfigSchema != 1 || record.Platform != releaseauth.Platform() || !supportedRollbackVersion(record.Version) {
		return errors.New("сохранённая версия несовместима с этой платформой или форматом настроек")
	}
	switch record.Provenance {
	case "signed-release":
		if record.Release == nil || strings.TrimPrefix(record.Version, "v") != record.Release.Version {
			return errors.New("нет подтверждения подписанного выпуска")
		}
		if err := releaseauth.Verify(record.Release, record.Platform); err != nil {
			return err
		}
	case "explicit-verified-local":
		if !commitPattern.MatchString(record.SourceCommit) || record.GoToolchain != "go1.26.9" {
			return errors.New("нет подтверждения проверенной локальной сборки")
		}
	default:
		return errors.New("источник сохранённой версии не подтверждён")
	}
	if len(record.Files) < 10 || len(record.Files) > 512 {
		return errors.New("неполный манифест сохранённой версии")
	}
	seen := make(map[string]bool)
	for _, file := range record.Files {
		lower := strings.ToLower(file.Path)
		if !rollbackRuntimePath(file.Path) || !digestPattern.MatchString(file.SHA256) || file.Size <= 0 || seen[lower] {
			return errors.New("небезопасный или повторный файл в сохранённой версии")
		}
		seen[lower] = true
	}
	for _, name := range []string{"greenrhythm.exe", "greenrhythm_core.exe", "updater.exe", "xray.exe", "qt6core.dll", "qt6gui.dll", "qt6network.dll", "qt6widgets.dll", "qt6svg.dll", "platforms/qwindows.dll", "wintun.dll"} {
		if !seen[name] {
			return fmt.Errorf("в сохранённой версии отсутствует %s", name)
		}
	}
	return nil
}
func checkedRuntimeFile(root, rel string) (runtimeFile, error) {
	if !rollbackRuntimePath(rel) {
		return runtimeFile{}, errors.New("небезопасный путь сохранённой версии")
	}
	absoluteRoot, err := filepath.Abs(root)
	if err != nil {
		return runtimeFile{}, err
	}
	path := filepath.Join(absoluteRoot, filepath.FromSlash(rel))
	for parent := path; ; parent = filepath.Dir(parent) {
		info, err := os.Lstat(parent)
		if err != nil || info.Mode()&os.ModeSymlink != 0 {
			return runtimeFile{}, fmt.Errorf("файл %s недоступен или является ссылкой", rel)
		}
		if parent == absoluteRoot {
			break
		}
	}
	f, err := os.Open(path)
	if err != nil {
		return runtimeFile{}, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil || !info.Mode().IsRegular() || info.Size() <= 0 {
		return runtimeFile{}, fmt.Errorf("файл %s повреждён", rel)
	}
	hash := sha256.New()
	if _, err = io.Copy(hash, f); err != nil {
		return runtimeFile{}, err
	}
	return runtimeFile{rel, hex.EncodeToString(hash.Sum(nil)), info.Size()}, nil
}
func verifyRuntimeFiles(root string, record *installationRecord) error {
	for _, file := range record.Files {
		actual, err := checkedRuntimeFile(root, file.Path)
		if err != nil {
			return err
		}
		if actual.SHA256 != file.SHA256 || actual.Size != file.Size {
			return fmt.Errorf("контрольная сумма сохранённого файла %s не совпадает; откат отменён", file.Path)
		}
	}
	return nil
}
func copyRuntimeFiles(source, target string, record *installationRecord) error {
	for _, file := range record.Files {
		from := filepath.Join(source, filepath.FromSlash(file.Path))
		to := filepath.Join(target, filepath.FromSlash(file.Path))
		if err := os.MkdirAll(filepath.Dir(to), 0755); err != nil {
			return err
		}
		f, err := os.Open(from)
		if err != nil {
			return err
		}
		err = writeFile(to, f, 0755)
		_ = f.Close()
		if err != nil {
			return err
		}
	}
	return verifyRuntimeFiles(target, record)
}
func writeRecord(path string, record *installationRecord) error {
	data, err := json.MarshalIndent(record, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0600)
}
func prepareSnapshot(install, source string, record *installationRecord) (string, error) {
	if err := validateRecord(record); err != nil {
		return "", err
	}
	if err := verifyRuntimeFiles(source, record); err != nil {
		return "", err
	}
	candidate, err := os.MkdirTemp(install, ".greenrhythm-snapshot-")
	if err != nil {
		return "", err
	}
	if err = copyRuntimeFiles(source, filepath.Join(candidate, "files"), record); err == nil {
		err = writeRecord(filepath.Join(candidate, "manifest.json"), record)
	}
	if err != nil {
		_ = os.RemoveAll(candidate)
		return "", err
	}
	return candidate, nil
}
func publishSnapshot(install, candidate string) error {
	previous := filepath.Join(install, previousDirectory)
	retired := ""
	if _, err := os.Lstat(previous); err == nil {
		info, err := os.Lstat(previous)
		if err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
			return errors.New("небезопасная папка предыдущей версии")
		}
		retired, err = os.MkdirTemp(install, ".greenrhythm-retired-")
		if err != nil {
			return err
		}
		if err = os.Remove(retired); err != nil {
			return err
		}
		if err = os.Rename(previous, retired); err != nil {
			return fmt.Errorf("предыдущая резервная версия занята; установка отменена: %w", err)
		}
	} else if !os.IsNotExist(err) {
		return err
	}
	if err := os.Rename(candidate, previous); err != nil {
		if retired != "" {
			return errors.Join(err, os.Rename(retired, previous))
		}
		return err
	}
	if retired != "" {
		_ = os.RemoveAll(retired)
	}
	return nil
}
func loadPrevious(install string) (*installationRecord, error) {
	for _, dir := range []string{filepath.Join(install, previousDirectory), filepath.Join(install, previousDirectory, "files")} {
		info, err := os.Lstat(dir)
		if err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
			return nil, errors.New("нет безопасного каталога предыдущей версии")
		}
	}
	manifestInfo, err := os.Lstat(filepath.Join(install, previousDirectory, "manifest.json"))
	if err != nil || !manifestInfo.Mode().IsRegular() {
		return nil, errors.New("манифест предыдущей версии отсутствует или небезопасен")
	}
	var record installationRecord
	if err := readJSONFile(filepath.Join(install, previousDirectory, "manifest.json"), &record); err != nil {
		return nil, errors.New("нет проверенной предыдущей версии")
	}
	if err := validateRecord(&record); err != nil {
		return nil, err
	}
	if err := verifyRuntimeFiles(filepath.Join(install, previousDirectory, "files"), &record); err != nil {
		return nil, err
	}
	return &record, nil
}
func previousVersionInfo(install string) rollbackInfo {
	record, err := loadPrevious(install)
	if err != nil {
		return rollbackInfo{Error: err.Error()}
	}
	var current installationRecord
	if err = readJSONFile(filepath.Join(install, installationReceipt), &current); err != nil {
		return rollbackInfo{Error: "текущая установка ещё не зарегистрирована для безопасного отката"}
	}
	if err = validateRecord(&current); err != nil {
		return rollbackInfo{Error: err.Error()}
	}
	if err = verifyRuntimeFiles(install, &current); err != nil {
		return rollbackInfo{Error: err.Error()}
	}
	if current.Version == record.Version && verifyRuntimeFiles(install, record) == nil {
		return rollbackInfo{Error: "сохранённая версия уже установлена"}
	}
	return rollbackInfo{Available: true, Version: record.Version, Provenance: record.Provenance, Platform: record.Platform, Files: len(record.Files)}
}
func signedInstallationRecord(archive, payload string, files []string) (*installationRecord, error) {
	var release releaseauth.Manifest
	if err := readJSONFile(archive+".manifest.json", &release); err != nil {
		return nil, err
	}
	if err := releaseauth.Verify(&release, releaseauth.Platform()); err != nil {
		return nil, err
	}
	record := &installationRecord{Schema: 1, ConfigSchema: 1, Platform: releaseauth.Platform(), Version: release.Version, Provenance: "signed-release", Release: &release}
	for _, rel := range files {
		rel = filepath.ToSlash(rel)
		if strings.HasPrefix(strings.ToLower(rel), "config/") {
			continue
		}
		file, err := checkedRuntimeFile(payload, rel)
		if err != nil {
			return nil, err
		}
		record.Files = append(record.Files, file)
	}
	if err := validateRecord(record); err != nil {
		return nil, err
	}
	return record, nil
}

// Support-only bootstrap: this is an explicit attestation from the operator,
// never an automatic discovery of unsigned files. The given receipt must name
// a complete known runtime and the same patched toolchain actually in its cores.
func registerPreviousLocal(install, source, receiptPath string) error {
	return registerVerifiedLocal(install, source, receiptPath, false)
}
func registerVerifiedLocal(install, source, receiptPath string, current bool) error {
	if !runtimeWindows() {
		return errors.New("локальная регистрация отката поддерживается только на Windows")
	}
	var receipt struct {
		Version      string `json:"version"`
		SourceCommit string `json:"source_commit"`
		GoToolchain  string `json:"go_toolchain"`
		Qt           string `json:"qt"`
		Files        []struct {
			File   string `json:"file"`
			SHA256 string `json:"sha256"`
		} `json:"files"`
	}
	if err := readJSONFile(receiptPath, &receipt); err != nil {
		return err
	}
	record := &installationRecord{Schema: 1, ConfigSchema: 1, Platform: releaseauth.Platform(), Version: receipt.Version, Provenance: "explicit-verified-local", SourceCommit: receipt.SourceCommit, GoToolchain: receipt.GoToolchain}
	for _, file := range receipt.Files {
		if strings.HasPrefix(strings.ToLower(file.File), "config/") {
			continue
		}
		actual, err := checkedRuntimeFile(source, file.File)
		if err != nil {
			return err
		}
		if file.SHA256 != actual.SHA256 {
			return fmt.Errorf("локальная сборка не совпадает с предоставленным receipt: %s", file.File)
		}
		record.Files = append(record.Files, actual)
	}
	if err := validateRecord(record); err != nil {
		return err
	}
	for _, name := range []string{"greenrhythm.exe", "greenrhythm_core.exe", "updater.exe"} {
		image, err := pe.Open(filepath.Join(source, name))
		if err != nil {
			return fmt.Errorf("неверный Windows executable %s: %w", name, err)
		}
		machine := image.Machine
		_ = image.Close()
		if machine != pe.IMAGE_FILE_MACHINE_AMD64 {
			return errors.New("архитектура локальной сборки не совпадает")
		}
		if name != "greenrhythm.exe" {
			info, err := buildinfo.ReadFile(filepath.Join(source, name))
			if err != nil || info.GoVersion != record.GoToolchain {
				return fmt.Errorf("toolchain файла %s не подтверждён", name)
			}
		}
	}
	// Components may be reused by a GUI-only bundle update. Verify the actual
	// core's numeric version and supported baseline without pretending the bundle
	// version was embedded in an older GUI's fixed PE version resource.
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	command := exec.CommandContext(ctx, filepath.Join(source, "greenrhythm_core.exe"), "--version")
	command.SysProcAttr = hiddenSubprocess()
	output, err := command.Output()
	if err != nil {
		return errors.New("не удалось проверить версию локального ядра")
	}
	match := regexp.MustCompile(`GreenRhythm:\s+(v?\d+\.\d+\.\d+(?:\.\d+)?)`).FindSubmatch(output)
	if len(match) != 2 || !supportedRollbackVersion(string(match[1])) {
		return errors.New("версия локального ядра не поддерживает безопасный возврат")
	}
	record.CoreVersion = string(match[1])
	unlock, err := lockInstallation(install)
	if err != nil {
		return err
	}
	defer unlock()
	if current {
		if err = verifyRuntimeFiles(install, record); err != nil {
			return err
		}
		file, err := os.CreateTemp(install, ".greenrhythm-receipt-")
		if err != nil {
			return err
		}
		name := file.Name()
		_ = file.Close()
		defer os.Remove(name)
		if err = writeRecord(name, record); err != nil {
			return err
		}
		return os.Rename(name, filepath.Join(install, installationReceipt))
	}
	candidate, err := prepareSnapshot(install, source, record)
	if err != nil {
		return err
	}
	defer os.RemoveAll(candidate)
	return publishSnapshot(install, candidate)
}

func restorePrevious(install string, options installOptions) error {
	if !runtimeWindows() {
		return errors.New("откат версии поддерживается только на Windows")
	}
	unlock, err := lockInstallation(install)
	if err != nil {
		return err
	}
	defer unlock()
	report := options.report
	if report == nil {
		report = func(string) {}
	}
	report("1/4 · Проверка сохранённой версии и контрольных сумм…")
	record, err := loadPrevious(install)
	if err != nil {
		return err
	}
	var current installationRecord
	if err = readJSONFile(filepath.Join(install, installationReceipt), &current); err != nil {
		return errors.New("нет манифеста текущей установки для безопасного возврата")
	}
	if err = validateRecord(&current); err != nil {
		return err
	}
	if err = verifyRuntimeFiles(install, &current); err != nil {
		return err
	}
	stage, err := os.MkdirTemp(install, ".greenrhythm-restore-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(stage)
	if err = copyRuntimeFiles(filepath.Join(install, previousDirectory, "files"), stage, record); err != nil {
		return err
	}
	tx, err := newInstallTransaction(stage, install, options.rename)
	if err != nil {
		return err
	}
	if err = writeRecord(filepath.Join(stage, installationReceipt), record); err != nil {
		tx.discard()
		return err
	}
	tx.files = append(tx.files, installationReceipt)
	tx.removeOnly = make(map[string]bool)
	oldFiles := make(map[string]bool)
	for _, file := range record.Files {
		oldFiles[strings.ToLower(file.Path)] = true
	}
	for _, file := range current.Files {
		if !oldFiles[strings.ToLower(file.Path)] {
			rel := filepath.FromSlash(file.Path)
			tx.files = append(tx.files, rel)
			tx.removeOnly[rel] = true
		}
	}
	sort.Strings(tx.files)
	report("2/4 · Ожидание завершения GreenRhythm…")
	if err = waitForFiles(tx.files, install, options.wait); err != nil {
		tx.discard()
		return err
	}
	report("3/4 · Возврат версии " + record.Version + " — настройки и серверы сохраняются…")
	if err = tx.apply(); err != nil {
		return errors.Join(err, tx.rollback())
	}
	report("4/4 · Запуск возвращённой версии…")
	if err = options.start(install); err != nil {
		restoreErr := tx.rollback()
		if restoreErr != nil {
			return errors.Join(err, restoreErr)
		}
		report("Возвращённая версия не запустилась. Запускаем прежнюю рабочую установку…")
		return errors.Join(fmt.Errorf("откат не применён; прежние файлы восстановлены: %w", err), options.start(install))
	}
	tx.discard()
	report("Версия " + record.Version + " восстановлена. GreenRhythm запущен.")
	return nil
}
func Rollback() error {
	return restorePrevious(".", installOptions{start: startApplication, rename: os.Rename, wait: 30 * time.Second, report: reportProgress})
}
