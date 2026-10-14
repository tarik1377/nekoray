// overlay-base: github.com/sagernet/sing-vmess v0.2.8-0.20250909125414-3aed155119a1
package vless

import (
	"bytes"
	"fmt"
	"testing"

	"github.com/sagernet/sing/common/buf"
	"github.com/sagernet/sing/common/logger"
)

var streamTestUUID = [16]byte{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}

func streamTestConn() *VisionConn {
	return &VisionConn{userUUID: streamTestUUID, logger: logger.NOP(), remainingContent: -1, remainingPadding: -1, withinPaddingBuffers: true}
}

func streamTestFrame(first bool, command byte, content []byte, padding int) []byte {
	var frame []byte
	if first {
		frame = append(frame, streamTestUUID[:]...)
	}
	frame = append(frame, command, byte(len(content)>>8), byte(len(content)), byte(padding>>8), byte(padding))
	frame = append(frame, content...)
	return append(frame, make([]byte, padding)...)
}

func streamTestFeed(t *testing.T, c *VisionConn, pieces ...[]byte) []byte {
	t.Helper()
	var result []byte
	for _, piece := range pieces {
		buffers, err := c.unPadding(piece)
		if err != nil {
			buf.ReleaseMulti(buffers)
			t.Fatal(err)
		}
		for _, buffer := range buffers {
			result = append(result, buffer.Bytes()...)
			buffer.Release()
		}
	}
	return result
}

func TestVisionEveryStreamBoundary(t *testing.T) {
	first := streamTestFrame(true, commandPaddingContinue, []byte("first"), 3)
	second := streamTestFrame(false, commandPaddingEnd, []byte("second"), 7)
	stream := append(append(first, second...), []byte("plain tail")...)
	for split := 0; split <= len(stream); split++ {
		t.Run(fmt.Sprint(split), func(t *testing.T) {
			got := streamTestFeed(t, streamTestConn(), stream[:split], stream[split:])
			if string(got) != "firstsecondplain tail" {
				t.Fatalf("stream corrupted at %d", split)
			}
		})
	}
	c := streamTestConn()
	var got []byte
	for _, value := range stream {
		got = append(got, streamTestFeed(t, c, []byte{value})...)
	}
	if string(got) != "firstsecondplain tail" {
		t.Fatal("bytewise stream corrupted")
	}
}

func TestVisionLargeMultiFrameStream(t *testing.T) {
	payload := bytes.Repeat([]byte("large-stream-"), 16000)
	var stream []byte
	for offset := 0; offset < len(payload); offset += 8192 {
		end := visionMin(offset+8192, len(payload))
		command := byte(commandPaddingContinue)
		if end == len(payload) {
			command = commandPaddingEnd
		}
		stream = append(stream, streamTestFrame(offset == 0, command, payload[offset:end], 17)...)
	}
	for _, step := range []int{1, 4, 21, 8191, 32768} {
		c := streamTestConn()
		var got []byte
		for offset := 0; offset < len(stream); offset += step {
			got = append(got, streamTestFeed(t, c, stream[offset:visionMin(offset+step, len(stream))])...)
		}
		if !bytes.Equal(got, payload) {
			t.Fatalf("large stream corrupted with step %d", step)
		}
	}
}

func TestVisionDirectAndPlainTransitions(t *testing.T) {
	for _, command := range []byte{commandPaddingEnd, commandPaddingDirect} {
		frame := streamTestFrame(true, command, nil, 0)
		for split := 1; split < len(frame); split++ {
			c := streamTestConn()
			got := streamTestFeed(t, c, frame[:split], append(frame[split:], []byte("raw")...))
			if string(got) != "raw" || !c.paddingEnded {
				t.Fatal("terminal transition corrupted")
			}
		}
	}
	c := streamTestConn()
	plain := append(append([]byte{}, streamTestUUID[:3]...), []byte("not padding")...)
	got := streamTestFeed(t, c, plain[:2], plain[2:])
	if !bytes.Equal(got, plain) || !c.paddingEnded {
		t.Fatal("partial matching UUID lost plain bytes")
	}
	if string(streamTestFeed(t, c, []byte("next"))) != "next" {
		t.Fatal("plain transition did not persist")
	}
}

func TestVisionMalformedCommandAndOwnedContent(t *testing.T) {
	c := streamTestConn()
	buffers, err := c.unPadding(streamTestFrame(true, 255, []byte("bad"), 0))
	buf.ReleaseMulti(buffers)
	if err == nil {
		t.Fatal("unknown command accepted")
	}
	c = streamTestConn()
	frame := streamTestFrame(true, commandPaddingContinue, []byte("valid"), 0)
	buffers, err = c.unPadding(frame)
	if err != nil {
		t.Fatal(err)
	}
	defer buf.ReleaseMulti(buffers)
	for i := range frame {
		frame[i] = 0
	}
	if string(buffers[0].Bytes()) != "valid" {
		t.Fatal("caller buffer reuse corrupts pending content")
	}
}
