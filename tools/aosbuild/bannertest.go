package main

// Banner test kit (aosbuild bannertest): tiny CIAs, each its own title, that differ only in the HOME-menu banner,
// to find which banner feature a real HOME menu chokes on. They carry the game's .elf but an empty romfs, so they
// are only for hovering on the HOME menu, not for launching.

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
)

type bannerVariant struct {
	letter, desc string
	flat         bool      // bannertool's flat PNG banner instead of a pycgfx scene
	mode         sceneMode // scene variant (when not flat)
	jingle       bool      // assets/cia/jingle.wav instead of the menu "decide" sound
	solo         bool      // Suguri without the afterimage
}

// Round 1 (A flat, B static scene, C moving, D/E flip-book with stepped scale on child nodes): A-C fine, D and
// E crashed the HOME menu (data abort). Round 2, the reworked flip-book (parked top-level quads, with and without
// the afterimage) and the jingle alone: F, G and H all fine on hardware. Round 3: F with the jingle, zoomed out,
// with and without the sway as rotation keys: J fine, I froze the whole system. Round 4, the upright dash
// (sg_dsh06-08): K (sway by swapping pre-turned logo copies) ran but glitched, L (no sway) fine. Round 5: the
// back-and-forth dash (mirrored on the way back), the shipped banner.
var bannerVariants = []bannerVariant{
	{"M", "upright dash back and forth, eased, paper-flip turns + afterimage, jingle", false, sceneFlipbook, true, false},
}

var (
	rsfUniqueID = regexp.MustCompile(`(?m)^(\s*UniqueId\s*:\s*)0x[0-9A-Fa-f]+`)
	rsfTitle    = regexp.MustCompile(`(?m)^(\s*Title\s*:\s*).*$`)
)

func bannerTestStages(o BuildOpts) []stage {
	root, tc := o.Root, o.TC
	base := filepath.Join(o.Out, "bannertest")
	src := filepath.Join(root, "extracted")
	var st []stage
	st = append(st, stage{"Prepare", 1, func(j *Job, _ func(float64)) error {
		if !tc.readyCIA() {
			return errors.New(`makerom/bannertool missing: press "Set up toolchain"`)
		}
		if !exists(filepath.Join(o.Out, "aos2_3ds.elf")) {
			return errors.New("build the game once first (no aos2_3ds.elf in " + o.Out + ")")
		}
		os.RemoveAll(base)
		romfs := filepath.Join(base, "romfs")
		if err := os.MkdirAll(romfs, 0o755); err != nil {
			return err
		}
		return os.WriteFile(filepath.Join(romfs, "bannertest.txt"), []byte("banner test kit: not a playable build\n"), 0o644)
	}})
	for _, v := range bannerVariants {
		st = append(st, stage{fmt.Sprintf("Test CIA %s: %s", v.letter, v.desc), 4, func(j *Job, _ func(float64)) error {
			w := filepath.Join(base, v.letter)
			if err := os.MkdirAll(w, 0o755); err != nil {
				return err
			}
			banner, err := makeBanner(src)
			if err != nil {
				return err
			}
			if err := savePNG(filepath.Join(w, "banner.png"), banner); err != nil {
				return err
			}
			if icon, err := gameIcon(filepath.Join(o.Game, "aos2.exe")); err == nil {
				err = savePNG(filepath.Join(w, "icon.png"), icon)
			} else if err := portraitIcon(src, filepath.Join(w, "icon.png")); err != nil {
				return err
			}
			wav := filepath.Join(src, "se", "se", "SH_DECIDE.WAV")
			if v.jingle {
				if wav = filepath.Join(root, "assets", "cia", "jingle.wav"); !exists(wav) {
					return errors.New("no assets/cia/jingle.wav")
				}
			}
			if err := copyFileSimple(wav, filepath.Join(w, "banner.wav")); err != nil {
				return err
			}
			img := []string{"-i", filepath.Join(w, "banner.png")}
			if !v.flat {
				members := trail
				if v.solo {
					members = trail[len(trail)-1:]
				}
				cgfx, err := makeSceneBanner(j, tc, src, w, v.mode, members)
				if err != nil {
					return err
				}
				img = []string{"-ci", cgfx}
			}
			if err := j.run(root, tc.Bannertool, append(append([]string{"makebanner"}, img...), "-a", filepath.Join(w, "banner.wav"),
				"-o", filepath.Join(w, "banner.bnr"))...); err != nil {
				return err
			}
			if err := j.run(root, tc.Bannertool, "makesmdh", "-s", "Banner test "+v.letter, "-l", v.desc,
				"-p", "aosbuild banner test", "-i", filepath.Join(w, "icon.png"), "-o", filepath.Join(w, "icon.smdh")); err != nil {
				return err
			}
			rsf, err := os.ReadFile(filepath.Join(root, "assets", "cia", "aos2_3ds.rsf"))
			if err != nil {
				return err
			}
			rsf = rsfUniqueID.ReplaceAll(rsf, []byte(fmt.Sprintf("${1}0x%X", 0xFA2A0+int(v.letter[0]-'A')))) // own title per variant
			rsf = rsfTitle.ReplaceAll(rsf, []byte("${1}AoS2 BT "+v.letter))
			if err := os.WriteFile(filepath.Join(w, "test.rsf"), rsf, 0o644); err != nil {
				return err
			}
			cia := filepath.Join(base, "bannertest_"+v.letter+".cia")
			if err := j.run(root, tc.Makerom, "-f", "cia", "-o", cia, "-target", "t", "-exefslogo",
				"-elf", filepath.Join(o.Out, "aos2_3ds.elf"), "-rsf", filepath.Join(w, "test.rsf"),
				"-icon", filepath.Join(w, "icon.smdh"), "-banner", filepath.Join(w, "banner.bnr"),
				"-DAPP_ROMFS="+filepath.Join(base, "romfs")); err != nil {
				return err
			}
			return logSize(j, cia)
		}})
	}
	return st
}
