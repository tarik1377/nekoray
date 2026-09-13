//go:build with_utls

// ПРАВЛЕННАЯ КОПИЯ ФАЙЛА SING-BOX. Подставляется при сборке:
// go/cmd/nekobox_core/_overlay/prepare.sh кладёт его в копию модуля sing-box,
// и сборка идёт с -modfile на replace к этой копии. Кэш модулей не трогается.
//
// overlay-base: github.com/sagernet/sing-box v1.13.18
//
// ЗАЧЕМ. 8 сентября 2026 в xtls/reality вошёл коммит 8cdf7bf9c7f0 («Reject
// outdated/strange Client Hello that doesn't have X25519MLKEM768 before
// optional X25519»), с ним вышли Xray 26.9.8 и 26.9.9. Такой сервер узнаёт
// клиента, только если в key_share ровно одна доля X25519MLKEM768 и она стоит
// перед обычной X25519. Остальных он молча отправляет на сайт-прикрытие, и
// клиент видит «reality verification failed».
//
// Штатный клиент sing-box эту долю, наоборот, вырезал (старые серверы REALITY
// не умели ML-KEM), поэтому с новыми серверами не соединялась ни одна версия
// sing-box — SagerNet/sing-box#4520, открыт. Замечено на нашей панели orsana
// 13.09.2026: после её обновления до 26.9.8 GreenRhythm перестал подключаться.
//
// ЧТО ИЗМЕНЕНО, три места, остальное совпадает с v1.13.18 байт в байт:
//  1. вместо вырезания гибридной доли — realityUClient: гарантирует ровно одну
//     X25519MLKEM768 перед X25519 для любого отпечатка (в нашей utls гибрид есть
//     только у Chrome; Firefox там 120, Safari 16, iOS 14 — без него);
//  2. ключ для общего секрета берётся от той доли, которую выберет сервер
//     (realityAuthKey), а не всегда первый обычный;
//  3. версия в session ID — xray-стиля 26.9.9, а не 1.8.1.
// Старые серверы это не ломает: обычная X25519 остаётся в ClientHello, и сервер
// до 8cdf7bf9c7f0 находит её, как находил.
//
// КАК ПРОВЕРИТЬ. go/cmd/nekobox_core/reality_mlkem_test.go повторяет проверку
// нового сервера дословно и без правки красный.
//
// КОГДА ВЫБРОСИТЬ. Когда sing-box сам начнёт слать гибридную долю (#4520):
// набор выше станет зелёным без -modfile. Поднимая sing-box раньше, эту копию
// придётся переснять с новой версии — prepare.sh сверит строку overlay-base и
// откажет сборке, если забыть.

package tls

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	mRand "math/rand"
	"net"
	"net/http"
	"reflect"
	"strings"
	"time"
	"unsafe"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/debug"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/common/logger"
	"github.com/sagernet/sing/common/ntp"
	aTLS "github.com/sagernet/sing/common/tls"

	utls "github.com/metacubex/utls"
	"golang.org/x/crypto/hkdf"
	"golang.org/x/net/http2"
)

var _ ConfigCompat = (*RealityClientConfig)(nil)

type RealityClientConfig struct {
	ctx       context.Context
	uClient   *UTLSClientConfig
	publicKey []byte
	shortID   [8]byte
}

func NewRealityClient(ctx context.Context, logger logger.ContextLogger, serverAddress string, options option.OutboundTLSOptions) (Config, error) {
	if options.UTLS == nil || !options.UTLS.Enabled {
		return nil, E.New("uTLS is required by reality client")
	}

	uClient, err := NewUTLSClient(ctx, logger, serverAddress, options)
	if err != nil {
		return nil, err
	}

	publicKey, err := base64.RawURLEncoding.DecodeString(options.Reality.PublicKey)
	if err != nil {
		return nil, E.Cause(err, "decode public_key")
	}
	if len(publicKey) != 32 {
		return nil, E.New("invalid public_key")
	}
	var shortID [8]byte
	decodedLen, err := hex.Decode(shortID[:], []byte(options.Reality.ShortID))
	if err != nil {
		return nil, E.Cause(err, "decode short_id")
	}
	if decodedLen > 8 {
		return nil, E.New("invalid short_id")
	}

	var config Config = &RealityClientConfig{ctx, uClient.(*UTLSClientConfig), publicKey, shortID}
	if options.KernelRx || options.KernelTx {
		if !C.IsLinux {
			return nil, E.New("kTLS is only supported on Linux")
		}
		config = &KTLSClientConfig{
			Config:   config,
			logger:   logger,
			kernelTx: options.KernelTx,
			kernelRx: options.KernelRx,
		}
	}
	return config, nil
}

