//go:build windows

package main

import (
	"runtime"
	"sync"
	"syscall"
	"unsafe"
)

var updateWindow struct {
	sync.Mutex
	hwnd  uintptr
	label uintptr
	text  string
	err   error
}
var progressUser32 = syscall.NewLazyDLL("user32.dll")

func winText(s string) *uint16 { return syscall.StringToUTF16Ptr(s) }
func reportProgress(text string) {
	updateWindow.Lock()
	updateWindow.text = text
	hwnd := updateWindow.hwnd
	updateWindow.Unlock()
	if hwnd != 0 {
		progressUser32.NewProc("PostMessageW").Call(hwnd, 0x8001, 0, 0)
	}
}

type windowMessage struct {
	hwnd           uintptr
	message        uint32
	wParam, lParam uintptr
	time           uint32
	x, y           int32
	private        uint32
}

func runUpdateUI(work func() error) {
	runUpdateWindow(work, true)
}

func runUpdateWindow(work func() error, visible bool) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	create := progressUser32.NewProc("CreateWindowExW")
	hwnd, _, _ := create.Call(0, uintptr(unsafe.Pointer(winText("STATIC"))), uintptr(unsafe.Pointer(winText("Обновление GreenRhythm"))), 0x00C00000|0x00800000, 200, 200, 620, 210, 0, 0, 0, 0)
	if hwnd == 0 {
		MessageBoxPlain("Обновление GreenRhythm", "Не удалось открыть окно установки. Пакет сохранён; запустите updater.exe повторно.")
		return
	}
	defer progressUser32.NewProc("DestroyWindow").Call(hwnd)
	// Closing a window during file replacement would leave a silent installer.
	// The error dialog provides Cancel once installation has safely stopped.
	var oldProc uintptr
	proc := syscall.NewCallback(func(window uintptr, message uint32, wParam, lParam uintptr) uintptr {
		if message == 0x10 {
			return 0
		} // WM_CLOSE
		result, _, _ := progressUser32.NewProc("CallWindowProcW").Call(oldProc, window, uintptr(message), wParam, lParam)
		return result
	})
	oldProc, _, _ = progressUser32.NewProc("SetWindowLongPtrW").Call(hwnd, ^uintptr(3), proc) // GWLP_WNDPROC = -4
	label, _, _ := create.Call(0, uintptr(unsafe.Pointer(winText("STATIC"))), uintptr(unsafe.Pointer(winText("Подготовка установки…"))), 0x50000000, 20, 25, 570, 100, hwnd, 0, 0, 0)
	note, _, _ := create.Call(0, uintptr(unsafe.Pointer(winText("STATIC"))), uintptr(unsafe.Pointer(winText("Это окно останется открытым до запуска программы.\r\nПри ошибке можно повторить действие; резервные файлы сохранятся."))), 0x50000000, 20, 125, 570, 45, hwnd, 0, 0, 0)
	font, _, _ := syscall.NewLazyDLL("gdi32.dll").NewProc("GetStockObject").Call(17)
	for _, control := range []uintptr{label, note} {
		progressUser32.NewProc("SendMessageW").Call(control, 0x30, font, 1)
	}
	updateWindow.Lock()
	updateWindow.hwnd = hwnd
	updateWindow.label = label
	updateWindow.Unlock()
	defer func() { updateWindow.Lock(); updateWindow.hwnd = 0; updateWindow.label = 0; updateWindow.Unlock() }()
	if visible {
		progressUser32.NewProc("ShowWindow").Call(hwnd, 5)
		progressUser32.NewProc("UpdateWindow").Call(hwnd)
	}
	start := func() {
		go func() {
			err := work()
			updateWindow.Lock()
			updateWindow.err = err
			updateWindow.Unlock()
			progressUser32.NewProc("PostMessageW").Call(hwnd, 0x8002, 0, 0)
		}()
	}
	start()
	var msg windowMessage
	for {
		got, _, _ := progressUser32.NewProc("GetMessageW").Call(uintptr(unsafe.Pointer(&msg)), 0, 0, 0)
		if got == 0 || int32(got) == -1 {
			return
		}
		switch msg.message {
		case 0x8001:
			updateWindow.Lock()
			text := updateWindow.text
			updateWindow.Unlock()
			progressUser32.NewProc("SetWindowTextW").Call(label, uintptr(unsafe.Pointer(winText(text))))
		case 0x8002:
			updateWindow.Lock()
			err := updateWindow.err
			updateWindow.Unlock()
			if err == nil {
				return
			}
			answer := MessageBox(hwnd, "Действие не завершено. Файлы сохранены для повтора.\r\n\r\n"+err.Error(), "Обновление GreenRhythm", 0x5|0x10)
			if answer != 4 {
				return
			}
			start()
		default:
			progressUser32.NewProc("TranslateMessage").Call(uintptr(unsafe.Pointer(&msg)))
			progressUser32.NewProc("DispatchMessageW").Call(uintptr(unsafe.Pointer(&msg)))
		}
	}
}
