package main

// Fonts and single menu textures for the menus (docs/re/modes.md §A.1, A.7).
//
// Fonts: the original BMFont v3 binary files (extracted/font/en/<family>_1080.fnt)
// are repacked for 400x240: only the glyphs used by the shipped Latin texts
// (script/en, ptbr, sp) plus ASCII, each glyph resampled (Catmull-Rom, exact
// sub-pixel placement) to the 3DS on-screen size so the device draws them at
// 1:1, packed into one power-of-two page. Output: romfs/font/<family>.fnt (a
// valid BMFont v3 binary with the new metrics) and gfx/font_<family>/page0.png
// + .t3s (LA8, converted by tools/convert_pages.sh to romfs/gfx/font_<family>_0.t3x).
// Metrics: integer, scaled by f = target/lineHeight; the outline width is
// folded into xadvance (the original adds (int)(outline*sx) per glyph).
//
// Menu textures: single images (crop + scale) -> gfx/m3_<name>/page0.png +
// .t3s -> romfs/gfx/m3_<name>_0.t3x, loaded one by one by src/app/mgfx.c.

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"image"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"golang.org/x/image/draw"
	"golang.org/x/image/math/f64"
)

type fontSpec struct {
	family string
	target int // on-screen line height in px at scale 1.0
}

// Must match font_id in src/app/text.h.
var fontSpecs = []fontSpec{
	{"system", 16},
	{"menu", 18},
	{"menubig", 20},
	{"menulabel", 12},
	{"menusubtitle", 14},
	{"winquote", 16},
	{"dialogue", 16},
}

type bmChar struct {
	id               uint32
	x, y, w, h       int
	xoff, yoff, xadv int
	page, chnl       int
	img              *image.NRGBA // repacked glyph
}

type bmFont struct {
	info       []byte // raw info block (font size patched)
	infoSize   int16
	outline    int
	lineHeight int
	base       int
	scaleW     int
	scaleH     int
	pages      []string
	chars      []*bmChar
	kerns      [][3]int // first, second, amount
	commonTail []byte   // bitField + channels (5 bytes)
}

func parseBMFont(b []byte) (*bmFont, error) {
	if len(b) < 4 || string(b[:3]) != "BMF" || b[3] != 3 {
		return nil, fmt.Errorf("not a BMFont v3 binary")
	}
	f := &bmFont{}
	p := 4
	le := binary.LittleEndian
	for p+5 <= len(b) {
		typ := b[p]
		sz := int(le.Uint32(b[p+1:]))
		p += 5
		if p+sz > len(b) {
			return nil, fmt.Errorf("block %d truncated", typ)
		}
		blk := b[p : p+sz]
		switch typ {
		case 1:
			f.info = append([]byte(nil), blk...)
			f.infoSize = int16(le.Uint16(blk[0:]))
			f.outline = int(blk[13])
		case 2:
			f.lineHeight = int(le.Uint16(blk[0:]))
			f.base = int(le.Uint16(blk[2:]))
			f.scaleW = int(le.Uint16(blk[4:]))
			f.scaleH = int(le.Uint16(blk[6:]))
			f.commonTail = append([]byte(nil), blk[10:15]...)
		case 3:
			for _, s := range strings.Split(string(blk), "\x00") {
				if s != "" {
					f.pages = append(f.pages, s)
				}
			}
		case 4:
			for i := 0; i+20 <= len(blk); i += 20 {
				c := blk[i:]
				f.chars = append(f.chars, &bmChar{
					id: le.Uint32(c), x: int(le.Uint16(c[4:])), y: int(le.Uint16(c[6:])),
					w: int(le.Uint16(c[8:])), h: int(le.Uint16(c[10:])),
					xoff: int(int16(le.Uint16(c[12:]))), yoff: int(int16(le.Uint16(c[14:]))),
					xadv: int(int16(le.Uint16(c[16:]))), page: int(c[18]), chnl: int(c[19]),
				})
			}
		case 5:
			for i := 0; i+10 <= len(blk); i += 10 {
				k := blk[i:]
				f.kerns = append(f.kerns, [3]int{int(le.Uint32(k)), int(le.Uint32(k[4:])), int(int16(le.Uint16(k[8:])))})
			}
		}
		p += sz
	}
	return f, nil
}

