//go:build !windows

package main

import "log"

func reportProgress(text string) { log.Println(text) }
func runUpdateUI(work func() error) {
	if err := work(); err != nil {
		log.Println(err)
		MessageBoxPlain("Обновление GreenRhythm", err.Error())
	}
}
