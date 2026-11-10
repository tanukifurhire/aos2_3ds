package main

// The window: CrystalDiskInfo-style status tiles and translucent panels over the cast wallpaper (left ~60%; the
// characters stay visible on the right).

import (
	"context"
	"errors"
	"fmt"
	"image"
	"image/color"
	_ "image/jpeg"
	_ "image/png"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"gioui.org/app"
	"gioui.org/f32"
	"gioui.org/font"
	"gioui.org/font/gofont"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/text"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
	"github.com/ncruces/zenity"
)

type (
	C = layout.Context
	D = layout.Dimensions
)

func argb(c uint32) color.NRGBA {
	return color.NRGBA{A: uint8(c >> 24), R: uint8(c >> 16), G: uint8(c >> 8), B: uint8(c)}
}

var (
	colBase   = argb(0xff0b0c18)
	colPanel  = argb(0xb40c0e1c)
	colLog    = argb(0xd007080f)
	colText   = argb(0xfff2f2f7)
	colDim    = argb(0xffa3a8c0)
	colAccent = argb(0xffe0508a) // the pink of the wallpaper's light streak
	colField  = argb(0x90000000)
	colBtn    = argb(0x38ffffff)
	colErr    = argb(0xffff7b72)
)

// status levels, coloured like CrystalDiskInfo's health box
const (
	lvUnknown = iota
	lvGood
	lvCaution
	lvBad
	lvBusy
)

var levelCol = [...]color.NRGBA{argb(0xe0565b6e), argb(0xe82f7de1), argb(0xe8d89a16), argb(0xe8d0443a), argb(0xe8209f92)}

type tile struct {
	title, value, detail string
	level                int
}

type result struct {
	what string
	err  error
	when time.Time
}

type ui struct {
	w       *app.Window // nil when rendering headless
	th      *material.Theme
	root    string
	rootErr error
	st      Settings

	mu        sync.Mutex // guards the fields the job goroutine touches
	logs      []string
	posts     []func()
	running   bool
	cancel    context.CancelFunc
	jobDone   chan struct{}
	prog      float64
	progLabel string
	last      *result

	bg *paint.ImageOp
	tc Toolchain

	gameEd, outEd                          widget.Editor
	btnGame, btnOut, btnOutReset, btnBg    widget.Clickable
	btnSetup, btnBuild, btnOpen, btnCancel widget.Clickable
	target                                 widget.Enum
	rebuild, profile, animBanner           widget.Bool
	logList                                widget.List
}

func runGUI() {
	go func() {
		w := new(app.Window)
		w.Option(app.Title("AoS2 3DS Builder"), app.Size(unit.Dp(1280), unit.Dp(760)), app.MinSize(unit.Dp(960), unit.Dp(660)))
		u := newUI(w)
		go u.loadBackground()
		var ops op.Ops
		for {
			switch e := w.Event().(type) {
			case app.DestroyEvent:
				u.shutdown()
				if e.Err != nil {
					fmt.Fprintln(os.Stderr, e.Err)
					os.Exit(1)
				}
				os.Exit(0)
			case app.FrameEvent:
				gtx := app.NewContext(&ops, e)
				u.frame(gtx)
				e.Frame(gtx.Ops)
			}
		}
	}()
	app.Main()
}

func newUI(w *app.Window) *ui {
	u := &ui{w: w}
	th := material.NewTheme()
	th.Shaper = text.NewShaper(text.WithCollection(gofont.Collection()))
	th.Palette = material.Palette{Fg: colText, Bg: colBase, ContrastBg: colAccent, ContrastFg: argb(0xffffffff)}
	th.TextSize = 14
	u.th = th
	u.root, u.rootErr = findRoot()
	if u.rootErr == nil {
		u.st = loadSettings(u.root)
	} else {
		u.st = Settings{Target: "3dsx"}
		u.logs = []string{"error: " + u.rootErr.Error()}
	}
	u.gameEd.SingleLine, u.outEd.SingleLine = true, true
	u.gameEd.SetText(u.st.Game)
	u.outEd.SetText(u.st.Out)
	u.target.Value, u.animBanner.Value = u.st.Target, !u.st.StaticBanner
	u.logList.Axis, u.logList.ScrollToEnd = layout.Vertical, true
	u.refresh()
	if len(u.logs) == 0 {
		u.logs = []string{"Ready."}
		if !u.tc.ready3dsx() {
			u.logs = append(u.logs, `First time on this PC? Press "Set up toolchain" (downloads devkitARM, makerom and bannertool into tools/).`)
		}
		if !gameOK(u.st.Game) {
			u.logs = append(u.logs, "Pick your Acceleration of SUGURI 2 folder (the one with aos2.exe).")
		}
	}
	return u
}

