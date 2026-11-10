// aos2mesh converts the AoS2 stage assets (docs/re/stages.md) for the 3DS build
// and for the host stage preview:
//
//   - mdl/*.x (DirectX text meshes) -> <romfs>/stage/mdl/<name>.bin
//     ("SGM1", one record per material subset: texture name, diffuse RGBA,
//     float pos[3] normal[3] uv[2] vertices, u16 triangle indices). Faces are
//     fan-triangulated like D3DXLoadMeshFromX; vertices are split per
//     (position, normal, uv) like D3DX does for MeshNormals face indices.
//   - mdl/3dtex.dat ("LAG" 16-bit RGBA textures the .x materials reference by
//     their Shift-JIS file name, matched with the engine's byte-wise tolower)
//     -> PNG -> <romfs>/stage/tex/3d/xNN.t3x
//   - texture/bgMdl, texture/bgMdlAdd PNGs -> <romfs>/stage/tex/<folder>/<name>.t3x
//     (power-of-two, mipmapped; ETC1 when opaque, ETC1A4 otherwise)
//   - height maps (dr_hightsand, dr_hightrock, gr_hightrock) -> <romfs>/stage/hf/<name>.bin
//     (u32 w, u32 h, then the red byte of every pixel, as FUN_00469450 reads it)
//   - with -host DIR: raw "w h RGBA" copies of every texture for tests/stagepreview.c
//
// Usage: aos2mesh -game <AoS2 dir> -tex <extracted/texture> -romfs build/romfs
//
//	[-host build/stagehost] [-tex3ds path/to/tex3ds.exe] [-work build/stagetex]
package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"log"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
)

type atlasGroup struct {
	folder, name string
	members      []string
}

var atlasGroups = []atlasGroup{
	{"bgMdl", "atlas_cy_smoke", []string{"cy_smoke_00", "cy_smoke_01", "cy_smoke_02", "cy_smoke_03"}},
	{"bgMdl", "atlas_cy_flame", []string{"cy_flame00", "cy_flame01", "cy_flame02", "cy_flame03", "cy_flame04", "cy_flame05"}},
	{"bgMdl", "atlas_sp_noise", []string{"sp_noise00", "sp_noise01", "sp_noise02", "sp_noise03"}},
}

var heightMaps =map[string]bool{"dr_hightsand": true, "dr_hightrock": true, "gr_hightrock": true}

