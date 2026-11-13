package main

// Port of build.sh (and tools/convert_pages.sh): extract -> pack -> compile -> .3dsx, with the same tools, flags
// and working directories, so both produce the same build. Keep the two in step until build.sh is retired.

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
)

type BuildOpts struct {
	Root, Game, Out string
	Rebuild         bool // re-pack assets even when Out already has them
	Profile         bool // -DAOS_PROFILE (on-device profiler, src/app/prof.h)
	Make3dsx        bool // package the .3dsx (a CIA-only build stops at the .elf)
	AnimBanner      bool // CIA: animated 3D banner via pycgfx (the HOME menu turns it); else the flat PNG banner
	TC              Toolchain
}

// Same list and order as SRCS in build.sh (globs expand sorted, like sh).
var srcList = strings.Fields(`src/net/rollback.c src/net/uds_link.c src/app/netplay.c src/app/netsel.c src/input/aos_input.c
src/input/hyper_touch.c src/app/movelist.c src/app/wav.c src/app/audio.c src/app/music.c src/app/lab.c src/app/prof.c
src/app/main.c src/app/scene.c src/app/scene_core.c src/app/strings.c src/app/bmfont.c src/app/text_c2d.c src/app/ui.c
src/app/menu.c src/app/menu_draw.c src/platform/spritebank.c src/platform/bgm.c src/platform/render.c
src/platform/sprbatch.c src/platform/stage.c src/stage/*.c src/platform/hud.c src/platform/hud_c2d.c src/platform/panel.c
src/sim/*.c src/sim/chars/*.c src/modes/ladder.c src/modes/score.c src/modes/save.c src/app/modes_flow.c
src/app/endroll.c src/app/ranking.c src/app/ranking_draw.c src/modes/conv.c src/app/conv_scene.c src/app/select.c
src/app/select_draw.c src/app/result_draw.c src/app/selgfx.c`)

var archFlags = []string{"-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"}

func expandSources(root string) ([]string, error) {
	var out []string
	for _, s := range srcList {
		if !strings.Contains(s, "*") {
			out = append(out, s)
			continue
		}
		m, err := filepath.Glob(filepath.Join(root, filepath.FromSlash(s)))
		if err != nil || len(m) == 0 {
			return nil, fmt.Errorf("no sources match %s", s)
		}
		for _, p := range m { // Glob returns sorted matches
			r, _ := filepath.Rel(root, p)
			out = append(out, filepath.ToSlash(r))
		}
	}
	return out, nil
}

type assetState int

const (
	assetsNone  assetState = iota // no finished asset pass in Out
	assetsStale                   // packed, but an asset tool changed since
	assetsReady                   // packed and current: reused unless Rebuild
)

// assetSources are the inputs of the asset pass besides the game data: the asset tools' code, the BGM table
// and assetflags.go (atlas scales). Any of them newer than the last pack makes it stale.
func assetSources(root string) []string {
	var fs []string
	for _, t := range []string{"aos2extract", "aos2pack", "aos2story", "aos2mesh"} {
		filepath.WalkDir(filepath.Join(root, "tools", t), func(p string, d os.DirEntry, err error) error {
			if err == nil && !d.IsDir() && (strings.HasSuffix(p, ".go") || d.Name() == "go.mod" || d.Name() == "go.sum") {
				fs = append(fs, p)
			}
			return nil
		})
	}
	return append(fs, filepath.Join(root, "docs", "re", "bgm_table.tsv"), filepath.Join(root, "tools", "aosbuild", "assetflags.go"))
}

// assetStatus: the smdh is the asset pass's last step and is deleted when a pass starts, so an interrupted pack
// is never mistaken for a finished one; its mtime dates the pack.
func assetStatus(root, out string) assetState {
	st, err := os.Stat(filepath.Join(out, "aos2_3ds.smdh"))
	if err != nil || !isDir(filepath.Join(out, "romfs")) {
		return assetsNone
	}
	for _, f := range assetSources(root) {
		if fs, err := os.Stat(f); err == nil && fs.ModTime().After(st.ModTime()) {
			return assetsStale
		}
	}
	return assetsReady
}

