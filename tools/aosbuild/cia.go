package main

// Port of tools/make_cia.sh and assets/cia/make_art.py: packages the last build (<out>/aos2_3ds.elf + romfs) as an
// installable CIA with HOME-menu art cut from the extracted PC assets.

import (
	"errors"
	"image"
	"image/color"
	"image/draw"
	"image/png"
	"io"
	"os"
	"path/filepath"

	xdraw "golang.org/x/image/draw"
)

func ciaStages(o BuildOpts) []stage {
	root, out, tc := o.Root, o.Out, o.TC
	w := filepath.Join(out, "cia")
	return []stage{
		{"CIA banner + icon", 2, func(j *Job, _ func(float64)) error {
			if tc.Makerom == "" || tc.Bannertool == "" {
				return errors.New("makerom/bannertool missing: press \"Set up toolchain\"")
			}
			if !exists(filepath.Join(out, "aos2_3ds.elf")) || !isDir(filepath.Join(out, "romfs")) {
				return errors.New("no build in " + out + " yet")
			}
			if err := os.MkdirAll(w, 0o755); err != nil {
				return err
			}
			if err := makeArt(j, root, o.Game, w); err != nil {
				return err
			}
			// bannertool's flat banner from banner.png: it does not turn. The 3D scene is opt-in: the drop-in (banner
			// test V), billboarded so the HOME menu's spin does not turn it.
			img := []string{"-i", filepath.Join(w, "banner.png")}
			if o.AnimBanner {
				if cgfx, err := makeSceneBanner(j, tc, filepath.Join(root, "extracted"), w, sceneDrop, nil); err != nil {
					j.line("3D banner unavailable, using the flat one: " + err.Error())
				} else {
					img = []string{"-ci", cgfx}
				}
			}
			if err := j.run(root, tc.Bannertool, append(append([]string{"makebanner"}, img...), "-a", filepath.Join(w, "banner.wav"),
				"-o", filepath.Join(w, "banner.bnr"))...); err != nil {
				return err
			}
			return j.run(root, tc.Bannertool, "makesmdh", "-s", "AoS2 3DS", "-l", "Acceleration of SUGURI 2 port - WIP",
				"-p", "personal research build", "-i", filepath.Join(w, "icon.png"), "-o", filepath.Join(w, "icon.smdh"))
		}},
		{"Package .cia (makerom)", 10, func(j *Job, _ func(float64)) error {
			cia := filepath.Join(out, "aos2_3ds.cia")
			if err := j.run(root, tc.Makerom, "-f", "cia", "-o", cia, "-target", "t", "-exefslogo",
				"-elf", filepath.Join(out, "aos2_3ds.elf"), "-rsf", filepath.Join(root, "assets", "cia", "aos2_3ds.rsf"),
				"-icon", filepath.Join(w, "icon.smdh"), "-banner", filepath.Join(w, "banner.bnr"),
				"-DAPP_ROMFS="+filepath.Join(out, "romfs")); err != nil {
				return err
			}
			return logSize(j, cia)
		}},
	}
}

var artBG = color.RGBA{34, 36, 58, 255}

// makeArt writes banner.png (256x128 flat fallback banner: Suguri dashing past the transparent title logo),
// icon.png (48x48: the game's own icon from aos2.exe, else a select-screen portrait) and banner.wav
// (assets/cia/jingle.wav or the menu "decide" sound; the HOME menu plays at most ~3 s).
func makeArt(j *Job, root, game, out string) error {
	src := filepath.Join(root, "extracted")
	banner, err := makeBanner(src)
	if err != nil {
		return err
	}
	if err := savePNG(filepath.Join(out, "banner.png"), banner); err != nil {
		return err
	}
	icon, err := gameIcon(filepath.Join(game, "aos2.exe"))
	if err == nil {
		err = savePNG(filepath.Join(out, "icon.png"), icon)
	}
	if err != nil {
		j.line("game icon unavailable, using a portrait: " + err.Error())
		if err := portraitIcon(src, filepath.Join(out, "icon.png")); err != nil {
			return err
		}
	}
	// assets/cia/jingle.wav (gitignored, your own sound) if present, else the menu "decide" sound
	jingle := filepath.Join(root, "assets", "cia", "jingle.wav")
	if !exists(jingle) {
		jingle = filepath.Join(src, "se", "se", "SH_DECIDE.WAV")
	}
	return copyFileSimple(jingle, filepath.Join(out, "banner.wav"))
}