func (u *ui) invalidate() {
	if u.w != nil {
		u.w.Invalidate()
	}
}

// post runs f on the UI goroutine at the next frame.
func (u *ui) post(f func()) {
	u.mu.Lock()
	u.posts = append(u.posts, f)
	u.mu.Unlock()
	u.invalidate()
}

func (u *ui) addLog(l string) {
	u.mu.Lock()
	u.logs = append(u.logs, l)
	if len(u.logs) > 4000 {
		u.logs = append([]string(nil), u.logs[len(u.logs)-3000:]...)
	}
	u.mu.Unlock()
	u.invalidate()
}

func (u *ui) refresh() {
	if u.rootErr != nil {
		return
	}
	u.tc = findToolchain(u.root)
}

func backgroundImage(root, custom string) (image.Image, error) {
	p := custom
	if p == "" {
		p = filepath.Join(cacheDir(root), "background.jpg")
		if !exists(p) {
			ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
			defer cancel()
			b, err := download(ctx, wallpaperURL, nil)
			if err != nil {
				return nil, err
			}
			if err := os.MkdirAll(cacheDir(root), 0o755); err != nil {
				return nil, err
			}
			if err := os.WriteFile(p, b, 0o644); err != nil {
				return nil, err
			}
		}
	}
	return loadImage(p)
}

func (u *ui) loadBackground() {
	if u.rootErr != nil {
		return
	}
	img, err := backgroundImage(u.root, u.st.Background)
	u.post(func() {
		if err != nil {
			u.bg = nil
			u.addLog("background: " + err.Error() + " (plain theme)")
			return
		}
		o := paint.NewImageOp(img)
		u.bg = &o
	})
}

func (u *ui) isRunning() bool {
	u.mu.Lock()
	defer u.mu.Unlock()
	return u.running
}

