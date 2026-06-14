//go:build !windows

package main

func MessageBoxPlain(title, caption string) int {
	return 0
}

// MessageBoxChoice on non-Windows has no dialog; return IDNO (7) = least-destructive
// (keep the user's current routing/settings) for headless/Linux updates.
func MessageBoxChoice(title, caption string) int {
	return 7
}
