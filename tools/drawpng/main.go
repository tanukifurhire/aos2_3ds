// drawpng renders draw-list dumps (tests/drawdump.c) to PNGs at the original
// 800x600 resolution using the extracted game textures, so draw code can be
// checked visually without a 3DS. Usage:
//
//	drawpng -in dump.tsv -tex ../../extracted/texture -out outdir [-scale 1]
//	drawpng -hud -in hud.tsv -tex ../../extracted/texture -out outdir [-zoom 2]   (hud.go)
package main

import (
	"bufio"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"log"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

const W, H = 800, 600

var charFolder = map[int]string{3: "charaSora", 4: "charaAlte", 5: "charaTsih", 6: "charaMira", 7: "charaSham",
	8: "charaNath", 9: "charaStar", 10: "charaSuguri", 11: "charaSaki", 12: "charaIru", 13: "charaNanako",
	14: "charaKae", 15: "charaKyoko", 16: "charaHime", 17: "charaSumika"}
var commonFolders = []string{"effectMdl", "beamMdl", "particleMdl", "msgMdl", "charaCmnA", "systemMdl", "charaAdd"}

type tex struct {
	img  *image.NRGBA
	w, h int
}

var texRoot string
var cache = map[string]*tex{}

func load(folder, name string) *tex {
	key := folder + "/" + name
	if t, ok := cache[key]; ok {
		return t
	}
	var t *tex
	if f, err := os.Open(filepath.Join(texRoot, folder, name+".png")); err == nil {
		if im, err := png.Decode(f); err == nil {
			b := im.Bounds()
			n := image.NewNRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
			for y := 0; y < b.Dy(); y++ {
				for x := 0; x < b.Dx(); x++ {
					n.Set(x, y, im.At(b.Min.X+x, b.Min.Y+y))
				}
			}
			t = &tex{n, b.Dx(), b.Dy()}
		}
		f.Close()
	}
	cache[key] = t
	return t
}

func playerFolder(id, col int) string {
	if id == 18 {
		return "charaAdd"
	}
	base := charFolder[id]
	for c := col; c >= 0; c-- {
		f := base + string(rune('A'+c))
		if _, err := os.Stat(filepath.Join(texRoot, f)); err == nil {
			return f
		}
	}
	return base + "A"
}

func find(name string, bank int, pf [2]string) *tex {
	if bank == 0 || bank == 1 {
		if t := load(pf[bank], name); t != nil {
			return t
		}
	}
	for _, f := range commonFolders {
		if t := load(f, name); t != nil {
			return t
		}
	}
	for _, f := range pf {
		if t := load(f, name); t != nil {
			return t
		}
	}
	return nil
}

type canvas struct{ px []float64 } // RGB premultiplied-ish accumulation in 0..1

func (c *canvas) blend(x, y int, r, g, b, a float64, mode int) {
	i := (y*W + x) * 3
	dr, dg, db := c.px[i], c.px[i+1], c.px[i+2]
	switch mode {
	case 0:
		dr, dg, db = r, g, b
	case 2: // SRCALPHA, ONE
		dr, dg, db = dr+r*a, dg+g*a, db+b*a
	case 5: // ONE, ONE
		dr, dg, db = dr+r, dg+g, db+b
	case 4: // multiply
		dr, dg, db = dr*r, dg*g, db*b
	case 6: // DESTCOLOR, ONE
		dr, dg, db = dr+dr*r, dg+dg*g, db+db*b
	case 7: // INVDESTCOLOR, ZERO
		dr, dg, db = (1-dr)*r, (1-dg)*g, (1-db)*b
	default: // alpha
		dr, dg, db = dr*(1-a)+r*a, dg*(1-a)+g*a, db*(1-a)+b*a
	}
	c.px[i], c.px[i+1], c.px[i+2] = math.Min(dr, 1), math.Min(dg, 1), math.Min(db, 1)
}

type tri struct {
	x, y  [3]float64
	c     [3]uint32
	blend int
}

func renderTri(cv *canvas, t tri, camX, camY, zoom float64) {
	var sx, sy [3]float64
	for k := 0; k < 3; k++ {
		sx[k] = W/2 + (t.x[k]-camX)/zoom
		sy[k] = H/2 - (t.y[k]-camY)/zoom
	}
	den := (sy[1]-sy[2])*(sx[0]-sx[2]) + (sx[2]-sx[1])*(sy[0]-sy[2])
	if math.Abs(den) < 1e-9 {
		return
	}
	x0 := int(math.Max(0, math.Min(sx[0], math.Min(sx[1], sx[2]))))
	x1 := int(math.Min(W-1, math.Max(sx[0], math.Max(sx[1], sx[2]))))
	y0 := int(math.Max(0, math.Min(sy[0], math.Min(sy[1], sy[2]))))
	y1 := int(math.Min(H-1, math.Max(sy[0], math.Max(sy[1], sy[2]))))
	for y := y0; y <= y1; y++ {
		for x := x0; x <= x1; x++ {
			px, py := float64(x)+0.5, float64(y)+0.5
			l0 := ((sy[1]-sy[2])*(px-sx[2]) + (sx[2]-sx[1])*(py-sy[2])) / den
			l1 := ((sy[2]-sy[0])*(px-sx[2]) + (sx[0]-sx[2])*(py-sy[2])) / den
			l2 := 1 - l0 - l1
			if l0 < 0 || l1 < 0 || l2 < 0 {
				continue
			}
			ch := func(sh uint) float64 {
				return (l0*float64(t.c[0]>>sh&0xff) + l1*float64(t.c[1]>>sh&0xff) + l2*float64(t.c[2]>>sh&0xff)) / 255
			}
			cv.blend(x, y, ch(24), ch(16), ch(8), ch(0), t.blend)
		}
	}
}

type cmd struct {
	tex                  string
	bank, blend          int
	x, y, hw, hh, sx, sy float64
	rot                  int
	flip                 bool
	rgba                 uint32
	hasSrc               bool
	s0, s1, s2, s3       float64
	tri                  *tri
}

func render(c *canvas, d cmd, camX, camY, zoom float64, pf [2]string) {
	var t *tex
	white := d.tex == "$white" || d.tex == "white"
	if !white {
		if t = find(d.tex, d.bank, pf); t == nil {
			return
		}
	}
	// world quad size
	var ww, wh float64
	switch {
	case d.hw > 0:
		ww = 2 * d.hw
		wh = 2 * d.hh
		if d.hh <= 0 {
			wh = ww
		}
	case d.hasSrc:
		ww, wh = d.s2-d.s0, d.s3-d.s1
	case t != nil:
		ww, wh = float64(t.w), float64(t.h)
	default:
		ww, wh = 2, 2
	}
	ww *= d.sx
	wh *= d.sy
	cx := W/2 + (d.x-camX)/zoom
	cy := H/2 - (d.y-camY)/zoom
	pw, ph := ww/zoom, wh/zoom
	ang := -float64(d.rot) * 2 * math.Pi / 65536 // world CCW -> screen
	ca, sa := math.Cos(ang), math.Sin(ang)
	ext := (math.Abs(pw) + math.Abs(ph)) * 0.75
	x0, x1 := int(math.Max(0, cx-ext)), int(math.Min(W-1, cx+ext))
	y0, y1 := int(math.Max(0, cy-ext)), int(math.Min(H-1, cy+ext))
	mr := float64(d.rgba>>24&0xff) / 255
	mg := float64(d.rgba>>16&0xff) / 255
	mb := float64(d.rgba>>8&0xff) / 255
	ma := float64(d.rgba&0xff) / 255
	u0, v0, u1, v1 := 0.0, 0.0, 1.0, 1.0
	if d.hasSrc && t != nil {
		u0, v0, u1, v1 = d.s0/float64(t.w), d.s1/float64(t.h), d.s2/float64(t.w), d.s3/float64(t.h)
	}
	for y := y0; y <= y1; y++ {
		for x := x0; x <= x1; x++ {
			dx, dy := float64(x)+0.5-cx, float64(y)+0.5-cy
			lx := dx*ca + dy*sa // inverse rotation
			ly := -dx*sa + dy*ca
			u := lx/pw + 0.5
			v := ly/ph + 0.5
			if pw < 0 {
				u = -lx/(-pw) + 0.5
			}
			if u < 0 || u >= 1 || v < 0 || v >= 1 {
				continue
			}
			if d.flip {
				u = 1 - u
			}
			r, g, b, a := 1.0, 1.0, 1.0, 1.0
			if t != nil {
				tu := (u0 + (u1-u0)*u) * float64(t.w)
				tv := (v0 + (v1-v0)*v) * float64(t.h)
				px := t.img.NRGBAAt(int(math.Min(tu, float64(t.w-1))), int(math.Min(tv, float64(t.h-1))))
				r, g, b, a = float64(px.R)/255, float64(px.G)/255, float64(px.B)/255, float64(px.A)/255
			}
			a *= ma
			if a <= 0.002 && d.blend != 4 {
				continue
			}
			c.blend(x, y, r*mr, g*mg, b*mb, a, d.blend)
		}
	}
}

func main() {
	in := flag.String("in", "", "dump TSV")
	flag.StringVar(&texRoot, "tex", "", "extracted/texture dir")
	out := flag.String("out", "drawpng_out", "output dir")
	hud := flag.Bool("hud", false, "input is a HUD primitive dump (tests/hudpreview.c), see hud.go")
	hudZoom := flag.Int("zoom", 1, "-hud: integer upscale of the output PNGs")
	flag.Parse()
	if *hud {
		os.MkdirAll(*out, 0o755)
		if err := hudMain(*in, *out, *hudZoom); err != nil {
			log.Fatal(err)
		}
		return
	}
	f, err := os.Open(*in)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	os.MkdirAll(*out, 0o755)
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	var cv *canvas
	var frame int
	var camX, camY, zoom float64
	var pf [2]string
	var cmds []cmd
	flush := func() {
		if cv == nil {
			return
		}
		for _, d := range cmds {
			if d.tri != nil {
				renderTri(cv, *d.tri, camX, camY, zoom)
				continue
			}
			render(cv, d, camX, camY, zoom, pf)
		}
		img := image.NewNRGBA(image.Rect(0, 0, W, H))
		for i := 0; i < W*H; i++ {
			img.SetNRGBA(i%W, i/W, color.NRGBA{uint8(cv.px[i*3] * 255), uint8(cv.px[i*3+1] * 255), uint8(cv.px[i*3+2] * 255), 255})
		}
		o, _ := os.Create(filepath.Join(*out, fmt.Sprintf("f%05d.png", frame)))
		png.Encode(o, img)
		o.Close()
		cmds = cmds[:0]
	}
	for sc.Scan() {
		line := sc.Text()
		if strings.HasPrefix(line, "#tri") {
			var t tri
			fmt.Sscanf(line, "#tri %f %f %f %f %f %f %x %x %x", &t.x[0], &t.y[0], &t.x[1], &t.y[1], &t.x[2], &t.y[2], &t.c[0], &t.c[1], &t.c[2])
			if n := len(cmds); n > 0 {
				t.blend = cmds[n-1].blend
				cmds[n-1].tri = &t
			}
			continue
		}
		if strings.HasPrefix(line, "#frame") {
			flush()
			var id1, c1, id2, c2 int
			var st string
			fmt.Sscanf(line, "#frame %d cam %f %f %f p1 %d %d p2 %d %d st %s", &frame, &camX, &camY, &zoom, &id1, &c1, &id2, &c2, &st)
			if zoom <= 0 {
				zoom = 1
			}
			pf = [2]string{playerFolder(id1, c1), playerFolder(id2, c2)}
			cv = &canvas{px: make([]float64, W*H*3)}
			for i := 0; i < W*H; i++ { // backdrop like the 3DS placeholder stage
				cv.px[i*3], cv.px[i*3+1], cv.px[i*3+2] = 0.04, 0.06, 0.16
			}
			continue
		}
		p := strings.Split(line, "\t")
		if len(p) < 17 {
			continue
		}
		fl := func(i int) float64 { v, _ := strconv.ParseFloat(p[i], 64); return v }
		in := func(i int) int { v, _ := strconv.Atoi(p[i]); return v }
		rgba, _ := strconv.ParseUint(p[11], 16, 32)
		cmds = append(cmds, cmd{p[0], in(1), in(2), fl(3), fl(4), fl(5), fl(6), fl(7), fl(8), in(9), p[10] == "1",
			uint32(rgba), p[12] == "1", fl(13), fl(14), fl(15), fl(16), nil})
	}
	flush()
}