func (u *ui) startJob(name string, stages []stage, err error) {
	if err != nil {
		u.mu.Lock()
		u.logs = append(u.logs, "error: "+err.Error())
		u.last = &result{name, err, time.Now()}
		u.mu.Unlock()
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	logf := openLog(u.st.outDir(u.root), name)
	done := make(chan struct{})
	u.mu.Lock()
	u.running, u.cancel, u.jobDone = true, cancel, done
	u.prog, u.progLabel, u.logs = 0, "Starting", nil
	u.mu.Unlock()
	var fmu sync.Mutex
	j := newJob(ctx, u.tc, func(l string) {
		u.addLog(l)
		if logf != nil {
			fmu.Lock()
			fmt.Fprintln(logf, l)
			fmu.Unlock()
		}
	}, func(f float64, label string) {
		u.mu.Lock()
		u.prog, u.progLabel = f, label
		u.mu.Unlock()
		u.invalidate()
	})
	go func() {
		defer close(done)
		err := j.runStages(stages)
		switch {
		case errors.Is(err, context.Canceled):
			j.line("cancelled")
		case err != nil:
			j.line("error: " + err.Error())
		}
		if logf != nil {
			logf.Close()
		}
		cancel()
		u.mu.Lock()
		u.running, u.cancel = false, nil
		u.last = &result{name, err, time.Now()}
		u.mu.Unlock()
		u.post(u.refresh)
	}()
}

func (u *ui) shutdown() {
	u.mu.Lock()
	cancel, done := u.cancel, u.jobDone
	u.mu.Unlock()
	if cancel != nil {
		cancel()
		select {
		case <-done:
		case <-time.After(5 * time.Second):
		}
	}
}

func (u *ui) pickFolder(ed *widget.Editor, title, start string) {
	go func() {
		p, err := zenity.SelectFile(zenity.Directory(), zenity.Title(title), zenity.Filename(start+string(filepath.Separator)))
		if err == nil && p != "" {
			u.post(func() { ed.SetText(p) })
		}
	}()
}

func (u *ui) outDir() string { return u.st.outDir(u.root) }

func (u *ui) frame(gtx C) D {
	u.mu.Lock()
	posts := u.posts
	u.posts = nil
	u.mu.Unlock()
	for _, f := range posts {
		f()
	}
	u.handle(gtx)
	d := u.layout(gtx)
	u.syncSettings()
	return d
}

func (u *ui) handle(gtx C) {
	running := u.isRunning()
	if u.btnGame.Clicked(gtx) {
		u.pickFolder(&u.gameEd, "Select your Acceleration of SUGURI 2 folder (the one with aos2.exe)", u.gameEd.Text())
	}
	if u.btnOut.Clicked(gtx) {
		u.pickFolder(&u.outEd, "Select the output folder", u.outDir())
	}
	if u.btnOutReset.Clicked(gtx) {
		u.outEd.SetText("")
	}
	if u.btnBg.Clicked(gtx) {
		go func() {
			p, err := zenity.SelectFile(zenity.Title("Background image"),
				zenity.FileFilter{Name: "Images", Patterns: []string{"*.jpg", "*.jpeg", "*.png"}})
			if err == nil && p != "" {
				u.post(func() {
					u.st.Background = p
					u.st.save(u.root)
					go u.loadBackground()
				})
			}
		}()
	}
	if u.btnOpen.Clicked(gtx) {
		d := u.outDir()
		if !isDir(d) {
			d = u.root
		}
		openFolder(d)
	}
	if u.btnCancel.Clicked(gtx) {
		u.mu.Lock()
		if u.cancel != nil {
			u.cancel()
		}
		u.mu.Unlock()
	}
	if running || u.rootErr != nil {
		return
	}
	if u.btnSetup.Clicked(gtx) {
		u.startJob("setup", setupStages(u.root), nil)
	}
	if u.btnBuild.Clicked(gtx) {
		u.refresh()
		st, err := buildPipeline(u.root, Request{Settings: u.st, Rebuild: u.rebuild.Value, Profile: u.profile.Value}, u.tc)
		u.startJob("build", st, err)
	}
}

// syncSettings copies edits back into Settings and saves when something changed.
func (u *ui) syncSettings() {
	if u.rootErr != nil {
		return
	}
	s := u.st
	s.Game = nativePath(u.gameEd.Text())
	s.Out = nativePath(u.outEd.Text())
	if s.Out != "" && strings.EqualFold(s.Out, defaultOut(u.root)) {
		s.Out = ""
	}
	s.Target, s.StaticBanner = u.target.Value, !u.animBanner.Value
	if s != u.st {
		u.st = s
		u.st.save(u.root)
	}
}

// ---- layout ----

func space(dp unit.Dp) layout.Widget {
	return func(gtx C) D { return D{Size: image.Pt(gtx.Dp(dp), gtx.Dp(dp))} }
}

// fill takes all the space it is given (a Flexed spacer).
func fill(gtx C) D { return D{Size: gtx.Constraints.Min} }

func roundFill(c color.NRGBA, r unit.Dp) layout.Widget {
	return func(gtx C) D {
		sz := gtx.Constraints.Min
		defer clip.UniformRRect(image.Rectangle{Max: sz}, gtx.Dp(r)).Push(gtx.Ops).Pop()
		paint.Fill(gtx.Ops, c)
		return D{Size: sz}
	}
}

func panel(gtx C, bg color.NRGBA, w layout.Widget) D {
	gtx.Constraints.Min.X = gtx.Constraints.Max.X
	return layout.Background{}.Layout(gtx, roundFill(bg, 8), func(gtx C) D {
		return layout.UniformInset(unit.Dp(12)).Layout(gtx, func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			return w(gtx)
		})
	})
}

