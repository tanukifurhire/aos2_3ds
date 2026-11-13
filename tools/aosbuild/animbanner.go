package main

// 3D HOME-menu banner: a glTF scene converted to CGFX with pycgfx (tools/pycgfx, fetched by setup) and wrapped by
// bannertool -ci. The title is menuMdl/ttl_logo (transparent) with no background quad, so the HOME menu's own
// backdrop shows around it; it does not move. The shipped scene is the drop-in (sceneDrop, see there); the
// others are kept for the banner test kit. Older scene (sceneFlipbook): Suguri dashes back and forth in front of it in her
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

	_ "embed"

	xdraw "golang.org/x/image/draw"
)

// autorotate: the logo is the root and counter-rotates against the HOME Menu's spin (banner_cgfx.py, with the
// pycgfx fixes). Off: it crashed this HOME menu (banner test P/Q); the banner test kit turns it on.
var autorotate = false

// spinSign: the logo's counter-rotation, +2*pi over spinSecs (as tested on hardware by GabeSud0/pokeplatinum-3ds);
// the banner test kit tries -1 too.
var spinSign = 1.0

// billboard: every node a Y-axial billboard (banner_cgfx.py --plain --billboard), so the HOME Menu's spin does not
// turn it. The drop-in scenes always are (banner tests T, V: fine on hardware).
var billboard = false

// ringCount: rings left by the dash off; ringLayer: their render layer. W (3 rings on layer 2, the only layer-2
// materials) crashed the HOME menu where V (layers 0, 1, 3) played; the banner test kit varies both.
var (
	ringCount = 3
	ringLayer = 1
)

// bannerScript: pycgfx plus the HOME Menu fixes (banner_cgfx.py next to this file).
//
//go:embed banner_cgfx.py
var bannerScript []byte

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
	spinSecs  = 10.0 // the HOME Menu turns every banner model once per 10 s; the logo counter-rotates over this
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
	sceneStatic    sceneMode = iota // pass frozen at its middle
	sceneMove                       // one dash frame sliding across (no frame swaps): the shipped animation
	sceneFlipbook                   // the real animation: dashing back and forth, playing the dash frames
	sceneDrop                       // drop-in: dashes down onto the spin axis under the logo, idles, dashes off up
	sceneDropRings                  // the drop-in, with the rings her dash leaves behind as she dashes off
)

func (m sceneMode) String() string {
	return [...]string{"static", "move only", "animated", "drop-in", "drop-in + rings"}[m]
}

func (m sceneMode) drop() bool { return m == sceneDrop || m == sceneDropRings }

// Drop-in scene. The HOME menu turns the banner model about the vertical axis through the origin; whatever sits
// on that axis turns in place instead of orbiting, so everything here does: the logo at the top, Suguri dropping
// straight down from above the screen into a hover under it, idling, then dashing off upward out of the screen,
// where the next loop's drop starts. Her frames, at the game's rates (60 fps ticks): upright dash sg_dsh06-08
// (pingpong per 3 ticks, dr_dash), braking with sg_bck00-02 (the game's moving-down pose, pingpong per 5 ticks,
// dr_neutral), idle sg_std00-02 (pingpong per 5 ticks), and dashing off up as the game draws a dash upward: the
// dash roll sg_dsh00-02 (per 2 ticks; the back-of-head twirl 03-05 left out) turned a quarter clockwise. One
// afterimage of the dash frame trails her during each dash. Layout in final banner units (divided by zoom here,
// since quads and positions get zoomed); the top screen shows about y -8..9 between the HOME menu's bars.
//
// Everything is on the axis (z = 0): anything in front of the logo would swing behind it for half of each spin,
// and the HOME menu then drew the logo over her. pycgfx's blended materials test depth but never write it, so the
// order is the draw order, fixed by each material's render layer (banner_cgfx.py --layers): logo, afterimage,
// rings, Suguri.
//
// Rings (sceneDropRings): the game's dash trail (fx id 100, core.c / fx.c): one every 5 ticks of the dash at her
// position, lying across the dash, growing (scale 0.5 + 0.08 per tick) and fading (alpha 1 - 0.025 per tick),
// drawn additively from sg_rng01 + sg_rng00. Here: the first ringCount of the dash off, smaller than the game's,
// their glow turned into alpha (no additive blending in pycgfx), the fade baked into two texture stages (the
// game's own stage cells), growing by linear scale keys.
var (
	dropLogo   = quad{cx: 0, cy: 4.0 / zoom, w: 20 / zoom, h: 10 / zoom} // 200x100 px
	dropSuguri = quad{cx: 0, cy: 0, w: 6 / zoom, h: 12 / zoom}           // 64x128 textures (her frames cropped): ~85 px tall
	dropTex    = []string{"sg_dsh06", "sg_dsh07", "sg_dsh08", "sg_bck00", "sg_bck01", "sg_bck02", "sg_std00", "sg_std01", "sg_std02"}
	dropUpTex  = []string{"sg_dsh00", "sg_dsh01", "sg_dsh02"} // turned to dash upward
	// a ring at scale 1: the game's 20x120 quad turned across the upward dash (one game unit is 0.172 banner
	// units at her size here), at ringSize of the game's
	ringQuad = quad{cx: 0, cy: 0, w: 120 * 0.172 * ringSize / zoom, h: 20 * 0.172 * ringSize / zoom}
)

