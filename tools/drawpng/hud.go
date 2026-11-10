package main

// HUD preview mode: renders the primitive dumps of tests/hudpreview.c (the
// HUD port in src/platform/hud.c recorded through hud_gfx.h) with the
// original PC textures. Lines:
//
//	#hud <frame> <W> <H> p1 <id> <col> p2 <id> <col>
//	I tex x0 y0 x1 y1 u0 v0 u1 v1 angle argb blend   image (corners may be mirrored)
//	Q tex x0 y0 x1 y1 x2 y2 x3 y3 u0 v0 u1 v1 argb blend   quad TL,TR,BR,BL
//	T x0 y0 x1 y1 x2 y2 c0 c1 c2 blend               coloured triangle
//	X tex x0 y0 x1 y1 x2 y2 u0 v0 u1 v1 u2 v2 argb blend   textured triangle
//	R l t r b argb blend                             solid rect
//	L x0 y0 x1 y1 argb blend                         line
//
// Texture coordinates are texels of the original texture; "P0"/"P1" are the
// players' <xx>_system portraits. Rasterisation: two affine triangles per
// quad, nearest sampling, like the PC's pre-transformed D3D9 fans.

import (
	"bufio"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

var portraitName = map[int]string{3: "sr_system", 4: "at_system", 5: "ts_system", 6: "mr_system", 7: "sa_system",
	8: "nt_system", 9: "sb_system", 10: "sg_system", 11: "sk_system", 12: "ir_system", 13: "nk_system",
	14: "ke_system", 15: "ky_system", 16: "hm_system", 17: "sm_system", 18: "sab_system"}

type hcanvas struct {
	w, h int
	px   []float64
}

func (c *hcanvas) blend(x, y int, r, g, b, a float64, mode int) {
	i := (y*c.w + x) * 3
	dr, dg, db := c.px[i], c.px[i+1], c.px[i+2]
	switch mode {
	case 0:
		dr, dg, db = r, g, b
	case 2:
		dr, dg, db = dr+r*a, dg+g*a, db+b*a
	case 5:
		dr, dg, db = dr+r, dg+g, db+b
	default:
		dr, dg, db = dr*(1-a)+r*a, dg*(1-a)+g*a, db*(1-a)+b*a
	}
	c.px[i], c.px[i+1], c.px[i+2] = math.Min(dr, 1), math.Min(dg, 1), math.Min(db, 1)
}

type hvert struct{ x, y, u, v float64 }

func argbf(c uint32) (float64, float64, float64, float64) {
	return float64(c>>16&0xff) / 255, float64(c>>8&0xff) / 255, float64(c&0xff) / 255, float64(c>>24) / 255
}

// tri rasterises one triangle; t == nil means untextured (per-vertex colours).
func (c *hcanvas) tri(v [3]hvert, col [3]uint32, t *tex, mode int) {
	den := (v[1].y-v[2].y)*(v[0].x-v[2].x) + (v[2].x-v[1].x)*(v[0].y-v[2].y)
	if math.Abs(den) < 1e-9 {
		return
	}
	x0 := int(math.Max(0, math.Floor(math.Min(v[0].x, math.Min(v[1].x, v[2].x)))))
	x1 := int(math.Min(float64(c.w-1), math.Ceil(math.Max(v[0].x, math.Max(v[1].x, v[2].x)))))
	y0 := int(math.Max(0, math.Floor(math.Min(v[0].y, math.Min(v[1].y, v[2].y)))))
	y1 := int(math.Min(float64(c.h-1), math.Ceil(math.Max(v[0].y, math.Max(v[1].y, v[2].y)))))
	for y := y0; y <= y1; y++ {
		for x := x0; x <= x1; x++ {
			px, py := float64(x)+0.5, float64(y)+0.5
			l0 := ((v[1].y-v[2].y)*(px-v[2].x) + (v[2].x-v[1].x)*(py-v[2].y)) / den
			l1 := ((v[2].y-v[0].y)*(px-v[2].x) + (v[0].x-v[2].x)*(py-v[2].y)) / den
			l2 := 1 - l0 - l1
			if l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9 {
				continue
			}
			var r, g, b, a float64
			for k, l := range [3]float64{l0, l1, l2} {
				cr, cg, cb, ca := argbf(col[k])
				r, g, b, a = r+cr*l, g+cg*l, b+cb*l, a+ca*l
			}
			if t != nil {
				u := l0*v[0].u + l1*v[1].u + l2*v[2].u
				w := l0*v[0].v + l1*v[1].v + l2*v[2].v
				tx := int(math.Floor(u))
				ty := int(math.Floor(w))
				if tx < 0 {
					tx = 0
				}
				if ty < 0 {
					ty = 0
				}
				if tx >= t.w {
					tx = t.w - 1
				}
				if ty >= t.h {
					ty = t.h - 1
				}
				p := t.img.NRGBAAt(tx, ty)
				r, g, b, a = r*float64(p.R)/255, g*float64(p.G)/255, b*float64(p.B)/255, a*float64(p.A)/255
			}
			if a <= 0.002 {
				continue
			}
			c.blend(x, y, r, g, b, a, mode)
		}
	}
}

func (c *hcanvas) quad(p [4]hvert, col uint32, t *tex, mode int) {
	cc := [3]uint32{col, col, col}
	c.tri([3]hvert{p[0], p[1], p[2]}, cc, t, mode)
	c.tri([3]hvert{p[0], p[2], p[3]}, cc, t, mode)
}

func hudMain(path, outDir string, zoom int) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	var cv *hcanvas
	var frame int
	var pf [2]string
	var pid [2]int
	flush := func() {
		if cv == nil {
			return
		}
		img := image.NewNRGBA(image.Rect(0, 0, cv.w*zoom, cv.h*zoom))
		for y := 0; y < cv.h*zoom; y++ {
			for x := 0; x < cv.w*zoom; x++ {
				i := ((y/zoom)*cv.w + x/zoom) * 3
				img.SetNRGBA(x, y, color.NRGBA{uint8(cv.px[i] * 255), uint8(cv.px[i+1] * 255), uint8(cv.px[i+2] * 255), 255})
			}
		}
		o, err := os.Create(filepath.Join(outDir, fmt.Sprintf("hud%05d.png", frame)))
		if err == nil {
			png.Encode(o, img)
			o.Close()
		}
	}
	texOf := func(name string) *tex {
		switch name {
		case "P0", "P1":
			i := int(name[1] - '0')
			return load(pf[i], portraitName[pid[i]])
		}
		parts := strings.SplitN(name, "/", 2)
		if len(parts) != 2 {
			return nil
		}
		return load(parts[0], parts[1])
	}
	for sc.Scan() {
		line := sc.Text()
		if strings.HasPrefix(line, "#hud") {
			flush()
			var w, h, id1, c1, id2, c2 int
			fmt.Sscanf(line, "#hud %d %d %d p1 %d %d p2 %d %d", &frame, &w, &h, &id1, &c1, &id2, &c2)
			pf = [2]string{playerFolder(id1, c1), playerFolder(id2, c2)}
			pid = [2]int{id1, id2}
			cv = &hcanvas{w: w, h: h, px: make([]float64, w*h*3)}
			for i := 0; i < w*h; i++ { // stage stand-in backdrop
				cv.px[i*3], cv.px[i*3+1], cv.px[i*3+2] = 0.30, 0.38, 0.55
			}
			continue
		}
		p := strings.Split(line, "\t")
		if cv == nil || len(p) < 2 {
			continue
		}
		fl := func(i int) float64 { v, _ := strconv.ParseFloat(p[i], 64); return v }
		hx := func(i int) uint32 { v, _ := strconv.ParseUint(p[i], 16, 32); return uint32(v) }
		in := func(i int) int { v, _ := strconv.Atoi(p[i]); return v }
		switch p[0] {
		case "I":
			t := texOf(p[1])
			if t == nil {
				continue
			}
			x0, y0, x1, y1 := fl(2), fl(3), fl(4), fl(5)
			u0, v0, u1, v1 := fl(6), fl(7), fl(8), fl(9)
			ang := fl(10)
			cx, cy := (x0+x1)/2, (y0+y1)/2
			ca, sa := math.Cos(ang), math.Sin(ang)
			rot := func(x, y, u, v float64) hvert {
				dx, dy := x-cx, y-cy
				return hvert{cx + dx*ca - dy*sa, cy + dx*sa + dy*ca, u, v}
			}
			cv.quad([4]hvert{rot(x0, y0, u0, v0), rot(x1, y0, u1, v0), rot(x1, y1, u1, v1), rot(x0, y1, u0, v1)}, hx(11), t, in(12))
		case "Q":
			t := texOf(p[1])
			if t == nil {
				continue
			}
			u0, v0, u1, v1 := fl(10), fl(11), fl(12), fl(13)
			cv.quad([4]hvert{{fl(2), fl(3), u0, v0}, {fl(4), fl(5), u1, v0}, {fl(6), fl(7), u1, v1}, {fl(8), fl(9), u0, v1}}, hx(14), t, in(15))
		case "T":
			cv.tri([3]hvert{{fl(1), fl(2), 0, 0}, {fl(3), fl(4), 0, 0}, {fl(5), fl(6), 0, 0}}, [3]uint32{hx(7), hx(8), hx(9)}, nil, in(10))
		case "X":
			t := texOf(p[1])
			if t == nil {
				continue
			}
			c := hx(14)
			cv.tri([3]hvert{{fl(2), fl(3), fl(8), fl(9)}, {fl(4), fl(5), fl(10), fl(11)}, {fl(6), fl(7), fl(12), fl(13)}}, [3]uint32{c, c, c}, t, in(15))
		case "R":
			l, t, r, b := fl(1), fl(2), fl(3), fl(4)
			cv.quad([4]hvert{{l, t, 0, 0}, {r, t, 0, 0}, {r, b, 0, 0}, {l, b, 0, 0}}, hx(5), nil, in(6))
		case "L":
			x0, y0, x1, y1 := fl(1), fl(2), fl(3), fl(4)
			dx, dy := x1-x0, y1-y0
			n := math.Hypot(dx, dy)
			if n == 0 {
				continue
			}
			ox, oy := -dy/n*0.5, dx/n*0.5
			cv.quad([4]hvert{{x0 + ox, y0 + oy, 0, 0}, {x1 + ox, y1 + oy, 0, 0}, {x1 - ox, y1 - oy, 0, 0}, {x0 - ox, y0 - oy, 0, 0}}, hx(5), nil, in(6))
		}
	}
	flush()
	return sc.Err()
}
