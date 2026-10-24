// overlay-base: github.com/sagernet/sing-box v1.13.18
// GreenRhythm: Windows executable names are case-insensitive, including UDP sniff rules.
package rule

import (
	"path/filepath"
	"runtime"
	"strings"

	"github.com/sagernet/sing-box/adapter"
)

var _ RuleItem = (*ProcessItem)(nil)

type ProcessItem struct {
	processes  []string
	processMap map[string]bool
}

func processNameKey(name string) string {
	if runtime.GOOS == "windows" {
		return strings.ToLower(name)
	}
	return name
}

func NewProcessItem(processNameList []string) *ProcessItem {
	rule := &ProcessItem{
		processes:  processNameList,
		processMap: make(map[string]bool),
	}
	for _, processName := range processNameList {
		rule.processMap[processNameKey(processName)] = true
	}
	return rule
}

func (r *ProcessItem) Match(metadata *adapter.InboundContext) bool {
	if metadata.ProcessInfo == nil || metadata.ProcessInfo.ProcessPath == "" {
		return false
	}
	return r.processMap[processNameKey(filepath.Base(metadata.ProcessInfo.ProcessPath))]
}

func (r *ProcessItem) String() string {
	var description string
	pLen := len(r.processes)
	if pLen == 1 {
		description = "process_name=" + r.processes[0]
	} else {
		description = "process_name=[" + strings.Join(r.processes, " ") + "]"
	}
	return description
}
