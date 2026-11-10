package main

// 3D HOME-menu banner: a glTF scene converted to CGFX with pycgfx (tools/pycgfx, fetched by setup) and wrapped by
// bannertool -ci. The title is menuMdl/ttl_logo (transparent) with no background quad, so the HOME menu's own
// backdrop shows around it; it does not move. Animated: Suguri dashes back and forth in front of it in her
// upright dash pose (charaSuguriA/sg_dsh06..08 ping-ponged at 20 fps, as dr_dash in src/sim/chars/suguri.c),
// right to left, then mirrored left to right, with one afterimage. Each dash is eased (fastest mid-screen, slowing
// to a stop near the edge); at each end she turns around with a paper flip, then dashes back. Static: the same
// scene frozen mid-dash.
//
// What the real HOME menu accepts (banner test kit, aosbuild bannertest): linear translation keys on top-level
// nodes. Stepped scale keys on child nodes crashed it (data abort); rotation keys froze the system. So the
// flip-book is one quad per pose (dash frame x direction, and the paper flip's pre-turned poses; mirrored quads
// reuse the texture with flipped UVs), parked off-screen while not showing, with a 1 ms key pair at every
// 1/20 s boundary making each swap instant.

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/gif"
	"math"
	"os"
	"path/filepath"
	"sort"

	xdraw "golang.org/x/image/draw"
)

// HOME-menu banner camera (pycgfx banner-camera.gltf): at (0, 1, 44.786) looking down -Z, 30° vertical FOV,
// aspect 5:3, near 26.5. At z=0 the view spans about x ±20, y 1±12.
const (
	camZ       = 44.786
	camY       = 1.0
	camTanHalf = 0.267949 // tan(15°)
	camAspect  = 5.0 / 3.0
)

// Scene layout, in banner units (before zoom).
var (
	logoQuad   = quad{cx: 0, cy: 1.5, w: 32, h: 16} // texture 256x128
	suguriQuad = quad{cx: 0, cy: 0, w: 12, h: 12}   // texture 128x128 per dash frame, centred on its node
)

// Dash: two legs per loop, right to left, then (mirrored) left to right. Each leg: an eased dash between turning
// points just inside the screen edges (fastest crossing the middle, swinging toward the viewer), then a paper
// flip in place, then a short hold facing the new way. Every phase boundary is on the 1/20 s grid.
const (
	loopSecs  = 1.8  // whole loop (banner test L: plays on hardware)
	passSecs  = 1.3  // right edge -> left edge, then off-screen until the loop restarts
	passX     = 30.0 // start/end x: off-screen at her depth
	legSecs   = loopSecs
	moveSecs  = passSecs
	turnStep  = 0.05
	passY     = -3.5 // height of the pass
	passArc   = 1.0  // rises this much mid-pass
	passZ     = 6.0  // depth in front of the logo at the edges
	passSwoop = 4.0  // extra depth toward the viewer mid-pass
	dashFPS   = 20.0 // dr_dash against facing: pingpong(timer, 3) + 6 at 60 fps
	zoom      = 0.75 // whole scene scaled about the origin: a bit more room around the title
	keyEps    = 0.001
)

// dashSeq: the dash frames played in order (sg_dsh06, 07, 08, 07: the game's ping-pong).
var dashSeq = []int{6, 7, 8, 7}

// dashTex: the distinct frames used, in texture order.
var dashTex = []int{6, 7, 8}

// trailMember: one sprite of the dash, running lag seconds behind Suguri at the given opacity.
type trailMember struct {
	name       string
	lag, alpha float64
}

// trail is drawn in this order (afterimage first, Suguri on top).
var trail = []trailMember{{"Afterimage", 2.0 / 30, 0.4}, {"Suguri", 0, 1}}

// parked: where flip-book quads wait while not showing (far below the view).
var parked = [3]float64{0, -200, 0}

type quad struct{ cx, cy, w, h float64 }

func zoomed(q quad) quad { return quad{q.cx * zoom, q.cy * zoom, q.w * zoom, q.h * zoom} }