func (u *ui) label(size unit.Sp, txt string, c color.NRGBA) material.LabelStyle {
	l := material.Label(u.th, size, txt)
	l.Color = c
	return l
}

func (u *ui) layout(gtx C) D {
	gtx.Constraints.Min = gtx.Constraints.Max
	size := gtx.Constraints.Max
	paint.Fill(gtx.Ops, colBase)
	if u.bg != nil {
		widget.Image{Src: *u.bg, Fit: widget.Cover, Position: layout.E}.Layout(gtx)
	}
	colW := min(max(size.X*3/5, gtx.Dp(660)), gtx.Dp(860), size.X)
	// soft shade behind the panels, fading out before the cast
	fade := gtx.Dp(180)
	r := clip.Rect{Max: image.Pt(min(colW+fade, size.X), size.Y)}.Push(gtx.Ops)
	paint.LinearGradientOp{Stop1: f32.Pt(float32(colW-fade/2), 0), Color1: argb(0x70000000),
		Stop2: f32.Pt(float32(colW+fade), 0), Color2: argb(0x00000000)}.Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)
	r.Pop()

	cg := gtx
	cg.Constraints = layout.Exact(image.Pt(colW, size.Y))
	layout.UniformInset(unit.Dp(16)).Layout(cg, u.column)
	return D{Size: size}
}

func (u *ui) column(gtx C) D {
	u.mu.Lock()
	running, prog, progLabel, last := u.running, u.prog, u.progLabel, u.last
	logs := u.logs
	u.mu.Unlock()
	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		layout.Rigid(u.header),
		layout.Rigid(space(12)),
		layout.Rigid(func(gtx C) D { return u.tilesRow(gtx, running, progLabel, last) }),
		layout.Rigid(space(12)),
		layout.Rigid(func(gtx C) D { return panel(gtx, colPanel, u.foldersPanel) }),
		layout.Rigid(space(10)),
		layout.Rigid(func(gtx C) D { return panel(gtx, colPanel, func(gtx C) D { return u.buildPanel(gtx, running) }) }),
		layout.Rigid(space(12)),
		layout.Rigid(func(gtx C) D { return u.actions(gtx, running) }),
		layout.Rigid(space(10)),
		layout.Rigid(func(gtx C) D { return u.progress(gtx, running, prog, progLabel) }),
		layout.Rigid(space(10)),
		layout.Flexed(1, func(gtx C) D { return u.logPanel(gtx, logs) }),
	)
}

func (u *ui) header(gtx C) D {
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(func(gtx C) D {
			return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
				layout.Rigid(func(gtx C) D {
					l := u.label(24, "AoS2 3DS Builder", colText)
					l.Font.Weight = font.Bold
					return l.Layout(gtx)
				}),
				layout.Rigid(u.label(12, "Acceleration of SUGURI 2 port · Homebrew Launcher app or installable CIA", colDim).Layout),
			)
		}),
		layout.Flexed(1, fill),
		layout.Rigid(func(gtx C) D { return u.button(gtx, &u.btnBg, "Background…", false, true) }),
	)
}

