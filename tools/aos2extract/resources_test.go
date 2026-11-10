package main

import (
	"encoding/binary"
	"testing"
	"unicode/utf16"
)

// buildRsrc lays out a minimal .rsrc tree: root → named type → named resource
// → language 0x411 → data entry, followed by the name strings and the data.
func buildRsrc(secRVA uint32, typ, name string, data []byte) []byte {
	le := binary.LittleEndian
	b := make([]byte, 0x100)
	dir := func(off, entries int) { le.PutUint16(b[off+12:], uint16(entries)) } // named count
	str := func(off int, s string) int {
		u := utf16.Encode([]rune(s))
		le.PutUint16(b[off:], uint16(len(u)))
		for i, c := range u {
			le.PutUint16(b[off+2+2*i:], c)
		}
		return off
	}
	// root @0 → type dir @0x18
	dir(0, 1)
	le.PutUint32(b[16:], 0x80000000|uint32(str(0x80, typ)))
	le.PutUint32(b[20:], 0x80000000|0x18)
	// type dir @0x18 → name dir @0x30
	dir(0x18, 1)
	le.PutUint32(b[0x18+16:], 0x80000000|uint32(str(0x90, name)))
	le.PutUint32(b[0x18+20:], 0x80000000|0x30)
	// language dir @0x30 (id entry) → data entry @0x48
	le.PutUint16(b[0x30+14:], 1)
	le.PutUint32(b[0x30+16:], 0x411)
	le.PutUint32(b[0x30+20:], 0x48)
	le.PutUint32(b[0x48:], secRVA+0xd0)
	le.PutUint32(b[0x4c:], uint32(len(data)))
	copy(b[0xd0:], data)
	return b
}

func TestParseResourceDir(t *testing.T) {
	data := []byte("[ACT]\r\n< ソラ >< 帯あり >\r\n[EOF]\r\n")
	sec := buildRsrc(0x5000, "TXT", "TEXT_CONVERSATION010", data)
	res, err := ParseResourceDir(sec, 0x5000, 0, "TXT")
	if err != nil {
		t.Fatal(err)
	}
	if string(res["TEXT_CONVERSATION010"]) != string(data) || len(res) != 1 {
		t.Fatalf("got %q", res)
	}
	if res, _ := ParseResourceDir(sec, 0x5000, 0, "PNG"); len(res) != 0 {
		t.Fatal("type filter failed")
	}
	if _, err := ParseResourceDir(sec[:0x40], 0x5000, 0, "TXT"); err == nil {
		t.Fatal("expected truncation error")
	}
}

func TestConversationNames(t *testing.T) {
	ids := ConversationIDs()
	if len(ids) != 56 || ids[0] != 10 || ids[55] != 143 {
		t.Fatalf("ids %v", ids)
	}
	if ConversationResName(10) != "TEXT_CONVERSATION010" || ConversationResName(143) != "TEXT_CONVERSATION143" ||
		ConversationResName(7) != "TEXT_CONVERSATION007" {
		t.Fatal("bad resource name")
	}
}