func (e *RealityClientConfig) ServerName() string {
	return e.uClient.ServerName()
}

func (e *RealityClientConfig) SetServerName(serverName string) {
	e.uClient.SetServerName(serverName)
}

func (e *RealityClientConfig) NextProtos() []string {
	return e.uClient.NextProtos()
}

func (e *RealityClientConfig) SetNextProtos(nextProto []string) {
	e.uClient.SetNextProtos(nextProto)
}

func (e *RealityClientConfig) STDConfig() (*STDConfig, error) {
	return nil, E.New("unsupported usage for reality")
}

func (e *RealityClientConfig) Client(conn net.Conn) (Conn, error) {
	return ClientHandshake(context.Background(), conn, e)
}

func (e *RealityClientConfig) ClientHandshake(ctx context.Context, conn net.Conn) (aTLS.Conn, error) {
	verifier := &realityVerifier{
		serverName: e.uClient.ServerName(),
	}
	uConfig := e.uClient.config.Clone()
	uConfig.InsecureSkipVerify = true
	uConfig.SessionTicketsDisabled = true
	uConfig.VerifyPeerCertificate = verifier.VerifyPeerCertificate
	// GREENRHYTHM: вместо вырезания X25519MLKEM768 — ровно одна такая доля перед
	// обычной X25519, иначе сервер REALITY 8cdf7bf9c7f0+ клиента не узнает.
	uConn, err := realityUClient(conn, uConfig, e.uClient.id)
	if err != nil {
		return nil, err
	}
	verifier.UConn = uConn

	if len(uConfig.NextProtos) > 0 {
		for _, extension := range uConn.Extensions {
			if alpnExtension, isALPN := extension.(*utls.ALPNExtension); isALPN {
				alpnExtension.AlpnProtocols = uConfig.NextProtos
				break
			}
		}
	}

	hello := uConn.HandshakeState.Hello
	hello.SessionId = make([]byte, 32)
	copy(hello.Raw[39:], hello.SessionId)

	var nowTime time.Time
	if uConfig.Time != nil {
		nowTime = uConfig.Time()
	} else {
		nowTime = time.Now()
	}
	binary.BigEndian.PutUint64(hello.SessionId, uint64(nowTime.Unix()))

	hello.SessionId[0] = realityClientVersionMajor
	hello.SessionId[1] = realityClientVersionMinor
	hello.SessionId[2] = realityClientVersionPatch
	binary.BigEndian.PutUint32(hello.SessionId[4:], uint32(time.Now().Unix()))
	copy(hello.SessionId[8:], e.shortID[:])
	if debug.Enabled {
		fmt.Printf("REALITY hello.sessionId[:16]: %v\n", hello.SessionId[:16])
	}
	publicKey, err := ecdh.X25519().NewPublicKey(e.publicKey)
	if err != nil {
		return nil, err
	}
	keyShareKeys := uConn.HandshakeState.State13.KeyShareKeys
	if keyShareKeys == nil {
		return nil, E.New("nil KeyShareKeys")
	}
	// GREENRHYTHM: ключ от той доли, которую выберет сервер (см. realityAuthKey).
	ecdheKey := realityAuthKey(uConn, keyShareKeys)
	if ecdheKey == nil {
		return nil, E.New("nil ecdheKey")
	}
	authKey, err := ecdheKey.ECDH(publicKey)
	if err != nil {
		return nil, err
	}
	if authKey == nil {
		return nil, E.New("nil auth_key")
	}
	verifier.authKey = authKey
	_, err = hkdf.New(sha256.New, authKey, hello.Random[:20], []byte("REALITY")).Read(authKey)
	if err != nil {
		return nil, err
	}
	aesBlock, _ := aes.NewCipher(authKey)
	aesGcmCipher, _ := cipher.NewGCM(aesBlock)
	aesGcmCipher.Seal(hello.SessionId[:0], hello.Random[20:], hello.SessionId[:16], hello.Raw)
	copy(hello.Raw[39:], hello.SessionId)
	if debug.Enabled {
		fmt.Printf("REALITY hello.sessionId: %v\n", hello.SessionId)
		fmt.Printf("REALITY uConn.AuthKey: %v\n", authKey)
	}

	err = uConn.HandshakeContext(ctx)
	if err != nil {
		return nil, err
	}

	if debug.Enabled {
		fmt.Printf("REALITY Conn.Verified: %v\n", verifier.verified)
	}

	if !verifier.verified {
		go realityClientFallback(e.ctx, uConn, e.uClient.ServerName(), e.uClient.id)
		return nil, E.New("reality verification failed")
	}

	return &realityClientConnWrapper{uConn}, nil
}