// portraitIcon: one select-screen portrait circle in a rounded square, 48x48.
func portraitIcon(src, out string) error {
	sheet, err := loadImage(filepath.Join(src, "texture", "selMdl", "sel_icon00.png"))
	if err != nil {
		return err
	}
	const col, row = 1, 1 // portrait cell in the 4x4 sheet of 256 px cells
	x, y := sheet.Bounds().Min.X+col*256, sheet.Bounds().Min.Y+row*256
	icon := solid(256, 256)
	xdraw.CatmullRom.Scale(icon, icon.Bounds(), sheet, image.Rect(x+18, y+18, x+238, y+238), draw.Over, nil) // circle edge to edge
	masked := image.NewNRGBA(icon.Bounds())
	for py := 0; py < 256; py++ {
		for px := 0; px < 256; px++ {
			c := icon.RGBAAt(px, py)
			a := uint8(0)
			if inRoundRect(px, py, 256, 256, 40) {
				a = 255
			}
			masked.SetNRGBA(px, py, color.NRGBA{c.R, c.G, c.B, a})
		}
	}
	small := image.NewNRGBA(image.Rect(0, 0, 48, 48))
	xdraw.CatmullRom.Scale(small, small.Bounds(), masked, masked.Bounds(), draw.Src, nil)
	return savePNG(out, small)
}

func solid(w, h int) *image.RGBA {
	m := image.NewRGBA(image.Rect(0, 0, w, h))
	draw.Draw(m, m.Bounds(), &image.Uniform{artBG}, image.Point{}, draw.Src)
	return m
}

// fitWithin is PIL's thumbnail(): shrink (never enlarge) to fit w x h, keeping the aspect ratio.
func fitWithin(sw, sh, w, h int) (int, int) {
	if sw <= w && sh <= h {
		return sw, sh
	}
	if sw*h > sh*w { // wider than the box
		return w, max(1, (sh*w+sw/2)/sw)
	}
	return max(1, (sw*h+sh/2)/sh), h
}

func inRoundRect(x, y, w, h, r int) bool {
	cx, cy := x, y
	switch {
	case x < r:
		cx = r
	case x >= w-r:
		cx = w - 1 - r
	}
	switch {
	case y < r:
		cy = r
	case y >= h-r:
		cy = h - 1 - r
	}
	dx, dy := x-cx, y-cy
	return dx*dx+dy*dy <= r*r
}

func cropAlpha(m image.Image) image.Image {
	b := m.Bounds()
	minX, minY, maxX, maxY := b.Max.X, b.Max.Y, b.Min.X, b.Min.Y
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			if _, _, _, a := m.At(x, y).RGBA(); a != 0 {
				minX, minY, maxX, maxY = min(minX, x), min(minY, y), max(maxX, x+1), max(maxY, y+1)
			}
		}
	}
	if minX >= maxX {
		return m
	}
	if s, ok := m.(interface {
		SubImage(image.Rectangle) image.Image
	}); ok {
		return s.SubImage(image.Rect(minX, minY, maxX, maxY))
	}
	return m
}

func loadImage(p string) (image.Image, error) {
	f, err := os.Open(p)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	m, _, err := image.Decode(f)
	return m, err
}

func savePNG(p string, m image.Image) error {
	f, err := os.Create(p)
	if err != nil {
		return err
	}
	if err := png.Encode(f, m); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

func copyFileSimple(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	o, err := os.Create(dst)
	if err != nil {
		return err
	}
	if _, err := io.Copy(o, in); err != nil {
		o.Close()
		return err
	}
	return o.Close()
}
