package main

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// Steam profile background of the AoS2 cast. Downloaded at runtime into the gitignored .aosbuild/ cache, never
// embedded or committed (it is the game's art, like everything under extracted/).
const wallpaperURL = "https://steamcommunity.com/economy/profilebackground/items/390710/e6fe8931d1c935bcf32292331c34b96816cc87a3.jpg"

func exists(p string) bool { _, err := os.Stat(p); return err == nil }

func isDir(p string) bool {
	st, err := os.Stat(p)
	return err == nil && st.IsDir()
}

func exe(name string) string {
	if runtime.GOOS == "windows" {
		return name + ".exe"
	}
	return name
}

// nativePath turns a Git Bash path (/e/Work/x) into E:/Work/x; other paths are only cleaned.
func nativePath(p string) string {
	p = strings.TrimSpace(strings.TrimRight(p, "\r\n"))
	if p == "" {
		return ""
	}
	if runtime.GOOS == "windows" && len(p) >= 2 && p[0] == '/' && isLetter(p[1]) && (len(p) == 2 || p[2] == '/') {
		p = strings.ToUpper(p[1:2]) + ":/" + strings.TrimPrefix(p[2:], "/")
	}
	return filepath.Clean(p)
}

func isLetter(c byte) bool { return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' }

func isRoot(d string) bool {
	return exists(filepath.Join(d, "build.sh")) && exists(filepath.Join(d, "src", "app", "main.c")) &&
		isDir(filepath.Join(d, "tools", "aos2pack"))
}

// findRoot locates the repo: walks up from the executable (aosbuild.exe sits in the repo root), then from the
// working directory (go run puts the binary in a temp dir).
func findRoot() (string, error) {
	var starts []string
	if p, err := os.Executable(); err == nil {
		starts = append(starts, filepath.Dir(p))
	}
	if wd, err := os.Getwd(); err == nil {
		starts = append(starts, wd)
	}
	for _, s := range starts {
		for d := s; ; {
			if isRoot(d) {
				return d, nil
			}
			p := filepath.Dir(d)
			if p == d {
				break
			}
			d = p
		}
	}
	return "", errors.New("aos2_3ds repo not found: put aosbuild.exe in the repo root (next to build.sh)")
}

// Settings persist in <repo>/.aosbuild/settings.json (gitignored). Profiler and "rebuild assets" are per-run
// choices and always start unchecked.
type Settings struct {
	Game       string `json:"game"`
	Out        string `json:"out,omitempty"`         // empty = <repo>/build
	Target     string `json:"target"`                // "3dsx", "cia" or "both"
	Background string `json:"background,omitempty"`  // custom background image; empty = the downloaded wallpaper
	AnimBanner bool   `json:"anim_banner,omitempty"` // CIA: animated 3D banner (the HOME menu turns it) instead of the flat one
}

func cacheDir(root string) string { return filepath.Join(root, ".aosbuild") }

func defaultOut(root string) string { return filepath.Join(root, "build") }

func (s Settings) outDir(root string) string {
	if s.Out == "" {
		return defaultOut(root)
	}
	return s.Out
}

func loadSettings(root string) Settings {
	s := Settings{Target: "3dsx"}
	if b, err := os.ReadFile(filepath.Join(cacheDir(root), "settings.json")); err == nil {
		json.Unmarshal(b, &s)
	}
	if s.Game == "" { // same lookup as build.sh: game_path.txt, else ../Acceleration of SUGURI 2
		if b, err := os.ReadFile(filepath.Join(root, "game_path.txt")); err == nil {
			s.Game = nativePath(strings.SplitN(string(b), "\n", 2)[0])
		} else if d := filepath.Join(filepath.Dir(root), "Acceleration of SUGURI 2"); isDir(d) {
			s.Game = d
		}
	}
	switch s.Target {
	case "3dsx", "cia", "both":
	default:
		s.Target = "3dsx"
	}
	return s
}

// save writes settings.json and keeps game_path.txt in step so ./build.sh picks up the same game folder.
func (s Settings) save(root string) error {
	if err := os.MkdirAll(cacheDir(root), 0o755); err != nil {
		return err
	}
	b, _ := json.MarshalIndent(s, "", "  ")
	if err := os.WriteFile(filepath.Join(cacheDir(root), "settings.json"), append(b, '\n'), 0o644); err != nil {
		return err
	}
	if gameOK(s.Game) { // never a half-typed path
		gp := filepath.Join(root, "game_path.txt")
		want := filepath.ToSlash(s.Game) + "\n"
		if cur, err := os.ReadFile(gp); err != nil || string(cur) != want {
			return os.WriteFile(gp, []byte(want), 0o644)
		}
	}
	return nil
}

func gameOK(game string) bool { return game != "" && exists(filepath.Join(game, "aos2.exe")) }