const (
	dropLoop    = 4.0      // whole loop
	dropDashEnd = 0.5      // dash frames until here, then braking
	dropLand    = 0.9      // eased to a stop here, then idle
	dropUp      = 3.3      // dashes off upward
	dropUpSecs  = 0.45     // ... out of the top of the screen by then
	dropFromY   = 16.0     // start: above the top screen
	dropTopY    = 18.0     // where the dash off ends: above the top screen
	dropRestY   = -3.6     // hover height, just overlapping the logo's bottom edge
	dropLag     = 2.0 / 30 // afterimage delay
	tick        = 1.0 / 60
	ringEvery   = 5   // ticks between rings (the game's)
	ringFade    = 20  // ticks in the bright stage (the game's alpha >= 0.5)
	ringLife    = 32  // ticks in all (the game's: 40, the last ones faint and huge)
	ringSize    = 0.7 // of the game's ring size, relative to her
)

// Texture indices: dropTex, then dropUpTex, then the two afterimages (down, up), then the two ring stages.
var (
	dropUpFrame  = len(dropTex)
	dropAfterTex = len(dropTex) + len(dropUpTex)
	dropRingTex  = dropAfterTex + 2
)

func ticks(s float64) int { return int(math.Round(s / tick)) }

func pingpong(t, n int) int {
	if f := (t / n) & 3; f != 3 {
		return f
	}
	return 1
}

// dropY is her height s seconds into the loop: an ease-out (cubic) from above the screen to the hover, and from
// dropUp an ease-in (quadratic) up out of the screen.
func dropY(s float64) float64 {
	if s >= dropUp {
		u := min((s-dropUp)/dropUpSecs, 1)
		return dropRestY + (dropTopY-dropRestY)*u*u
	}
	u := min(max(s/dropLand, 0), 1)
	return dropFromY + (dropRestY-dropFromY)*(1-math.Pow(1-u, 3))
}

// dropFrame is the texture index of her frame at tick k of the loop, or -1 once she is out of the screen.
func dropFrame(k int) int {
	dash, land, up, gone := ticks(dropDashEnd), ticks(dropLand), ticks(dropUp), ticks(dropUp+dropUpSecs)
	switch {
	case k < dash:
		return pingpong(k, 3)
	case k < land:
		return 3 + pingpong(k-dash, 5)
	case k < up:
		return 6 + pingpong(k-land, 5)
	case k < gone:
		return dropUpFrame + pingpong(k-up, 2)
	}
	return -1
}

// dropAfterShows: whether afterimage i (0 the drop, 1 the dash off) shows at tick k: during its dash, once it
// has a past to trail.
func dropAfterShows(i, k int) bool {
	t := float64(k) * tick
	if i == 0 {
		return t >= dropLag && t < dropDashEnd
	}
	return t >= dropUp+dropLag && t < dropUp+dropUpSecs
}

