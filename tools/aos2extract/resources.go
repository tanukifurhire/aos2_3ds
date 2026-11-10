package main

// Win32 resources of aos2.exe. The story conversation scripts are not in
// script.pak: they are 56 resources of type "TXT" named TEXT_CONVERSATION<id, 3 digits>
// (id 10..13, 20..23, ..., 140..143), loaded by FUN_005d6430 with
// FindResourceA(module, name, "TXT") (docs/re/modes.md §C.1). They are written
// out byte-for-byte (UTF-8 text, CRLF) to extracted/script_res/<name>.txt.

import (
	"debug/pe"
	"encoding/binary"
	"errors"
	"fmt"
	"strings"
	"unicode/utf16"
)

// ConversationResType is the resource type of the conversation scripts.
const ConversationResType = "TXT"

// PEResources returns the data of every resource of the given (named) type in
// a PE image, keyed by resource name (the first language entry of each name).
func PEResources(image []byte, typ string) (map[string][]byte, error) {
	f, err := pe.NewFile(bytesReaderAt(image))
	if err != nil {
		return nil, err
	}
	var dir pe.DataDirectory
	switch oh := f.OptionalHeader.(type) {
	case *pe.OptionalHeader32:
		if len(oh.DataDirectory) > pe.IMAGE_DIRECTORY_ENTRY_RESOURCE {
			dir = oh.DataDirectory[pe.IMAGE_DIRECTORY_ENTRY_RESOURCE]
		}
	case *pe.OptionalHeader64:
		if len(oh.DataDirectory) > pe.IMAGE_DIRECTORY_ENTRY_RESOURCE {
			dir = oh.DataDirectory[pe.IMAGE_DIRECTORY_ENTRY_RESOURCE]
		}
	}
	if dir.VirtualAddress == 0 {
		return nil, errors.New("no resource directory")
	}
	for _, s := range f.Sections {
		if dir.VirtualAddress >= s.VirtualAddress && dir.VirtualAddress < s.VirtualAddress+s.VirtualSize {
			data, err := s.Data()
			if err != nil {
				return nil, err
			}
			off := dir.VirtualAddress - s.VirtualAddress
			if int(off) > len(data) {
				return nil, errors.New("resource directory outside its section")
			}
			// RVAs in data entries are image-relative; rebase to the section.
			return ParseResourceDir(data, s.VirtualAddress, off, typ)
		}
	}
	return nil, errors.New("resource directory not in any section")
}

type bytesReaderAt []byte

func (b bytesReaderAt) ReadAt(p []byte, off int64) (int, error) {
	if off < 0 || off >= int64(len(b)) {
		return 0, errors.New("EOF")
	}
	n := copy(p, b[off:])
	if n < len(p) {
		return n, errors.New("EOF")
	}
	return n, nil
}

// ParseResourceDir walks an IMAGE_RESOURCE_DIRECTORY tree (type → name →
// language → IMAGE_RESOURCE_DATA_ENTRY). sec is the raw section holding the
// tree, secRVA its RVA, root the offset of the root directory in sec.
func ParseResourceDir(sec []byte, secRVA, root uint32, typ string) (map[string][]byte, error) {
	le := binary.LittleEndian
	type entry struct {
		name   string
		id     uint32
		named  bool
		subdir bool
		off    uint32
	}
	readDir := func(off uint32) ([]entry, error) {
		if uint64(off)+16 > uint64(len(sec)) {
			return nil, fmt.Errorf("directory at %#x out of range", off)
		}
		n := int(le.Uint16(sec[off+12:])) + int(le.Uint16(sec[off+14:]))
		var es []entry
		for i := 0; i < n; i++ {
			p := off + 16 + uint32(i)*8
			if uint64(p)+8 > uint64(len(sec)) {
				return nil, fmt.Errorf("directory entry at %#x out of range", p)
			}
			nm, to := le.Uint32(sec[p:]), le.Uint32(sec[p+4:])
			e := entry{subdir: to&0x80000000 != 0, off: to & 0x7fffffff}
			if nm&0x80000000 != 0 {
				so := nm & 0x7fffffff
				if uint64(so)+2 > uint64(len(sec)) {
					return nil, fmt.Errorf("name at %#x out of range", so)
				}
				l := uint32(le.Uint16(sec[so:]))
				if uint64(so)+2+uint64(l)*2 > uint64(len(sec)) {
					return nil, fmt.Errorf("name at %#x out of range", so)
				}
				u := make([]uint16, l)
				for k := range u {
					u[k] = le.Uint16(sec[so+2+uint32(k)*2:])
				}
				e.name, e.named = string(utf16.Decode(u)), true
			} else {
				e.id = nm
			}
			es = append(es, e)
		}
		return es, nil
	}
	types, err := readDir(root)
	if err != nil {
		return nil, err
	}
	out := map[string][]byte{}
	for _, t := range types {
		if !t.named || !strings.EqualFold(t.name, typ) || !t.subdir {
			continue
		}
		names, err := readDir(t.off)
		if err != nil {
			return nil, err
		}
		for _, nm := range names {
			key := nm.name
			if !nm.named {
				key = fmt.Sprintf("#%d", nm.id)
			}
			off := nm.off
			if nm.subdir { // language level: take the first entry
				langs, err := readDir(nm.off)
				if err != nil {
					return nil, err
				}
				if len(langs) == 0 || langs[0].subdir {
					return nil, fmt.Errorf("%s: bad language directory", key)
				}
				off = langs[0].off
			}
			if uint64(off)+16 > uint64(len(sec)) {
				return nil, fmt.Errorf("%s: data entry out of range", key)
			}
			rva, size := le.Uint32(sec[off:]), le.Uint32(sec[off+4:])
			if rva < secRVA || uint64(rva-secRVA)+uint64(size) > uint64(len(sec)) {
				return nil, fmt.Errorf("%s: data out of range", key)
			}
			out[key] = append([]byte(nil), sec[rva-secRVA:rva-secRVA+size]...)
		}
	}
	return out, nil
}

// ConversationIDs are the 56 script ids FUN_004e2600 loads: 10*k+n, k 1..14, n 0..3.
func ConversationIDs() []int {
	var ids []int
	for k := 1; k <= 14; k++ {
		for n := 0; n < 4; n++ {
			ids = append(ids, 10*k+n)
		}
	}
	return ids
}

// ConversationResName is FUN_004e4fa0: "TEXT_CONVERSATION" + id zero-padded
// to 3 digits ("00" prefix below 10, "0" below 100).
func ConversationResName(id int) string { return fmt.Sprintf("TEXT_CONVERSATION%03d", id) }

// ExtractConversations returns name → bytes for all 56 conversation scripts.
func ExtractConversations(exe []byte) (map[string][]byte, error) {
	res, err := PEResources(exe, ConversationResType)
	if err != nil {
		return nil, err
	}
	out := map[string][]byte{}
	for _, id := range ConversationIDs() {
		n := ConversationResName(id)
		b, ok := res[n]
		if !ok {
			return nil, fmt.Errorf("resource %s/%s missing", ConversationResType, n)
		}
		out[n] = b
	}
	return out, nil
}