func main() {
	game := flag.String("game", "", "AoS2 install dir")
	texDir := flag.String("tex", "", "extracted/texture dir (decrypted PNGs)")
	romfs := flag.String("romfs", "", "romfs output root")
	host := flag.String("host", "", "optional host-preview output dir")
	tex3ds := flag.String("tex3ds", "", "tex3ds executable (empty: skip .t3x)")
	work := flag.String("work", "build/stagetex", "staging dir for converted PNGs")
	flag.IntVar(&texDiv, "texdiv", 1, "divide stage texture sizes by this (2 = half resolution; docs/PERF.md memory plan)")
	flag.Parse()
	if *game == "" || *texDir == "" || *romfs == "" {
		flag.Usage()
		os.Exit(2)
	}
	lag := readLAG(filepath.Join(*game, "mdl", "3dtex.dat"))
	type job struct{ folder, name string; img image.Image }
	var jobs []job
	// .x models
	xnames := map[string]string{} // lowercased LAG key -> ascii texture name
	for i, e := range lag {
		xnames[e.key] = fmt.Sprintf("x%02d", i)
		jobs = append(jobs, job{"3d", fmt.Sprintf("x%02d", i), e.img})
	}
	must(os.MkdirAll(filepath.Join(*romfs, "stage", "mdl"), 0o755))
	ents, err := os.ReadDir(filepath.Join(*game, "mdl"))
	must(err)
	for _, e := range ents {
		n := e.Name()
		if !strings.EqualFold(filepath.Ext(n), ".x") {
			continue
		}
		base := strings.ToLower(strings.TrimSuffix(n, filepath.Ext(n)))
		data, err := os.ReadFile(filepath.Join(*game, "mdl", n))
		must(err)
		m := parseX(data)
		out := encodeModel(m, xnames)
		must(os.WriteFile(filepath.Join(*romfs, "stage", "mdl", base+".bin"), out, 0o644))
		nt := 0
		for _, s := range m.subsets {
			nt += len(s.idx) / 3
		}
		fmt.Printf("aos2mesh: %s: %d subsets, %d triangles\n", base, len(m.subsets), nt)
	}
	// textures + height maps
	must(os.MkdirAll(filepath.Join(*romfs, "stage", "hf"), 0o755))
	for _, folder := range []string{"bgMdl", "bgMdlAdd"} {
		fs, err := os.ReadDir(filepath.Join(*texDir, folder))
		must(err)
		for _, f := range fs {
			if !strings.HasSuffix(f.Name(), ".png") {
				continue
			}
			name := strings.TrimSuffix(f.Name(), ".png")
			img := loadPNG(filepath.Join(*texDir, folder, f.Name()))
			if heightMaps[name] {
				writeHeightMap(filepath.Join(*romfs, "stage", "hf", name+".bin"), img)
				continue
			}
			jobs = append(jobs, job{folder, name, img})
		}
	}
	// Sprite animations that a stage draws interleaved (one billboard per frame image)
	// are also packed into one atlas each, so the 3DS backend can batch them
	// (stages.md §5). Index: <romfs>/stage/tex/atlas.txt "folder/name atlas u0 v0 u1 v1".
	var index strings.Builder
	for _, grp := range atlasGroups {
		var imgs []image.Image
		for _, n := range grp.members {
			for _, j := range jobs {
				if j.folder == grp.folder && j.name == n {
					imgs = append(imgs, j.img)
				}
			}
		}
		if len(imgs) != len(grp.members) {
			log.Fatalf("atlas %s: missing member", grp.name)
		}
		cw, ch := imgs[0].Bounds().Dx(), imgs[0].Bounds().Dy()
		cols := 2
		rows := (len(imgs) + cols - 1) / cols
		aw, ah := nextPOT(cw*cols), nextPOT(ch*rows)
		atlas := image.NewNRGBA(image.Rect(0, 0, aw, ah))
		for i, im := range imgs {
			x0, y0 := i%cols*cw, i/cols*ch
			b := im.Bounds()
			for y := 0; y < ch; y++ {
				for x := 0; x < cw; x++ {
					atlas.Set(x0+x, y0+y, im.At(b.Min.X+x, b.Min.Y+y))
				}
			}
			fmt.Fprintf(&index, "%s/%s %s %g %g %g %g\n", grp.folder, grp.members[i], grp.name,
				float64(x0)/float64(aw), float64(y0)/float64(ah), float64(x0+cw)/float64(aw), float64(y0+ch)/float64(ah))
		}
		jobs = append(jobs, job{grp.folder, grp.name, atlas})
	}
	must(os.MkdirAll(filepath.Join(*romfs, "stage", "tex"), 0o755))
	must(os.WriteFile(filepath.Join(*romfs, "stage", "tex", "atlas.txt"), []byte(index.String()), 0o644))
	var wg sync.WaitGroup
	sem := make(chan bool, runtime.NumCPU())
	var mu sync.Mutex
	failed := 0
	for _, j := range jobs {
		wg.Add(1)
		sem <- true
		go func(folder, name string, img image.Image) {
			defer func() { <-sem; wg.Done() }()
			if *host != "" {
				writeRGBA(filepath.Join(*host, "tex", folder, name+".rgba"), img)
			}
			if *tex3ds == "" {
				return
			}
			pot := toPOT(img)
			stage := filepath.Join(*work, folder, name+".png")
			dst := filepath.Join(*romfs, "stage", "tex", folder, name+".t3x")
			must(os.MkdirAll(filepath.Dir(stage), 0o755))
			must(os.MkdirAll(filepath.Dir(dst), 0o755))
			var buf bytes.Buffer
			must(png.Encode(&buf, pot))
			if old, err := os.ReadFile(stage); err == nil && bytes.Equal(old, buf.Bytes()) {
				if _, err := os.Stat(dst); err == nil {
					return // unchanged
				}
			}
			must(os.WriteFile(stage, buf.Bytes(), 0o644))
			format := "etc1a4"
			if opaque(pot) {
				format = "etc1"
			}
			cmd := exec.Command(*tex3ds, "-f", format, "-m", "linear", "-z", "auto", "-o", dst, stage)
			if out, err := cmd.CombinedOutput(); err != nil {
				mu.Lock()
				failed++
				log.Printf("tex3ds %s/%s: %v %s", folder, name, err, out)
				mu.Unlock()
			}
		}(j.folder, j.name, j.img)
	}
	wg.Wait()
	if failed > 0 {
		log.Fatalf("%d textures failed", failed)
	}
	fmt.Printf("aos2mesh: %d stage textures\n", len(jobs))
}