// ringAge: ticks since ring i spawned, at loop tick k (rings late in the loop carry on into the next one).
func ringAge(i, k int) int {
	n := ticks(dropLoop)
	return ((k-ticks(dropUp)-i*ringEvery)%n + n) % n
}

// ringStage: 0 bright, 1 faded, -1 not showing.
func ringStage(i, k int) int {
	switch a := ringAge(i, k); {
	case a < ringFade:
		return 0
	case a < ringLife:
		return 1
	}
	return -1
}

// ringPos: where ring i sits: her position when it spawned.
func ringPos(i int) [3]float64 {
	return [3]float64{0, dropY(dropUp+float64(i*ringEvery)*tick) / zoom, 0}
}

// ringScale: ring i's scale t seconds into the loop.
func ringScale(i int, t float64) float64 {
	a := math.Mod(t-dropUp-float64(i*ringEvery)*tick+2*dropLoop, dropLoop) / tick
	return 0.5 + 0.08*a
}

// turnCW turns m a quarter clockwise.
func turnCW(m image.Image) *image.NRGBA {
	b := m.Bounds()
	o := image.NewNRGBA(image.Rect(0, 0, b.Dy(), b.Dx()))
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			o.Set(b.Max.Y-1-y, x-b.Min.X, m.At(x, y))
		}
	}
	return o
}

// dropTextures loads her frames (see the texture indices): each group cropped around its common box (so its
// frames stay aligned, as on the game's 96-unit quad), all at one scale, into 64x128; then the afterimages, the
// first frame of each dash at 40% opacity.
func dropTextures(src string) ([]*image.NRGBA, error) {
	type group struct {
		ims []image.Image
		box image.Rectangle
	}
	var groups []group
	for gi, names := range [][]string{dropTex, dropUpTex} {
		var g group
		for _, n := range names {
			m, err := loadImage(filepath.Join(src, "texture", "charaSuguriA", n+".png"))
			if err != nil {
				return nil, err
			}
			if gi == 1 {
				m = turnCW(m)
			}
			g.ims = append(g.ims, m)
			g.box = g.box.Union(cropAlpha(m).Bounds())
		}
		groups = append(groups, g)
	}
	scale := math.Inf(1)
	for _, g := range groups {
		scale = min(scale, 64/float64(g.box.Dx()), 128/float64(g.box.Dy()))
	}
	var out []*image.NRGBA
	for _, g := range groups {
		c := g.box.Min.Add(g.box.Max).Div(2)
		w, h := int(64/scale+0.5), int(128/scale+0.5)
		box := image.Rect(c.X-w/2, c.Y-h/2, c.X-w/2+w, c.Y-h/2+h)
		for _, m := range g.ims {
			t := image.NewNRGBA(image.Rect(0, 0, 64, 128))
			xdraw.CatmullRom.Scale(t, t.Bounds(), m, box, draw.Src, nil)
			out = append(out, t)
		}
	}
	return append(out, withAlpha(out[0], 0.4), withAlpha(out[dropUpFrame], 0.4)), nil
}

