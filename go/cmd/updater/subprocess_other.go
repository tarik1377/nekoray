//go:build !windows

package main

import "syscall"

func hiddenSubprocess() *syscall.SysProcAttr { return nil }
