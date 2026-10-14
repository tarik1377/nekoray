//go:build !windows

package main

func lockInstallation(string) (func(), error) { return func() {}, nil }
