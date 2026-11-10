// aos2pack prepares extracted AoS2 assets for the 3DS build.
//
// Textures: every folder under <assets>/texture (characters, effects, stage,
// UI) is downscaled by -scale and partitioned into atlas pages that fit the
// 3DS 1024x1024 texture limit. For each folder it writes
//
//	<gfx>/<folder>/pageN.t3s           tex3ds atlas inputs
//	<romfs>/gfx/<folder>.idx           "name page index w h origW origH" per line
//
// build.sh then runs tex3ds on every page -> romfs:/gfx/<folder>_pN.t3x.
//
// Sounds: all extracted SE WAVs are copied to romfs:/se/ (lower-case names).
package main

import (
	"flag"
	"fmt"
	"image"
	"image/png"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"

	"golang.org/x/image/draw"
)

const maxTex = 1024

// pageBudget is the pixel area we allow per 1024x1024 page; tex3ds' atlas
// packer needs slack, and square power-of-two frames pack well at ~85%.
const pageBudget = maxTex * maxTex * 85 / 100

type frame struct {
	name      string // file name without extension
	pma       bool   // -pma: RGB premultiplied by the stored (4-bit) alpha (8th .idx field = 1)
	big       bool   // keeps a -fscale frame override (own pages with -vfxpages)
	src, dst  string
	w, h      int // scaled size
	ow, oh    int // original size
	page, idx int
}

func main() {
	assets := flag.String("assets", "", "aos2extract output dir")
	out := flag.String("out", "", "romfs staging dir")
	gfx := flag.String("gfx", "", "gfx staging dir (tex3ds inputs)")
	scale := flag.Float64("scale", 0.5, "texture scale factor")
	only := flag.String("only", "", "comma-separated folder names to pack (default: all)")
	fscale := flag.String("fscale", "", "per-folder scale overrides: name=scale,... (name may end in * as a prefix)")
	mip := flag.Bool("mip", true, "mipmap the character atlases (mipmapped())")
	nomip := flag.String("nomip", "", "with -mip: comma-separated folder patterns (* = prefix) packed without mipmaps")
	pma := flag.String("pma", "", "comma-separated folder patterns (* = prefix) whose atlases are stored premultiplied (render.c sprbatch)")
	vfxPages := flag.Bool("vfxpages", false, "frames with a -fscale frame override go to their own pages (the large beams)")
	exclude := flag.String("exclude", "", "comma-separated folder/name patterns (as -fscale frame keys) left out of the atlases")
	bgmTable := flag.String("bgm", "", "docs/re/bgm_table.tsv: copy BGM to romfs:/bgm/<id>.ogg (empty = skip)")
	flag.Parse()
	if *assets == "" || *out == "" || *gfx == "" {
		flag.Usage()
		os.Exit(2)
	}
	dirs, err := os.ReadDir(filepath.Join(*assets, "texture"))
	if err != nil {
		log.Fatal(err)
	}
	want := map[string]bool{}
	for _, s := range strings.Split(*only, ",") {
		if s != "" {
			want[s] = true
		}
	}
	opt.mip = *mip
	opt.scales = map[string]float64{}
	for _, kv := range strings.Split(*fscale, ",") {
		if k, v, ok := strings.Cut(kv, "="); ok {
			var f float64
			fmt.Sscanf(v, "%g", &f)
			opt.scales[k] = f
		}
	}
	opt.pma = splitList(*pma)
	opt.nomip = splitList(*nomip)
	opt.vfxPages = *vfxPages
	opt.exclude = splitList(*exclude)
	total := 0
	for _, d := range dirs {
		if !d.IsDir() || (len(want) > 0 && !want[d.Name()]) {
			continue
		}
		n, pages, err := packFolder(filepath.Join(*assets, "texture", d.Name()), d.Name(), *gfx, *out, folderScale(d.Name(), *scale))
		if err != nil {
			log.Fatalf("%s: %v", d.Name(), err)
		}
		total += n
		fmt.Printf("%-16s %4d frames  %d page(s)\n", d.Name(), n, pages)
	}
	if len(want) == 0 || want["fonts"] {
		if err := packFonts(*assets, *out, *gfx); err != nil {
			log.Fatal(err)
		}
		if err := packMenuTextures(*assets, *gfx); err != nil {
			log.Fatal(err)
		}
		if err := packEndrollTextures(*assets, *gfx); err != nil { // staff roll (endroll.go)
			log.Fatal(err)
		}
		if err := copyScripts(*assets, *out); err != nil {
			log.Fatal(err)
		}
	}
	if len(want) == 0 || want["fonts"] || want["select"] { // Part B: select/result textures (seltex.go)
		if err := packSelectTextures(*assets, *gfx); err != nil {
			log.Fatal(err)
		}
	}
	if err := copySounds(*assets, *out); err != nil {
		log.Fatal(err)
	}
	if *bgmTable != "" {
		n, err := copyBGM(*assets, *out, *bgmTable)
		if err != nil {
			log.Fatal(err)
		}
		fmt.Printf("bgm: %d tracks\n", n)
	}
	fmt.Printf("packed %d frames\n", total)
}

