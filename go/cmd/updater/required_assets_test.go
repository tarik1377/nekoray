package main

import (
	"archive/zip"
	"io"
	"os"
	"testing"
)

func TestIncompleteGeoDataCannotReplaceWorkingRuntime(t *testing.T) {
	for _, missing := range []string{"geoip.db", "geosite.db"} {
		for _, empty := range []bool{false, true} {
			t.Run(missing+map[bool]string{false: "-missing", true: "-empty"}[empty], func(t *testing.T) {
				archive, install, old := installFixture(t)
				r, err := zip.OpenReader(archive)
				if err != nil {
					t.Fatal(err)
				}
				entries := map[string]string{}
				for _, f := range r.File {
					if f.Name == "GreenRhythm/"+missing {
						if empty {
							entries[f.Name] = ""
						}
						continue
					}
					file, err := f.Open()
					if err != nil {
						t.Fatal(err)
					}
					body, err := io.ReadAll(file)
					file.Close()
					if err != nil {
						t.Fatal(err)
					}
					entries[f.Name] = string(body)
				}
				r.Close()
				makeZip(t, archive, entries)
				options := fixtureOptions()
				started := false
				options.start = func(string) error { started = true; return nil }
				if err := installUpdate(archive, install, options); err == nil {
					t.Fatal("incomplete payload installed")
				}
				assertOriginals(t, install, old)
				if started {
					t.Fatal("started incomplete runtime")
				}
				if _, err := os.Stat(archive); err != nil {
					t.Fatal("failed package removed")
				}
			})
		}
	}
}
