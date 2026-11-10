package main

// Dialogue fonts for the story conversations in the non-Latin languages
// (docs/re/modes.md §C.6, src/app/conv_scene.c). The en "dialogue" family in
// fonts.go already covers en/ptbr/sp (its glyph set is every en/ptbr/sp script
// file, story_*.txt included). Here each other language gets its own font from
// the original's language folder (FUN_00597a50: jp, cn for chs+cht, ko, ru),
// holding only the glyphs of that language's story_*.txt files plus ASCII, at
// the same 16 px line height: romfs/font/dialogue_<lang>.fnt + font_dialogue_<lang>_0.t3x.
// Loaded only while a conversation runs, for the current language.

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type dialogueLang struct {
	lang, fontDir, texFmt string
}

var dialogueLangs = []dialogueLang{
	{"jp", "jp", "la8"},
	{"chs", "cn", "la8"},
	{"cht", "cn", "la8"},
	{"ko", "ko", "la8"},
	{"ru", "ru", "la8"},
}

func storyCodepoints(assets, lang string) map[rune]bool {
	cps := map[rune]bool{}
	for r := rune(32); r < 127; r++ {
		cps[r] = true
	}
	ents, _ := os.ReadDir(filepath.Join(assets, "script", lang))
	for _, e := range ents {
		if !strings.HasPrefix(e.Name(), "story_") {
			continue
		}
		b, err := os.ReadFile(filepath.Join(assets, "script", lang, e.Name()))
		if err != nil {
			continue
		}
		for _, r := range string(b) {
			if r >= 32 && r != 0xfeff {
				cps[r] = true
			}
		}
	}
	return cps
}

func packDialogueLangs(assets, romfs, gfx string) error {
	spec := fontSpec{"dialogue", 16}
	for _, d := range dialogueLangs {
		dir := filepath.Join(assets, "font", d.fontDir)
		if _, err := os.Stat(filepath.Join(dir, "dialogue_1080.fnt")); err != nil {
			fmt.Printf("font dialogue_%s: no %s/dialogue_1080.fnt, skipped\n", d.lang, d.fontDir)
			continue
		}
		n, px, err := packFontAs(dir, "dialogue_"+d.lang, romfs, gfx, spec, storyCodepoints(assets, d.lang), d.texFmt)
		if err != nil {
			return fmt.Errorf("dialogue_%s: %w", d.lang, err)
		}
		bpp := 2
		if d.texFmt == "la4" {
			bpp = 1
		}
		fmt.Printf("font dialogue_%-5s %4d glyphs  page %d px (%s %d KB)\n", d.lang, n, px, strings.ToUpper(d.texFmt), px*bpp/1024)
	}
	return nil
}
