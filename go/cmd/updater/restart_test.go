package main

import (
	"os"
	"os/exec"
	"testing"
	"time"
)

func init() {
	switch os.Getenv("GR_UPDATER_START_FIXTURE") {
	case "exit":
		os.Exit(3)
	case "stay":
		time.Sleep(10 * time.Second)
		os.Exit(0)
	}
}
func TestRestartDetectsImmediateExitAndAcceptsRunningProcess(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	for _, mode := range []string{"exit", "stay"} {
		t.Run(mode, func(t *testing.T) {
			cmd := exec.Command(exe)
			cmd.Env = append(os.Environ(), "GR_UPDATER_START_FIXTURE="+mode)
			err := startAndObserve(cmd, 250*time.Millisecond)
			if mode == "exit" && err == nil {
				t.Fatal("immediate crash accepted")
			}
			if mode == "stay" {
				defer cmd.Process.Kill()
				if err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}
