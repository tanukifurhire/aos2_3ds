package main

import (
	"image"
	"image/color"
	"testing"
)

func TestPortraitCrop(t *testing.T) {
	src := image.NewNRGBA(image.Rect(0, 0, 2048, 512))
	// opaque red inside the source rect, opaque blue outside
	for y := 0; y < 512; y++ {
		for x := 0; x < 2048; x++ {
			c := color.NRGBA{0, 0, 255, 255}
			if image.Pt(x, y).In(SrcRect) {
				c = color.NRGBA{255, 0, 0, 255}
			}
			src.SetNRGBA(x, y, c)
		}
	}
	p := Portrait(src, 232, 102)
	if p.Bounds().Dx() != 232 || p.Bounds().Dy() != 102 {
		t.Fatalf("size %v", p.Bounds())
	}
	for _, pt := range []image.Point{{0, 0}, {231, 101}, {116, 50}} {
		if c := p.NRGBAAt(pt.X, pt.Y); c.B > 8 || c.R < 247 {
			t.Fatalf("pixel %v = %v (blue leaked from outside the crop)", pt, c)
		}
	}
}
