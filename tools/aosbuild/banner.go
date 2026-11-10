package main

import (
	"image"
	"image/color"
	"image/draw"
	"path/filepath"

	xdraw "golang.org/x/image/draw"
)

// makeBanner composes the flat 256x128 fallback banner (used only when the 3D scene cannot be built): the
// transparent title logo (menuMdl/ttl_logo) with Suguri's first dash frame (charaSuguriA/sg_dsh00) and one
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
	lw, lh := fitWithin(logo.Bounds().Dx(), logo.Bounds().Dy(), 236, 112)
	at := image.Rect((W-lw)/2, (H-lh)/2, (W-lw)/2+lw, (H-lh)/2+lh)
	xdraw.CatmullRom.Scale(b, at, logo, logo.Bounds(), draw.Over, nil)

	s, err := loadImage(filepath.Join(tex, "charaSuguriA", "sg_dsh00.png"))
	if err != nil {
		return nil, err
	}
	s = cropAlpha(s)
	sw, sh := s.Bounds().Dx()*8/10, s.Bounds().Dy()*8/10
	sg := image.NewNRGBA(image.Rect(0, 0, sw, sh))
	xdraw.CatmullRom.Scale(sg, sg.Bounds(), s, s.Bounds(), draw.Src, nil)
	x, y := W-sw-40, H-sh-2
	for _, g := range []struct {
		dx    int
		alpha uint8
	}{{14, 102}, {0, 255}} { // afterimage, then Suguri on top
		r := image.Rect(x+g.dx, y, x+g.dx+sw, y+sh)
		draw.DrawMask(b, r, sg, image.Point{}, &image.Uniform{color.Alpha{g.alpha}}, image.Point{}, draw.Over)
	}
	return b, nil
}
