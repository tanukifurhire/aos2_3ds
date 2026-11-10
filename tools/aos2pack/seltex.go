package main

// Select / VS / result / continue / loading textures (docs/re/modes.md §B, Part B).
//
// One texture per image (like the menu textures in fonts.go): crop in original
// texture pixels, scale, power-of-two canvas, tex3ds format. Output
// gfx/m3_b_<name>/page0.{png,t3s} -> romfs:/gfx/m3_b_<name>_0.t3x (tools/convert_pages.sh).
// src/app/selgfx.c knows each crop origin + scale (sel_desc / dynamic classes there).
//
// Scales are chosen for the 3DS top screen at s = 0.4 (the PC's 800x600 layout):
// art is stored at ~1.25x its on-screen size, text-like images at 0.5.
// Per-character images (standing sprites, result art) are packed for every palette
// folder that exists and loaded one at a time by the scenes.

import (
	"fmt"
	"image"
	"os"
	"path/filepath"
)

func selStatic() []menuTex {
	r := func(x0, y0, x1, y1 int) image.Rectangle { return image.Rect(x0, y0, x1, y1) }
	full := func(n int) image.Rectangle { return image.Rect(0, 0, n, n) }
	t := []menuTex{
		// ArcSelect / VS (selMdl, bgMdl, systemMdl)
		{"b_slbgp", "bgMdlAdd/sl_bgp.png", r(0x70, 0xd4, 0x390, 0x32c), 0.5, "etc1"}, // folder 3 (FUN_007abaf0); the 800x600 bgMdl copy does not fit this crop
		{"b_chara00", "selMdl/sel_chara00.png", full(2048), 0.25, "etc1a4"},
		{"b_chara01", "selMdl/sel_chara01.png", full(2048), 0.25, "etc1a4"},
		{"b_chara02", "selMdl/sel_chara02.png", full(2048), 0.25, "etc1a4"},
		{"b_icon00", "selMdl/sel_icon00.png", full(1024), 0.25, "etc1a4"},
		{"b_icon01", "selMdl/sel_icon01.png", full(1024), 0.25, "etc1a4"},
		{"b_name0", "selMdl/sel_name0.png", full(1024), 0.25, "rgba4"},
		{"b_selsys", "selMdl/sel_system.png", r(0, 0xf0, 0x400, 0x280), 0.5, "rgba4"},
		{"b_lockb", "selMdl/lockb.png", full(512), 0.25, "rgba4"},
		{"b_lockbb", "selMdl/lockbb.png", full(512), 0.25, "rgba4"},
		{"b_lockr", "selMdl/lockr.png", full(512), 0.25, "rgba4"},
		{"b_lockrb", "selMdl/lockrb.png", full(512), 0.25, "rgba4"},
		{"b_statbase", "selMdl/charstatbase.png", r(0, 0, 315, 336), 0.5, "rgba4"},
		{"b_barempty", "selMdl/charstatbarempty.png", r(0, 0, 188, 40), 0.5, "rgba4"},
		{"b_bar25", "selMdl/charstatbarfull25.png", r(0, 0, 188, 40), 0.5, "rgba4"},
		{"b_bar50", "selMdl/charstatbarfull50.png", r(0, 0, 188, 40), 0.5, "rgba4"},
		{"b_bar75", "selMdl/charstatbarfull75.png", r(0, 0, 188, 40), 0.5, "rgba4"},
		{"b_barfull", "selMdl/charstatbarfull.png", r(0, 0, 188, 40), 0.5, "rgba4"},
		{"b_rateD", "selMdl/charstatrateD.png", full(128), 0.25, "rgba4"},
		{"b_rateC", "selMdl/charstatrateC.png", full(128), 0.25, "rgba4"},
		{"b_rateB", "selMdl/charstatrateB.png", full(128), 0.25, "rgba4"},
		{"b_rateA", "selMdl/charstatrateA.png", full(128), 0.25, "rgba4"},
		{"b_rateS", "selMdl/charstatrateS.png", full(128), 0.25, "rgba4"},
		{"b_star", "selMdl/simplestar2.png", full(64), 0.25, "rgba4"},
		{"b_stagename", "selMdl/stagename.png", full(1024), 0.5, "rgba4"},
		{"b_loading", "systemMdl/loading.png", r(0, 0, 512, 64), 0.5, "rgba4"},
		// Result / continue (resMdl)
		{"b_ressystem", "resMdl/res_system.png", full(1024), 0.5, "rgba4"},
		{"b_ressystem2", "resMdl/res_system2.png", r(0, 0, 0x2d0, 0xc8), 0.5, "rgba4"},
		{"b_restag", "resMdl/res_tag.png", r(0, 0, 1024, 0x380), 0.5, "rgba4"},
		{"b_rescont", "resMdl/res_continue.png", r(0, 0, 1024, 0x250), 0.5, "rgba4"},
	}
	for i := 0; i < 8; i++ { // stage-card noise (selMdl/stagenz0..7)
		t = append(t, menuTex{fmt.Sprintf("b_stagenz%d", i), fmt.Sprintf("selMdl/stagenz%d.png", i), full(256), 0.5, "etc1"})
	}
	for _, s := range []string{"bf", "br", "ct", "dr", "ft", "gr", "hl", "iw", "ml", "rd", "ri", "se", "tr"} {
		t = append(t, menuTex{"b_stage" + s, "selMdl/stage" + s + ".png", full(256), 0.5, "etc1"})
	}
	return t
}

// Per palette folder: standing sprites <xx>_std00.. (VS select), res_chr (result art),
// res_sys (name plate + band cell); per character resMdlAdd/<xx>_res_bgp.
func selDynamic(assets string) []menuTex {
	var t []menuTex
	tex := filepath.Join(assets, "texture")
	ents, _ := os.ReadDir(tex)
	for _, e := range ents {
		d := e.Name()
		if !e.IsDir() || len(d) < 5 || d[:5] != "chara" {
			continue
		}
		files, _ := os.ReadDir(filepath.Join(tex, d))
		for _, f := range files {
			n := f.Name()
			switch {
			case n == "res_chr.png":
				t = append(t, menuTex{"b_reschr_" + d, d + "/" + n, image.Rect(0, 0, 2048, 2048), 0.25, "etc1a4"})
			case n == "res_sys.png":
				t = append(t, menuTex{"b_ressys_" + d, d + "/" + n, image.Rect(0, 0, 1024, 0xc0), 0.5, "rgba4"})
			case len(n) > 10 && n[len(n)-10:len(n)-6] == "_std": // <xx>_std0N.png
				t = append(t, menuTex{"b_std_" + d + "_" + n[len(n)-6:len(n)-4], d + "/" + n,
					image.Rect(0, 0, 256, 256), 0.5, "rgba4"})
			}
		}
	}
	bg, _ := os.ReadDir(filepath.Join(tex, "resMdlAdd"))
	for _, f := range bg {
		n := f.Name()
		if len(n) == len("xx_res_bgp.png") && n[2:] == "_res_bgp.png" {
			t = append(t, menuTex{"b_resbgp_" + n[:2], "resMdlAdd/" + n, image.Rect(0, 0x96, 0x800, 0x36a), 0.25, "etc1"})
		}
	}
	return t
}

func packSelectTextures(assets, gfx string) error {
	list := append(selStatic(), selDynamic(assets)...)
	saved := menuTexs
	menuTexs = list
	err := packMenuTextures(assets, gfx)
	menuTexs = saved
	if err == nil {
		fmt.Printf("select/result textures: %d\n", len(list))
	}
	return err
}
