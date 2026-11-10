package main

import (
	"bytes"
	"debug/pe"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"strconv"

	"golang.org/x/arch/x86/x86asm"
)

var f *pe.File
var text []byte
var textVA uint32
var base uint32

func load(p string) {
	var err error
	f, err = pe.Open(p)
	if err != nil {
		panic(err)
	}
	base = f.OptionalHeader.(*pe.OptionalHeader32).ImageBase
	for _, s := range f.Sections {
		if s.Name == ".text" {
			text, _ = s.Data()
			textVA = base + s.VirtualAddress
		}
	}
}

func strVA(s string) uint32 {
	for _, sec := range f.Sections {
		d, _ := sec.Data()
		if i := bytes.Index(d, []byte(s)); i >= 0 {
			return base + sec.VirtualAddress + uint32(i)
		}
	}
	return 0
}

// find all code locations referencing imm32 va
func refs(va uint32) []uint32 {
	var out []uint32
	var k [4]byte
	binary.LittleEndian.PutUint32(k[:], va)
	for i := 0; ; {
		j := bytes.Index(text[i:], k[:])
		if j < 0 {
			break
		}
		out = append(out, textVA+uint32(i+j))
		i += j + 1
	}
	return out
}

func dis(va uint32, n int) {
	off := int(va - textVA)
	for c := 0; c < n && off < len(text); c++ {
		in, err := x86asm.Decode(text[off:], 32)
		if err != nil {
			fmt.Printf("%08x  ??\n", textVA+uint32(off))
			off++
			continue
		}
		fmt.Printf("%08x  %-24x %s\n", textVA+uint32(off), text[off:off+in.Len], x86asm.IntelSyntax(in, uint64(textVA)+uint64(off), nil))
		off += in.Len
	}
}

func main() {
	load(os.Args[1])
	switch os.Args[2] {
	case "str":
		v := strVA(os.Args[3])
		fmt.Printf("string at %08x\n", v)
		for _, r := range refs(v) {
			fmt.Printf("ref at %08x\n", r)
		}
	case "refs":
		v, _ := strconv.ParseUint(os.Args[3], 16, 32)
		for _, r := range refs(uint32(v)) {
			fmt.Printf("ref at %08x\n", r)
		}
	case "rd": // rd <va> <count>: dump dwords as hex, int, float, double(pair)
		v, _ := strconv.ParseUint(os.Args[3], 16, 32)
		n, _ := strconv.Atoi(os.Args[4])
		for i := 0; i < n; i++ {
			b := readVA(uint32(v)+uint32(4*i), 8)
			if b == nil {
				fmt.Printf("%08x  (unmapped or bss)\n", uint32(v)+uint32(4*i))
				continue
			}
			u := binary.LittleEndian.Uint32(b)
			fmt.Printf("%08x  %08x  int=%d  float=%g  double=%g\n", uint32(v)+uint32(4*i), u, int32(u), math.Float32frombits(u), math.Float64frombits(binary.LittleEndian.Uint64(b)))
		}
	case "dis":
		v, _ := strconv.ParseUint(os.Args[3], 16, 32)
		n, _ := strconv.Atoi(os.Args[4])
		dis(uint32(v), n)
	}
}

// readVA returns n bytes at virtual address va from file-backed section data
// (nil for BSS / uninitialised data, which only has runtime values).
func readVA(va uint32, n int) []byte {
	for _, s := range f.Sections {
		lo := base + s.VirtualAddress
		if va >= lo && va+uint32(n) <= lo+s.Size {
			d, _ := s.Data()
			return d[va-lo : va-lo+uint32(n)]
		}
	}
	return nil
}