func buildStages(o BuildOpts) []stage {
	root, out, tc := o.Root, o.Out, o.TC
	ext := filepath.Join(root, "extracted")
	toolDir := func(name string) string { return filepath.Join(root, "tools", name) }
	toolExe := func(name string) string { return filepath.Join(toolDir(name), exe(name)) }
	tex3ds := tc.tool("tex3ds")
	var st []stage

	if state := assetStatus(root, out); o.Rebuild || state != assetsReady {
		if state == assetsStale && !o.Rebuild {
			st = append(st, stage{"Asset tools changed since the last pack: re-packing", 0, func(*Job, func(float64)) error { return nil }})
		}
		st = append(st,
			stage{"Extract game data", 12, func(j *Job, _ func(float64)) error {
				os.Remove(filepath.Join(out, "aos2_3ds.smdh"))
				if err := os.MkdirAll(filepath.Join(out, "obj"), 0o755); err != nil {
					return err
				}
				needTex, needBGM := !isDir(filepath.Join(ext, "texture")), !isDir(filepath.Join(ext, "bgm"))
				needConv := !isDir(filepath.Join(ext, "script_res"))
				if !gameOK(o.Game) { // aos2mesh reads the game folder on every asset pass, not just extraction
					return fmt.Errorf("game folder %q has no aos2.exe: pick your Acceleration of SUGURI 2 install", o.Game)
				}
				if !needTex && !needBGM && !needConv {
					j.line("already extracted (delete extracted/ to re-extract)")
					return nil
				}
				if err := j.goBuild(toolDir("aos2extract"), exe("aos2extract")); err != nil {
					return err
				}
				x := func(extra ...string) error {
					return j.run(toolDir("aos2extract"), toolExe("aos2extract"), append([]string{"-game", o.Game, "-out", ext}, extra...)...)
				}
				if needTex {
					if err := x("-skip-bgm"); err != nil {
						return err
					}
				}
				if !isDir(filepath.Join(ext, "bgm")) {
					if err := x(); err != nil {
						return err
					}
				}
				if needConv {
					return x("-conv-only")
				}
				return nil
			}},
			stage{"Pack sprites, fonts, menus, music", 25, func(j *Job, _ func(float64)) error {
				if err := j.goBuild(toolDir("aos2pack"), exe("aos2pack")); err != nil {
					return err
				}
				return j.run(toolDir("aos2pack"), toolExe("aos2pack"), "-assets", ext, "-out", filepath.Join(out, "romfs"),
					"-gfx", filepath.Join(out, "gfx"), "-bgm", filepath.Join(root, "docs", "re", "bgm_table.tsv"),
					"-fscale", packScales, "-edge", packEdge, "-exclude", "chara*/res_*")
			}},
			stage{"Convert texture pages (tex3ds)", 15, func(j *Job, sub func(float64)) error {
				return convertPages(j, tex3ds, out, sub)
			}},
			stage{"Story conversations", 6, func(j *Job, _ func(float64)) error {
				if err := j.goBuild(toolDir("aos2story"), exe("aos2story")); err != nil {
					return err
				}
				return j.run(root, toolExe("aos2story"), "-assets", ext, "-romfs", filepath.Join(out, "romfs"),
					"-work", filepath.Join(out, "storygfx"), "-tex3ds", tex3ds)
			}},
			stage{"Stage backgrounds", 10, func(j *Job, _ func(float64)) error {
				if err := j.goBuild(toolDir("aos2mesh"), exe("aos2mesh")); err != nil {
					return err
				}
				return j.run(root, toolExe("aos2mesh"), "-game", o.Game, "-tex", filepath.Join(ext, "texture"),
					"-romfs", filepath.Join(out, "romfs"), "-host", filepath.Join(out, "stagehost"), "-tex3ds", tex3ds,
					"-work", filepath.Join(out, "stagetex"), "-texdiv", "2")
			}},
			stage{"Homebrew icon (smdh)", 1, func(j *Job, _ func(float64)) error {
				args := []string{"--create", "AoS2 3DS input lab", "Acceleration of SUGURI 2 port - WIP", "personal research build"}
				smdh := filepath.Join(out, "aos2_3ds.smdh")
				if _, err := j.output(root, tc.tool("smdhtool"), append(args, filepath.Join(out, "gfx", "icon.png"), smdh)...); err == nil {
					return nil
				}
				return j.run(root, tc.tool("smdhtool"), append(args, filepath.Join(tc.DKP, "libctru", "default_icon.png"), smdh)...)
			}},
		)
	} else {
		st = append(st, stage{"Reuse packed assets", 0, func(j *Job, _ func(float64)) error {
			j.logf("assets in %s are up to date (tick \"Rebuild assets\" to re-pack)", out)
			return nil
		}})
	}

	st = append(st,
		stage{"Shaders (picasso)", 1, func(j *Job, _ func(float64)) error {
			for _, sh := range [][2]string{{"stage/stage.shbin", "src/platform/stage.v.pica"}, {"shader/sprite.shbin", "src/platform/sprite.v.pica"}} {
				dst := filepath.Join(out, "romfs", filepath.FromSlash(sh[0]))
				if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
					return err
				}
				if err := j.run(root, tc.tool("picasso"), "-o", dst, sh[1]); err != nil {
					return err
				}
			}
			return nil
		}},
		stage{"Compile", 15, func(j *Job, sub func(float64)) error { return compile(j, o, sub) }},
		stage{"Link", 3, func(j *Job, _ func(float64)) error { return link(j, o) }},
	)
	if o.Make3dsx {
		st = append(st, stage{"Package .3dsx (3dsxtool)", 8, func(j *Job, _ func(float64)) error {
			if err := j.run(root, tc.tool("3dsxtool"), filepath.Join(out, "aos2_3ds.elf"), filepath.Join(out, "aos2_3ds.3dsx"),
				"--smdh="+filepath.Join(out, "aos2_3ds.smdh"), "--romfs="+filepath.Join(out, "romfs")); err != nil {
				return err
			}
			return logSize(j, filepath.Join(out, "aos2_3ds.3dsx"))
		}})
	}
	return st
}

