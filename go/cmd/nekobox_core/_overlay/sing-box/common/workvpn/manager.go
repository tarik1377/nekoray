// overlay-base: github.com/sagernet/sing-box v1.13.18
package workvpn

import (
	"context"
	"fmt"
	"net"
	"net/netip"
	"reflect"
	"strings"
	"sync"
	"time"

	"github.com/miekg/dns"
	"github.com/sagernet/sing/common/control"
)

const queryBudget = 900 * time.Millisecond

type cachedAnswer struct {
	message         *dns.Msg // nil means that the ordinary configured DNS should handle it
	stored, expires time.Time
}

// Manager is per box, not global: separate running instances cannot enable or
// poison each other's routing. No service, route, DNS or adapter is modified.
type Manager struct {
	mu         sync.Mutex
	excluded   []string
	read       func() (Snapshot, error)
	exchange   func(context.Context, resolver, *dns.Msg) (*dns.Msg, error)
	snapshot   Snapshot
	checked    time.Time
	generation uint64
	answers    map[dns.Question]cachedAnswer
}

func New(excludedInterfaces []string) *Manager {
	return &Manager{excluded: append([]string(nil), excludedInterfaces...), read: readSystemSnapshot, exchange: exchangeBound, answers: make(map[dns.Question]cachedAnswer)}
}

func (m *Manager) current() (Snapshot, uint64) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if time.Since(m.checked) >= 2*time.Second {
		s, err := m.read()
		if err != nil {
			s = Snapshot{}
		} // Never retain routes after failed discovery.
		if !reflect.DeepEqual(s, m.snapshot) {
			m.snapshot = s
			m.generation++
			clear(m.answers)
		}
		m.checked = time.Now()
	}
	return m.snapshot, m.generation
}

func (m *Manager) Interface(ip netip.Addr) (string, int, bool) {
	s, _ := m.current()
	a, index, ok := s.Interface(ip, m.excluded)
	return a.Name, index, ok
}

func (m *Manager) HasScope(domain string) bool {
	s, _ := m.current()
	servers, scope := s.resolversForName(m.excluded, domain)
	return scope > 0 && len(servers) > 0
}

func (m *Manager) HasNetworks() bool {
	s, _ := m.current()
	for _, route := range s.Routes {
		if !specificPrefix(route.Prefix) {
			continue
		}
		if adapter, ok := s.adapter(route.Index); ok && adapter.isVPN(m.excluded) {
			return true
		}
	}
	return false
}

func (m *Manager) Reset() {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.checked = time.Time{}
	m.generation++
	clear(m.answers)
}

func responseAddresses(message *dns.Msg) []netip.Addr {
	if message == nil || message.Rcode != dns.RcodeSuccess {
		return nil
	}
	var result []netip.Addr
	for _, rr := range message.Answer {
		var bytes net.IP
		switch r := rr.(type) {
		case *dns.A:
			bytes = r.A
		case *dns.AAAA:
			bytes = r.AAAA
		default:
			continue
		}
		if ip, ok := netip.AddrFromSlice(bytes); ok {
			result = append(result, ip.Unmap())
		}
	}
	return result
}

func routedResponse(s Snapshot, excluded []string, message *dns.Msg) bool {
	ips := responseAddresses(message)
	if len(ips) == 0 {
		return false
	}
	for _, ip := range ips {
		if _, _, ok := s.Interface(ip, excluded); !ok {
			return false
		}
	}
	return true
}