func realityClientFallback(ctx context.Context, uConn net.Conn, serverName string, fingerprint utls.ClientHelloID) {
	defer uConn.Close()
	client := &http.Client{
		Transport: &http2.Transport{
			DialTLSContext: func(ctx context.Context, network, addr string, config *tls.Config) (net.Conn, error) {
				return uConn, nil
			},
			TLSClientConfig: &tls.Config{
				Time:    ntp.TimeFuncFromContext(ctx),
				RootCAs: adapter.RootPoolFromContext(ctx),
			},
		},
	}
	request, _ := http.NewRequest("GET", "https://"+serverName, nil)
	request.Header.Set("User-Agent", fingerprint.Client)
	request.AddCookie(&http.Cookie{Name: "padding", Value: strings.Repeat("0", mRand.Intn(32)+30)})
	response, err := client.Do(request)
	if err != nil {
		return
	}
	_, _ = io.Copy(io.Discard, response.Body)
	response.Body.Close()
}

func (e *RealityClientConfig) Clone() Config {
	return &RealityClientConfig{
		e.ctx,
		e.uClient.Clone().(*UTLSClientConfig),
		e.publicKey,
		e.shortID,
	}
}

type realityVerifier struct {
	*utls.UConn
	serverName string
	authKey    []byte
	verified   bool
}

func (c *realityVerifier) VerifyPeerCertificate(rawCerts [][]byte, verifiedChains [][]*x509.Certificate) error {
	p, _ := reflect.TypeFor[utls.Conn]().FieldByName("peerCertificates")
	certs := *(*([]*x509.Certificate))(unsafe.Add(unsafe.Pointer(c.Conn), p.Offset))
	if pub, ok := certs[0].PublicKey.(ed25519.PublicKey); ok {
		h := hmac.New(sha512.New, c.authKey)
		h.Write(pub)
		if bytes.Equal(h.Sum(nil), certs[0].Signature) {
			c.verified = true
			return nil
		}
	}
	opts := x509.VerifyOptions{
		DNSName:       c.serverName,
		Intermediates: x509.NewCertPool(),
	}
	for _, cert := range certs[1:] {
		opts.Intermediates.AddCert(cert)
	}
	if _, err := certs[0].Verify(opts); err != nil {
		return err
	}
	return nil
}

type realityClientConnWrapper struct {
	*utls.UConn
}

func (c *realityClientConnWrapper) ConnectionState() tls.ConnectionState {
	state := c.Conn.ConnectionState()
	//nolint:staticcheck
	return tls.ConnectionState{
		Version:                     state.Version,
		HandshakeComplete:           state.HandshakeComplete,
		DidResume:                   state.DidResume,
		CipherSuite:                 state.CipherSuite,
		NegotiatedProtocol:          state.NegotiatedProtocol,
		NegotiatedProtocolIsMutual:  state.NegotiatedProtocolIsMutual,
		ServerName:                  state.ServerName,
		PeerCertificates:            state.PeerCertificates,
		VerifiedChains:              state.VerifiedChains,
		SignedCertificateTimestamps: state.SignedCertificateTimestamps,
		OCSPResponse:                state.OCSPResponse,
		TLSUnique:                   state.TLSUnique,
	}
}

func (c *realityClientConnWrapper) Upstream() any {
	return c.UConn
}

// Due to low implementation quality, the reality server intercepted half close and caused memory leaks.
// We fixed it by calling Close() directly.
func (c *realityClientConnWrapper) CloseWrite() error {
	return c.Close()
}

func (c *realityClientConnWrapper) ReaderReplaceable() bool {
	return true
}

func (c *realityClientConnWrapper) WriterReplaceable() bool {
	return true
}

// ── GREENRHYTHM: ClientHello для серверов REALITY с ML-KEM ───────────────────

// Версия, которой клиент представляется серверу (байты 0..2 session ID).
//
// Сервер сверяет её с minClientVer и maxClientVer. sing-box слал 1.8.1 — версию
// xray многолетней давности: сервер с разумным порогом снизу отказал бы ему не
// хуже, чем за отсутствие гибридной доли. Здесь та версия xray, с сервером
// которой правка сверена (26.9.8 и 26.9.9); так же поступил mihomo.
const (
	realityClientVersionMajor byte = 26
	realityClientVersionMinor byte = 9
	realityClientVersionPatch byte = 9
)