func (f *bmFont) write(pageName string) []byte {
	var w bytes.Buffer
	le := binary.LittleEndian
	w.WriteString("BMF\x03")
	blk := func(t byte, data []byte) {
		w.WriteByte(t)
		var n [4]byte
		le.PutUint32(n[:], uint32(len(data)))
		w.Write(n[:])
		w.Write(data)
	}
	info := append([]byte(nil), f.info...)
	le.PutUint16(info[0:], uint16(f.infoSize))
	info[13] = 0 // outline folded into xadvance
	blk(1, info)
	common := make([]byte, 15)
	le.PutUint16(common[0:], uint16(f.lineHeight))
	le.PutUint16(common[2:], uint16(f.base))
	le.PutUint16(common[4:], uint16(f.scaleW))
	le.PutUint16(common[6:], uint16(f.scaleH))
	le.PutUint16(common[8:], 1)
	copy(common[10:], f.commonTail)
	blk(2, common)
	blk(3, append([]byte(pageName), 0))
	var cs bytes.Buffer
	for _, c := range f.chars {
		var r [20]byte
		le.PutUint32(r[0:], c.id)
		le.PutUint16(r[4:], uint16(c.x))
		le.PutUint16(r[6:], uint16(c.y))
		le.PutUint16(r[8:], uint16(c.w))
		le.PutUint16(r[10:], uint16(c.h))
		le.PutUint16(r[12:], uint16(int16(c.xoff)))
		le.PutUint16(r[14:], uint16(int16(c.yoff)))
		le.PutUint16(r[16:], uint16(int16(c.xadv)))
		r[18] = 0
		r[19] = 15
		cs.Write(r[:])
	}
	blk(4, cs.Bytes())
	if len(f.kerns) > 0 {
		var ks bytes.Buffer
		for _, k := range f.kerns {
			var r [10]byte
			le.PutUint32(r[0:], uint32(k[0]))
			le.PutUint32(r[4:], uint32(k[1]))
			le.PutUint16(r[8:], uint16(int16(k[2])))
			ks.Write(r[:])
		}
		blk(5, ks.Bytes())
	}
	return w.Bytes()
}

func loadPNG(path string) (image.Image, error) {
	fh, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer fh.Close()
	return png.Decode(fh)
}

func savePNG(path string, img image.Image) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	o, err := os.Create(path)
	if err != nil {
		return err
	}
	defer o.Close()
	return png.Encode(o, img)
}

// textCodepoints collects the code points of every script file of the given languages.
func textCodepoints(assets string, langs []string) map[rune]bool {
	cps := map[rune]bool{}
	for r := rune(32); r < 127; r++ {
		cps[r] = true
	}
	for _, l := range langs {
		ents, _ := os.ReadDir(filepath.Join(assets, "script", l))
		for _, e := range ents {
			b, err := os.ReadFile(filepath.Join(assets, "script", l, e.Name()))
			if err != nil {
				continue
			}
			for _, r := range string(b) {
				if r >= 32 && r != 0xfeff {
					cps[r] = true
				}
			}
		}
	}
	return cps
}

func nextPow2(v int) int {
	p := 8
	for p < v {
		p <<= 1
	}
	return p
}