func (u *ui) tiles(running bool, progLabel string, last *result) [4]tile {
	var t [4]tile
	tc := u.tc
	switch {
	case u.rootErr != nil:
		t[0] = tile{"Toolchain", "Bad", "repo not found", lvBad}
	case tc.Go == "":
		t[0] = tile{"Toolchain", "Bad", "Go not installed (go.dev/dl)", lvBad}
	case !tc.DKPOK:
		t[0] = tile{"Toolchain", "Bad", "press Set up toolchain", lvBad}
	case !tc.readyCIA():
		t[0] = tile{"Toolchain", "Caution", ".3dsx only: no CIA tools", lvCaution}
	default:
		if tc.PyCGFX == "" {
			t[0] = tile{"Toolchain", "Caution", "no pycgfx: static CIA banner", lvCaution}
		} else {
			t[0] = tile{"Toolchain", "Good", "devkitARM + CIA tools", lvGood}
		}
	}
	if gameOK(u.st.Game) {
		t[1] = tile{"Game folder", "Good", "aos2.exe found", lvGood}
	} else {
		t[1] = tile{"Game folder", "Bad", "no aos2.exe there", lvBad}
	}
	out := u.outDir()
	switch as := assetStatus(u.root, out); {
	case as == assetsReady:
		t[2] = tile{"Assets", "Good", "packed; reused", lvGood}
	case as == assetsStale:
		t[2] = tile{"Assets", "Caution", "tools changed; re-packs", lvCaution}
	case isDir(filepath.Join(u.root, "extracted", "texture")):
		t[2] = tile{"Assets", "Caution", "extracted; packs on build", lvCaution}
	default:
		t[2] = tile{"Assets", "Unknown", "first build extracts", lvUnknown}
	}
	switch {
	case running:
		t[3] = tile{"Last run", "Running", progLabel, lvBusy}
	case last != nil && errors.Is(last.err, context.Canceled):
		t[3] = tile{"Last run", "Cancelled", last.what + " " + last.when.Format("15:04"), lvCaution}
	case last != nil && last.err != nil:
		t[3] = tile{"Last run", "Failed", last.what + " " + last.when.Format("15:04"), lvBad}
	case last != nil:
		t[3] = tile{"Last run", "Done", last.what + " " + last.when.Format("15:04"), lvGood}
	default:
		t[3] = tile{"Last run", "None", "nothing built this session", lvUnknown}
		var newest os.FileInfo // the newest output of the selected format
		for _, f := range outputs(out, u.st.Target) {
			if st, err := os.Stat(f); err == nil && (newest == nil || st.ModTime().After(newest.ModTime())) {
				newest = st
			}
		}
		if newest != nil {
			t[3] = tile{"Last build", fmt.Sprintf("%d MB", newest.Size()>>20),
				strings.TrimPrefix(filepath.Ext(newest.Name()), ".") + " · " + newest.ModTime().Format("Jan 2 15:04"), lvGood}
		}
	}
	return t
}

func (u *ui) tilesRow(gtx C, running bool, progLabel string, last *result) D {
	ts := u.tiles(running, progLabel, last)
	var ch []layout.FlexChild
	for i, t := range ts {
		if i > 0 {
			ch = append(ch, layout.Rigid(space(10)))
		}
		ch = append(ch, layout.Flexed(1, func(gtx C) D { return u.tile(gtx, t) }))
	}
	return layout.Flex{}.Layout(gtx, ch...)
}

func (u *ui) tile(gtx C, t tile) D {
	gtx.Constraints.Min.X = gtx.Constraints.Max.X
	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		layout.Rigid(u.label(12, t.title, colDim).Layout),
		layout.Rigid(space(3)),
		layout.Rigid(func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			return layout.Background{}.Layout(gtx, roundFill(levelCol[t.level], 6), func(gtx C) D {
				return layout.Inset{Top: 8, Bottom: 8, Left: 10, Right: 10}.Layout(gtx, func(gtx C) D {
					gtx.Constraints.Min.X = gtx.Constraints.Max.X
					return layout.Flex{Axis: layout.Vertical, Alignment: layout.Middle}.Layout(gtx,
						layout.Rigid(func(gtx C) D {
							l := u.label(20, t.value, argb(0xffffffff))
							l.Font.Weight = font.Bold
							l.MaxLines, l.Alignment = 1, text.Middle
							gtx.Constraints.Min.X = gtx.Constraints.Max.X
							return l.Layout(gtx)
						}),
						layout.Rigid(func(gtx C) D {
							l := u.label(11, t.detail, argb(0xe0ffffff))
							l.MaxLines, l.Alignment = 1, text.Middle
							gtx.Constraints.Min.X = gtx.Constraints.Max.X
							return l.Layout(gtx)
						}),
					)
				})
			})
		}),
	)
}

func (u *ui) section(gtx C, title string) D {
	l := u.label(13, title, colAccent)
	l.Font.Weight = font.Bold
	return layout.Inset{Bottom: 6}.Layout(gtx, l.Layout)
}