// Размер доли X25519MLKEM768: ключ инкапсуляции ML-KEM-768 и открытый X25519.
const realityHybridShareSize = 1184 + 32

// realityUClient строит соединение uTLS, чей ClientHello узнаёт сервер REALITY
// из xtls/reality 8cdf7bf9c7f0 и новее.
func realityUClient(conn net.Conn, config *utls.Config, id utls.ClientHelloID) (*utls.UConn, error) {
	// Сначала как есть. Современный Chrome уже годен, и его ClientHello не должен
	// отличаться от настоящего ни на байт.
	uConn := utls.UClient(conn, config, id)
	if err := uConn.BuildHandshakeState(); err != nil {
		return nil, err
	}
	if realityHelloReady(uConn) == nil {
		return uConn, nil
	}

	// Отпечаток с постоянной спецификацией: берём её и ставим гибридную долю на
	// место. Ключи для новой доли utls создаёт только при применении
	// спецификации (ApplyPreset), поэтому правится спецификация, а не готовый
	// ClientHello: вставленная поверх доля ушла бы в сеть без ключа.
	if spec, err := utls.UTLSIdToSpec(id); err == nil {
		realityShapeSpec(&spec)
		uConn = utls.UClient(conn, config, utls.HelloCustom)
		if err := uConn.ApplyPreset(&spec); err != nil {
			return nil, err
		}
		if err := uConn.BuildHandshakeState(); err != nil {
			return nil, err
		}
		if err := realityHelloReady(uConn); err != nil {
			return nil, E.Cause(err, "reality: fingerprint ", id.Str())
		}
		return uConn, nil
	}

	// У случайного отпечатка постоянной спецификации нет. Генератор utls сам
	// кладёт гибридную долю первой, когда её выбирает, — тянем, пока не выпадет.
	var last error
	for attempt := 0; attempt < 64; attempt++ {
		uConn = utls.UClient(conn, config, id)
		if err := uConn.BuildHandshakeState(); err != nil {
			return nil, err
		}
		if last = realityHelloReady(uConn); last == nil {
			return uConn, nil
		}
	}
	return nil, E.Cause(last, "reality: randomized fingerprint")
}

// realityHelloReady — узнает ли новый сервер этот ClientHello. Проверяется не
// только расстановка долей: закрытый ключ, которым клиент посчитает общий
// секрет, обязан быть ровно от той доли, что выберет сервер, иначе session ID
// у сервера не расшифруется и клиент снова уйдёт на сайт-прикрытие.
func realityHelloReady(uConn *utls.UConn) error {
	shares := realityKeyShares(uConn)
	if shares == nil {
		return E.New("no key_share: this fingerprint has no TLS 1.3, and REALITY runs over TLS 1.3 only")
	}
	hybrid, plain, err := realityServerChoice(shares)
	if err != nil {
		return err
	}
	keys := uConn.HandshakeState.State13.KeyShareKeys
	if keys == nil {
		return E.New("nil KeyShareKeys")
	}
	private, public := realityChosenKey(keys, hybrid, plain)
	if private == nil || !bytes.Equal(private.PublicKey().Bytes(), public) {
		return E.New("the private key does not belong to the share the server will pick")
	}
	if !realityCurveListed(uConn, utls.X25519MLKEM768) {
		return E.New("X25519MLKEM768 is missing from supported_groups")
	}
	return nil
}

// realityServerChoice повторяет отбор долей сервером (tls.go коммита
// 8cdf7bf9c7f0): ровно одна гибридная доля, и стоит она раньше первой X25519.
func realityServerChoice(shares []utls.KeyShare) (hybrid, plain *utls.KeyShare, err error) {
	for i := range shares {
		switch shares[i].Group {
		case utls.X25519MLKEM768:
			if hybrid != nil {
				return nil, nil, E.New("two X25519MLKEM768 key shares")
			}
			if plain != nil {
				return nil, nil, E.New("X25519MLKEM768 comes after X25519")
			}
			hybrid = &shares[i]
		case utls.X25519:
			if plain == nil {
				plain = &shares[i]
			}
		}
	}
	if hybrid == nil {
		return nil, nil, E.New("no X25519MLKEM768 key share")
	}
	if len(hybrid.Data) != realityHybridShareSize {
		return nil, nil, E.New("X25519MLKEM768 key share of size ", len(hybrid.Data))
	}
	return hybrid, plain, nil
}

