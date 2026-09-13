//go:build with_utls

package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/hkdf"
	"crypto/mlkem"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"testing"
	"time"

	sbtls "github.com/sagernet/sing-box/common/tls"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/logger"
)

/*
Клиент REALITY обязан проходить проверку серверов Xray 26.9.8 и новее.

ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. 8 сентября 2026 в xtls/reality вошёл коммит
8cdf7bf9c7f0: сервер больше не узнаёт клиента, если в key_share его ClientHello
нет ровно одной доли X25519MLKEM768, стоящей перед обычной X25519. Такого
клиента он молча отправляет на сайт-прикрытие, и клиент видит «reality
verification failed» — без единого слова о причине. Штатный sing-box эту долю
вырезал сам, поэтому ни одна его версия с такими серверами не соединялась, и
наш клиент тоже (orsana, 13 сентября 2026: v2raytun, v2box и GreenRhythm —
отказ, xray 26.7.11 и новее — проходят).

Правка живёт в _overlay/: при сборке prepare.sh подставляет правленый файл в
копию sing-box, и сборка идёт с -modfile. Этот набор — единственное, что её
стережёт.

ЧТО ПРОВЕРЯЕТСЯ И КАК. Сервер здесь не нужен: проверка сервера повторена
дословно (realityServerAccepts ниже — перенос цикла из tls.go того коммита).
Клиенту дают трубу вместо сети, читают его ClientHello и спрашивают: узнал бы
его новый сервер? Узнать — значит найти долю по правилу коммита, посчитать
общий ключ закрытым ключом сервера и расшифровать session ID. Расшифровка
проходит только тогда, когда клиент выбрал тот же ключ X25519, что и сервер.
Поэтому набор ловит не только отсутствие доли, но и неверный выбор ключа.

КРАСНЫЙ БЕЗ ПРАВКИ. go test без -modfile соберёт штатный клиент — и все
отпечатки упадут на «нет X25519MLKEM768». Так и должно быть: зелёный набор без
правки значил бы, что правка больше не нужна, и её пора выбросить
(patches/README.md).

Запуск (из go/cmd/nekobox_core):

	go test -modfile "$(bash _overlay/prepare.sh)" -tags "with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api" -run TestRealityClientHello ./...
*/

// Числа протокола — те же, что в xtls/reality и в TLS.
const (
	tlsGroupX25519         = 0x001d
	tlsGroupX25519MLKEM768 = 0x11ec
	tlsExtSupportedGroups  = 10
	tlsExtKeyShare         = 51
	// type(1) + length(3) + legacy_version(2) + random(32) + session_id length(1)
	clientHelloSessionIDOffset = 39
)

// Все имена отпечатков, которые понимает sing-box (common/tls/utls_client.go).
// У orsana в подписках стоит firefox — а в нашей utls Firefox это версия 120,
// в которой гибридной доли нет вовсе. Поэтому проверяются все, а не только chrome.
var realityFingerprints = []string{
	"chrome", "firefox", "edge", "safari", "ios", "android", "360", "qq", "random", "randomized",
}