func place(p [3]float64) [3]float64 { return [3]float64{p[0] * zoom, p[1] * zoom, p[2] * zoom} }

// fitTexture letterboxes m (cropped to crop) into a w x h transparent texture (power-of-two sizes for the GPU).
func fitTexture(m image.Image, crop image.Rectangle, w, h int) *image.NRGBA {
	fw, fh := crop.Dx(), crop.Dy()
	if fw*h > fh*w {
		fw, fh = w, max(1, fh*w/fw)
	} else {
		fw, fh = max(1, fw*h/fh), h
	}
	t := image.NewNRGBA(image.Rect(0, 0, w, h))
	at := image.Rect((w-fw)/2, (h-fh)/2, (w-fw)/2+fw, (h-fh)/2+fh)
	xdraw.CatmullRom.Scale(t, at, m, crop, draw.Src, nil)
	return t
}

// withAlpha returns a copy of m with its alpha scaled (the afterimage bakes its opacity into the texture, so it
// does not depend on how the HOME menu treats material colour).
func withAlpha(m *image.NRGBA, a float64) *image.NRGBA {
	o := image.NewNRGBA(m.Bounds())
	copy(o.Pix, m.Pix)
	for i := 3; i < len(o.Pix); i += 4 {
		o.Pix[i] = uint8(float64(o.Pix[i])*a + 0.5)
	}
	return o
}

// turnAngles: the paper flip's poses (degrees about the vertical axis); the last two show the back of the sprite,
// i.e. the mirrored art, turned the other way.
var turnAngles = []float64{36, 72, -72, -36}

// dashState is what shows s seconds into a member's loop: its position, whether the art is mirrored (facing
// right), and either a dash frame (turn < 0, frame = texture index) or paper-flip pose turn.
func dashState(s float64) (pos [3]float64, mirror bool, frame, turn int) {
	// one-way dash, right to left, exactly the shape of banner test L (the back-and-forth / paper-flip version,
	// test M, froze the HOME menu)
	s = max(s, 0)
	u := min(s/passSecs, 1)
	arc := math.Sin(math.Pi * u)
	return [3]float64{passX - 2*passX*u, passY + passArc*arc, passZ + passSwoop*arc}, false, dashFrame(s), -1
}

// dashPos is the member's position s seconds into its loop.
func dashPos(s float64) [3]float64 { p, _, _, _ := dashState(s); return p }

// loopTime maps loop time t of a trail member running lag behind to its own time (still finishing the previous
// loop while t < lag).
func loopTime(t, lag float64) float64 {
	s := t - lag
	if s < -1e-9 {
		s += loopSecs
	}
	return max(s, 0)
}

// dashFrame: texture index (into dashTex) showing s seconds into a member's loop.
func dashFrame(s float64) int {
	f := dashSeq[int(s*dashFPS+1e-6)%len(dashSeq)]
	for i, t := range dashTex {
		if t == f {
			return i
		}
	}
	return 0
}

// memberKeys: key times for a member's quads: the loop ends, its restart (lag), and every dash-frame boundary of
// its own clock (every phase change is on that grid), each change as a pair 1 ms apart (old state, new state). Evenly spread pairs, as in banner test L/F, which the HOME menu plays correctly.
func memberKeys(lag float64) []float64 {
	ts := []float64{0, loopSecs - keyEps, loopSecs}
	if lag > 0 {
		ts = append(ts, lag-keyEps, lag)
	}
	for i := 1; float64(i)/dashFPS < loopSecs-1e-9; i++ {
		b := lag + float64(i)/dashFPS
		if b > loopSecs {
			b -= loopSecs
		}
		ts = append(ts, b-keyEps, b)
	}
	sort.Float64s(ts)
	out := ts[:0]
	for _, t := range ts {
		if t >= 0 && t <= loopSecs && (len(out) == 0 || t-out[len(out)-1] > 1e-6) {
			out = append(out, t)
		}
	}
	return out
}

func frameTexture(member string, k int) string { return fmt.Sprintf("%s%d.png", member, k) }