// row: fixed-width caption, a flexible field, then extra widgets.
func (u *ui) row(gtx C, caption string, field layout.Widget, extras ...layout.Widget) D {
	ch := []layout.FlexChild{
		layout.Rigid(func(gtx C) D {
			w := min(gtx.Dp(104), gtx.Constraints.Max.X)
			gtx.Constraints.Min.X, gtx.Constraints.Max.X = w, w
			return u.label(13, caption, colDim).Layout(gtx)
		}),
		layout.Flexed(1, field),
	}
	for _, e := range extras {
		ch = append(ch, layout.Rigid(space(6)), layout.Rigid(e))
	}
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx, ch...)
}

func (u *ui) editor(ed *widget.Editor, hint string, enabled bool) layout.Widget {
	return func(gtx C) D {
		if !enabled {
			gtx = gtx.Disabled()
		}
		gtx.Constraints.Min.X = gtx.Constraints.Max.X
		return layout.Background{}.Layout(gtx, roundFill(colField, 5), func(gtx C) D {
			return layout.Inset{Top: 7, Bottom: 7, Left: 9, Right: 9}.Layout(gtx, func(gtx C) D {
				gtx.Constraints.Min.X = gtx.Constraints.Max.X
				e := material.Editor(u.th, ed, hint)
				e.TextSize = 13
				e.HintColor = argb(0xff7c8098)
				return e.Layout(gtx)
			})
		})
	}
}

func (u *ui) btn(c *widget.Clickable, label string, enabled bool) layout.Widget {
	return func(gtx C) D { return u.button(gtx, c, label, false, enabled) }
}

func (u *ui) button(gtx C, c *widget.Clickable, label string, primary, enabled bool) D {
	if !enabled {
		gtx = gtx.Disabled()
	}
	b := material.Button(u.th, c, label)
	b.CornerRadius = 6
	b.TextSize = 13
	b.Background = colBtn
	b.Inset = layout.Inset{Top: 7, Bottom: 7, Left: 12, Right: 12}
	if primary {
		b.Background = colAccent
		b.TextSize = 15
		b.Font.Weight = font.Bold
		b.Inset = layout.Inset{Top: 9, Bottom: 9, Left: 28, Right: 28}
	}
	if !enabled { // the theme's disabled tint barely shows on translucent buttons
		b.Background.A /= 3
		b.Color = argb(0x80ffffff)
	}
	return b.Layout(gtx)
}

func (u *ui) foldersPanel(gtx C) D {
	idle := !u.isRunning()
	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		layout.Rigid(func(gtx C) D { return u.section(gtx, "FOLDERS") }),
		layout.Rigid(func(gtx C) D {
			return u.row(gtx, "Game folder", u.editor(&u.gameEd, "Acceleration of SUGURI 2 folder (with aos2.exe)", idle),
				u.btn(&u.btnGame, "Browse…", idle))
		}),
		layout.Rigid(space(8)),
		layout.Rigid(func(gtx C) D {
			return u.row(gtx, "Output folder", u.editor(&u.outEd, defaultOut(u.root)+"  (default)", idle),
				u.btn(&u.btnOut, "Browse…", idle), u.btn(&u.btnOutReset, "Default", idle && u.st.Out != ""))
		}),
	)
}

func (u *ui) radio(gtx C, e *widget.Enum, key, label string) D {
	r := material.RadioButton(u.th, e, key, label)
	r.Color, r.IconColor, r.TextSize, r.Size = colText, colAccent, 13, 20
	return r.Layout(gtx)
}

func (u *ui) check(gtx C, b *widget.Bool, label string) D {
	c := material.CheckBox(u.th, b, label)
	c.Color, c.IconColor, c.TextSize, c.Size = colText, colAccent, 13, 20
	return c.Layout(gtx)
}

