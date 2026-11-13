package main

import (
	"image"
	"image/color"
	"image/draw"
	"path/filepath"

	xdraw "golang.org/x/image/draw"
)

// makeBanner composes the flat 256x128 banner (the CIA default; bannertool turns it into the HOME-menu banner): the
// transparent title logo (menuMdl/ttl_logo) with Suguri's upright dash frame (charaSuguriA/sg_dsh06) and one
// afterimage in front, on a transparent background.
func makeBanner(src string) (*image.NRGBA, error) {
	const W, H = 256, 128
	tex := filepath.Join(src, "texture")
	b := image.NewNRGBA(image.Rect(0, 0, W, H))

	logo, err := loadImage(filepath.Join(tex, "menuMdl", "ttl_logo.png"))
	if err != nil {
		return nil, err
	}
	logo = cropAlpha(logo)
	lw, lh := fitWithin(logo.Bounds().Dx(), logo.Bounds().Dy(), 192, 104) // left part; Suguri on the right
	at := image.Rect(4, (H-lh)/2, 4+lw, (H-lh)/2+lh)
	xdraw.CatmullRom.Scale(b, at, logo, logo.Bounds(), draw.Over, nil)

	s, err := loadImage(filepath.Join(tex, "charaSuguriA", "sg_dsh06.png"))
	if err != nil {
		return nil, err
	}
	s = cropAlpha(s)
	sw, sh := s.Bounds().Dx()*62/100, s.Bounds().Dy()*62/100
	sg := image.NewNRGBA(image.Rect(0, 0, sw, sh))
	xdraw.CatmullRom.Scale(sg, sg.Bounds(), s, s.Bounds(), draw.Src, nil)
	x, y := W-sw-16, H-sh-4
	for _, g := range []struct {
		dx    int
		alpha uint8
	}{{10, 102}, {0, 255}} { // afterimage, then Suguri on top
		r := image.Rect(x+g.dx, y, x+g.dx+sw, y+sh)
		draw.DrawMask(b, r, sg, image.Point{}, &image.Uniform{color.Alpha{g.alpha}}, image.Point{}, draw.Over)
	}
	return b, nil
}