// dashTextures loads the dash frames in dashTex, cropped to their common bounding box so they stay aligned.
func dashTextures(src string) ([]*image.NRGBA, error) {
	var ims []image.Image
	var box image.Rectangle
	for _, f := range dashTex {
		m, err := loadImage(filepath.Join(src, "texture", "charaSuguriA", fmt.Sprintf("sg_dsh%02d.png", f)))
		if err != nil {
			return nil, err
		}
		ims = append(ims, m)
		box = box.Union(cropAlpha(m).Bounds())
	}
	out := make([]*image.NRGBA, len(ims))
	for k, m := range ims {
		out[k] = fitTexture(m, box, 128, 128)
	}
	return out, nil
}

// sceneMode picks the banner scene variant.
type sceneMode int

const (
	sceneStatic   sceneMode = iota // pass frozen at its middle
	sceneMove                      // one dash frame moving along the legs (no flip-book): banner test kit only
	sceneFlipbook                  // the real animation: dashing back and forth, playing the dash frames
)

func (m sceneMode) String() string { return [...]string{"static", "move only", "animated"}[m] }

// writeBannerScene writes banner.gltf (+ .bin and the PNG textures) into dir.
func writeBannerScene(src, dir string, mode sceneMode, members []trailMember) error {
	logo, err := loadImage(filepath.Join(src, "texture", "menuMdl", "ttl_logo.png"))
	if err != nil {
		return err
	}
	frames, err := dashTextures(src)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	if err := savePNG(filepath.Join(dir, "logo.png"), fitTexture(logo, cropAlpha(logo).Bounds(), 256, 128)); err != nil {
		return err
	}
	for _, m := range members {
		for k, f := range frames {
			if err := savePNG(filepath.Join(dir, frameTexture(m.name, k)), withAlpha(f, m.alpha)); err != nil {
				return err
			}
		}
	}

	var bin bytes.Buffer
	var views, accessors []map[string]any
	acc := func(data any, count int, typ string, comp int, extra map[string]any) int {
		for bin.Len()%4 != 0 {
			bin.WriteByte(0)
		}
		off := bin.Len()
		binary.Write(&bin, binary.LittleEndian, data)
		views = append(views, map[string]any{"buffer": 0, "byteOffset": off, "byteLength": bin.Len() - off})
		a := map[string]any{"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
		for k, x := range extra {
			a[k] = x
		}
		accessors = append(accessors, a)
		return len(accessors) - 1
	}
	const f32, u16 = 5126, 5123
	var meshes, materials, textures, images, nodes, samplers, channels []any
	addMaterial := func(name, png string) int {
		images = append(images, map[string]any{"uri": png, "name": name})
		textures = append(textures, map[string]any{"source": len(images) - 1, "sampler": 0})
		materials = append(materials, map[string]any{"name": name + "Mat", "alphaMode": "BLEND",
			"pbrMetallicRoughness": map[string]any{"baseColorTexture": map[string]any{"index": len(textures) - 1},
				"metallicFactor": 0, "roughnessFactor": 1}})
		return len(materials) - 1
	}
	// addQuad adds a quad mesh (zoomed, turned th about the vertical axis) using material mat; mirror flips the
	// texture left-right.
	addQuad := func(name string, q quad, mat int, mirror bool, th float64) int {
		q = zoomed(q)
		c, sn := float32(math.Cos(th)), float32(math.Sin(th))
		x0, x1, y0, y1 := float32(q.cx-q.w/2), float32(q.cx+q.w/2), float32(q.cy-q.h/2), float32(q.cy+q.h/2)
		var pos []float32
		for _, v := range [][2]float32{{x0, y1}, {x1, y1}, {x1, y0}, {x0, y0}} { // TL TR BR BL
			pos = append(pos, v[0]*c, v[1], -v[0]*sn)
		}
		uv := []float32{0, 0, 1, 0, 1, 1, 0, 1}
		if mirror {
			uv = []float32{1, 0, 0, 0, 0, 1, 1, 1}
		}
		attrs := map[string]any{
			"POSITION": acc(pos, 4, "VEC3", f32, map[string]any{
				"min": []float32{min(x0*c, x1*c), y0, min(-x0*sn, -x1*sn)},
				"max": []float32{max(x0*c, x1*c), y1, max(-x0*sn, -x1*sn)}}),
			"NORMAL":     acc([]float32{sn, 0, c, sn, 0, c, sn, 0, c, sn, 0, c}, 4, "VEC3", f32, nil),
			"TEXCOORD_0": acc(uv, 4, "VEC2", f32, nil),
		}
		meshes = append(meshes, map[string]any{"name": name + "Mesh", "primitives": []any{map[string]any{
			"attributes": attrs, "material": mat, "mode": 4,
			"indices": acc([]uint16{0, 3, 2, 0, 2, 1}, 6, "SCALAR", u16, nil)}}}) // counter-clockwise from +Z
		return len(meshes) - 1
	}
	var scene []int
	addNode := func(n map[string]any) int {
		nodes = append(nodes, n)
		scene = append(scene, len(nodes)-1)
		return len(nodes) - 1
	}
	// addTrack gives a node linear translation keys: pos(t) at the given times.
	addTrack := func(node int, times []float64, pos func(t float64) [3]float64) {
		var ts, vs []float32
		for _, t := range times {
			p := pos(t)
			ts = append(ts, float32(t))
			vs = append(vs, float32(p[0]), float32(p[1]), float32(p[2]))
		}
		nodes[node].(map[string]any)["translation"] = vs[:3]
		in := acc(ts, len(ts), "SCALAR", f32, map[string]any{"min": []float32{0}, "max": []float32{float32(loopSecs)}})
		out := acc(vs, len(ts), "VEC3", f32, nil)
		samplers = append(samplers, map[string]any{"input": in, "output": out, "interpolation": "LINEAR"})
		channels = append(channels, map[string]any{"sampler": len(samplers) - 1, "target": map[string]any{"node": node, "path": "translation"}})
	}

	addNode(map[string]any{"name": "Logo", "mesh": addQuad("Logo", logoQuad, addMaterial("Logo", "logo.png"), false, 0)})
	for _, m := range members {
		mats := make([]int, len(frames))
		for k := range frames {
			mats[k] = addMaterial(fmt.Sprintf("%s%d", m.name, k), frameTexture(m.name, k))
		}
		switch mode {
		case sceneStatic: // frozen mid-pass, the afterimage a little behind
			p := place(dashPos(moveSecs/2 - m.lag))
			addNode(map[string]any{"name": m.name, "translation": p[:], "mesh": addQuad(m.name, suguriQuad, mats[0], false, 0)})
		case sceneMove: // one quad (first dash frame) moving along the legs
			node := addNode(map[string]any{"name": m.name, "mesh": addQuad(m.name, suguriQuad, mats[0], false, 0)})
			addTrack(node, memberKeys(m.lag), func(t float64) [3]float64 { return place(dashPos(loopTime(t, m.lag))) })
		case sceneFlipbook: // one quad per pose, moving while showing, parked otherwise
			ts := memberKeys(m.lag)
			track := func(name string, mesh int, shows func(mirror bool, frame, turn int) bool) {
				node := addNode(map[string]any{"name": name, "mesh": mesh})
				addTrack(node, ts, func(t float64) [3]float64 {
					if p, mirror, frame, turn := dashState(loopTime(t, m.lag)); shows(mirror, frame, turn) {
						return place(p)
					}
					return parked
				})
			}
			for _, mir := range []bool{false} { // (mirrored and paper-flip quads: see test M)
				suffix := map[bool]string{false: "", true: "R"}[mir]
				for k := range frames { // dash frames
					name := fmt.Sprintf("%sFrame%d%s", m.name, k, suffix)
					track(name, addQuad(name, suguriQuad, mats[k], mir, 0), func(mirror bool, frame, turn int) bool {
						return turn < 0 && mirror == mir && frame == k
					})
				}
				for i, deg := range turnAngles[:0] { // paper-flip poses: off (test M froze)
					name := fmt.Sprintf("%sTurn%d%s", m.name, i, suffix)
					track(name, addQuad(name, suguriQuad, mats[1], mir, deg*math.Pi/180), func(mirror bool, frame, turn int) bool {
						return turn == i && mirror == mir
					})
				}
			}
		}
	}
	doc := map[string]any{
		"asset":       map[string]any{"version": "2.0", "generator": "aosbuild"},
		"scene":       0,
		"scenes":      []any{map[string]any{"nodes": scene}},
		"nodes":       nodes,
		"meshes":      meshes,
		"materials":   materials,
		"textures":    textures,
		"samplers":    []any{map[string]any{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}},
		"images":      images,
		"accessors":   accessors,
		"bufferViews": views,
		"buffers":     []any{map[string]any{"uri": "banner.bin", "byteLength": bin.Len()}},
	}
	if mode != sceneStatic {
		doc["animations"] = []any{map[string]any{"name": "Dash", "samplers": samplers, "channels": channels}}
	}
	if err := os.WriteFile(filepath.Join(dir, "banner.bin"), bin.Bytes(), 0o644); err != nil {
		return err
	}
	j, _ := json.MarshalIndent(doc, "", " ")
	return os.WriteFile(filepath.Join(dir, "banner.gltf"), j, 0o644)
}

// makeSceneBanner writes <w>/anim/banner.gltf, converts it to <w>/banner.cgfx and writes a preview.
func makeSceneBanner(j *Job, tc Toolchain, src, w string, mode sceneMode, members []trailMember) (string, error) {
	if tc.PyCGFX == "" {
		return "", errors.New(`pycgfx not installed (press "Set up toolchain")`)
	}
	dir := filepath.Join(w, "anim")
	os.RemoveAll(dir)
	if err := writeBannerScene(src, dir, mode, members); err != nil {
		return "", err
	}
	out := filepath.Join(w, "banner.cgfx")
	os.Remove(out)
	if err := j.run(filepath.Dir(tc.PyCGFXMain), tc.PyCGFX, tc.PyCGFXMain, filepath.Join(dir, "banner.gltf"), out); err != nil {
		return "", err
	}
	st, err := os.Stat(out)
	if err != nil {
		return "", err
	}
	if st.Size() > 0x80000 {
		return "", fmt.Errorf("banner.cgfx is %d KB, over the HOME menu's 512 KB limit", st.Size()>>10)
	}
	j.logf("banner.cgfx  %d KB (%s)", st.Size()>>10, mode)
	prev := filepath.Join(w, "banner_preview.gif")
	if err := writeBannerPreview(dir, prev, mode, members); err != nil {
		j.line("preview: " + err.Error())
	} else {
		j.line("preview (approximate, no 3D): " + prev)
	}
	return out, nil
}

// writeBannerPreview renders the scene through the banner camera (400x240, the top screen) on a light grey like
// the default HOME menu theme, as a looping GIF at 30 fps. Flat shading and no stereo: framing and motion only.
func writeBannerPreview(dir, out string, mode sceneMode, members []trailMember) error {
	logo, err := loadImage(filepath.Join(dir, "logo.png"))
	if err != nil {
		return err
	}
	tex := map[string][]image.Image{}
	for _, m := range members {
		for k := range dashTex {
			im, err := loadImage(filepath.Join(dir, frameTexture(m.name, k)))
			if err != nil {
				return err
			}
			tex[m.name] = append(tex[m.name], im)
		}
	}
	const W, H, fps = 400, 240, 30
	n := int(loopSecs * fps)
	if mode == sceneStatic {
		n = 1
	}
	g := &gif.GIF{LoopCount: 0}
	bg := color.RGBA{232, 232, 236, 255}
	for f := range n {
		t := float64(f) / fps
		fr := image.NewRGBA(image.Rect(0, 0, W, H))
		draw.Draw(fr, fr.Bounds(), &image.Uniform{bg}, image.Point{}, draw.Src)
		drawQuad(fr, logo, zoomed(logoQuad), [3]float64{}, false, 0)
		for _, m := range members {
			s := loopTime(t, m.lag)
			if mode == sceneStatic {
				s = moveSecs/2 - m.lag
			}
			p, mirror, k, turn := dashState(s)
			th := 0.0
			if mode != sceneFlipbook {
				mirror, k, turn = false, 0, -1
			}
			if turn >= 0 {
				k, th = 1, turnAngles[turn]*math.Pi/180
			}
			drawQuad(fr, tex[m.name][k], zoomed(suguriQuad), place(p), mirror, th)
		}
		pal := image.NewPaletted(fr.Bounds(), previewPalette)
		draw.FloydSteinberg.Draw(pal, pal.Bounds(), fr, image.Point{})
		g.Image = append(g.Image, pal)
		g.Delay = append(g.Delay, 3+f%3/2) // 3,3,4 hundredths: 30 fps on average
	}
	f, err := os.Create(out)
	if err != nil {
		return err
	}
	if err := gif.EncodeAll(f, g); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// previewPalette: 6x7x6 colour cube plus greys, enough for a recognisable preview.
var previewPalette = func() color.Palette {
	var p color.Palette
	for r := 0; r < 6; r++ {
		for g := 0; g < 7; g++ {
			for b := 0; b < 6; b++ {
				p = append(p, color.RGBA{uint8(r * 51), uint8(g * 255 / 6), uint8(b * 51), 255})
			}
		}
	}
	for i := 0; len(p) < 256; i++ {
		v := uint8(i * 255 / (256 - 252))
		p = append(p, color.RGBA{v, v, v, 255})
	}
	return p
}()

// drawQuad renders a textured quad (node at pos, turned th about the vertical axis; mirror flips the texture)
// through the banner camera, alpha-blending it into dst: per pixel, the camera ray meets the quad's plane.
func drawQuad(dst *image.RGBA, texm image.Image, q quad, pos [3]float64, mirror bool, th float64) {
	W, H := float64(dst.Bounds().Dx()), float64(dst.Bounds().Dy())
	tb := texm.Bounds()
	sin, cos := math.Sin(th), math.Cos(th)
	n := [3]float64{sin, 0, cos} // quad normal
	cam := [3]float64{0, camY, camZ}
	d0 := (pos[0]-cam[0])*n[0] + (pos[1]-cam[1])*n[1] + (pos[2]-cam[2])*n[2]
	for py := 0; py < int(H); py++ {
		for px := 0; px < int(W); px++ {
			dir := [3]float64{((float64(px)+0.5)/W*2 - 1) * camTanHalf * camAspect, (1 - (float64(py)+0.5)/H*2) * camTanHalf, -1}
			dn := dir[0]*n[0] + dir[1]*n[1] + dir[2]*n[2]
			if dn == 0 {
				continue
			}
			k := d0 / dn
			hx, hy, hz := cam[0]+k*dir[0]-pos[0], cam[1]+k*dir[1]-pos[1], cam[2]+k*dir[2]-pos[2]
			lx, ly := hx*cos-hz*sin, hy // back into the quad's own axes
			u := (lx - (q.cx - q.w/2)) / q.w
			v := ((q.cy + q.h/2) - ly) / q.h
			if u < 0 || u >= 1 || v < 0 || v >= 1 {
				continue
			}
			if mirror {
				u = 1 - u - 1e-9
			}
			c := color.NRGBAModel.Convert(texm.At(tb.Min.X+int(u*float64(tb.Dx())), tb.Min.Y+int(v*float64(tb.Dy())))).(color.NRGBA)
			if c.A == 0 {
				continue
			}
			o := dst.RGBAAt(px, py)
			a := uint32(c.A)
			blend := func(s, d uint8) uint8 { return uint8((uint32(s)*a + uint32(d)*(255-a)) / 255) }
			dst.SetRGBA(px, py, color.RGBA{blend(c.R, o.R), blend(c.G, o.G), blend(c.B, o.B), 255})
		}
	}
}