func must(err error) {
	if err != nil {
		log.Fatal(err)
	}
}

func loadPNG(path string) image.Image {
	f, err := os.Open(path)
	must(err)
	defer f.Close()
	img, err := png.Decode(f)
	must(err)
	return img
}

// ---- LAG (mdl/3dtex.dat) ----

type lagEntry struct {
	key string // byte-wise lower-cased Shift-JIS name (engine lookup key)
	img image.Image
}

// readLAG: "LAG\0", u32 0, then { char name[16]; u32 w, h, fmt(=2), size; u16 R,G,B,A per pixel }.
func readLAG(path string) []lagEntry {
	d, err := os.ReadFile(path)
	must(err)
	if string(d[:4]) != "LAG\x00" {
		log.Fatalf("%s: not a LAG file", path)
	}
	var out []lagEntry
	for o := 8; o+32 <= len(d); {
		name := d[o : o+16]
		if i := bytes.IndexByte(name, 0); i >= 0 {
			name = name[:i]
		}
		w := int(binary.LittleEndian.Uint32(d[o+16:]))
		h := int(binary.LittleEndian.Uint32(d[o+20:]))
		size := int(binary.LittleEndian.Uint32(d[o+28:]))
		px := d[o+32 : o+32+size]
		img := image.NewNRGBA(image.Rect(0, 0, w, h))
		for i := 0; i < w*h; i++ {
			for c := 0; c < 4; c++ {
				img.Pix[i*4+c] = px[i*8+c*2+1] // high byte of each 16-bit channel
			}
		}
		out = append(out, lagEntry{lowerBytes(string(name)), img})
		o += 32 + size
	}
	return out
}

// lowerBytes mimics the engine's tolower over raw bytes (it also hits
// Shift-JIS trail bytes, which is why 3dtex.dat names look garbled).
func lowerBytes(s string) string {
	b := []byte(s)
	for i, c := range b {
		if c >= 'A' && c <= 'Z' {
			b[i] = c + 32
		}
	}
	return string(b)
}

// ---- height maps ----

func writeHeightMap(path string, img image.Image) {
	b := img.Bounds()
	out := make([]byte, 8+b.Dx()*b.Dy())
	binary.LittleEndian.PutUint32(out, uint32(b.Dx()))
	binary.LittleEndian.PutUint32(out[4:], uint32(b.Dy()))
	for y := 0; y < b.Dy(); y++ {
		for x := 0; x < b.Dx(); x++ {
			r, _, _, _ := img.At(b.Min.X+x, b.Min.Y+y).RGBA()
			out[8+y*b.Dx()+x] = byte(r >> 8)
		}
	}
	must(os.WriteFile(path, out, 0o644))
}

// ---- textures ----