func packFont(assets, romfs, gfx string, spec fontSpec, cps map[rune]bool) (int, int, error) {
	dir := filepath.Join(assets, "font", "en")
	raw, err := os.ReadFile(filepath.Join(dir, spec.family+"_1080.fnt"))
	if err != nil {
		return 0, 0, err
	}
	f, err := parseBMFont(raw)
	if err != nil {
		return 0, 0, err
	}
	pages := make([]image.Image, len(f.pages))
	for i, p := range f.pages {
		if pages[i], err = loadPNG(filepath.Join(dir, p)); err != nil {
			return 0, 0, err
		}
	}
	sc := float64(spec.target) / float64(f.lineHeight)
	var keep []*bmChar
	for _, c := range f.chars {
		if !cps[rune(c.id)] || c.page >= len(pages) {
			continue
		}
		n := &bmChar{id: c.id}
		n.xadv = int(math.Round(float64(c.xadv+f.outline) * sc))
		if c.w > 0 && c.h > 0 {
			x0 := math.Floor(float64(c.xoff) * sc)
			y0 := math.Floor(float64(c.yoff) * sc)
			x1 := math.Ceil(float64(c.xoff+c.w) * sc)
			y1 := math.Ceil(float64(c.yoff+c.h) * sc)
			n.xoff, n.yoff = int(x0), int(y0)
			n.w, n.h = int(x1-x0), int(y1-y0)
			n.img = image.NewNRGBA(image.Rect(0, 0, n.w, n.h))
			// dst = sc*(src - c.x + c.xoff) - x0
			s2d := f64.Aff3{sc, 0, sc*float64(c.xoff-c.x) - x0, 0, sc, sc*float64(c.yoff-c.y) - y0}
			sr := image.Rect(c.x, c.y, c.x+c.w, c.y+c.h)
			draw.CatmullRom.Transform(n.img, s2d, pages[c.page], sr, draw.Src, nil)
		}
		keep = append(keep, n)
	}
	sort.Slice(keep, func(i, j int) bool { return keep[i].id < keep[j].id })
	// shelf pack by height, 1 px gap; smallest pow2 square-ish page that fits
	order := append([]*bmChar(nil), keep...)
	sort.SliceStable(order, func(i, j int) bool { return order[i].h > order[j].h })
	var pw, ph int
	for pw = 64; pw <= 1024; pw *= 2 {
		x, y, rowH := 1, 1, 0
		ok := true
		for _, c := range order {
			if c.img == nil {
				continue
			}
			if x+c.w+1 > pw {
				x, y, rowH = 1, y+rowH+1, 0
			}
			c.x, c.y = x, y
			x += c.w + 1
			if c.h > rowH {
				rowH = c.h
			}
		}
		ph = nextPow2(y + rowH + 1)
		if ph <= pw && ok {
			break
		}
	}
	if pw > 1024 {
		return 0, 0, fmt.Errorf("%s: glyphs do not fit one 1024 page", spec.family)
	}
	page := image.NewNRGBA(image.Rect(0, 0, pw, ph))
	for _, c := range order {
		if c.img != nil {
			draw.Copy(page, image.Pt(c.x, c.y), c.img, c.img.Bounds(), draw.Src, nil)
		} else {
			c.x, c.y = 0, 0
		}
	}
	nf := &bmFont{info: f.info, infoSize: int16(math.Round(float64(f.infoSize) * sc)),
		lineHeight: spec.target, base: int(math.Round(float64(f.base) * sc)),
		scaleW: pw, scaleH: ph, chars: keep, commonTail: f.commonTail}
	for _, k := range f.kerns {
		if cps[rune(k[0])] && cps[rune(k[1])] {
			if a := int(math.Round(float64(k[2]) * sc)); a != 0 {
				nf.kerns = append(nf.kerns, [3]int{k[0], k[1], a})
			}
		}
	}
	folder := "font_" + spec.family
	if err := savePNG(filepath.Join(gfx, folder, "page0.png"), page); err != nil {
		return 0, 0, err
	}
	if err := os.WriteFile(filepath.Join(gfx, folder, "page0.t3s"), []byte("-f la8 -z auto\npage0.png\n"), 0o644); err != nil {
		return 0, 0, err
	}
	if err := os.MkdirAll(filepath.Join(romfs, "font"), 0o755); err != nil {
		return 0, 0, err
	}
	if err := os.WriteFile(filepath.Join(romfs, "font", spec.family+".fnt"), nf.write(folder+"_0.t3x"), 0o644); err != nil {
		return 0, 0, err
	}
	return len(keep), pw * ph, nil
}

