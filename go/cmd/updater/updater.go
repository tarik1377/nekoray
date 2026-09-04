package main

import (
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)


// waitForPreviousInstance ждёт, пока прежняя копия отпустит свои файлы.
//
// ЗАЧЕМ. Программа запускает обновлятор и лишь ПОТОМ начинает выходить, а её
// ядро и xray живут ещё какое-то время. Обновлятор в этот момент уже переносит
// файлы и получает от системы отказ: «rename ... xray.exe: Access is denied».
// Человек видел окно «закройте работающую копию и запустите обновление снова» —
// при том что закрывать было нечего: копия закрывалась сама, просто медленнее
// нас. Совет вдобавок невыполним, обновлятор запускает сама программа.
//
// Проверяем не список процессов, а сам файл: открыть его на запись можно ровно
// тогда, когда его отпустили.
//
// Имена собираются по платформе. Прежде в списке стояли только .exe, то есть
// вне Windows цикл был пуст и функция возвращалась немедленно — при
// комментарии, обещавшем работу на всех платформах. На POSIX блокировка файла
// не обязательна, и ожидание там менее надёжно; но пустой список не надёжен
// вовсе.
func waitForPreviousInstance(deadline time.Duration) {
	locked := []string{"./xray", "./greenrhythm_core", "./greenrhythm"}
	if runtime.GOOS == "windows" {
		for i := range locked {
			locked[i] += ".exe"
		}
	}
	start := time.Now()
	for time.Since(start) < deadline {
		busy := ""
		for _, p := range locked {
			if !Exist(p) {
				continue
			}
			f, err := os.OpenFile(p, os.O_WRONLY, 0)
			if err != nil {
				busy = p
				break
			}
			_ = f.Close()
		}
		if busy == "" {
			return
		}
		log.Println("waiting for", busy, "to be released")
		time.Sleep(500 * time.Millisecond)
	}
	// Не дождались — пробуем всё равно: отказ тогда будет назван честно, а ждать
	// дольше значит держать человека перед окном, которое ничего не делает.
	log.Println("previous instance still holds files, trying anyway")
}
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
	} else {
		MessageBoxPlain("GreenRhythm Updater", "No update package found.")
		log.Fatalln("no update package found")
	}
	log.Println("updating from", updatePackagePath)

	// Проверяем ДО распаковки: распаковщик пишет на диск, и разбирать потом,
	// что он успел разложить, дороже, чем не начинать.
	if err := verifyArchive(updatePackagePath); err != nil {
		os.Remove(updatePackagePath)
		os.Remove(updatePackagePath + ".sha256")
		MessageBoxPlain("GreenRhythm Updater", err.Error())
		log.Fatalln(err.Error())
	}

	// extract update package
	extractDir := "./greenrhythm_update"
	if strings.HasSuffix(updatePackagePath, ".zip") {
		pre_cleanup()
		if err := extractZip(updatePackagePath, extractDir); err != nil {
			log.Fatalln(err.Error())
		}
	} else if strings.HasSuffix(updatePackagePath, ".tar.gz") {
		pre_cleanup()
		if err := extractTarGz(updatePackagePath, extractDir); err != nil {
			log.Fatalln(err.Error())
		}
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

	// Ждём, пока прежняя копия отпустит файлы, и только потом переносим.
	waitForPreviousInstance(30 * time.Second)

	log.Println("applying update from", updateDir)

	// Consent-based config migration: keep server profiles always; optionally reset
	// routing to the new bundled default. Runs while both old config and new defaults
	// coexist and before anything destructive (Mv) happens.
	runMigration(updateDir, "./")

	err := Mv(updateDir, "./")
	if err != nil {
		MessageBoxPlain("GreenRhythm Updater", "Не удалось применить обновление: файлы всё ещё заняты.\n\nЗакройте программу полностью — значок в трее, «Выход», — и запустите updater.exe из папки установки.\n\n"+err.Error())
		log.Fatalln(err.Error())
	}

	// cleanup
	os.RemoveAll(extractDir)
	os.Remove("./greenrhythm.zip")
	os.Remove("./greenrhythm.zip.sha256")
	os.Remove("./greenrhythm.tar.gz")
	os.Remove("./greenrhythm.tar.gz.sha256")

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
