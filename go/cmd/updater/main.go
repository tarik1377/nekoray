package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

func runtimeWindows() bool { return runtime.GOOS == "windows" }
func main() {
	exe, err := os.Executable()
	if err != nil {
		log.Fatal(err)
	}
	if strings.HasPrefix(strings.ToLower(filepath.Base(os.Args[0])), "launcher") {
		Launcher()
		return
	}
	// Inspection and explicit support registration never launch UI, terminate
	// applications or bootstrap a second helper.
	if len(os.Args) == 2 && os.Args[1] == "--rollback-info" {
		_ = json.NewEncoder(os.Stdout).Encode(previousVersionInfo(filepath.Dir(exe)))
		return
	}
	if len(os.Args) == 4 && (os.Args[1] == "--register-previous-local" || os.Args[1] == "--register-current-local") {
		if err = registerVerifiedLocal(filepath.Dir(exe), os.Args[3], os.Args[2], os.Args[1] == "--register-current-local"); err != nil {
			_, _ = fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		_, _ = fmt.Fprintln(os.Stdout, "Проверенная локальная версия зарегистрирована.")
		return
	}
	if runtimeWindows() && !strings.HasPrefix(strings.ToLower(filepath.Base(exe)), "greenrhythm-updater-") {
		// A unique helper outside the installation leaves updater.exe replaceable.
		// The old fixed updater.old could be locked; Copy/Start errors were ignored.
		helper, err := copyUpdater(exe)
		if err == nil {
			args := append([]string{"--install-dir", filepath.Dir(exe)}, os.Args[1:]...)
			cmd := exec.Command(helper, args...)
			cmd.Dir = filepath.Dir(exe)
			err = cmd.Start()
		}
		if err != nil {
			MessageBoxPlain("Обновление GreenRhythm", "Не удалось запустить установку. Программа и пакет сохранены.\n\n"+err.Error())
		}
		return
	}
	install := filepath.Dir(exe)
	if len(os.Args) >= 3 && os.Args[1] == "--install-dir" {
		install = os.Args[2]
		os.Args = append(os.Args[:1], os.Args[3:]...)
	}
	if err = os.Chdir(install); err != nil {
		MessageBoxPlain("Обновление GreenRhythm", err.Error())
		return
	}
	if len(os.Args) == 2 && os.Args[1] == "--rollback" {
		runUpdateUI(Rollback)
	} else {
		runUpdateUI(Updater)
	}
}
func copyUpdater(exe string) (string, error) {
	data, err := os.ReadFile(exe)
	if err != nil {
		return "", err
	}
	f, err := os.CreateTemp("", "greenrhythm-updater-*.exe")
	if err != nil {
		return "", err
	}
	name := f.Name()
	_, err = f.Write(data)
	closeErr := f.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		os.Remove(name)
		return "", err
	}
	return name, nil
}
func startApplication(install string) error {
	name := "greenrhythm"
	if runtimeWindows() {
		name += ".exe"
	}
	absolute, err := filepath.Abs(filepath.Join(install, name))
	if err != nil {
		return err
	}
	cmd := exec.Command(absolute)
	cmd.Dir = install
	if runtime.GOOS == "linux" && os.Getenv("NKR_FROM_LAUNCHER") == "1" {
		cmd.Env = append(os.Environ(), "LD_LIBRARY_PATH="+filepath.Join(install, "usr/lib"), "QT_PLUGIN_PATH="+filepath.Join(install, "usr/plugins"))
	}
	return startAndObserve(cmd, 2*time.Second)
}

// Detect loader errors and immediate crashes before discarding the originals.
// Remaining alive is a startup check, not a guarantee of VPN connectivity.
func startAndObserve(cmd *exec.Cmd, grace time.Duration) error {
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("не удалось запустить GreenRhythm: %w", err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	select {
	case err := <-done:
		return fmt.Errorf("GreenRhythm завершился сразу после запуска: %v", err)
	case <-time.After(grace):
		return nil
	}
}