// ringTextures: the ring's two fade stages (the game's stage cells 1 and 9 of the 64x250 cells in sg_rng01 +
// sg_rng00, added as the game blends them), the glow turned into alpha, baked a little above the stage's mean
// opacity (there is no additive glow on a light HOME menu), and turned a quarter to lie across the upward dash.
func ringTextures(src string) ([]*image.NRGBA, error) {
	var sheets []image.Image
	for _, n := range []string{"sg_rng01", "sg_rng00"} {
		m, err := loadImage(filepath.Join(src, "texture", "charaSuguriA", n+".png"))
		if err != nil {
			return nil, err
		}
		sheets = append(sheets, m)
	}
	stage := func(f int, alpha float64) *image.NRGBA {
		x0, y0 := (f%8)*64, (f/8)*250
		turned := image.NewNRGBA(image.Rect(0, 0, 250, 64))
		for y := range 250 {
			for x := range 64 {
				var c [3]uint32
				for _, s := range sheets {
					r, g, b, _ := s.At(s.Bounds().Min.X+x0+x, s.Bounds().Min.Y+y0+y).RGBA()
					c[0], c[1], c[2] = c[0]+r>>8, c[1]+g>>8, c[2]+b>>8
				}
				c[0], c[1], c[2] = min(c[0], 255), min(c[1], 255), min(c[2], 255)
				a := max(c[0], c[1], c[2])
				if a == 0 {
					continue
				}
				turned.SetNRGBA(y, 63-x, color.NRGBA{uint8(c[0] * 255 / a), uint8(c[1] * 255 / a), uint8(c[2] * 255 / a),
					uint8(float64(a)*alpha + 0.5)})
			}
		}
		out := image.NewNRGBA(image.Rect(0, 0, 128, 32)) // plenty at the rings' size
		xdraw.CatmullRom.Scale(out, out.Bounds(), turned, turned.Bounds(), draw.Src, nil)
		return out
	}
	return []*image.NRGBA{stage(1, 0.9), stage(9, 0.4)}, nil
}

func dropTexture(k int) string { return fmt.Sprintf("drop%d.png", k) }

// tickKeys samples a node over the loop at every tick: val(t) while showing, hidden otherwise, each switch as a
// 1 ms key pair; then drops every key a straight line through its neighbours already gives (hidden spans, steady
// motion), so a node carries keys only where it changes.
func tickKeys(shows func(k int) bool, val func(t float64) [3]float64, hidden [3]float64) ([]float64, [][3]float64) {
	n := ticks(dropLoop)
	var ts []float64
	var vs [][3]float64
	at := func(k int, t float64) [3]float64 {
		if shows(k) {
			return val(t)
		}
		return hidden
	}
	for k := 0; k <= n; k++ {
		t := float64(k) * tick
		kk := k % n // the loop's last key repeats its first
		if k > 0 && shows(kk) != shows(k-1) {
			ts, vs = append(ts, t-keyEps), append(vs, at(k-1, t-keyEps))
		}
		ts, vs = append(ts, t), append(vs, at(kk, float64(kk)*tick))
	}
	return simplifyKeys(ts, vs)
}