func (m *Manager) query(ctx context.Context, s Snapshot, r resolver, question *dns.Msg) *dns.Msg {
	response, err := m.exchange(ctx, r, question.Copy())
	if err != nil || response == nil {
		return nil
	}
	if len(response.Question) != 1 || !strings.EqualFold(response.Question[0].Name, question.Question[0].Name) || response.Question[0].Qtype != question.Question[0].Qtype {
		return nil
	}
	if r.ownsName(question.Question[0].Name) {
		if response.Rcode == dns.RcodeSuccess || response.Rcode == dns.RcodeNameError {
			return response
		}
		return nil
	}
	if question.Question[0].Qtype != dns.TypeA && question.Question[0].Qtype != dns.TypeAAAA {
		return nil
	}
	if routedResponse(s, m.excluded, response) {
		return response
	}
	// An internal IPv4-only/IPv6-only name must not escape to the public DNS
	// merely because its other address family is empty (or publicly different).
	if response.Rcode != dns.RcodeSuccess && response.Rcode != dns.RcodeNameError {
		return nil
	}
	other := new(dns.Msg)
	q := question.Question[0]
	otherType := uint16(dns.TypeAAAA)
	if q.Qtype == dns.TypeAAAA {
		otherType = dns.TypeA
	}
	other.SetQuestion(q.Name, otherType)
	classification, err := m.exchange(ctx, r, other)
	if err != nil || !routedResponse(s, m.excluded, classification) {
		return nil
	}
	empty := new(dns.Msg)
	empty.SetReply(question)
	empty.RecursionAvailable = true
	// Use the opposite family's TTL as this NODATA's short cache lifetime.
	empty.Ns = []dns.RR{&dns.SOA{Hdr: dns.RR_Header{Name: q.Name, Rrtype: dns.TypeSOA, Class: dns.ClassINET, Ttl: answerTTL(classification)}, Ns: ".", Mbox: ".", Minttl: answerTTL(classification)}}
	return empty
}

func answerTTL(message *dns.Msg) uint32 {
	ttl := uint32(30)
	for _, list := range [][]dns.RR{message.Answer, message.Ns} {
		for _, rr := range list {
			if rr.Header().Ttl < ttl {
				ttl = rr.Header().Ttl
			}
		}
	}
	return ttl
}

// TryExchange tries only OS-advertised VPN DNS. Unrelated answers, NXDOMAIN,
// refusals and timeouts leave the caller's existing DNS rules and final intact.
// Without a suffix, an unknown name cannot be classified before asking the VPN.
func (m *Manager) TryExchange(ctx context.Context, question *dns.Msg) (*dns.Msg, bool) {
	if len(question.Question) != 1 || question.Opcode != dns.OpcodeQuery || question.Response || len(question.Answer) != 0 || len(question.Ns) != 0 {
		return nil, false
	}
	// Workers can outlive a cancelled caller. Own the request before spawning
	// them, so that the caller may safely reuse its message after return.
	question = question.Copy()
	q := question.Question[0]
	if q.Qclass != dns.ClassINET {
		return nil, false
	}
	q.Name = strings.ToLower(dns.Fqdn(q.Name))
	s, generation := m.current()
	servers, scope := s.resolversForName(m.excluded, q.Name)
	if q.Qtype != dns.TypeA && q.Qtype != dns.TypeAAAA {
		filtered := servers[:0]
		for _, server := range servers {
			if server.ownsName(q.Name) {
				filtered = append(filtered, server)
			}
		}
		servers = filtered
	}
	if len(servers) == 0 {
		return nil, false
	}
	// Queries carrying DNSSEC/EDNS options are sent unchanged and not cached.
	cacheable := len(question.Extra) == 0 && question.RecursionDesired && !question.CheckingDisabled && !question.AuthenticatedData
	if cacheable {
		m.mu.Lock()
		cached, ok := m.answers[q]
		m.mu.Unlock()
		if ok && time.Now().Before(cached.expires) {
			if cached.message == nil {
				return nil, false
			}
			response := cached.message.Copy()
			response.Id, response.Question = question.Id, question.Question
			elapsed := uint32(time.Since(cached.stored) / time.Second)
			for _, list := range [][]dns.RR{response.Answer, response.Ns, response.Extra} {
				for _, rr := range list {
					if rr.Header().Rrtype != dns.TypeOPT {
						rr.Header().Ttl -= min(rr.Header().Ttl, elapsed)
					}
				}
			}
			return response, true
		}
	}
	callerContext := ctx
	ctx, cancel := context.WithTimeout(ctx, queryBudget)
	defer cancel()
	results := make(chan *dns.Msg, len(servers))
	for _, server := range servers {
		go func(r resolver) { results <- m.query(ctx, s, r, question) }(server)
	}
	var response *dns.Msg
	for range servers {
		select {
		case candidate := <-results:
			if candidate != nil {
				response = candidate
				cancel()
			}
		case <-ctx.Done():
		}
		if response != nil || ctx.Err() != nil {
			break
		}
	}
	if response == nil && scope > 0 {
		response = new(dns.Msg)
		response.SetRcode(question, dns.RcodeServerFailure)
	}
	if cacheable && callerContext.Err() == nil {
		ttl := 3 * time.Second
		if response != nil {
			ttl = time.Duration(answerTTL(response)) * time.Second
		}
		m.mu.Lock()
		if generation == m.generation && ttl > 0 {
			if len(m.answers) >= 512 {
				clear(m.answers)
			}
			var saved *dns.Msg
			if response != nil {
				saved = response.Copy()
			}
			now := time.Now()
			m.answers[q] = cachedAnswer{saved, now, now.Add(ttl)}
		}
		m.mu.Unlock()
	}
	return response, response != nil
}

