//go:build windows

package main

import (
	"crypto/sha256"
	"fmt"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"unsafe"
)

// An OS mutex prevents two manually started installers from replacing the same
// files. Windows releases it after a crash; no stale lock file is left behind.
func lockInstallation(install string) (func(), error) {
	absolute, err := filepath.Abs(install)
	if err != nil {
		return nil, err
	}
	sum := sha256.Sum256([]byte(strings.ToLower(filepath.Clean(absolute))))
	name, _ := syscall.UTF16PtrFromString(fmt.Sprintf("Local\\GreenRhythmUpdate-%x", sum))
	runtime.LockOSThread()
	kernel := syscall.NewLazyDLL("kernel32.dll")
	mutex, _, err := kernel.NewProc("CreateMutexW").Call(0, 0, uintptr(unsafe.Pointer(name)))
	if mutex == 0 {
		runtime.UnlockOSThread()
		return nil, err
	}
	acquired, _, _ := kernel.NewProc("WaitForSingleObject").Call(mutex, 0)
	if acquired != 0 && acquired != 0x80 {
		syscall.CloseHandle(syscall.Handle(mutex))
		runtime.UnlockOSThread()
		return nil, fmt.Errorf("установка обновления уже выполняется; дождитесь её завершения")
	}
	return func() {
		kernel.NewProc("ReleaseMutex").Call(mutex)
		syscall.CloseHandle(syscall.Handle(mutex))
		runtime.UnlockOSThread()
	}, nil
}