func nextPOT(n int) int {
	p := 8
	for p < n && p < 1024 {
		p *= 2
	}
	return p
}

// toPOT resamples to power-of-two dimensions (bilinear). UVs are normalized,
// so a resampled texture maps identically.
var texDiv = 1

func toPOT(img image.Image) *image.NRGBA {
	b := img.Bounds()
	w, h := nextPOT(b.Dx()), nextPOT(b.Dy())
	r := toPOTsize(img, w, h)
	for d := texDiv; d > 1 && w > 16 && h > 16; d /= 2 { // halving (2:1 bilinear = 2x2 box) per step
		w, h = w/2, h/2
		r = toPOTsize(r, w, h)
	}
	return r
}

func toPOTsize(img image.Image, w, h int) *image.NRGBA {
	b := img.Bounds()
	src := image.NewNRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	for y := 0; y < b.Dy(); y++ {
		for x := 0; x < b.Dx(); x++ {
			src.Set(x, y, img.At(b.Min.X+x, b.Min.Y+y))
		}
	}
	if w == b.Dx() && h == b.Dy() {
		return src
	}
	dst := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		fy := (float64(y)+0.5)*float64(b.Dy())/float64(h) - 0.5
		for x := 0; x < w; x++ {
			fx := (float64(x)+0.5)*float64(b.Dx())/float64(w) - 0.5
			var acc [4]float64
			x0, y0 := int(math.Floor(fx)), int(math.Floor(fy))
			for k := 0; k < 4; k++ {
				sx, sy := x0+k&1, y0+k>>1
				wgt := (1 - math.Abs(float64(sx)-fx)) * (1 - math.Abs(float64(sy)-fy))
				sx = clampi(sx, 0, b.Dx()-1)
				sy = clampi(sy, 0, b.Dy()-1)
				p := src.Pix[sy*src.Stride+sx*4:]
				for c := 0; c < 4; c++ {
					acc[c] += wgt * float64(p[c])
				}
			}
			q := dst.Pix[y*dst.Stride+x*4:]
			for c := 0; c < 4; c++ {
				q[c] = uint8(math.Max(0, math.Min(255, math.Round(acc[c]))))
			}
		}
	}
	return dst
}

