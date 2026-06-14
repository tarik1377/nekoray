package main

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// Win32 MessageBox return codes (IDYES / IDNO), reused cross-platform.
const (
	idYes = 6
	idNo  = 7
)

// runMigration decides what happens to the user's existing config when applying an
// update. Server profiles (config/profiles/*, config/groups/<N>.json) are never part
// of the update archive, so they ALWAYS survive Mv untouched. This only governs
// routing (config/routes_box) and the settings file (config/groups/nekobox.json):
//
//	Yes -> reset routing to the new bundled Default (servers + settings kept)
//	No  -> keep current routing and settings (only binaries + geo assets update)
//
// Must be called AFTER the update folder is resolved and BEFORE Mv(updateDir, "./").
func runMigration(newDir, installDir string) {
	cfg := filepath.Join(installDir, "config")
	userNekobox := filepath.Join(cfg, "groups", "nekobox.json")

	// Fresh install (no existing config): let Mv lay down bundled defaults, no prompt.
	if !Exist(userNekobox) {
		return
	}

	newNekobox := filepath.Join(newDir, "config", "groups", "nekobox.json")
	newDefault := filepath.Join(newDir, "config", "routes_box", "Default")

	choice := MessageBoxChoice("GreenRhythm Update",
		"Reset routing to the new optimized default?\r\n\r\n"+
			"Yes - reset routing to the new default (your servers and settings are kept)\r\n"+
			"No  - keep your current routing and settings\r\n\r\n"+
			"Server profiles are always preserved.")

	// Always preserve the user's settings file: never let Mv overwrite nekobox.json.
	os.Remove(newNekobox)

	if choice == idYes {
		// Reset routing: drop all of the user's route profiles so that only the
		// freshly-copied bundled Default remains, and point active_routing at it.
		wipeDir(filepath.Join(cfg, "routes_box"))
		setActiveRouting(userNekobox, "Default")
		// newDefault stays in newDir, so Mv copies the bundled Default in.
	} else {
		// Keep everything: also protect the user's Default route from overwrite.
		os.Remove(newDefault)
	}
}

// wipeDir removes all entries inside dir, leaving dir itself in place.
func wipeDir(dir string) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return
	}
	for _, e := range entries {
		os.RemoveAll(filepath.Join(dir, e.Name()))
	}
}

// setActiveRouting patches only the active_routing key in nekobox.json, preserving
// every other setting (active server, port, theme, mux, etc.).
func setActiveRouting(path, route string) {
	data, err := os.ReadFile(path)
	if err != nil {
		return
	}
	var m map[string]interface{}
	if err := json.Unmarshal(data, &m); err != nil {
		return
	}
	m["active_routing"] = route
	out, err := json.MarshalIndent(m, "", "    ")
	if err != nil {
		return
	}
	_ = os.WriteFile(path, out, 0644)
}
