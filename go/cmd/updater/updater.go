package main

import (
	"context"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/codeclysm/extract"
)

func Updater() {
	pre_cleanup := func() {
		if runtime.GOOS == "linux" {
			os.RemoveAll("./usr")
		}
		os.RemoveAll("./greenrhythm_update")
	}

	// find update package (try new name first, fallback to old)
	var updatePackagePath string
	if len(os.Args) == 2 && Exist(os.Args[1]) {
		updatePackagePath = os.Args[1]
	} else if Exist("./greenrhythm.zip") {
		updatePackagePath = "./greenrhythm.zip"
	} else if Exist("./greenrhythm.tar.gz") {
		updatePackagePath = "./greenrhythm.tar.gz"
	} else if Exist("./nekoray.zip") {
		updatePackagePath = "./nekoray.zip"
	} else if Exist("./nekoray.tar.gz") {
		updatePackagePath = "./nekoray.tar.gz"
	} else {
		MessageBoxPlain("GreenRhythm Updater", "No update package found.")
		log.Fatalln("no update package found")
	}
	log.Println("updating from", updatePackagePath)

	// extract update package
	extractDir := "./greenrhythm_update"
	if strings.HasSuffix(updatePackagePath, ".zip") {
		pre_cleanup()
		f, err := os.Open(updatePackagePath)
		if err != nil {
			log.Fatalln(err.Error())
		}
		err = extract.Zip(context.Background(), f, extractDir, nil)
		if err != nil {
			log.Fatalln(err.Error())
		}
		f.Close()
	} else if strings.HasSuffix(updatePackagePath, ".tar.gz") {
		pre_cleanup()
		f, err := os.Open(updatePackagePath)
		if err != nil {
			log.Fatalln(err.Error())
		}
		err = extract.Gz(context.Background(), f, extractDir, nil)
		if err != nil {
			log.Fatalln(err.Error())
		}
		f.Close()
	}

	// remove old crash dumps
	removeAll("./*.dmp")

	// find the update folder inside extracted archive
	updateDir := FindExist([]string{
		extractDir + "/GreenRhythm",
		extractDir + "/greenrhythm",
		extractDir + "/nekoray",
	})
	if updateDir == "" {
		// try any single folder inside extract dir
		entries, _ := os.ReadDir(extractDir)
		for _, e := range entries {
			if e.IsDir() {
				updateDir = extractDir + "/" + e.Name()
				break
			}
		}
	}
	if updateDir == "" {
		MessageBoxPlain("GreenRhythm Updater", "Update failed: no update folder found inside archive.")
		log.Fatalln("no update folder found")
	}

	log.Println("applying update from", updateDir)

	err := Mv(updateDir, "./")
	if err != nil {
		MessageBoxPlain("GreenRhythm Updater", "Update failed. Please close the running instance and run the updater again.\n\n"+err.Error())
		log.Fatalln(err.Error())
	}

	// cleanup
	os.RemoveAll(extractDir)
	os.Remove("./greenrhythm.zip")
	os.Remove("./greenrhythm.tar.gz")
	os.Remove("./nekoray.zip")
	os.Remove("./nekoray.tar.gz")

	// clean up old binaries from previous versions
	os.Remove("./nekoray.exe")
	os.Remove("./nekoray.png")
	os.Remove("./nekoray_core.exe")
	os.Remove("./nekobox.exe")
	os.Remove("./nekobox_core.exe")
	os.Remove("./nekobox.png")

	log.Println("update complete")
}

func Exist(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func FindExist(paths []string) string {
	for _, path := range paths {
		if Exist(path) {
			return path
		}
	}
	return ""
}

func Mv(src, dst string) error {
	s, err := os.Stat(src)
	if err != nil {
		return err
	}
	if s.IsDir() {
		es, err := os.ReadDir(src)
		if err != nil {
			return err
		}
		for _, e := range es {
			err = Mv(filepath.Join(src, e.Name()), filepath.Join(dst, e.Name()))
			if err != nil {
				return err
			}
		}
	} else {
		err = os.MkdirAll(filepath.Dir(dst), 0755)
		if err != nil {
			return err
		}
		err = os.Rename(src, dst)
		if err != nil {
			return err
		}
	}
	return nil
}

func removeAll(glob string) {
	files, _ := filepath.Glob(glob)
	for _, f := range files {
		os.Remove(f)
	}
}