// simplifyKeys removes interior keys within 0.005 units of the line through the kept key before and the next one.
func simplifyKeys(ts []float64, vs [][3]float64) ([]float64, [][3]float64) {
	ot, ov := []float64{ts[0]}, [][3]float64{vs[0]}
	for i := 1; i < len(ts)-1; i++ {
		p, pt := ov[len(ov)-1], ot[len(ot)-1]
		u := (ts[i] - pt) / (ts[i+1] - pt)
		keep := false
		for c := range 3 {
			if math.Abs(p[c]+(vs[i+1][c]-p[c])*u-vs[i][c]) > 0.005 {
				keep = true
			}
		}
		if keep {
			ot, ov = append(ot, ts[i]), append(ov, vs[i])
		}
	}
	return append(ot, ts[len(ts)-1]), append(ov, vs[len(vs)-1])
}

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
	var drop []*image.NRGBA
	if mode.drop() {
		if drop, err = dropTextures(src); err != nil {
			return err
		}
		if mode == sceneDropRings {
			rings, err := ringTextures(src)
			if err != nil {
				return err
			}
			drop = append(drop, rings...)
		}
		for k, m := range drop {
			if err := savePNG(filepath.Join(dir, dropTexture(k)), m); err != nil {
				return err
			}
		}
		members = nil
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
		// single-sided (the HOME menu culls back faces); addQuad builds each quad's back face itself
		materials = append(materials, map[string]any{"name": name + "Mat", "alphaMode": "BLEND",
			"pbrMetallicRoughness": map[string]any{"baseColorTexture": map[string]any{"index": len(textures) - 1},
				"metallicFactor": 0, "roughnessFactor": 1}})
		return len(materials) - 1
	}
	// addQuad adds a two-faced quad mesh (zoomed, turned th about the vertical axis) using material mat; mirror flips
	// the texture left-right. The back face is a second set of vertices facing -Z, so the quad still shows when the
	// HOME menu's spin turns it away: readBack gives it the texture flipped back, so it reads the right way round from
	// behind (the logo); otherwise it shows the mirror image, as a paper cutout would (Suguri then faces the way she
	// moves, which from behind is the other way).
	addQuad := func(name string, q quad, mat int, mirror, readBack bool, th float64) int {
		q = zoomed(q)
		c, sn := float32(math.Cos(th)), float32(math.Sin(th))
		x0, x1, y0, y1 := float32(q.cx-q.w/2), float32(q.cx+q.w/2), float32(q.cy-q.h/2), float32(q.cy+q.h/2)
		var pos []float32
		for range 2 { // front, then back (same corners)
			for _, v := range [][2]float32{{x0, y1}, {x1, y1}, {x1, y0}, {x0, y0}} { // TL TR BR BL
				pos = append(pos, v[0]*c, v[1], -v[0]*sn)
			}
		}
		uvL, uvR := float32(0), float32(1)
		if mirror {
			uvL, uvR = 1, 0
		}
		bL, bR := uvL, uvR // back face, seen from behind: TL/TR swap places on screen
		if readBack {
			bL, bR = uvR, uvL
		}
		uv := []float32{uvL, 0, uvR, 0, uvR, 1, uvL, 1, bL, 0, bR, 0, bR, 1, bL, 1}
		attrs := map[string]any{
			"POSITION": acc(pos, 8, "VEC3", f32, map[string]any{
				"min": []float32{min(x0*c, x1*c), y0, min(-x0*sn, -x1*sn)},
				"max": []float32{max(x0*c, x1*c), y1, max(-x0*sn, -x1*sn)}}),
			"NORMAL": acc([]float32{sn, 0, c, sn, 0, c, sn, 0, c, sn, 0, c,
				-sn, 0, -c, -sn, 0, -c, -sn, 0, -c, -sn, 0, -c}, 8, "VEC3", f32, nil),
			"TEXCOORD_0": acc(uv, 8, "VEC2", f32, nil),
		}
		meshes = append(meshes, map[string]any{"name": name + "Mesh", "primitives": []any{map[string]any{
			"attributes": attrs, "material": mat, "mode": 4,
			// front counter-clockwise from +Z, back counter-clockwise from -Z
			"indices": acc([]uint16{0, 3, 2, 0, 2, 1, 4, 6, 7, 4, 5, 6}, 12, "SCALAR", u16, nil)}}})
		return len(meshes) - 1
	}
	var scene []int
	addNode := func(n map[string]any) int {
		nodes = append(nodes, n)
		scene = append(scene, len(nodes)-1)
		return len(nodes) - 1
	}
	// addKeys gives a node linear keys on path ("translation" or "scale"): ps at the times ts.
	addKeys := func(node int, path string, times []float64, ps [][3]float64) {
		var ts, vs []float32
		for i, t := range times {
			ts = append(ts, float32(t))
			vs = append(vs, float32(ps[i][0]), float32(ps[i][1]), float32(ps[i][2]))
		}
		nodes[node].(map[string]any)[path] = vs[:3]
		in := acc(ts, len(ts), "SCALAR", f32, map[string]any{"min": []float32{ts[0]}, "max": []float32{ts[len(ts)-1]}})
		out := acc(vs, len(ts), "VEC3", f32, nil)
		samplers = append(samplers, map[string]any{"input": in, "output": out, "interpolation": "LINEAR"})
		channels = append(channels, map[string]any{"sampler": len(samplers) - 1, "target": map[string]any{"node": node, "path": path}})
	}
	// addTrack: pos(t) at the given times.
	addTrack := func(node int, times []float64, pos func(t float64) [3]float64) {
		var ps [][3]float64
		for _, t := range times {
			ps = append(ps, pos(t))
		}
		addKeys(node, "translation", times, ps)
	}
	// addTicked: a drop-in node, at pos(t) (unzoomed) while shows(tick), parked otherwise.
	addTicked := func(name string, mesh int, shows func(k int) bool, pos func(t float64) [3]float64) int {
		node := addNode(map[string]any{"name": name, "mesh": mesh})
		ts, ps := tickKeys(shows, func(t float64) [3]float64 { return place(pos(t)) }, parked)
		addKeys(node, "translation", ts, ps)
		return node
	}

	// the logo is the one root: banner_cgfx.py turns it against the HOME Menu's spin (its placeholder track here
	// only makes pycgfx size the animation to spinSecs), and everything else hangs under it
	lq := logoQuad
	if mode.drop() {
		lq = dropLogo
	}
	logoNode := addNode(map[string]any{"name": "Logo", "mesh": addQuad("Logo", lq, addMaterial("Logo", "logo.png"), false, true, 0)})
	if autorotate {
		addTrack(logoNode, []float64{0, spinSecs}, func(float64) [3]float64 { return [3]float64{} })
	}
	if mode.drop() { // back to front: the afterimages, the rings, then one quad per frame of hers (all on the axis)
		for i, name := range []string{"Afterimage", "AfterimageUp"} {
			addTicked(name, addQuad(name, dropSuguri, addMaterial("Drop"+name, dropTexture(dropAfterTex+i)), false, false, 0),
				func(k int) bool { return dropAfterShows(i, k) }, func(t float64) [3]float64 { return [3]float64{0, dropY(t-dropLag) / zoom, 0} })
		}
		if mode == sceneDropRings {
			stageMats := []int{addMaterial("Ring0", dropTexture(dropRingTex)), addMaterial("Ring1", dropTexture(dropRingTex+1))}
			for i := range ringCount {
				for st, mat := range stageMats {
					name := fmt.Sprintf("Ring%d_%d", i, st)
					node := addTicked(name, addQuad(name, ringQuad, mat, false, false, 0),
						func(k int) bool { return ringStage(i, k) == st }, func(float64) [3]float64 { return ringPos(i) })
					ts, ps := tickKeys(func(k int) bool { return ringStage(i, k) == st },
						func(t float64) [3]float64 { s := ringScale(i, t); return [3]float64{s, s, 1} }, [3]float64{1, 1, 1})
					addKeys(node, "scale", ts, ps)
				}
			}
		}
		for i := range dropAfterTex {
			name := fmt.Sprintf("Suguri%d", i)
			addTicked(name, addQuad(name, dropSuguri, addMaterial(name, dropTexture(i)), false, false, 0),
				func(k int) bool { return dropFrame(k) == i }, func(t float64) [3]float64 { return [3]float64{0, dropY(t) / zoom, 0} })
		}
	}
	for _, m := range members {
		mats := make([]int, len(frames))
		for k := range frames {
			mats[k] = addMaterial(fmt.Sprintf("%s%d", m.name, k), frameTexture(m.name, k))
		}
		switch mode {
		case sceneStatic: // frozen mid-pass, the afterimage a little behind
			p := place(dashPos(moveSecs/2 - m.lag))
			addNode(map[string]any{"name": m.name, "translation": p[:], "mesh": addQuad(m.name, suguriQuad, mats[0], false, false, 0)})
		case sceneMove: // one quad (first dash frame) moving along the legs
			node := addNode(map[string]any{"name": m.name, "mesh": addQuad(m.name, suguriQuad, mats[0], false, false, 0)})
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
					track(name, addQuad(name, suguriQuad, mats[k], mir, false, 0), func(mirror bool, frame, turn int) bool {
						return turn < 0 && mirror == mir && frame == k
					})
				}
				for i, deg := range turnAngles[:0] { // paper-flip poses: off (test M froze)
					name := fmt.Sprintf("%sTurn%d%s", m.name, i, suffix)
					track(name, addQuad(name, suguriQuad, mats[1], mir, false, deg*math.Pi/180), func(mirror bool, frame, turn int) bool {
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
	if autorotate && len(scene) > 1 { // everything hangs under the counter-rotating logo
		nodes[logoNode].(map[string]any)["children"] = scene[1:]
		doc["scenes"] = []any{map[string]any{"nodes": []int{logoNode}}}
	}
	if len(channels) > 0 {
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
	var err error
	if autorotate || billboard || mode.drop() {
		script := filepath.Join(dir, "banner_cgfx.py")
		if err := os.WriteFile(script, bannerScript, 0o644); err != nil {
			return "", err
		}
		args := []string{script, filepath.Dir(tc.PyCGFXMain), filepath.Join(dir, "banner.gltf"), out}
		if autorotate {
			args = append(args, "--autorotate", "Logo", "--sign", fmt.Sprint(spinSign))
		} else { // plain pycgfx output, only billboarded
			args = append(args, "--plain", "--billboard")
			if mode.drop() { // draw order: logo, afterimages and rings, Suguri (all on the axis, at one depth)
				args = append(args, "--layers", fmt.Sprintf("Logo=0,DropAfterimage=1,Ring=%d,Suguri=3", ringLayer))
			}
		}
		err = j.run(dir, tc.PyCGFX, args...)
	} else { // plain pycgfx, as banner test L
		err = j.run(filepath.Dir(tc.PyCGFXMain), tc.PyCGFX, tc.PyCGFXMain, filepath.Join(dir, "banner.gltf"), out)
	}
	if err != nil {
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
	if mode.drop() {
		return writeDropPreview(dir, out, logo, mode == sceneDropRings)
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
	const fps = 30
	n := int(loopSecs * fps)
	if mode == sceneStatic {
		n = 1
	}
	g := &gif.GIF{LoopCount: 0}
	for f := range n {
		t := float64(f) / fps
		fr := previewFrame()
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
		addPreviewFrame(g, fr, f)
	}
	return saveGIF(out, g)
}

// writeDropPreview: the drop-in scene, sampled at 30 fps with the same frame logic as its keys, drawn in the
// render layers' order.
func writeDropPreview(dir, out string, logo image.Image, rings bool) error {
	var tex []image.Image
	n := dropRingTex
	if rings {
		n += 2
	}
	for k := range n {
		im, err := loadImage(filepath.Join(dir, dropTexture(k)))
		if err != nil {
			return err
		}
		tex = append(tex, im)
	}
	g := &gif.GIF{LoopCount: 0}
	for f := range int(dropLoop * 30) {
		t, k := float64(f)/30, f*2
		fr := previewFrame()
		drawQuad(fr, logo, zoomed(dropLogo), [3]float64{}, false, 0)
		for i := range 2 {
			if dropAfterShows(i, k) {
				drawQuad(fr, tex[dropAfterTex+i], zoomed(dropSuguri), place([3]float64{0, dropY(t-dropLag) / zoom, 0}), false, 0)
			}
		}
		for i := range ringCount {
			if st := ringStage(i, k); rings && st >= 0 {
				s := ringScale(i, t)
				q := ringQuad
				q.w, q.h = q.w*s, q.h*s
				drawQuad(fr, tex[dropRingTex+st], zoomed(q), place(ringPos(i)), false, 0)
			}
		}
		if fi := dropFrame(k); fi >= 0 {
			drawQuad(fr, tex[fi], zoomed(dropSuguri), place([3]float64{0, dropY(t) / zoom, 0}), false, 0)
		}
		addPreviewFrame(g, fr, f)
	}
	return saveGIF(out, g)
}

// previewFrame: a blank 400x240 top screen in the default HOME menu theme's light grey.
func previewFrame() *image.RGBA {
	fr := image.NewRGBA(image.Rect(0, 0, 400, 240))
	draw.Draw(fr, fr.Bounds(), &image.Uniform{color.RGBA{232, 232, 236, 255}}, image.Point{}, draw.Src)
	return fr
}

func addPreviewFrame(g *gif.GIF, fr *image.RGBA, f int) {
	pal := image.NewPaletted(fr.Bounds(), previewPalette)
	draw.FloydSteinberg.Draw(pal, pal.Bounds(), fr, image.Point{})
	g.Image = append(g.Image, pal)
	g.Delay = append(g.Delay, 3+f%3/2) // 3,3,4 hundredths: 30 fps on average
}

func saveGIF(out string, g *gif.GIF) error {
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