func objPath(out, src string) string {
	return filepath.Join(out, "obj", strings.TrimSuffix(strings.ReplaceAll(src, "/", "_"), ".c")+".o")
}

func compile(j *Job, o BuildOpts, sub func(float64)) error {
	srcs, err := expandSources(o.Root)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Join(o.Out, "obj"), 0o755); err != nil {
		return err
	}
	dkp := filepath.ToSlash(o.TC.DKP)
	cflags := []string{"-g", "-O2"}
	if o.Profile {
		cflags = append(cflags, "-DAOS_PROFILE")
	}
	cflags = append(cflags, "-Wall", "-Wextra", "-Werror", "-Wno-stringop-overread", "-std=gnu11", "-ffunction-sections")
	cflags = append(cflags, archFlags...)
	cflags = append(cflags, "-D__3DS__", "-I"+dkp+"/libctru/include", "-I"+dkp+"/portlibs/3ds/include", "-I"+filepath.ToSlash(o.Out))
	cc := filepath.Join(o.TC.DKP, "devkitARM", "bin", exe("arm-none-eabi-gcc"))
	if o.Profile {
		j.line("profiler build (-DAOS_PROFILE)")
	}

	ctx, cancel := context.WithCancel(j.ctx)
	defer cancel()
	cj := *j
	cj.ctx = ctx
	var (
		wg       sync.WaitGroup
		mu       sync.Mutex
		firstErr error
		done     atomic.Int32
	)
	sem := make(chan struct{}, runtime.NumCPU())
	for _, s := range srcs {
		wg.Add(1)
		go func() {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			if ctx.Err() != nil {
				return
			}
			args := append(append([]string{}, cflags...), "-c", s, "-o", objPath(o.Out, s))
			msg, err := cj.output(o.Root, cc, args...)
			if msg != "" {
				j.line(msg)
			}
			if err != nil {
				mu.Lock()
				if firstErr == nil {
					firstErr = fmt.Errorf("%s: %w", s, err)
					cancel()
				}
				mu.Unlock()
				return
			}
			sub(float64(done.Add(1)) / float64(len(srcs)))
		}()
	}
	wg.Wait()
	if j.ctx.Err() != nil {
		return j.ctx.Err()
	}
	if firstErr != nil {
		return firstErr
	}
	j.logf("%d files compiled", len(srcs))
	return nil
}

