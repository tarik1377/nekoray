//go:build windows

package main

import (
	"syscall"
	"testing"
	"time"
	"unsafe"
)

func TestHiddenInstallerWindowPumpsStagesAndSurvivesClose(t *testing.T) {
	runUpdateWindow(func() error {
		updateWindow.Lock()
		hwnd, label := updateWindow.hwnd, updateWindow.label
		updateWindow.Unlock()
		for _, stage := range []string{"Проверка пакета", "3/4 · Установка", "4/4 · Запуск"} {
			reportProgress(stage)
			deadline := time.Now().Add(2 * time.Second)
			for {
				buf := make([]uint16, 200)
				progressUser32.NewProc("GetWindowTextW").Call(label, uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)))
				if syscall.UTF16ToString(buf) == stage {
					break
				}
				if time.Now().After(deadline) {
					t.Errorf("progress label did not display %s", stage)
					break
				}
				time.Sleep(10 * time.Millisecond)
			}
		}
		progressUser32.NewProc("PostMessageW").Call(hwnd, 0x10, 0, 0)
		time.Sleep(30 * time.Millisecond)
		alive, _, _ := progressUser32.NewProc("IsWindow").Call(hwnd)
		if alive == 0 {
			t.Error("installer window closed during work")
		}
		return nil
	}, false)
}