// Memory-plan options (docs/PERF.md "Texture memory plan").
var opt struct {
	mip      bool
	scales   map[string]float64
	pma      []string
	nomip    []string
	vfxPages bool
	exclude  []string
}

func splitList(s string) []string {
	var out []string
	for _, k := range strings.Split(s, ",") {
		if k = strings.TrimSpace(k); k != "" {
			out = append(out, k)
		}
	}
	return out
}

// matchAny: folder equals a pattern, or starts with a pattern ending in '*'.
func matchAny(pats []string, folder string) bool {
	for _, k := range pats {
		if k == folder || (strings.HasSuffix(k, "*") && strings.HasPrefix(folder, k[:len(k)-1])) {
			return true
		}
	}
	return false
}

// frameOverride reports whether a "folder/name" -fscale key (exact or prefix*) or a "*/name" key (any folder)
// matches the frame.
func frameKeyMatch(k, folder, name string) bool {
	if !strings.Contains(k, "/") {
		return false
	}
	kf, kn, _ := strings.Cut(k, "/")
	if kf != "*" && !(kf == folder || (strings.HasSuffix(kf, "*") && strings.HasPrefix(folder, kf[:len(kf)-1]))) {
		return false
	}
	return kn == name || (strings.HasSuffix(kn, "*") && strings.HasPrefix(name, kn[:len(kn)-1]))
}

func folderScale(folder string, def float64) float64 {
	if s, ok := opt.scales[folder]; ok {
		return s
	}
	best, bl := def, -1
	for k, s := range opt.scales {
		if strings.HasSuffix(k, "*") && strings.HasPrefix(folder, k[:len(k)-1]) && len(k) > bl {
			best, bl = s, len(k)
		}
	}
	return best
}

// frameScale: a "folder/name" key overrides the folder's scale for single frames; folder and name may end in
// * (prefix) and folder may be * (any folder: "*/sg_lsr*" = Suguri's laser frames in every palette folder).
// The longest matching key wins.
func frameScale(folder, name string, def float64) float64 {
	s, _ := frameOverride(folder, name, def)
	return s
}

func frameOverride(folder, name string, def float64) (float64, bool) {
	if s, ok := opt.scales[folder+"/"+name]; ok {
		return s, true
	}
	best, bl := def, -1
	for k, s := range opt.scales {
		if frameKeyMatch(k, folder, name) && len(k) > bl {
			best, bl = s, len(k)
		}
	}
	return best, bl >= 0
}