func link(j *Job, o BuildOpts) error {
	srcs, err := expandSources(o.Root)
	if err != nil {
		return err
	}
	dkp := o.TC.DKP
	args := append([]string{"-specs=3dsx.specs", "-g"}, archFlags...)
	args = append(args, "-Wl,-Map,"+filepath.Join(o.Out, "aos2_3ds.map"))
	for _, s := range srcs {
		args = append(args, objPath(o.Out, s))
	}
	args = append(args, "-L"+filepath.Join(dkp, "portlibs", "3ds", "lib"), "-L"+filepath.Join(dkp, "libctru", "lib"),
		"-lcitro2d", "-lcitro3d", "-lvorbisidec", "-logg", "-lctru", "-lm", "-o", filepath.Join(o.Out, "aos2_3ds.elf"))
	return j.run(o.Root, filepath.Join(dkp, "devkitARM", "bin", exe("arm-none-eabi-gcc")), args...)
}

// convertPages is tools/convert_pages.sh: every <out>/gfx/<folder>/pageN.t3s -> <out>/romfs/gfx/<folder>_N.t3x,
// 8 at a time, skipping pages whose .t3x is newer than the .t3s and dropping .t3x a repack no longer has.
func convertPages(j *Job, tex3ds, out string, sub func(float64)) error {
	gfx, dstDir := filepath.Join(out, "gfx"), filepath.Join(out, "romfs", "gfx")
	if err := os.MkdirAll(dstDir, 0o755); err != nil {
		return err
	}
	old, _ := filepath.Glob(filepath.Join(dstDir, "*.t3x"))
	for _, t3x := range old {
		b := strings.TrimSuffix(filepath.Base(t3x), ".t3x")
		i := strings.LastIndexByte(b, '_')
		if i < 0 {
			continue
		}
		folder, n := b[:i], b[i+1:]
		if _, err := strconv.Atoi(n); err != nil || strings.ContainsAny(n, "+-") {
			continue
		}
		if isDir(filepath.Join(gfx, folder)) && !exists(filepath.Join(gfx, folder, "page"+n+".t3s")) {
			os.Remove(t3x)
		}
	}

	var pages []string
	filepath.WalkDir(gfx, func(p string, d os.DirEntry, err error) error {
		if err == nil && !d.IsDir() && strings.HasPrefix(d.Name(), "page") && strings.HasSuffix(d.Name(), ".t3s") {
			pages = append(pages, p)
		}
		return nil
	})
	var todo [][2]string
	for _, p := range pages {
		folder := filepath.Base(filepath.Dir(p))
		page := strings.TrimSuffix(filepath.Base(p), ".t3s")
		dst := filepath.Join(dstDir, folder+"_"+strings.TrimPrefix(page, "page")+".t3x")
		if ds, err := os.Stat(dst); err == nil {
			if ss, err := os.Stat(p); err == nil && ds.ModTime().After(ss.ModTime()) {
				continue
			}
		}
		todo = append(todo, [2]string{p, dst})
	}
	j.logf("%d pages, %d to convert", len(pages), len(todo))

	var (
		wg    sync.WaitGroup
		mu    sync.Mutex
		fails int
		done  atomic.Int32
	)
	sem := make(chan struct{}, 8)
	for _, t := range todo {
		wg.Add(1)
		go func() {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			if j.ctx.Err() != nil {
				return
			}
			src, dst := t[0], t[1]
			msg, err := j.output(filepath.Dir(src), tex3ds, "-i", filepath.Base(src), "-o", dst)
			if err != nil {
				os.Remove(dst) // a partial .t3x newer than its .t3s would be skipped forever
				if j.ctx.Err() == nil {
					mu.Lock()
					fails++
					mu.Unlock()
					j.logf("FAIL %s %s: %s", filepath.Base(filepath.Dir(src)), filepath.Base(src), msg)
				}
				return
			}
			sub(float64(done.Add(1)) / float64(len(todo)))
		}()
	}
	wg.Wait()
	if j.ctx.Err() != nil {
		return j.ctx.Err()
	}
	if fails > 0 {
		return fmt.Errorf("%d page(s) failed", fails)
	}
	return nil
}

func logSize(j *Job, p string) error {
	st, err := os.Stat(p)
	if err != nil {
		return err
	}
	j.logf("%s  %.1f MB", p, float64(st.Size())/(1<<20))
	return nil
}
