package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestTextureRoundTrip(t *testing.T) {
	plain := append(append([]byte{}, pngSig...), bytes.Repeat([]byte{1, 2, 3}, 200)...)
	enc := append([]byte(nil), plain...)
	XorTexture(enc)
	if bytes.Equal(enc, plain) {
		t.Fatal("xor did nothing")
	}
	got, err := DecodeTexture(enc)
	if err != nil || !bytes.Equal(got, plain) {
		t.Fatalf("round trip failed: %v", err)
	}
	if _, err := DecodeTexture(plain); err == nil {
		t.Fatal("expected error decoding unencrypted data")
	}
}

func mkRecord(name string, pcm []byte) []byte {
	var b bytes.Buffer
	binary.Write(&b, binary.LittleEndian, uint32(len(pcm)))
	b.WriteString(name)
	b.WriteByte(0)
	binary.Write(&b, binary.LittleEndian, uint32(18))
	b.Write([]byte{1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xac, 0, 0, 2, 0, 16, 0, 0, 0})
	b.Write(pcm)
	return b.Bytes()
}

func TestParseSoundBank(t *testing.T) {
	bank := append(mkRecord("A.WAV", []byte{1, 2, 3, 4}), mkRecord("LONGER_NAME.WAV", []byte{9, 9})...)
	s, err := ParseSoundBank(bank)
	if err != nil {
		t.Fatal(err)
	}
	if len(s) != 2 || s[0].Name != "A.WAV" || s[1].Name != "LONGER_NAME.WAV" || !bytes.Equal(s[1].PCM, []byte{9, 9}) {
		t.Fatalf("bad parse: %+v", s)
	}
	w := s[0].WAV()
	if string(w[:4]) != "RIFF" || string(w[8:16]) != "WAVEfmt " || int(binary.LittleEndian.Uint32(w[4:])) != len(w)-8 {
		t.Fatal("bad wav header")
	}
	if _, err := ParseSoundBank(bank[:len(bank)-1]); err == nil {
		t.Fatal("expected truncation error")
	}
}

func oggPage(bos bool, payload []byte) []byte {
	h := make([]byte, 27)
	copy(h, "OggS")
	if bos {
		h[5] = 2
	}
	h[26] = 1
	return append(append(h, byte(len(payload))), payload...)
}

func TestSplitOgg(t *testing.T) {
	var b []byte
	b = append(b, oggPage(true, []byte("a"))...)
	b = append(b, oggPage(false, []byte("bb"))...)
	b = append(b, oggPage(true, []byte("ccc"))...)
	s, skipped, err := SplitOgg(b)
	if err != nil || skipped != 0 || len(s) != 2 || len(s[0])+len(s[1]) != len(b) {
		t.Fatalf("split: %v %d", err, len(s))
	}
	padded := append(append(append([]byte{}, oggPage(true, []byte("a"))...), 0x55), oggPage(true, []byte("b"))...)
	s, skipped, err = SplitOgg(padded)
	if err != nil || skipped != 1 || len(s) != 2 || bytes.Contains(s[0], []byte{0x55}) {
		t.Fatalf("resync: %v skipped=%d n=%d", err, skipped, len(s))
	}
	if _, _, err := SplitOgg(oggPage(true, []byte("abc"))[:29]); err == nil {
		t.Fatal("expected truncation error")
	}
}

func TestSafeName(t *testing.T) {
	if got := safeName("../../evil/../x.png"); got != "evil/x.png" {
		t.Fatalf("got %q", got)
	}
}
