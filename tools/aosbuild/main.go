// aosbuild: one-window builder for the AoS2 3DS port. Wraps the toolchain setup (tools/setup.sh), the build
// (build.sh) and CIA packaging (tools/make_cia.sh), so nothing needs Git Bash.
//
//	aosbuild                 open the GUI
//	aosbuild setup           fetch devkitARM etc. into tools/devkitpro, makerom/bannertool into tools/ctrtools
//	aosbuild 3dsx|cia|both   build (flags: -game DIR -out DIR -rebuild -profile)
//	aosbuild status          print what is installed and found
//
// Build the exe (once, needs Go): tools/aosbuild/make_exe.cmd -> aosbuild.exe in the repo root.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"sync"
)

func main() {
	if len(os.Args) > 1 {
		attachConsole()
		os.Exit(cli(os.Args[1], os.Args[2:]))
	}
	runGUI()
}

func cli(cmd string, args []string) int {
	root, err := findRoot()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		return 1
	}
	s := loadSettings(root)
	fs := flag.NewFlagSet("aosbuild "+cmd, flag.ContinueOnError)
	game := fs.String("game", s.Game, "Acceleration of SUGURI 2 folder (with aos2.exe)")
	out := fs.String("out", s.outDir(root), "output folder")
	rebuild := fs.Bool("rebuild", false, "re-pack assets even if the output folder has them")
	profile := fs.Bool("profile", false, "profiler build (-DAOS_PROFILE)")
	static := fs.Bool("static-banner", s.StaticBanner, "CIA: static PNG banner instead of the animated one")
	shot := fs.String("o", "aosbuild.png", "for screenshot: output png")
	if err := fs.Parse(args); err != nil {
		return 2
	}
	s.Game, s.Out, s.StaticBanner = nativePath(*game), nativePath(*out), *static
	tc := findToolchain(root)

	var stages []stage
	switch cmd {
	case "setup":
		stages = setupStages(root)
	case "3dsx", "cia", "both":
		s.Target = cmd
		stages, err = buildPipeline(root, Request{Settings: s, Rebuild: *rebuild, Profile: *profile}, tc)
	case "bannertest": // tiny CIAs differing only in the banner, to bisect a HOME menu crash
		if !tc.readyCIA() {
			err = fmt.Errorf("CIA tools (makerom, bannertool) not installed: %s", setupHint)
			break
		}
		stages = bannerTestStages(BuildOpts{Root: root, Game: s.Game, Out: s.outDir(root), TC: tc})
	case "status":
		printStatus(root, s, tc)
		return 0
	case "screenshot": // dev: render the GUI headless to a png
		if err := screenshot(root, *shot); err != nil {
			fmt.Fprintln(os.Stderr, err)
			return 1
		}
		return 0
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q (setup, 3dsx, cia, both, status; no command opens the GUI)\n", cmd)
		return 2
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		return 1
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	logf := openLog(s.outDir(root), cmd)
	var mu sync.Mutex
	j := newJob(ctx, tc, func(l string) {
		mu.Lock()
		fmt.Println(l)
		if logf != nil {
			fmt.Fprintln(logf, l)
		}
		mu.Unlock()
	}, func(float64, string) {})
	err = j.runStages(stages)
	if logf != nil {
		if err != nil {
			fmt.Fprintln(logf, "error:", err)
		}
		logf.Close()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		return 1
	}
	return 0
}

func printStatus(root string, s Settings, tc Toolchain) {
	yes := func(b bool) string {
		if b {
			return "ok"
		}
		return "MISSING"
	}
	fmt.Println("repo      ", root)
	fmt.Println("go        ", yes(tc.Go != ""), tc.Go)
	fmt.Println("devkitPro ", yes(tc.DKPOK), tc.DKP)
	fmt.Println("makerom   ", yes(tc.Makerom != ""), tc.Makerom)
	fmt.Println("bannertool", yes(tc.Bannertool != ""), tc.Bannertool)
	fmt.Println("game      ", yes(gameOK(s.Game)), s.Game)
	fmt.Println("output    ", s.outDir(root), "(assets:", [...]string{"not packed", "stale, will re-pack", "packed, reused"}[assetStatus(root, s.outDir(root))]+")")
}
