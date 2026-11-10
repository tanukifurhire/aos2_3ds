package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// Request is one press of Build (or one CLI build command).
type Request struct {
	Settings
	Rebuild, Profile bool
}

const setupHint = `press "Set up toolchain" (or run: aosbuild setup)`

func buildPipeline(root string, r Request, tc Toolchain) ([]stage, error) {
	switch {
	case tc.Go == "":
		return nil, errors.New("Go is not on PATH (https://go.dev/dl/)")
	case !tc.DKPOK:
		return nil, errors.New("3DS toolchain not installed: " + setupHint)
	case r.Target != "3dsx" && !tc.readyCIA():
		return nil, errors.New("CIA tools (makerom, bannertool) not installed: " + setupHint)
	}
	o := BuildOpts{Root: root, Game: r.Game, Out: r.outDir(root), Rebuild: r.Rebuild, Profile: r.Profile,
		Make3dsx: r.Target != "cia", AnimBanner: !r.StaticBanner, TC: tc}
	if (o.Rebuild || assetStatus(root, o.Out) != assetsReady) && !gameOK(o.Game) {
		return nil, fmt.Errorf("game folder %q has no aos2.exe: pick your Acceleration of SUGURI 2 install", o.Game)
	}
	st := buildStages(o)
	if r.Target != "3dsx" {
		st = append(st, ciaStages(o)...)
	}
	return st, nil
}

// openLog starts <out>/aosbuild.log for a run (best effort: a nil file just means no log file).
func openLog(out, what string) *os.File {
	if os.MkdirAll(out, 0o755) != nil {
		return nil
	}
	f, err := os.Create(filepath.Join(out, "aosbuild.log"))
	if err != nil {
		return nil
	}
	fmt.Fprintf(f, "aosbuild %s  %s\n", what, time.Now().Format("2006-01-02 15:04:05"))
	return f
}
