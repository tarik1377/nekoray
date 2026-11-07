// Package robloxauto selects routes only for explicitly opted-in Roblox apps.
package robloxauto

import (
	"net/netip"
	"strings"
)

// Suffix roots cover the supplied API/CDN/edge names without pinning changing
// game servers. Shared hosting providers are deliberately exact names only.
var roots = strings.Fields(`roblox.com rbxcdn.com rbxinfra.net rbxlabs.com robloxlabs.com rbxtrk.com
rblx.co rblx.com rblx.org rbx.com roblox.ca roblox.cc roblox.cl roblox.co.jp roblox.co.uk
roblox.de roblox.dk roblox.eu roblox.fr roblox.hu roblox.it roblox.jp roblox.kr roblox.mx
roblox.net roblox.org roblox.ph roblox.pt roblox.sk roblox.tv roblox.uk roblox.us roblox.vn
blox.com goblocks.com goblox.com gooblox.com humancoexperience.com ro-blox.com roadblocks.com roblocks.com rowblocks.com rowblox.com`)
var exact = strings.Fields(`auth.roblox.com.cdn.cloudflare.net cdn.arkoselabs.com roblox-api.arkoselabs.com
css.rbxcdn.edgesuite.net images.rbxcdn.edgesuite.net static.rbxcdn.edgesuite.net
jsak.rbxcdn.com.edgesuite.net tr.rbxcdn.com.edgesuite.net roblox-poc.global.ssl.fastly.net robloxcorp.s.llnwi.net
d143j4fdqe1jki.cloudfront.net d1kpbbfl4rco16.cloudfront.net d1unuk07s6td74.cloudfront.net
d27udkwzs2awi8.cloudfront.net d2gbj0c64xar4a.cloudfront.net d2shmbw56nyjcv.cloudfront.net
d2yzw3aiudktwi.cloudfront.net d348twzf7a8p5.cloudfront.net d34jux8p1jw4or.cloudfront.net
d36gcs9qe30vnp.cloudfront.net d3smszjb1gn4q5.cloudfront.net dapx4swc8lj69.cloudfront.net
doy2mn9upadnk.cloudfront.net dw04ej0wrfjel.cloudfront.net dy1kt1ycd11k4.cloudfront.net
devforum-uploads.s3.dualstack.us-east-2.amazonaws.com
roblox-load-generator-configuration.s3.us-east-2.amazonaws.com
lms-aws-eu-central-1-ath-1a-1593284305.eu-central-1.elb.amazonaws.com
lms-aws-us-east-2-932560212.us-east-2.elb.amazonaws.com`)

func Host(value string) string {
	value = strings.TrimSuffix(strings.ToLower(value), ".")
	if len(value) == 0 || len(value) > 253 {
		return ""
	}
	for _, ch := range value {
		if !(ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9' || ch == '-' || ch == '.') {
			return ""
		}
	}
	return value
}
func KnownHost(host string) bool {
	host = Host(host)
	if host == "" {
		return false
	}
	for _, name := range roots {
		if host == name || strings.HasSuffix(host, "."+name) {
			return true
		}
	}
	for _, name := range exact {
		if host == name {
			return true
		}
	}
	return false
}
func Local(host string) bool {
	if ip, err := netip.ParseAddr(host); err == nil {
		return !ip.IsGlobalUnicast() || ip.IsPrivate() || ip.IsLoopback() || ip.IsLinkLocalUnicast()
	}
	host = Host(host)
	return host == "localhost" || strings.HasSuffix(host, ".localhost") || strings.HasSuffix(host, ".local") || strings.HasSuffix(host, ".lan")
}

// Only an unauthenticated, fixed public URL is probed, never a captured URL,
// asset query, cookie, Authorization header, POST or application payload.
func ProbeURL(host string) string {
	if !KnownHost(host) {
		return ""
	}
	path := "/"
	if host == "users.roblox.com" {
		path = "/v1/users/1"
	}
	if host == "setup.rbxcdn.com" {
		path = "/version"
	}
	return "https://" + host + path
}