func clampi(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func opaque(img *image.NRGBA) bool {
	for i := 3; i < len(img.Pix); i += 4 {
		if img.Pix[i] != 255 {
			return false
		}
	}
	return true
}

func writeRGBA(path string, img image.Image) {
	must(os.MkdirAll(filepath.Dir(path), 0o755))
	b := img.Bounds()
	out := make([]byte, 8, 8+b.Dx()*b.Dy()*4)
	binary.LittleEndian.PutUint32(out, uint32(b.Dx()))
	binary.LittleEndian.PutUint32(out[4:], uint32(b.Dy()))
	for y := 0; y < b.Dy(); y++ {
		for x := 0; x < b.Dx(); x++ {
			c := color.NRGBAModel.Convert(img.At(b.Min.X+x, b.Min.Y+y)).(color.NRGBA)
			out = append(out, c.R, c.G, c.B, c.A)
		}
	}
	must(os.WriteFile(path, out, 0o644))
}

// ---- DirectX .x (text) ----

type material struct {
	diffuse [4]float32
	texture string // raw file name (Shift-JIS), "" = none
}

type subset struct {
	mat  material
	vtx  [][8]float32
	idx  []uint16
	seen map[[8]float32]uint16
}

type model struct{ subsets []*subset }

type xlexer struct {
	d   []byte
	pos int
}

// next returns the next token: identifiers/numbers, '{', '}', or a "string".
// ',' and ';' are separators.
func (l *xlexer) next() string {
	for l.pos < len(l.d) {
		c := l.d[l.pos]
		if c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';' {
			l.pos++
			continue
		}
		if c == '/' && l.pos+1 < len(l.d) && l.d[l.pos+1] == '/' || c == '#' {
			for l.pos < len(l.d) && l.d[l.pos] != '\n' {
				l.pos++
			}
			continue
		}
		break
	}
	if l.pos >= len(l.d) {
		return ""
	}
	c := l.d[l.pos]
	if c == '{' || c == '}' {
		l.pos++
		return string(c)
	}
	if c == '"' {
		e := bytes.IndexByte(l.d[l.pos+1:], '"')
		s := string(l.d[l.pos : l.pos+2+e])
		l.pos += 2 + e
		return s
	}
	s := l.pos
	for l.pos < len(l.d) {
		c = l.d[l.pos]
		if c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';' || c == '{' || c == '}' {
			break
		}
		l.pos++
	}
	return string(l.d[s:l.pos])
}

func (l *xlexer) num() float64 {
	t := l.next()
	v, err := strconv.ParseFloat(t, 64)
	if err != nil {
		log.Fatalf(".x: expected number, got %q", t)
	}
	return v
}

// skip skips the rest of a block whose '{' was consumed.
func (l *xlexer) skip() {
	depth := 1
	for depth > 0 {
		switch l.next() {
		case "{":
			depth++
		case "}":
			depth--
		case "":
			return
		}
	}
}

// open consumes an optional block name and the '{'.
func (l *xlexer) open() {
	t := l.next()
	if t != "{" {
		if l.next() != "{" {
			log.Fatalf(".x: expected {")
		}
	}
}

func parseX(d []byte) *model {
	l := &xlexer{d: d, pos: 16} // skip "xof 0302txt 0064"
	var m *model
	for {
		t := l.next()
		if t == "" {
			break
		}
		switch t {
		case "template":
			l.next()
			l.open()
			l.skip()
		case "Mesh":
			l.open()
			m = parseMesh(l)
		case "Frame":
			l.open() // frames only wrap meshes in these files; transforms are identity
		case "FrameTransformMatrix":
			l.open()
			l.skip()
		case "{", "}":
		default:
			if l.next() == "{" {
				l.skip()
			}
		}
	}
	if m == nil {
		log.Fatal(".x: no Mesh")
	}
	return m
}

func parseMesh(l *xlexer) *model {
	nv := int(l.num())
	pos := make([][3]float32, nv)
	for i := range pos {
		for k := 0; k < 3; k++ {
			pos[i][k] = float32(l.num())
		}
	}
	nf := int(l.num())
	faces := make([][]int, nf)
	for i := range faces {
		n := int(l.num())
		faces[i] = make([]int, n)
		for k := range faces[i] {
			faces[i][k] = int(l.num())
		}
	}
	var normals [][3]float32
	var nfaces [][]int
	var uvs [][2]float32
	var mats []material
	var faceMat []int
	for {
		t := l.next()
		if t == "}" || t == "" {
			break
		}
		switch t {
		case "MeshNormals":
			l.open()
			nn := int(l.num())
			normals = make([][3]float32, nn)
			for i := range normals {
				for k := 0; k < 3; k++ {
					normals[i][k] = float32(l.num())
				}
			}
			n2 := int(l.num())
			nfaces = make([][]int, n2)
			for i := range nfaces {
				n := int(l.num())
				nfaces[i] = make([]int, n)
				for k := range nfaces[i] {
					nfaces[i][k] = int(l.num())
				}
			}
			if l.next() != "}" {
				log.Fatal(".x: MeshNormals")
			}
		case "MeshTextureCoords":
			l.open()
			n := int(l.num())
			uvs = make([][2]float32, n)
			for i := range uvs {
				uvs[i][0] = float32(l.num())
				uvs[i][1] = float32(l.num())
			}
			if l.next() != "}" {
				log.Fatal(".x: MeshTextureCoords")
			}
		case "MeshMaterialList":
			l.open()
			nm := int(l.num())
			nfi := int(l.num())
			faceMat = make([]int, nfi)
			for i := range faceMat {
				faceMat[i] = int(l.num())
			}
			for len(mats) < nm {
				tt := l.next()
				if tt != "Material" {
					log.Fatalf(".x: expected Material, got %q", tt)
				}
				l.open()
				var mt material
				for k := 0; k < 4; k++ {
					mt.diffuse[k] = float32(l.num())
				}
				for k := 0; k < 7; k++ { // power, specular rgb, emissive rgb
					l.num()
				}
				for {
					tt = l.next()
					if tt == "}" {
						break
					}
					if tt == "TextureFilename" {
						l.open()
						s := l.next()
						mt.texture = strings.Trim(s, "\"")
						l.next() // }
					} else if l.next() == "{" {
						l.skip()
					}
				}
				mats = append(mats, mt)
			}
			if l.next() != "}" {
				log.Fatal(".x: MeshMaterialList")
			}
		default:
			if l.next() == "{" {
				l.skip()
			}
		}
	}
	if len(mats) == 0 {
		mats = []material{{diffuse: [4]float32{1, 1, 1, 1}}}
	}
	m := &model{}
	for _, mt := range mats {
		m.subsets = append(m.subsets, &subset{mat: mt, seen: map[[8]float32]uint16{}})
	}
	for f, face := range faces {
		mi := 0
		if f < len(faceMat) {
			mi = faceMat[f]
		} else if len(faceMat) > 0 {
			mi = faceMat[len(faceMat)-1] // .x allows a shorter list: the last entry repeats
		}
		s := m.subsets[mi]
		vid := make([]uint16, len(face))
		for k, p := range face {
			var v [8]float32
			copy(v[0:3], pos[p][:])
			if nfaces != nil {
				copy(v[3:6], normals[nfaces[f][k]][:])
			}
			if uvs != nil {
				v[6], v[7] = uvs[p][0], uvs[p][1]
			}
			id, ok := s.seen[v]
			if !ok {
				if len(s.vtx) >= 65535 {
					log.Fatal(".x: subset exceeds 65535 vertices")
				}
				id = uint16(len(s.vtx))
				s.vtx = append(s.vtx, v)
				s.seen[v] = id
			}
			vid[k] = id
		}
		for k := 1; k+1 < len(vid); k++ { // fan, D3DX order
			s.idx = append(s.idx, vid[0], vid[k], vid[k+1])
		}
	}
	return m
}

// encodeModel: "SGM1" u32 nsub; per subset: char tex[16], f32 diffuse[4],
// u32 nv, u32 ni, f32 vtx[nv][8], u16 idx[ni] (padded to 4 bytes).
func encodeModel(m *model, xnames map[string]string) []byte {
	var b bytes.Buffer
	b.WriteString("SGM1")
	binary.Write(&b, binary.LittleEndian, uint32(len(m.subsets)))
	for _, s := range m.subsets {
		var tex [16]byte
		if s.mat.texture != "" {
			base := s.mat.texture
			if i := strings.LastIndex(base, "\\"); i >= 0 {
				base = base[i+1:]
			}
			base = strings.TrimSuffix(strings.TrimSuffix(base, ".bmp"), ".BMP")
			name, ok := xnames[lowerBytes(base)]
			if !ok {
				log.Printf("aos2mesh: texture %q not in 3dtex.dat", base)
			}
			copy(tex[:], name)
		}
		b.Write(tex[:])
		binary.Write(&b, binary.LittleEndian, s.mat.diffuse)
		binary.Write(&b, binary.LittleEndian, uint32(len(s.vtx)))
		binary.Write(&b, binary.LittleEndian, uint32(len(s.idx)))
		for _, v := range s.vtx {
			binary.Write(&b, binary.LittleEndian, v)
		}
		binary.Write(&b, binary.LittleEndian, s.idx)
		if len(s.idx)%2 == 1 {
			b.Write([]byte{0, 0})
		}
	}
	return b.Bytes()
}