// realityChosenKey — наш закрытый ключ для доли, которую выберет сервер, и её
// открытая часть. Сервер берёт обычную X25519, если она есть, иначе X25519-половину
// гибридной доли. utls кладёт в Ecdhe ключ первой негибридной доли, а в
// MlkemEcdhe — отдельный ключ X25519 из гибридной.
func realityChosenKey(keys *utls.KeySharePrivateKeys, hybrid, plain *utls.KeyShare) (*ecdh.PrivateKey, []byte) {
	if plain != nil {
		return keys.Ecdhe, plain.Data
	}
	return keys.MlkemEcdhe, hybrid.Data[realityHybridShareSize-32:]
}

// realityAuthKey — ключ для общего секрета REALITY. realityUClient уже
// убедился, что доли стоят как надо, поэтому отказ здесь невозможен; на всякий
// случай возвращается прежний выбор sing-box.
func realityAuthKey(uConn *utls.UConn, keys *utls.KeySharePrivateKeys) *ecdh.PrivateKey {
	hybrid, plain, err := realityServerChoice(realityKeyShares(uConn))
	if err != nil {
		return keys.Ecdhe
	}
	private, _ := realityChosenKey(keys, hybrid, plain)
	return private
}

func realityKeyShares(uConn *utls.UConn) []utls.KeyShare {
	for _, extension := range uConn.Extensions {
		if ks, ok := extension.(*utls.KeyShareExtension); ok {
			return ks.KeyShares
		}
	}
	return nil
}

func realityCurveListed(uConn *utls.UConn, curve utls.CurveID) bool {
	for _, extension := range uConn.Extensions {
		if ce, ok := extension.(*utls.SupportedCurvesExtension); ok {
			for _, c := range ce.Curves {
				if c == curve {
					return true
				}
			}
		}
	}
	return false
}

// realityShapeSpec ставит гибридную долю туда, где её ждёт сервер: сразу перед
// обычной X25519, и в key_share, и в supported_groups. Обычная X25519 при этом
// становится первой негибридной долей: utls кладёт в Ecdhe ключ именно первой
// такой доли, а сервер считает общий секрет именно по X25519.
func realityShapeSpec(spec *utls.ClientHelloSpec) {
	for _, extension := range spec.Extensions {
		switch e := extension.(type) {
		case *utls.SupportedCurvesExtension:
			e.Curves = realityShapeCurves(e.Curves)
		case *utls.KeyShareExtension:
			e.KeyShares = realityShapeKeyShares(e.KeyShares)
		}
	}
}

func realityIsGREASE(v uint16) bool {
	return v&0x0f0f == 0x0a0a && v>>8 == v&0xff
}

func realityShapeCurves(curves []utls.CurveID) []utls.CurveID {
	out := make([]utls.CurveID, 0, len(curves)+1)
	placed := false
	for _, c := range curves {
		if c == utls.X25519MLKEM768 {
			continue
		}
		if c == utls.X25519 && !placed {
			out = append(out, utls.X25519MLKEM768)
			placed = true
		}
		out = append(out, c)
	}
	if !placed {
		// X25519 нет вовсе — гибрид встаёт сразу после ведущих GREASE.
		i := 0
		for i < len(out) && realityIsGREASE(uint16(out[i])) {
			i++
		}
		out = append(out[:i], append([]utls.CurveID{utls.X25519MLKEM768}, out[i:]...)...)
	}
	return out
}

func realityShapeKeyShares(shares []utls.KeyShare) []utls.KeyShare {
	if len(shares) == 0 {
		return shares // нет key_share — нет TLS 1.3, чинить нечего
	}
	var grease, rest []utls.KeyShare
	hasX25519 := false
	for _, s := range shares {
		switch {
		case realityIsGREASE(uint16(s.Group)):
			grease = append(grease, s)
		case s.Group == utls.X25519MLKEM768:
			// своя встанет ниже; вторая гибридная доля сервером запрещена
		case s.Group == utls.X25519 && !hasX25519:
			hasX25519 = true
		default:
			rest = append(rest, s)
		}
	}
	out := append(grease, utls.KeyShare{Group: utls.X25519MLKEM768})
	if hasX25519 {
		out = append(out, utls.KeyShare{Group: utls.X25519})
	}
	return append(out, rest...)
}
