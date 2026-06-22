package grpc_server

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"grpc_server/gen"
	"io"
	"net/http"
	"os"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
)

var update_download_url string
var update_checksum_url string

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}

	client := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())

	if in.Action == gen.UpdateAction_Check { // Check update
		ctx, cancel := context.WithTimeout(ctx, time.Second*10)
		defer cancel()

		req, _ := http.NewRequestWithContext(ctx, "GET", "https://api.github.com/repos/tarik1377/nekoray/releases", nil)
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()

		v := []struct {
			TagName string `json:"tag_name"`
			HtmlUrl string `json:"html_url"`
			Assets  []struct {
				Name               string `json:"name"`
				BrowserDownloadUrl string `json:"browser_download_url"`
			} `json:"assets"`
			Prerelease bool   `json:"prerelease"`
			Body       string `json:"body"`
		}{}
		err = json.NewDecoder(resp.Body).Decode(&v)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		nowVer := neko_common.Version_neko

		var search string
		if runtime.GOOS == "windows" && runtime.GOARCH == "amd64" {
			search = "windows-x64"
		} else if runtime.GOOS == "linux" && runtime.GOARCH == "amd64" {
			search = "linux-x64"
		} else if runtime.GOOS == "darwin" {
			search = "macos-" + runtime.GOARCH
		} else {
			ret.Error = "Not official support platform"
			return ret, nil
		}

		for _, release := range v {
			if release.Prerelease && !in.CheckPreRelease {
				continue
			}
			// First acceptable release is the newest (GitHub returns newest-first).
			// Offer it only if it is strictly newer than what we run now.
			if !shouldUpdate(release.TagName, nowVer) {
				return ret, nil // Already up to date
			}
			var mainURL, mainName, checksumURL string
			for _, asset := range release.Assets {
				if !strings.Contains(asset.Name, search) {
					continue
				}
				if strings.HasSuffix(asset.Name, ".sha256") {
					checksumURL = asset.BrowserDownloadUrl
				} else if mainURL == "" {
					mainURL = asset.BrowserDownloadUrl
					mainName = asset.Name
				}
			}
			if mainURL != "" {
				update_download_url = mainURL
				update_checksum_url = checksumURL
				ret.AssetsName = mainName
				ret.DownloadUrl = mainURL
				ret.ReleaseUrl = release.HtmlUrl
				ret.ReleaseNote = release.Body
				ret.IsPreRelease = release.Prerelease
				return ret, nil // Update available
			}
			return ret, nil // Newest release has no matching asset; nothing to offer
		}
	} else { // Download update
		if update_download_url == "" {
			ret.Error = "No update URL"
			return ret, nil
		}

		req, _ := http.NewRequestWithContext(ctx, "GET", update_download_url, nil)
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()

		// Save as greenrhythm.zip (updater looks for this)
		const zipPath = "../greenrhythm.zip"
		f, err := os.OpenFile(zipPath, os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer f.Close()

		// Hash while downloading so integrity can be checked before the updater runs.
		h := sha256.New()
		_, err = io.Copy(io.MultiWriter(f, h), resp.Body)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		f.Sync()

		// Verify against the release-published SHA-256. If the release published a
		// checksum, we MUST fetch and match it — a fetch/parse failure is treated as a
		// verification failure (abort), not a silent skip. (Signature with an offline
		// key remains the stronger follow-up against a fully compromised release.)
		if update_checksum_url != "" {
			expected, err := fetchExpectedSha(ctx, client, update_checksum_url)
			if err != nil || expected == "" {
				f.Close()
				os.Remove(zipPath)
				ret.Error = "could not verify update checksum — aborting"
				return ret, nil
			}
			got := hex.EncodeToString(h.Sum(nil))
			if !strings.EqualFold(got, expected) {
				f.Close()
				os.Remove(zipPath)
				ret.Error = "update package checksum mismatch — aborting"
				return ret, nil
			}
		}
	}

	return ret, nil
}

// fetchExpectedSha downloads a sha256sum file and returns the hex digest (the first
// whitespace-separated field). Returns an error on any failure so the caller can
// abort rather than silently skip verification when a checksum was published.
func fetchExpectedSha(ctx context.Context, client *http.Client, url string) (string, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return "", err
	}
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	b, err := io.ReadAll(io.LimitReader(resp.Body, 1024))
	if err != nil {
		return "", err
	}
	fields := strings.Fields(string(b))
	if len(fields) == 0 {
		return "", errors.New("empty checksum file")
	}
	return fields[0], nil
}

// parseVer parses a clean GreenRhythm tag "vX.Y.Z" into numeric parts.
// Returns ok=false for anything that is NOT a clean dotted-numeric version —
// notably the legacy upstream string "4.0.1-2024-12-12" (it carries a suffix).
func parseVer(s string) ([]int, bool) {
	s = strings.TrimPrefix(s, "v")
	if s == "" || strings.ContainsAny(s, "-+") {
		return nil, false
	}
	parts := strings.Split(s, ".")
	nums := make([]int, 0, len(parts))
	for _, p := range parts {
		n, err := strconv.Atoi(p)
		if err != nil {
			return nil, false
		}
		nums = append(nums, n)
	}
	return nums, true
}

// shouldUpdate reports whether release tag `latest` should be offered to a client
// running `cur`. If `cur` is not a clean vX.Y.Z (e.g. a stale upstream build that
// reports "4.0.1-2024-12-12"), always offer the latest clean release so those
// stragglers get rescued instead of being trapped by a higher legacy major.
// If `latest` itself is unparseable, never offer (avoid junk prompts).
func shouldUpdate(latest, cur string) bool {
	lv, lok := parseVer(latest)
	if !lok {
		return false
	}
	cv, cok := parseVer(cur)
	if !cok {
		return true
	}
	for i := 0; i < len(lv) || i < len(cv); i++ {
		var a, b int
		if i < len(lv) {
			a = lv[i]
		}
		if i < len(cv) {
			b = cv[i]
		}
		if a != b {
			return a > b
		}
	}
	return false
}
