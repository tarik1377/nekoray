package main

import (
	"syscall"
	"unsafe"
)

// MessageBoxPlain of Win32 API.
func MessageBoxPlain(title, caption string) int {
	const (
		NULL  = 0
		MB_OK = 0
	)
	return MessageBox(NULL, caption, title, MB_OK)
}

// MessageBoxChoice shows a Yes/No dialog and returns the Win32 code (IDYES=6, IDNO=7).
func MessageBoxChoice(title, caption string) int {
	const (
		NULL            = 0
		MB_YESNO        = 0x00000004
		MB_ICONQUESTION = 0x00000020
	)
	return MessageBox(NULL, caption, title, MB_YESNO|MB_ICONQUESTION)
}

// MessageBox of Win32 API.
func MessageBox(hwnd uintptr, caption, title string, flags uint) int {
	ret, _, _ := syscall.NewLazyDLL("user32.dll").NewProc("MessageBoxW").Call(
		uintptr(hwnd),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(caption))),
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr(title))),
		uintptr(flags))

	return int(ret)
}
