package main

import (
	"archive/zip"
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// Toolchain is what a build needs from the machine. Everything but Go is project-local under tools/
// (gitignored): devkitPro in tools/devkitpro (dkpfetch), makerom/bannertool in tools/ctrtools.
type Toolchain struct {
	Go         string // go on PATH ("" = missing)
	DKP        string // devkitPro root (opt/devkitpro); "" = none
	DKPOK      bool   // DKP has every package the build uses
	Makerom    string
	Bannertool string
	PyCGFX     string // python.exe of the tools/pycgfx venv ("" = no animated banner)
	PyCGFXMain string // pycgfx main.py
}

// pycgfx (glTF -> CGFX for the animated banner, MIT): pinned commit, run from a project-local venv.
const (
	pycgfxCommit = "6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e"
	pycgfxURL    = "https://github.com/skyfloogle/pycgfx/archive/" + pycgfxCommit + ".zip"
)

func pycgfxDir(root string) string { return filepath.Join(root, "tools", "pycgfx") }

func pycgfxPaths(root string) (python, main string) {
	return filepath.Join(pycgfxDir(root), "venv", "Scripts", exe("python")), filepath.Join(pycgfxDir(root), "src", "main.py")
}

// Same package set as tools/setup.sh.
var dkpPackages = []string{"devkitARM", "libctru", "citro2d", "citro3d", "3ds-libogg", "3ds-libvorbisidec",
	"tex3ds", "picasso", "3dstools", "general-tools"}

// Pinned releases (the versions make_cia.sh was set up with); zips are listed in setupStages.
const (
	makeromURL    = "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-win_x86_64.zip"
	bannertoolURL = "https://github.com/carstene1ns/3ds-bannertool/releases/download/1.2.3/bannertool-1.2.3-windows.zip"
)

func localDKP(root string) string {
	return filepath.Join(root, "tools", "devkitpro", "opt", "devkitpro")
}

func dkpComplete(d string) bool {
	for _, f := range []string{
		"devkitARM/bin/" + exe("arm-none-eabi-gcc"),
		"tools/bin/" + exe("tex3ds"), "tools/bin/" + exe("picasso"), "tools/bin/" + exe("3dsxtool"), "tools/bin/" + exe("smdhtool"),
		"libctru/lib/libctru.a", "libctru/lib/libcitro2d.a", "libctru/lib/libcitro3d.a",
		"portlibs/3ds/lib/libvorbisidec.a", "portlibs/3ds/lib/libogg.a",
	} {
		if !exists(filepath.Join(d, f)) {
			return false
		}
	}
	return true
}

// findToolchain mirrors tools/devkitpro_env.sh: the project-local devkitPro first, else an existing install in
// $DEVKITPRO (e.g. C:/devkitPro).
func findToolchain(root string) Toolchain {
	var t Toolchain
	t.Go, _ = exec.LookPath("go")
	cands := []string{localDKP(root)}
	if e := os.Getenv("DEVKITPRO"); e != "" {
		cands = append(cands, nativePath(e))
	}
	for _, c := range cands {
		if dkpComplete(c) {
			t.DKP, t.DKPOK = c, true
			break
		}
	}
	if t.DKP == "" && isDir(localDKP(root)) {
		t.DKP = localDKP(root)
	}
	ct := filepath.Join(root, "tools", "ctrtools")
	if p := filepath.Join(ct, "makerom", exe("makerom")); exists(p) {
		t.Makerom = p
	}
	// make_cia.sh accepts bannertool/<release dir>/bannertool.exe and a flat bannertool/bannertool.exe
	if m, _ := filepath.Glob(filepath.Join(ct, "bannertool", "*", exe("bannertool"))); len(m) > 0 {
		t.Bannertool = m[0]
	} else if p := filepath.Join(ct, "bannertool", exe("bannertool")); exists(p) {
		t.Bannertool = p
	}
	if py, m := pycgfxPaths(root); exists(py) && exists(m) && exists(filepath.Join(pycgfxDir(root), "venv", ".deps-ok")) {
		t.PyCGFX, t.PyCGFXMain = py, m
	}
	return t
}

func (t Toolchain) ready3dsx() bool { return t.Go != "" && t.DKPOK }
func (t Toolchain) readyCIA() bool  { return t.ready3dsx() && t.Makerom != "" && t.Bannertool != "" }

func (t Toolchain) tool(name string) string { return filepath.Join(t.DKP, "tools", "bin", exe(name)) }

// setupStages fetches whatever is missing; finished steps are skipped, so it is safe to re-run.
func setupStages(root string) []stage {
	ct := filepath.Join(root, "tools", "ctrtools")
	return []stage{
		{"Check Go", 1, func(j *Job, _ func(float64)) error {
			p, err := exec.LookPath("go")
			if err != nil {
				return errors.New("Go is not on PATH: install it from https://go.dev/dl/ and restart aosbuild")
			}
			v, _ := j.output("", p, "version")
			j.line(v)
			return nil
		}},
		{"devkitARM, libctru, citro2d/3d, tremor, tex3ds, picasso, 3dstools", 20, func(j *Job, _ func(float64)) error {
			if dkpComplete(localDKP(root)) {
				j.line("already in tools/devkitpro")
				return nil
			}
			if t := findToolchain(root); t.DKPOK {
				j.logf("using the existing devkitPro in %s ($DEVKITPRO)", t.DKP)
				return nil
			}
			j.line("fetching packages into tools/devkitpro (a few hundred MB)...")
			dir := filepath.Join(root, "tools", "dkpfetch")
			if err := j.goBuild(dir, exe("dkpfetch")); err != nil {
				return err
			}
			args := append([]string{"-root", filepath.Join(root, "tools", "devkitpro")}, dkpPackages...)
			if err := j.run(dir, filepath.Join(dir, exe("dkpfetch")), args...); err != nil {
				return err
			}
			if !dkpComplete(localDKP(root)) {
				return errors.New("dkpfetch finished but tools/devkitpro is still incomplete")
			}
			return nil
		}},
		{"makerom (CIA packager)", 2, func(j *Job, sub func(float64)) error {
			dst := filepath.Join(ct, "makerom", exe("makerom"))
			if exists(dst) {
				j.line("already in tools/ctrtools/makerom")
				return nil
			}
			return j.fetchZip(makeromURL, func(name string) string {
				if filepath.Base(name) == exe("makerom") {
					return dst
				}
				return ""
			}, sub)
		}},
		{"bannertool (HOME menu banner/icon)", 2, func(j *Job, sub func(float64)) error {
			if findToolchain(root).Bannertool != "" {
				j.line("already in tools/ctrtools/bannertool")
				return nil
			}
			base := filepath.Join(ct, "bannertool")
			return j.fetchZip(bannertoolURL, func(name string) string { // keep the release's own folder
				clean := filepath.Clean(filepath.FromSlash(name))
				if strings.HasPrefix(clean, "..") || filepath.IsAbs(clean) || strings.HasSuffix(name, "/") {
					return ""
				}
				return filepath.Join(base, clean)
			}, sub)
		}},
		{"pycgfx (animated CIA banner; needs Python 3.12+)", 4, func(j *Job, sub func(float64)) error {
			py, m := pycgfxPaths(root)
			ok := filepath.Join(pycgfxDir(root), "venv", ".deps-ok")
			if exists(py) && exists(m) && exists(ok) {
				j.line("already in tools/pycgfx")
				return nil
			}
			sysPy, pyArgs, err := findPython(j)
			if err != nil { // optional: CIA builds fall back to the static banner
				j.line("skipped: " + err.Error() + " (install it from https://www.python.org/ for the animated banner)")
				return nil
			}
			if !exists(m) {
				src := filepath.Join(pycgfxDir(root), "src")
				if err := j.fetchZip(pycgfxURL, func(name string) string { // drop the pycgfx-<commit>/ top folder
					i := strings.IndexByte(name, '/')
					rest := name[i+1:]
					clean := filepath.Clean(filepath.FromSlash(rest))
					if i < 0 || rest == "" || strings.HasSuffix(name, "/") || strings.HasPrefix(clean, "..") || filepath.IsAbs(clean) {
						return ""
					}
					return filepath.Join(src, clean)
				}, sub); err != nil {
					return err
				}
			}
			venv := filepath.Join(pycgfxDir(root), "venv")
			if !exists(py) {
				if err := j.run(root, sysPy, append(pyArgs, "-m", "venv", venv)...); err != nil {
					return err
				}
			}
			if err := j.run(root, py, "-m", "pip", "install", "--disable-pip-version-check", "gltflib==1.0.13", "pillow"); err != nil {
				return err
			}
			return os.WriteFile(ok, []byte(pycgfxCommit+"\n"), 0o644)
		}},
	}
}

// findPython returns a Python 3.12+ interpreter (pycgfx uses match and itertools.batched): the py launcher, then
// python/python3 on PATH (the Microsoft Store stub fails the version check).
func findPython(j *Job) (string, []string, error) {
	for _, c := range [][]string{{"py", "-3"}, {"python"}, {"python3"}} {
		p, err := exec.LookPath(c[0])
		if err != nil {
			continue
		}
		v, err := j.output("", p, append(c[1:], "-c", "import sys; print('%d %d' % sys.version_info[:2])")...)
		var major, minor int
		if err == nil {
			if _, err := fmt.Sscan(v, &major, &minor); err == nil && (major > 3 || major == 3 && minor >= 12) {
				j.logf("using Python %d.%d (%s)", major, minor, p)
				return p, c[1:], nil
			}
		}
	}
	return "", nil, errors.New("no Python 3.12+ found")
}

// fetchZip downloads a zip into memory and writes the entries dest() maps to a path ("" = skip).
func (j *Job) fetchZip(url string, dest func(name string) string, sub func(float64)) error {
	j.line("downloading " + url)
	b, err := download(j.ctx, url, sub)
	if err != nil {
		return err
	}
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		return err
	}
	n := 0
	for _, f := range zr.File {
		d := dest(f.Name)
		if d == "" {
			continue
		}
		if err := os.MkdirAll(filepath.Dir(d), 0o755); err != nil {
			return err
		}
		r, err := f.Open()
		if err != nil {
			return err
		}
		data, err := io.ReadAll(r)
		r.Close()
		if err != nil {
			return err
		}
		if err := os.WriteFile(d, data, 0o755); err != nil {
			return err
		}
		j.line("  " + d)
		n++
	}
	if n == 0 {
		return fmt.Errorf("%s: nothing to extract", url)
	}
	return nil
}

func download(ctx context.Context, url string, sub func(float64)) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "aosbuild")
	r, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer r.Body.Close()
	if r.StatusCode != 200 {
		return nil, fmt.Errorf("%s: %s", url, r.Status)
	}
	var buf bytes.Buffer
	chunk := make([]byte, 256*1024)
	for {
		n, err := r.Body.Read(chunk)
		buf.Write(chunk[:n])
		if sub != nil && r.ContentLength > 0 {
			sub(float64(buf.Len()) / float64(r.ContentLength))
		}
		if err == io.EOF {
			return buf.Bytes(), nil
		}
		if err != nil {
			return nil, err
		}
	}
}