func (u *ui) buildPanel(gtx C, running bool) D {
	if running {
		gtx = gtx.Disabled()
	}
	gap := layout.Rigid(space(14))
	hint := map[string]string{
		"3dsx": "aos2_3ds.3dsx: copy to /3ds/ on the SD card and start it from the Homebrew Launcher.",
		"cia":  "aos2_3ds.cia: install with FBI; it gets a HOME menu icon and full application memory.",
		"both": "Builds the .3dsx and the .cia from the same compile.",
	}[u.target.Value]
	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		layout.Rigid(func(gtx C) D { return u.section(gtx, "BUILD") }),
		layout.Rigid(func(gtx C) D {
			return u.row(gtx, "Format", func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Rigid(func(gtx C) D { return u.radio(gtx, &u.target, "3dsx", "Homebrew (.3dsx)") }), gap,
					layout.Rigid(func(gtx C) D { return u.radio(gtx, &u.target, "cia", "CIA") }), gap,
					layout.Rigid(func(gtx C) D { return u.radio(gtx, &u.target, "both", "Both") }),
				)
			})
		}),
		layout.Rigid(func(gtx C) D {
			return layout.Inset{Left: 104, Bottom: 4}.Layout(gtx, u.label(11, hint, colDim).Layout)
		}),
		layout.Rigid(func(gtx C) D {
			return u.row(gtx, "Options", func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Rigid(func(gtx C) D { return u.check(gtx, &u.rebuild, "Rebuild assets") }), gap,
					layout.Rigid(func(gtx C) D { return u.check(gtx, &u.profile, "Profiler build") }), gap,
					layout.Rigid(func(gtx C) D {
						if u.target.Value == "3dsx" {
							gtx = gtx.Disabled()
						}
						return u.check(gtx, &u.animBanner, "Animated CIA banner")
					}),
				)
			})
		}),
	)
}

func (u *ui) actions(gtx C, running bool) D {
	idle := !running && u.rootErr == nil
	build := map[string]string{"3dsx": "Build .3dsx", "cia": "Build CIA", "both": "Build both"}[u.target.Value]
	gap := layout.Rigid(space(8))
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(func(gtx C) D { return u.button(gtx, &u.btnBuild, build, true, idle) }), gap,
		layout.Rigid(u.btn(&u.btnSetup, "Set up toolchain", idle)), gap,
		layout.Rigid(u.btn(&u.btnOpen, "Open output folder", u.rootErr == nil)),
		layout.Flexed(1, fill),
		layout.Rigid(u.btn(&u.btnCancel, "Cancel", running)),
	)
}

func (u *ui) progress(gtx C, running bool, prog float64, label string) D {
	if !running && prog == 0 {
		label = "Idle"
	}
	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		layout.Rigid(func(gtx C) D {
			return layout.Flex{}.Layout(gtx,
				layout.Flexed(1, u.label(12, label, colText).Layout),
				layout.Rigid(u.label(12, fmt.Sprintf("%d%%", int(prog*100+0.5)), colDim).Layout),
			)
		}),
		layout.Rigid(space(4)),
		layout.Rigid(func(gtx C) D {
			p := material.ProgressBar(u.th, float32(prog))
			p.Color, p.TrackColor, p.Height, p.Radius = colAccent, argb(0x40ffffff), 6, 3
			return p.Layout(gtx)
		}),
	)
}

func (u *ui) logPanel(gtx C, logs []string) D {
	gtx.Constraints.Min = gtx.Constraints.Max
	return layout.Background{}.Layout(gtx, roundFill(colLog, 8), func(gtx C) D {
		return layout.UniformInset(unit.Dp(10)).Layout(gtx, func(gtx C) D {
			gtx.Constraints.Min = gtx.Constraints.Max
			l := material.List(u.th, &u.logList)
			l.AnchorStrategy = material.Overlay
			return l.Layout(gtx, len(logs), func(gtx C, i int) D {
				s := logs[i]
				c := argb(0xffc9cce0)
				switch {
				case strings.HasPrefix(s, "== "):
					c = colAccent
				case strings.HasPrefix(s, "error") || strings.HasPrefix(s, "FAIL") || strings.Contains(s, ": error:"):
					c = colErr
				}
				lb := u.label(12, s, c)
				lb.Font.Typeface = "Go Mono"
				return lb.Layout(gtx)
			})
		})
	})
}
