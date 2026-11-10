package main

// Part D single textures (docs/re/modes.md §D.4, §D.6, ranking §D.2.3), each
// gfx/m3_<name>/page0 -> romfs:/gfx/m3_<name>_0.t3x, loaded by the scene that draws it.
//
// Staff roll (src/app/endroll.c): resMdl/ed_staff1..4.png are 1024x1024 with white text on
// black in the left 512x1024. That half is scaled to 352x512 (x 0.6875 = the PC's 503 -> 352 px
// piece width at s = 1; y 0.5 ~ the PC's 0.44..0.505 vertical factors) and stored as L8
// (drawn additively): 512x512, 256 KB each, loaded only while scene 11 runs.
//
// Ranking (src/app/ranking_draw.c, drawn at s = 0.5): menuMdl/ttl_face.png eye strips
// (400x64 cells) at 0.25 -> 100x16 = their size at s = 0.5; systemAddMdl/ttl_ranking.png
// cursor ring (200,200)-(359,359) + page arrows (200..559, 360..419) at 0.5.

import (
	"fmt"
	"image"
	"os"
	"path/filepath"

	"golang.org/x/image/draw"
)

type modesTex struct {
	name   string          // romfs:/gfx/m3_<name>_0.t3x
	src    string          // relative to texture/
	crop   image.Rectangle // source pixels
	w, h   int             // scaled size (placed at 0,0 of a power-of-two page)
	format string          // tex3ds -f
}

var modesTexs = []modesTex{
	{"staff1", "resMdl/ed_staff1.png", image.Rect(0, 0, 512, 1024), 352, 512, "l8"},
	{"staff2", "resMdl/ed_staff2.png", image.Rect(0, 0, 512, 1024), 352, 512, "l8"},
	{"staff3", "resMdl/ed_staff3.png", image.Rect(0, 0, 512, 1024), 352, 512, "l8"},
	{"staff4", "resMdl/ed_staff4.png", image.Rect(0, 0, 512, 1024), 352, 512, "l8"},
	{"face", "menuMdl/ttl_face.png", image.Rect(0, 0, 1024, 1024), 256, 256, "etc1a4"},
	{"ranking", "systemAddMdl/ttl_ranking.png", image.Rect(200, 200, 560, 420), 180, 110, "rgba4"},
}

func packEndrollTextures(assets, gfx string) error {
	for _, t := range modesTexs {
		img, err := loadPNG(filepath.Join(assets, "texture", t.src))
		if err != nil {
			return err
		}
		dst := image.NewNRGBA(image.Rect(0, 0, nextPow2(t.w), nextPow2(t.h)))
		draw.CatmullRom.Scale(dst, image.Rect(0, 0, t.w, t.h), img, t.crop, draw.Src, nil)
		folder := "m3_" + t.name
		if err := savePNG(filepath.Join(gfx, folder, "page0.png"), dst); err != nil {
			return err
		}
		t3s := fmt.Sprintf("-f %s -z auto\npage0.png\n", t.format)
		if err := os.WriteFile(filepath.Join(gfx, folder, "page0.t3s"), []byte(t3s), 0o644); err != nil {
			return err
		}
	}
	fmt.Printf("modes (staff roll, ranking) textures: %d\n", len(modesTexs))
	return nil
}
