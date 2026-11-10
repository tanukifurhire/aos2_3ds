package main

import (
	"bytes"
	"debug/pe"
	"encoding/binary"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/png"

	xdraw "golang.org/x/image/draw"
)

// gameIcon reads the application icon of aos2.exe (its RT_GROUP_ICON / RT_ICON resources; the PC game ships a
// single 48x48 24-bit icon, which is exactly the HOME menu's large icon size) and returns it at 48x48.
func gameIcon(exePath string) (image.Image, error) {
	f, err := pe.Open(exePath)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	s := f.Section(".rsrc")
	if s == nil {
		return nil, errors.New("aos2.exe has no resources")
	}
	data, err := s.Data()
	if err != nil {
		return nil, err
	}
	base := s.VirtualAddress
	u16 := func(o uint32) uint32 { return uint32(binary.LittleEndian.Uint16(data[o:])) }
	u32 := func(o uint32) uint32 { return binary.LittleEndian.Uint32(data[o:]) }
	type entry struct{ id, off uint32 }
	dir := func(off uint32) ([]entry, error) {
		if int(off)+16 > len(data) {
			return nil, errors.New("bad resource directory")
		}
		n := u16(off+12) + u16(off+14)
		var es []entry
		for i := uint32(0); i < n; i++ {
			p := off + 16 + i*8
			if int(p)+8 > len(data) {
				return nil, errors.New("bad resource directory")
			}
			es = append(es, entry{u32(p), u32(p + 4)})
		}
		return es, nil
	}
	leaf := func(off uint32) ([]byte, error) { // first language of a resource
		for off&0x80000000 != 0 {
			es, err := dir(off & 0x7fffffff)
			if err != nil || len(es) == 0 {
				return nil, errors.New("bad resource entry")
			}
			off = es[0].off
		}
		rva, size := u32(off), u32(off+4)
		if rva < base || int(rva-base+size) > len(data) {
			return nil, errors.New("bad resource data")
		}
		return data[rva-base : rva-base+size], nil
	}
	types, err := dir(0)
	if err != nil {
		return nil, err
	}
	icons := map[uint32][]byte{}
	var group []byte
	for _, t := range types {
		if t.off&0x80000000 == 0 || (t.id != 3 && t.id != 14) {
			continue
		}
		es, err := dir(t.off & 0x7fffffff)
		if err != nil {
			return nil, err
		}
		for _, e := range es {
			b, err := leaf(e.off)
			if err != nil {
				return nil, err
			}
			if t.id == 3 {
				icons[e.id] = b
			} else if group == nil {
				group = b
			}
		}
	}
	if len(group) < 6 {
		return nil, errors.New("aos2.exe has no icon")
	}
	// pick the largest image of the first icon group
	var best []byte
	bestW := 0
	for i := 0; i < int(binary.LittleEndian.Uint16(group[4:])) && 6+14*i+14 <= len(group); i++ {
		e := group[6+14*i:]
		w := int(e[0])
		if w == 0 {
			w = 256
		}
		if b := icons[uint32(binary.LittleEndian.Uint16(e[12:]))]; b != nil && w > bestW {
			best, bestW = b, w
		}
	}
	if best == nil {
		return nil, errors.New("aos2.exe icon group has no images")
	}
	var m image.Image
	if bytes.HasPrefix(best, []byte("\x89PNG")) {
		m, err = png.Decode(bytes.NewReader(best))
	} else {
		m, err = decodeIconDIB(best)
	}
	if err != nil {
		return nil, err
	}
	if b := m.Bounds(); b.Dx() == 48 && b.Dy() == 48 {
		return m, nil
	}
	out := image.NewNRGBA(image.Rect(0, 0, 48, 48))
	xdraw.CatmullRom.Scale(out, out.Bounds(), m, m.Bounds(), draw.Src, nil)
	return out, nil
}

// decodeIconDIB decodes an icon's BITMAPINFOHEADER image: 24- or 32-bit colour, bottom-up, followed by the 1-bit
// AND mask (1 = transparent; 32-bit icons carry their own alpha).
func decodeIconDIB(b []byte) (image.Image, error) {
	if len(b) < 40 {
		return nil, errors.New("short icon bitmap")
	}
	hs := int(binary.LittleEndian.Uint32(b))
	w := int(int32(binary.LittleEndian.Uint32(b[4:])))
	h := int(int32(binary.LittleEndian.Uint32(b[8:]))) / 2 // colour + mask
	bpp := int(binary.LittleEndian.Uint16(b[14:]))
	if bpp != 24 && bpp != 32 || w <= 0 || h <= 0 {
		return nil, fmt.Errorf("unsupported %d-bit icon", bpp)
	}
	row := (w*bpp + 31) / 32 * 4
	mrow := (w + 31) / 32 * 4
	if hs+row*h+mrow*h > len(b) {
		return nil, errors.New("truncated icon bitmap")
	}
	m := image.NewNRGBA(image.Rect(0, 0, w, h))
	px, mask := b[hs:], b[hs+row*h:]
	for y := 0; y < h; y++ {
		r, mr := px[(h-1-y)*row:], mask[(h-1-y)*mrow:]
		for x := 0; x < w; x++ {
			c := r[x*bpp/8:]
			a := uint8(255)
			if bpp == 32 {
				a = c[3]
			} else if mr[x>>3]>>(7-x&7)&1 == 1 {
				a = 0
			}
			m.SetNRGBA(x, y, color.NRGBA{c[2], c[1], c[0], a})
		}
	}
	return m, nil
}