func packFonts(assets, romfs, gfx string) error {
	cps := textCodepoints(assets, []string{"en", "ptbr", "sp"})
	for _, s := range fontSpecs {
		n, px, err := packFont(assets, romfs, gfx, s, cps)
		if err != nil {
			return err
		}
		fmt.Printf("font %-14s %3d glyphs  page %d px (LA8 %d KB)\n", s.family, n, px, px*2/1024)
	}
	return nil
}

// ---------------------------------------------------------------- menu textures

type menuTex struct {
	name   string // romfs:/gfx/m3_<name>_0.t3x
	src    string // relative to texture/
	crop   image.Rectangle
	scale  float64
	format string // tex3ds -f
}

// Crops are in original texture pixels; src/app/mgfx.c knows the origins.
var menuTexs = []menuTex{
	{"logo", "menuMdl/ttl_logo.png", image.Rect(0, 0, 1024, 1024), 0.25, "etc1a4"},       // drawn 204 px
	{"daidai", "systemAddMdl/daidai.png", image.Rect(0, 0, 2048, 2048), 0.125, "etc1a4"}, // drawn 240 px
	{"shinden", "systemAddMdl/shinden.png", image.Rect(0, 0, 2048, 2048), 0.125, "etc1a4"},
	{"pills", "systemAddMdl/ttl_menu.png", image.Rect(0x2d0, 0, 0x350, 0xc0), 1, "rgba4"}, // header pills
	{"arrows", "systemAddMdl/arrows.png", image.Rect(0, 0x5e, 0x78, 0x112), 0.5, "rgba4"}, // config scroll arrows
}

func init() {
	for i := 1; i <= 9; i++ { // How to Play pages, one texture each (loaded one at a time)
		menuTexs = append(menuTexs, menuTex{fmt.Sprintf("manual_en_%02d", i), fmt.Sprintf("manual/eng_%02d.png", i),
			image.Rect(0, 0, 1024, 1024), 0.5, "rgb565"})
	}
}

func packMenuTextures(assets, gfx string) error {
	for _, t := range menuTexs {
		img, err := loadPNG(filepath.Join(assets, "texture", t.src))
		if err != nil {
			return err
		}
		w := int(math.Round(float64(t.crop.Dx()) * t.scale))
		h := int(math.Round(float64(t.crop.Dy()) * t.scale))
		dst := image.NewNRGBA(image.Rect(0, 0, nextPow2(w), nextPow2(h)))
		draw.CatmullRom.Scale(dst, image.Rect(0, 0, w, h), img, t.crop, draw.Src, nil)
		folder := "m3_" + t.name
		if err := savePNG(filepath.Join(gfx, folder, "page0.png"), dst); err != nil {
			return err
		}
		t3s := fmt.Sprintf("-f %s -z auto\npage0.png\n", t.format)
		if err := os.WriteFile(filepath.Join(gfx, folder, "page0.t3s"), []byte(t3s), 0o644); err != nil {
			return err
		}
	}
	fmt.Printf("menu textures: %d\n", len(menuTexs))
	return nil
}

// copyScripts copies the Latin-language text files (the ones the packed glyph
// set covers) to romfs/script/<lang>/ for src/app/strings.c.
func copyScripts(assets, romfs string) error {
	n := 0
	for _, l := range []string{"en", "ptbr", "sp"} {
		ents, err := os.ReadDir(filepath.Join(assets, "script", l))
		if err != nil {
			return err
		}
		if err := os.MkdirAll(filepath.Join(romfs, "script", l), 0o755); err != nil {
			return err
		}
		for _, e := range ents {
			b, err := os.ReadFile(filepath.Join(assets, "script", l, e.Name()))
			if err != nil {
				return err
			}
			if err := os.WriteFile(filepath.Join(romfs, "script", l, e.Name()), b, 0o644); err != nil {
				return err
			}
			n++
		}
	}
	fmt.Printf("scripts: %d files\n", n)
	return nil
}
