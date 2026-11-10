// Command aos2story builds the story-conversation data of the 3DS port
// (docs/FORMATS.md "Story conversations", docs/re/modes.md §C):
//
//	<romfs>/story/res/TEXT_CONVERSATION<id>.txt  the 56 aos2.exe resources (verbatim)
//	<romfs>/story/<lang>/story_<char>_0<n>.txt     story text, 8 languages (verbatim)
//	<romfs>/story/portrait/<name>.t3x              convMdl portraits, RGBA8
//
// Scripts and texts are copied unchanged: src/modes/conv.c parses them with
// the original parsers (FUN_005d6430 / FUN_005d7620), a few KB per scene.
//
// Portraits: the PC draws src rect (0x1b8,0)-(0x647,0x1ff) of the 2048x512
// texture into a 2·(int)(292s) x 2·(int)(128s) rect (FUN_004e54b0). We crop
// that rect and resample it (Catmull-Rom, premultiplied) to the on-screen size
// at the top screen's s = 0.4: 232x102, so the 3DS draws 1:1. tex3ds pads it to
// 256x128 (one sub-texture); RGBA8 = 128 KB of linear memory per portrait.
// none.png (fully transparent) is not converted: name id 0 draws nothing.
package main

import (
	"bytes"
	"flag"
	"fmt"
	"image"
	"image/png"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"golang.org/x/image/draw"
)

var langs = []string{"en", "jp", "chs", "cht", "ptbr", "sp", "ko", "ru"}

// SrcRect is the portrait source rect of FUN_004e54b0 (SetRect 0x1b8,0,0x647,0x1ff).
var SrcRect = image.Rect(0x1b8, 0, 0x647, 0x1ff)

func main() {
	assets := flag.String("assets", "extracted", "aos2extract output directory")
	romfs := flag.String("romfs", "build/romfs", "romfs directory")
	work := flag.String("work", "build/storygfx", "intermediate PNGs")
	tex3ds := flag.String("tex3ds", "", "tex3ds executable (empty: skip portraits)")
	pw := flag.Int("pw", 232, "portrait width on screen (2*(int)(292*s))")
	ph := flag.Int("ph", 102, "portrait height on screen (2*(int)(128*s))")
	flag.Parse()
	out := filepath.Join(*romfs, "story")

	res, err := filepath.Glob(filepath.Join(*assets, "script_res", "TEXT_CONVERSATION*.txt"))
	must(err)
	if len(res) != 56 {
		log.Fatalf("expected 56 conversation scripts in %s/script_res, found %d (run aos2extract -conv-only)", *assets, len(res))
	}
	for _, p := range res {
		copyFile(p, filepath.Join(out, "res", filepath.Base(p)))
	}
	nt := 0
	for _, l := range langs {
		txt, err := filepath.Glob(filepath.Join(*assets, "script", l, "story_*.txt"))
		must(err)
		if len(txt) != 56 {
			log.Fatalf("%s: expected 56 story files, found %d", l, len(txt))
		}
		for _, p := range txt {
			copyFile(p, filepath.Join(out, l, filepath.Base(p)))
			nt++
		}
	}
	np := 0
	if *tex3ds != "" {
		pngs, err := filepath.Glob(filepath.Join(*assets, "texture", "convMdl", "*.png"))
		must(err)
		for _, p := range pngs {
			name := strings.TrimSuffix(filepath.Base(p), ".png")
			if name == "none" {
				continue
			}
			img := Portrait(load(p), *pw, *ph)
			var buf bytes.Buffer
			must(png.Encode(&buf, img))
			stage := filepath.Join(*work, name+".png")
			dst := filepath.Join(out, "portrait", name+".t3x")
			must(os.MkdirAll(filepath.Dir(stage), 0o755))
			must(os.MkdirAll(filepath.Dir(dst), 0o755))
			if old, err := os.ReadFile(stage); err == nil && bytes.Equal(old, buf.Bytes()) {
				if _, err := os.Stat(dst); err == nil {
					np++
					continue
				}
			}
			must(os.WriteFile(stage, buf.Bytes(), 0o644))
			if o, err := exec.Command(*tex3ds, "-f", "rgba8888", "-z", "auto", "-o", dst, stage).CombinedOutput(); err != nil {
				log.Fatalf("tex3ds %s: %v %s", name, err, o)
			}
			np++
		}
	}
	fmt.Printf("aos2story: %d scripts, %d story texts, %d portraits\n", len(res), nt, np)
}

// Portrait crops SrcRect and resamples it to w x h.
func Portrait(src image.Image, w, h int) *image.NRGBA {
	r := SrcRect.Add(src.Bounds().Min).Intersect(src.Bounds())
	dst := image.NewRGBA(image.Rect(0, 0, w, h))
	draw.CatmullRom.Scale(dst, dst.Bounds(), src, r, draw.Src, nil)
	out := image.NewNRGBA(dst.Bounds())
	draw.Draw(out, out.Bounds(), dst, image.Point{}, draw.Src)
	return out
}

func load(p string) image.Image {
	f, err := os.Open(p)
	must(err)
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		log.Fatalf("%s: %v", p, err)
	}
	return img
}

func copyFile(src, dst string) {
	b, err := os.ReadFile(src)
	must(err)
	if old, err := os.ReadFile(dst); err == nil && bytes.Equal(old, b) {
		return
	}
	must(os.MkdirAll(filepath.Dir(dst), 0o755))
	must(os.WriteFile(dst, b, 0o644))
}

func must(err error) {
	if err != nil {
		log.Fatal(err)
	}
}
