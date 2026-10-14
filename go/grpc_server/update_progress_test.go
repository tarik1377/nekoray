package grpc_server

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"errors"
	"net/http"
	"os"
	"path/filepath"
	"testing"
)

func updaterZip(t *testing.T, entry, body string) string {
	t.Helper()
	var b bytes.Buffer
	w := zip.NewWriter(&b)
	f, err := w.Create(entry)
	if err != nil {
		t.Fatal(err)
	}
	f.Write([]byte(body))
	if err = w.Close(); err != nil {
		t.Fatal(err)
	}
	return b.String()
}
func TestPrepareUpdaterFromVerifiedPackage(t *testing.T) {
	archive := filepath.Join(t.TempDir(), "package.zip")
	body := updaterZip(t, "GreenRhythm/updater.exe", "fixture updater bytes")
	os.WriteFile(archive, []byte(body), 0644)
	if err := prepareWindowsUpdater(archive); err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(filepath.Join(filepath.Dir(archive), "updater.next.exe"))
	if err != nil || string(got) != "fixture updater bytes" {
		t.Fatalf("%q %v", got, err)
	}
	if ExistForTest(archive + ".partial") {
		t.Fatal("partial helper remains")
	}
	for _, entry := range []string{"../updater.exe", "GreenRhythm/greenrhythm.exe"} {
		bad := filepath.Join(t.TempDir(), "bad.zip")
		os.WriteFile(bad, []byte(updaterZip(t, entry, "bad")), 0644)
		if err := prepareWindowsUpdater(bad); err == nil {
			t.Fatal("unrecognized entry accepted")
		}
	}
}
func ExistForTest(name string) bool { _, err := os.Stat(name); return err == nil }
func TestUpdateProgressRecordsErrorAndPreservesPreviousPackage(t *testing.T) {
	body := "new fixture body"
	network := outageNetwork(t, &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return outageResponse(r, 200, "truncated"), nil })})
	(&updateState{}).store(outageCandidate(body))
	os.WriteFile(network.archivePath, []byte("previous checked package"), 0644)
	result := outageDownload(t, network)
	if result.Error == "" {
		t.Fatal("corrupt body accepted")
	}
	got, _ := os.ReadFile(network.archivePath)
	if string(got) != "previous checked package" {
		t.Fatalf("previous package lost: %q", got)
	}
	statusBytes, _ := os.ReadFile(network.archivePath + ".progress.json")
	var status updateProgress
	if err := json.Unmarshal(statusBytes, &status); err != nil {
		t.Fatal(err)
	}
	if status.Stage != "error" || status.Error == "" || status.Total != int64(len(body)) {
		t.Fatalf("incorrect failure status: %+v", status)
	}
	if ExistForTest(network.archivePath + ".partial") {
		t.Fatal("incomplete download left on disk")
	}
	network.direct = &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return outageResponse(r, 200, body), nil })}
	if retry := outageDownload(t, network); retry.Error != "" {
		t.Fatal(retry.Error)
	}
	statusBytes, _ = os.ReadFile(network.archivePath + ".progress.json")
	json.Unmarshal(statusBytes, &status)
	if status.Stage != "ready" || status.Bytes != int64(len(body)) {
		t.Fatalf("retry status: %+v", status)
	}
}
func TestUpdateReportsBootstrapFailure(t *testing.T) {
	body := "fixture body"
	network := outageNetwork(t, &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return outageResponse(r, 200, body), nil })})
	(&updateState{}).store(outageCandidate(body))
	network.prepareUpdater = func(string) error { return errors.New("fixture helper blocked") }
	result := outageDownload(t, network)
	if result.Error == "" {
		t.Fatal("helper failure accepted")
	}
	if !ExistForTest(network.archivePath) {
		t.Fatal("verified package not retained")
	}
}