func packFolder(dir, folder, gfx, romfs string, scale float64) (int, int, error) {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return 0, 0, err
	}
	var frames []*frame
	for _, e := range ents {
		if e.IsDir() || !strings.HasSuffix(strings.ToLower(e.Name()), ".png") {
			continue
		}
		name := strings.TrimSuffix(e.Name(), filepath.Ext(e.Name()))
		skip := false
		for _, k := range opt.exclude {
			if frameKeyMatch(k, folder, name) {
				skip = true
			}
		}
		if skip {
			continue
		}
		_, big := frameOverride(folder, name, scale)
		frames = append(frames, &frame{
			pma:  matchAny(opt.pma, folder),
			big:  big && opt.vfxPages,
			name: name,
			src:  filepath.Join(dir, e.Name()),
			dst:  filepath.Join(gfx, folder, name+".png"),
		})
	}
	if len(frames) == 0 {
		return 0, 0, nil
	}
	if err := os.MkdirAll(filepath.Join(gfx, folder), 0o755); err != nil {
		return 0, 0, err
	}
	// Scale in parallel.
	var wg sync.WaitGroup
	errs := make(chan error, len(frames))
	sem := make(chan struct{}, runtime.NumCPU())
	for _, fr := range frames {
		wg.Add(1)
		go func(fr *frame) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			if err := scalePNG(fr, frameScale(folder, fr.name, scale)); err != nil {
				errs <- fmt.Errorf("%s: %w", fr.name, err)
			}
		}(fr)
	}
	wg.Wait()
	close(errs)
	if err := <-errs; err != nil {
		return 0, 0, err
	}
	// Partition into pages: largest first, first-fit by area budget; with -vfxpages the frames that keep a
	// higher scale (-fscale frame overrides: the large beams) get their own pages after the others.
	sorted := append([]*frame(nil), frames...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].w*sorted[i].h > sorted[j].w*sorted[j].h })
	var used []int
	var members [][]*frame
	for _, group := range []bool{false, true} {
		first := len(used)
		for _, fr := range sorted {
			if fr.big != group {
				continue
			}
			a := fr.w * fr.h
			p := -1
			for i := first; i < len(used); i++ {
				if used[i]+a <= pageBudget {
					p = i
					break
				}
			}
			if p < 0 {
				used = append(used, 0)
				members = append(members, nil)
				p = len(used) - 1
			}
			used[p] += a
			fr.page = p
			members[p] = append(members[p], fr)
		}
	}
	// pages of an earlier pack with more pages (tools/convert_pages.sh drops their .t3x too)
	if old, _ := filepath.Glob(filepath.Join(gfx, folder, "page*.t3s")); len(old) > 0 {
		for _, f := range old {
			os.Remove(f)
		}
	}
	// Stable, name-ordered indices within each page so builds are reproducible.
	for p := range members {
		sort.Slice(members[p], func(i, j int) bool { return members[p][i].name < members[p][j].name })
		var lines []string
		for i, fr := range members[p] {
			fr.idx = i
			lines = append(lines, fr.name+".png")
		}
		f := "etc1a4"
		opts := "--atlas -f " + f + " -q medium -z auto"
		if mipmapped(folder) && opt.mip && !matchAny(opt.nomip, folder) {
			// tex3ds' (ImageMagick) box filter weights colour by alpha: correct for straight alpha only,
			// so premultiplied folders get no mipmaps (render.c draws them unmipmapped anyway)
			if !matchAny(opt.pma, folder) {
				opts = "--atlas --mipmap box -f " + f + " -q medium -z auto"
			}
		}
		t3s := opts + "\n" + strings.Join(lines, "\n") + "\n"
		if err := os.WriteFile(filepath.Join(gfx, folder, fmt.Sprintf("page%d.t3s", p)), []byte(t3s), 0o644); err != nil {
			return 0, 0, err
		}
	}
	sort.Slice(frames, func(i, j int) bool { return frames[i].name < frames[j].name })
	var idx strings.Builder
	for _, fr := range frames {
		if fr.pma {
			fmt.Fprintf(&idx, "%s %d %d %d %d %d %d 1\n", fr.name, fr.page, fr.idx, fr.w, fr.h, fr.ow, fr.oh)
		} else {
			fmt.Fprintf(&idx, "%s %d %d %d %d %d %d\n", fr.name, fr.page, fr.idx, fr.w, fr.h, fr.ow, fr.oh)
		}
	}
	if err := os.MkdirAll(filepath.Join(romfs, "gfx"), 0o755); err != nil {
		return 0, 0, err
	}
	if err := os.WriteFile(filepath.Join(romfs, "gfx", folder+".idx"), []byte(idx.String()), 0o644); err != nil {
		return 0, 0, err
	}
	return len(frames), len(members), nil
}

// mipmapped reports whether a folder's atlas pages get a mipmap chain: the
// per-player character folders (charaXxxA/B/C). Their projectiles and
// effects are drawn 1.5-3x minified at 400x240 (64-texel missiles are ~20 px
// on screen) and only mipmaps keep the edges anti-aliased while they rotate;
// bilinear alone barely differs from point sampling there. Costs +1/3 of the
// folder's texture memory (<= 1.5 MB per player, linear heap). The common
// folders stay without mipmaps: they live in VRAM, which has no room left.
func mipmapped(folder string) bool {
	switch folder {
	case "charaCmnA", "charaAdd", "charaHide", "charaRndm":
		return false
	}
	last := folder[len(folder)-1]
	return strings.HasPrefix(folder, "chara") && (last == 'A' || last == 'B' || last == 'C')
}