func TestRealityClientHelloPassesXray2698Server(t *testing.T) {
	serverKey, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	const shortID = "fb53"

	for _, fp := range realityFingerprints {
		t.Run(fp, func(t *testing.T) {
			// randomized каждый раз разный: гоняем его многократно, иначе
			// удачный жребий выдал бы зелёный там, где бывает красный.
			rounds := 1
			if fp == "randomized" || fp == "random" {
				rounds = 12
			}
			for i := 0; i < rounds; i++ {
				raw, err := captureRealityClientHello(fp, serverKey.PublicKey().Bytes(), shortID)
				if err != nil && bytes.Contains([]byte(err.Error()), []byte("TLS 1.3")) {
					// REALITY работает только поверх TLS 1.3. Отпечаток без него
					// не соединялся никогда, и правка не обязана это чинить —
					// обязана внятно сказать, что случилось.
					t.Skipf("отпечаток без TLS 1.3, REALITY с ним невозможен: %v", err)
				}
				if err != nil {
					t.Fatalf("ClientHello не получен: %v", err)
				}
				plain, err := realityServerAccepts(raw, serverKey)
				if err != nil {
					t.Fatalf("новый сервер REALITY не узнал бы этого клиента: %v", err)
				}
				if plain[0] != 26 || plain[1] != 9 || plain[2] != 9 {
					t.Fatalf("версия в session ID %d.%d.%d, ожидалась 26.9.9 (см. _overlay)", plain[0], plain[1], plain[2])
				}
				if !bytes.HasPrefix(plain[8:], []byte{0xfb, 0x53}) {
					t.Fatalf("short_id в session ID %x, ожидался fb53", plain[8:])
				}
				if err := groupListed(raw, tlsGroupX25519MLKEM768); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

// captureRealityClientHello запускает настоящий клиент sing-box на трубе и
// возвращает сообщение ClientHello без заголовка записи TLS.
func captureRealityClientHello(fingerprint string, serverPub []byte, shortID string) ([]byte, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	config, err := sbtls.NewRealityClient(ctx, logger.NOP(), "max.test", option.OutboundTLSOptions{
		Enabled:    true,
		ServerName: "max.test",
		UTLS:       &option.OutboundUTLSOptions{Enabled: true, Fingerprint: fingerprint},
		Reality: &option.OutboundRealityOptions{
			Enabled:   true,
			PublicKey: base64.RawURLEncoding.EncodeToString(serverPub),
			ShortID:   shortID,
		},
	})
	if err != nil {
		return nil, fmt.Errorf("конфигурация клиента: %w", err)
	}

	client, server := net.Pipe()
	defer server.Close()
	done := make(chan error, 1)
	go func() {
		// Ответа не будет: клиент упадёт, как только трубу закроют. Нам
		// нужен только его первый пакет.
		_, err := sbtls.ClientHandshake(ctx, client, config)
		done <- err
	}()

	// Чтение — отдельно, а ожидание — на двух каналах сразу. Клиент может
	// отказать до отправки (отпечаток без TLS 1.3), и тогда чтение ждало бы
	// до срока, а причина отказа потерялась бы за «i/o timeout».
	type hello struct {
		body []byte
		err  error
	}
	got := make(chan hello, 1)
	go func() {
		_ = server.SetReadDeadline(time.Now().Add(10 * time.Second))
		header := make([]byte, 5)
		if _, err := io.ReadFull(server, header); err != nil {
			got <- hello{nil, fmt.Errorf("заголовок записи: %w", err)}
			return
		}
		if header[0] != 0x16 {
			got <- hello{nil, fmt.Errorf("первая запись не рукопожатие: тип %d", header[0])}
			return
		}
		body := make([]byte, binary.BigEndian.Uint16(header[3:5]))
		if _, err := io.ReadFull(server, body); err != nil {
			got <- hello{nil, fmt.Errorf("тело записи: %w", err)}
			return
		}
		got <- hello{body, nil}
	}()

	select {
	case h := <-got:
		_ = server.Close()
		<-done
		return h.body, h.err
	case err := <-done:
		_ = server.Close()
		if err == nil {
			err = errors.New("клиент завершился, не прислав ClientHello")
		}
		return nil, err
	}
}

// realityServerAccepts — проверка сервера из xtls/reality 8cdf7bf9c7f0
// (tls.go, func Server), перенесённая без изменений логики. Возвращает
// расшифрованные 16 байт session ID или причину отказа.
func realityServerAccepts(raw []byte, serverKey *ecdh.PrivateKey) ([]byte, error) {
	random, sessionID, shares, err := parseClientHello(raw)
	if err != nil {
		return nil, err
	}

	var peerPub, peerPub2 []byte
	for _, share := range shares {
		if share.group == tlsGroupX25519MLKEM768 && len(share.data) == mlkem.EncapsulationKeySize768+32 {
			if peerPub2 != nil {
				peerPub2 = nil // вторая гибридная доля — отказ
				break
			}
			peerPub2 = share.data[mlkem.EncapsulationKeySize768:]
			continue
		}
		if share.group == tlsGroupX25519 && len(share.data) == 32 {
			peerPub = share.data
			break
		}
	}
	if peerPub2 == nil {
		return nil, errors.New("нет X25519MLKEM768 перед обычной X25519 (или долей две)")
	}
	if peerPub == nil {
		peerPub = peerPub2 // запасной выбор сервера: X25519 внутри гибрида
	}

	pub, err := ecdh.X25519().NewPublicKey(peerPub)
	if err != nil {
		return nil, err
	}
	shared, err := serverKey.ECDH(pub)
	if err != nil {
		return nil, err
	}
	authKey, err := hkdf.Key(sha256.New, shared, random[:20], "REALITY", 32)
	if err != nil {
		return nil, err
	}
	block, err := aes.NewCipher(authKey)
	if err != nil {
		return nil, err
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	aad := bytes.Clone(raw)
	copy(aad[clientHelloSessionIDOffset:clientHelloSessionIDOffset+32], make([]byte, 32))
	plain, err := aead.Open(nil, random[20:], sessionID, aad)
	if err != nil {
		return nil, fmt.Errorf("session ID не расшифровался — клиент считал ключ не от той доли: %w", err)
	}
	return plain, nil
}

type keyShare struct {
	group uint16
	data  []byte
}

func parseClientHello(raw []byte) (random, sessionID []byte, shares []keyShare, err error) {
	if len(raw) < clientHelloSessionIDOffset+32 || raw[0] != 1 {
		return nil, nil, nil, errors.New("это не ClientHello")
	}
	random = raw[6:38]
	if raw[38] != 32 {
		return nil, nil, nil, fmt.Errorf("session ID длиной %d, REALITY требует 32", raw[38])
	}
	sessionID = raw[clientHelloSessionIDOffset : clientHelloSessionIDOffset+32]
	exts, err := clientHelloExtensions(raw)
	if err != nil {
		return nil, nil, nil, err
	}
	ks, ok := exts[tlsExtKeyShare]
	if !ok || len(ks) < 2 {
		return nil, nil, nil, errors.New("нет расширения key_share")
	}
	list := ks[2:]
	for len(list) >= 4 {
		group := binary.BigEndian.Uint16(list[0:2])
		n := int(binary.BigEndian.Uint16(list[2:4]))
		if len(list) < 4+n {
			return nil, nil, nil, errors.New("key_share обрезан")
		}
		shares = append(shares, keyShare{group, list[4 : 4+n]})
		list = list[4+n:]
	}
	return random, sessionID, shares, nil
}

func clientHelloExtensions(raw []byte) (map[uint16][]byte, error) {
	p := clientHelloSessionIDOffset + 32
	if len(raw) < p+2 {
		return nil, errors.New("ClientHello обрезан")
	}
	p += 2 + int(binary.BigEndian.Uint16(raw[p:])) // шифры
	if len(raw) < p+1 {
		return nil, errors.New("ClientHello обрезан")
	}
	p += 1 + int(raw[p]) // сжатие
	if len(raw) < p+2 {
		return nil, errors.New("нет расширений")
	}
	end := p + 2 + int(binary.BigEndian.Uint16(raw[p:]))
	p += 2
	out := map[uint16][]byte{}
	for p+4 <= end && end <= len(raw) {
		typ := binary.BigEndian.Uint16(raw[p:])
		n := int(binary.BigEndian.Uint16(raw[p+2:]))
		if p+4+n > end {
			return nil, errors.New("расширение обрезано")
		}
		out[typ] = raw[p+4 : p+4+n]
		p += 4 + n
	}
	return out, nil
}

// groupListed — доля обязана быть и в supported_groups: TLS-сервер вправе
// отвергнуть долю группы, которую клиент не объявил поддерживаемой.
func groupListed(raw []byte, group uint16) error {
	exts, err := clientHelloExtensions(raw)
	if err != nil {
		return err
	}
	g, ok := exts[tlsExtSupportedGroups]
	if !ok || len(g) < 2 {
		return errors.New("нет supported_groups")
	}
	for list := g[2:]; len(list) >= 2; list = list[2:] {
		if binary.BigEndian.Uint16(list) == group {
			return nil
		}
	}
	return fmt.Errorf("группы 0x%04x нет в supported_groups", group)
}
