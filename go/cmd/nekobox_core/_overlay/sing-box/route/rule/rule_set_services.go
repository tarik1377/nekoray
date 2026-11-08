// overlay-base: github.com/sagernet/sing-box v1.13.18
package rule

import "github.com/sagernet/sing-box/option"

// ReplaceServiceRules atomically replaces an already registered, local inline
// service matcher. Its immutable reader snapshots and update callbacks use the
// same synchronization as upstream reloadFile. No router or connection restarts.
func (s *LocalRuleSet) ReplaceServiceRules(rules []option.HeadlessRule) error {
	return s.reloadRules(rules)
}
