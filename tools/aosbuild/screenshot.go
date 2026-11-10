package main

import (
	"image"
	"time"

	"gioui.org/gpu/headless"
	"gioui.org/io/input"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/paint"
	"gioui.org/unit"
)

// screenshot renders one frame of the GUI off-screen (aosbuild screenshot -o file.png), for checking the look
// without opening a window.
func screenshot(root, out string) error {
	const w, h = 1280, 760
	hw, err := headless.NewWindow(w, h)
	if err != nil {
		return err
	}
	defer hw.Release()
	u := newUI(nil)
	if img, err := backgroundImage(root, u.st.Background); err == nil {
		o := paint.NewImageOp(img)
		u.bg = &o
	}
	u.logs = append(u.logs, "== Pack sprites, fonts, menus, music", "beamMdl             4 frames  1 page(s)",
		"== Compile", "101 files compiled")
	var ops op.Ops
	var router input.Router // a live input source, so widgets draw enabled
	gtx := layout.Context{Ops: &ops, Metric: unit.Metric{PxPerDp: 1, PxPerSp: 1}, Constraints: layout.Exact(image.Pt(w, h)),
		Now: time.Now(), Source: router.Source()}
	u.frame(gtx)
	if err := hw.Frame(&ops); err != nil {
		return err
	}
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	if err := hw.Screenshot(img); err != nil {
		return err
	}
	return savePNG(out, img)
}