func scalePNG(fr *frame, s float64) error {
	f, err := os.Open(fr.src)
	if err != nil {
		return err
	}
	img, err := png.Decode(f)
	f.Close()
	if err != nil {
		return err
	}
	b := img.Bounds()
	fr.ow, fr.oh = b.Dx(), b.Dy()
	w, h := int(float64(b.Dx())*s+0.5), int(float64(b.Dy())*s+0.5)
	for w > maxTex || h > maxTex { // a single frame must fit one page
		w, h = (w+1)/2, (h+1)/2
	}
	if w < 1 {
		w = 1
	}
	if h < 1 {
		h = 1
	}
	fr.w, fr.h = w, h
	// the staged PNG is reused while it is newer than the source and was made with the same size and form
	stamp := fr.dst + fmt.Sprintf(".%dx%d.%v", w, h, fr.pma)
	if st, err := os.Stat(fr.dst); err == nil {
		if si, err2 := os.Stat(fr.src); err2 == nil && st.ModTime().After(si.ModTime()) {
			if _, err3 := os.Stat(stamp); err3 == nil {
				return nil // up to date
			}
		}
	}
	dst := image.NewRGBA(image.Rect(0, 0, w, h))
	draw.CatmullRom.Scale(dst, dst.Bounds(), img, b, draw.Src, nil)
	var out image.Image = dst
	if fr.pma {
		// Premultiplied by the alpha the GPU will see: tex3ds stores ETC1A4 alpha as a>>4 (quantum_to_bits<4>),
		// sampled as (a>>4)*17. image.RGBA holds colour*a/255 (Go's premultiplied form): rescale to
		// colour*q/255. Written as NRGBA so the PNG keeps these bytes as they are.
		n := image.NewNRGBA(dst.Bounds())
		for i := 0; i < len(dst.Pix); i += 4 {
			a := uint32(dst.Pix[i+3])
			q := (a >> 4) * 17
			for c := 0; c < 3; c++ {
				v := uint32(0)
				if a > 0 {
					v = (uint32(dst.Pix[i+c])*q + a/2) / a
					if v > q {
						v = q
					}
				}
				n.Pix[i+c] = uint8(v)
			}
			n.Pix[i+3] = uint8(a)
		}
		out = n
	}
	o, err := os.Create(fr.dst)
	if err != nil {
		return err
	}
	if err := png.Encode(o, out); err != nil {
		o.Close()
		return err
	}
	o.Close()
	old, _ := filepath.Glob(fr.dst + ".*x*.*")
	for _, f := range old {
		os.Remove(f)
	}
	return os.WriteFile(stamp, nil, 0o644)
}

func copySounds(assets, romfs string) error {
	if err := os.MkdirAll(filepath.Join(romfs, "se"), 0o755); err != nil {
		return err
	}
	for _, bank := range []string{"se", "se2", "se3"} {
		ents, err := os.ReadDir(filepath.Join(assets, "se", bank))
		if err != nil {
			return err
		}
		for _, e := range ents {
			b, err := os.ReadFile(filepath.Join(assets, "se", bank, e.Name()))
			if err != nil {
				return err
			}
			if err := os.WriteFile(filepath.Join(romfs, "se", strings.ToLower(e.Name())), b, 0o644); err != nil {
				return err
			}
		}
	}
	return nil
}

// copyBGM copies extracted tracks to romfs:/bgm/<id>.ogg following the
// original id table (bgm_id <TAB> file <TAB> loop <TAB> size). Up-to-date
// copies (same size) are skipped since the tracks total ~860 MB.
func copyBGM(assets, romfs, table string) (int, error) {
	b, err := os.ReadFile(table)
	if err != nil {
		return 0, err
	}
	if err := os.MkdirAll(filepath.Join(romfs, "bgm"), 0o755); err != nil {
		return 0, err
	}
	n := 0
	seen := map[string]bool{}
	for _, line := range strings.Split(string(b), "\n") {
		f := strings.Split(strings.TrimSpace(line), "\t")
		if len(f) < 2 || strings.HasPrefix(f[0], "#") || f[1] == "?" || seen[f[0]] {
			continue
		}
		seen[f[0]] = true
		src := filepath.Join(assets, "bgm", f[1])
		dst := filepath.Join(romfs, "bgm", f[0]+".ogg")
		si, err := os.Stat(src)
		if err != nil {
			return n, err
		}
		if di, err := os.Stat(dst); err == nil && di.Size() == si.Size() {
			n++
			continue
		}
		data, err := os.ReadFile(src)
		if err != nil {
			return n, err
		}
		if err := os.WriteFile(dst, data, 0o644); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}