// TryLookup avoids normal DNS completely for a positively classified work
// resource. Address families are queried concurrently within one query budget.
type LookupError struct{ Rcode int }

func (e LookupError) Error() string {
	return fmt.Sprintf("work VPN DNS: %s", dns.RcodeToString[e.Rcode])
}

func (m *Manager) TryLookup(ctx context.Context, domain string) ([]netip.Addr, bool, error) {
	type result struct {
		ips     []netip.Addr
		handled bool
		rcode   int
	}
	results := make(chan result, 2)
	for _, qtype := range []uint16{dns.TypeA, dns.TypeAAAA} {
		go func(qtype uint16) {
			q := new(dns.Msg)
			q.SetQuestion(dns.Fqdn(domain), qtype)
			response, ok := m.TryExchange(ctx, q)
			rcode := dns.RcodeSuccess
			if response != nil {
				rcode = response.Rcode
			}
			results <- result{responseAddresses(response), ok, rcode}
		}(qtype)
	}
	var ips []netip.Addr
	handled := false
	failure := dns.RcodeSuccess
	for range 2 {
		select {
		case r := <-results:
			ips = append(ips, r.ips...)
			handled = handled || r.handled
			if r.handled && r.rcode != dns.RcodeSuccess {
				failure = r.rcode
			}
		case <-ctx.Done():
			return nil, false, ctx.Err()
		}
	}
	if len(ips) == 0 && handled && failure != dns.RcodeSuccess {
		return nil, true, LookupError{failure}
	}
	return ips, handled, nil
}

func exchangeBound(ctx context.Context, server resolver, question *dns.Msg) (*dns.Msg, error) {
	// Use numeric interface indices and numeric DNS addresses: this cannot invoke
	// another resolver, a proxy outbound, or an automatic default/TUN interface.
	dialer := &net.Dialer{Control: control.BindToInterface(nil, server.adapter.Name, server.index)}
	address := net.JoinHostPort(server.address.String(), "53")
	udpCtx, cancel := context.WithTimeout(ctx, queryBudget/2)
	client := &dns.Client{Net: "udp", Dialer: dialer, Timeout: queryBudget / 2}
	response, _, err := client.ExchangeContext(udpCtx, question, address)
	cancel()
	if err == nil && response != nil && !response.Truncated {
		return response, nil
	}
	client.Net = "tcp"
	response, _, err = client.ExchangeContext(ctx, question, address)
	return response, err
}
