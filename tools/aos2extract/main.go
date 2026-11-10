package main

import (
	"archive/zip"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	game := flag.String("game", "", "path to the Acceleration of SUGURI 2 install directory")
	out := flag.String("out", "", "output directory")
	skipBGM := flag.Bool("skip-bgm", false, "do not split the (large) BGM archives")
	convOnly := flag.Bool("conv-only", false, "only extract the conversation scripts (aos2.exe resources)")
	flag.Parse()
	if *game == "" || *out == "" {
		flag.Usage()
		os.Exit(2)
	}
	if *convOnly {
		if err := extractConversations(*game, *out); err != nil {
			log.Fatal(err)
		}
		return
	}
	if err := run(*game, *out, *skipBGM); err != nil {
		log.Fatal(err)
	}
}

func run(game, out string, skipBGM bool) error {
	paks := map[string]string{
		"script.pak":      "script",
		"font.pak":        "font",
		"shader.pak":      "shader",
		"mdl/shader.pak":  "mdl/shader",
		"mdl/texture.pak": "texture",
	}
	for src, dst := range paks {
		n, err := extractPak(filepath.Join(game, src), filepath.Join(out, dst), src == "mdl/texture.pak")
		if err != nil {
			return fmt.Errorf("%s: %w", src, err)
		}
		log.Printf("%-16s %5d files", src, n)
	}
	for _, se := range []string{"se.dat", "se2.dat", "se3.dat"} {
		b, err := os.ReadFile(filepath.Join(game, se))
		if err != nil {
			return err
		}
		snds, err := ParseSoundBank(b)
		if err != nil {
			return fmt.Errorf("%s: %w", se, err)
		}
		dir := filepath.Join(out, "se", strings.TrimSuffix(se, ".dat"))
		for _, s := range snds {
			if err := writeFile(filepath.Join(dir, safeName(s.Name)), s.WAV()); err != nil {
				return err
			}
		}
		log.Printf("%-16s %5d sounds", se, len(snds))
	}
	if !skipBGM {
		bgms, _ := filepath.Glob(filepath.Join(game, "bgm*.dat"))
		for _, p := range bgms {
			b, err := os.ReadFile(p)
			if err != nil {
				return err
			}
			streams, skipped, err := SplitOgg(b)
			if err != nil {
				return fmt.Errorf("%s: %w", p, err)
			}
			base := strings.TrimSuffix(filepath.Base(p), ".dat")
			for i, s := range streams {
				if err := writeFile(filepath.Join(out, "bgm", fmt.Sprintf("%s_%02d.ogg", base, i)), s); err != nil {
					return err
				}
			}
			log.Printf("%-16s %5d streams (%d padding bytes skipped)", filepath.Base(p), len(streams), skipped)
		}
	}
	if err := extractConversations(game, out); err != nil {
		return err
	}
	// Models and LAG textures are copied as-is for now (LAG decoding is TODO).
	mdl, _ := filepath.Glob(filepath.Join(game, "mdl", "*"))
	for _, p := range mdl {
		ext := strings.ToLower(filepath.Ext(p))
		if ext == ".pak" {
			continue
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		if err := writeFile(filepath.Join(out, "mdl", filepath.Base(p)), b); err != nil {
			return err
		}
	}
	return nil
}

// extractConversations writes the 56 TEXT_CONVERSATION0<id> resources of
// aos2.exe to <out>/script_res/<name>.txt (docs/FORMATS.md, modes.md §C.1).
func extractConversations(game, out string) error {
	exe, err := os.ReadFile(filepath.Join(game, "aos2.exe"))
	if err != nil {
		return err
	}
	conv, err := ExtractConversations(exe)
	if err != nil {
		return fmt.Errorf("aos2.exe: %w", err)
	}
	for name, b := range conv {
		if err := writeFile(filepath.Join(out, "script_res", name+".txt"), b); err != nil {
			return err
		}
	}
	log.Printf("%-16s %5d conversation scripts", "aos2.exe", len(conv))
	return nil
}

func extractPak(path, dst string, textures bool) (int, error) {
	zr, err := zip.OpenReader(path)
	if err != nil {
		return 0, err
	}
	defer zr.Close()
	n := 0
	for _, f := range zr.File {
		if f.FileInfo().IsDir() {
			continue
		}
		rc, err := f.Open()
		if err != nil {
			return n, err
		}
		b, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			return n, err
		}
		name := f.Name
		if textures && strings.HasSuffix(name, ".dat") {
			png, err := DecodeTexture(b)
			if err != nil {
				return n, fmt.Errorf("%s: %w", name, err)
			}
			b, name = png, strings.TrimSuffix(name, ".dat")+".png"
		}
		if err := writeFile(filepath.Join(dst, filepath.FromSlash(safeName(name))), b); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}

// safeName rejects path traversal from archive entry names.
func safeName(n string) string {
	n = strings.ReplaceAll(n, "\\", "/")
	parts := strings.Split(n, "/")
	var keep []string
	for _, p := range parts {
		if p == "" || p == "." || p == ".." {
			continue
		}
		keep = append(keep, p)
	}
	return strings.Join(keep, "/")
}

func writeFile(p string, b []byte) error {
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	return os.WriteFile(p, b, 0o644)
}
