package main

import (
	"bufio"
	"bytes"
	"compress/zlib"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net/netip"
	"strings"

	"github.com/sagernet/sing-box/common/srs"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"go4.org/netipx"
)

const (
	geoDataMaxDownload = 8 << 20
	geoDataMaxDecoded  = 32 << 20
	geoDataMaxPrefixes = 200000
	geoDataMaxRanges   = 100000
)

var geoDataForbidden = func() []netip.Prefix {
	var result []netip.Prefix
	for _, value := range []string{
		"0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
		"169.254.0.0/16", "172.16.0.0/12", "192.168.0.0/16", "198.18.0.0/15",
		"224.0.0.0/4", "240.0.0.0/4", "::/128", "::1/128", "::ffff:0:0/96",
		"fc00::/7", "fe80::/10", "ff00::/8",
	} {
		result = append(result, netip.MustParsePrefix(value))
	}
	return result
}()

func validateGeoDataPrefix(prefix netip.Prefix) error {
	if !prefix.IsValid() || prefix.Bits() == 0 || prefix.Addr().Is4In6() {
		return errors.New("invalid or catch-all country prefix")
	}
	for _, forbidden := range geoDataForbidden {
		if prefix.Overlaps(forbidden) {
			return errors.New("country prefix overlaps a private or reserved network")
		}
	}
	return nil
}

// normalizeGeoData makes the generated binary reproducible across duplicate,
// overlapping and differently ordered source prefixes.
func normalizeGeoData(prefixes []netip.Prefix) ([]netip.Prefix, error) {
	if len(prefixes) == 0 || len(prefixes) > geoDataMaxPrefixes {
		return nil, errors.New("empty or oversized country prefix list")
	}
	var builder netipx.IPSetBuilder
	for _, prefix := range prefixes {
		if err := validateGeoDataPrefix(prefix); err != nil {
			return nil, err
		}
		builder.AddPrefix(prefix.Masked())
	}
	set, err := builder.IPSet()
	if err != nil {
		return nil, err
	}
	return set.Prefixes(), nil
}

func parseGeoDataText(content []byte) ([]netip.Prefix, error) {
	if len(content) == 0 || len(content) > geoDataMaxDownload {
		return nil, errors.New("empty or oversized country download")
	}
	content = bytes.TrimPrefix(content, []byte{0xef, 0xbb, 0xbf})
	scanner := bufio.NewScanner(bytes.NewReader(content))
	scanner.Buffer(make([]byte, 256), 256)
	var prefixes []netip.Prefix
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if len(prefixes) == geoDataMaxPrefixes {
			return nil, errors.New("too many country prefixes")
		}
		prefix, err := netip.ParsePrefix(line)
		if err != nil {
			return nil, errors.New("country download contains a non-CIDR line")
		}
		prefixes = append(prefixes, prefix)
	}
	if err := scanner.Err(); err != nil {
		return nil, errors.New("country download contains an oversized line")
	}
	return normalizeGeoData(prefixes)
}

func encodeGeoData(prefixes []netip.Prefix) ([]byte, error) {
	prefixes, err := normalizeGeoData(prefixes)
	if err != nil {
		return nil, err
	}
	cidrs := make([]string, len(prefixes))
	for i, prefix := range prefixes {
		cidrs[i] = prefix.String()
	}
	var result bytes.Buffer
	err = srs.Write(&result, option.PlainRuleSet{Rules: []option.HeadlessRule{{
		Type: C.RuleTypeDefault, DefaultOptions: option.DefaultHeadlessRule{IPCIDR: cidrs},
	}}}, C.RuleSetVersion3)
	if err != nil {
		return nil, err
	}
	return result.Bytes(), nil
}

// decodeGeoData accepts only our country-IP subset of SRS: one non-inverted
// default rule containing one IP set. It bounds decompression and every count
// BEFORE allocation; upstream srs.Read trusts varint/uint64 allocation lengths.
// Arbitrary user rule-set files are never passed through this decoder/updater.
func decodeGeoData(content []byte) ([]netip.Prefix, error) {
	if len(content) < 5 || len(content) > geoDataMaxDownload || string(content[:3]) != "SRS" ||
		content[3] < 1 || content[3] > C.RuleSetVersionCurrent {
		return nil, errors.New("invalid country rule-set header")
	}
	compressed := bytes.NewReader(content[4:])
	reader, err := zlib.NewReader(compressed)
	if err != nil {
		return nil, errors.New("invalid country rule-set compression")
	}
	decoded, readErr := io.ReadAll(io.LimitReader(reader, geoDataMaxDecoded+1))
	closeErr := reader.Close()
	if readErr != nil || closeErr != nil || len(decoded) > geoDataMaxDecoded || compressed.Len() != 0 {
		return nil, errors.New("invalid or oversized country rule-set body")
	}
	r := bytes.NewReader(decoded)
	ruleCount, err := binary.ReadUvarint(r)
	if err != nil || ruleCount != 1 {
		return nil, errors.New("country rule-set must contain one IP rule")
	}
	// Rule type default=0, IP-CIDR item=6, IP-set encoding version=1.
	for _, expected := range []byte{0, 6, 1} {
		value, err := r.ReadByte()
		if err != nil || value != expected {
			return nil, errors.New("unexpected country rule-set item")
		}
	}
	var ranges uint64
	if err := binary.Read(r, binary.BigEndian, &ranges); err != nil || ranges == 0 ||
		ranges > geoDataMaxRanges || ranges > uint64(r.Len()/10) {
		return nil, errors.New("invalid country IP-range count")
	}
	readAddress := func() (netip.Addr, error) {
		length, err := binary.ReadUvarint(r)
		if err != nil || (length != 4 && length != 16) {
			return netip.Addr{}, errors.New("invalid country IP address length")
		}
		var raw [16]byte
		if _, err := io.ReadFull(r, raw[:length]); err != nil {
			return netip.Addr{}, err
		}
		address, valid := netip.AddrFromSlice(raw[:length])
		if !valid || address.Is4In6() {
			return netip.Addr{}, errors.New("invalid country IP address")
		}
		return address, nil
	}
	var prefixes []netip.Prefix
	var previous netip.Addr
	for i := uint64(0); i < ranges; i++ {
		from, err := readAddress()
		if err != nil {
			return nil, err
		}
		to, err := readAddress()
		if err != nil || from.BitLen() != to.BitLen() || from.Compare(to) > 0 ||
			(previous.IsValid() && previous.Compare(from) >= 0) {
			return nil, errors.New("invalid or overlapping country IP range")
		}
		for _, prefix := range netipx.IPRangeFrom(from, to).Prefixes() {
			if len(prefixes) == geoDataMaxPrefixes {
				return nil, errors.New("too many recovered country prefixes")
			}
			prefixes = append(prefixes, prefix)
		}
		previous = to
	}
	end, endErr := r.ReadByte()
	invert, invertErr := r.ReadByte()
	if endErr != nil || invertErr != nil || end != 0xff || invert != 0 || r.Len() != 0 {
		return nil, fmt.Errorf("invalid country rule-set terminator")
	}
	return normalizeGeoData(prefixes)
}
