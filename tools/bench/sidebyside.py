"""Side-by-side texture-variant comparison of emulator captures (tools/bench/run_capture.sh).
usage: python tools/bench/sidebyside.py <capdir> <outdir> <variant>...
<capdir>/<variant>/<scene>/f1XXXXXX.png -> <outdir>/perf_<scene>_<frame>.png: the full frames in a grid
(labelled) plus, below, a 3x zoom of the 120x72 region where the first two variants differ most.
Also prints the mean absolute difference of each variant against the first one (0-255 scale)."""
import glob
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageStat

cap, out, variants = sys.argv[1], sys.argv[2], sys.argv[3:]
os.makedirs(out, exist_ok=True)
base = variants[0]
for scene_dir in sorted(glob.glob(os.path.join(cap, base, '*'))):
    if not os.path.isdir(scene_dir):
        continue
    scene = os.path.basename(scene_dir)
    have = {int(os.path.basename(f)[2:8]) for f in glob.glob(os.path.join(scene_dir, 'f*.png'))}
    for frame in sorted(have):
        # run_capture2.sh grabs N-1, N, N+1: compare at the centre frame; for the other variants take the
        # neighbour that matches best (emulator read-back of the render target can be a frame early/late,
        # which shows as hit-flash/blink differences that have nothing to do with the textures)
        triples = any(f + 1 in have for f in have)
        if triples and not {frame - 1, frame + 1} <= have:
            continue
        fname = f"f1{frame:06d}.png"
        ims = []
        for v in variants:
            best = None
            for f in (frame, frame - 1, frame + 1):
                p = os.path.join(cap, v, scene, f"f1{f:06d}.png")
                if not os.path.exists(p):
                    continue
                im = Image.open(p).convert('RGB')
                if not ims:
                    best = (0, im)
                    break
                dd = sum(ImageStat.Stat(ImageChops.difference(ims[0], im)).mean)
                if best is None or dd < best[0]:
                    best = (dd, im)
            ims.append(best[1] if best else Image.new('RGB', (400, 240), (80, 0, 0)))
        diffs = [ImageStat.Stat(ImageChops.difference(ims[0], im)).mean for im in ims]
        # region of largest difference between variants[0] and variants[1]
        d = ImageChops.difference(ims[0], ims[1] if len(ims) > 1 else ims[0]).convert('L')
        best, bx, by = -1, 0, 0
        for y in range(0, 240 - 72 + 1, 12):
            for x in range(0, 400 - 120 + 1, 20):
                h = d.crop((x, y, x + 120, y + 72)).histogram()
                s = sum(h[i] * i for i in range(256))
                if s > best:
                    best, bx, by = s, x, y
        cols = 3
        rows = (len(ims) + cols - 1) // cols
        zoom = 3
        zw, zh = 120 * zoom, 72 * zoom
        W = cols * 404
        H = rows * 258 + 18 + ((len(ims) + 2) // 3) * (zh + 18)
        sheet = Image.new('RGB', (W, H), (20, 20, 20))
        dr = ImageDraw.Draw(sheet)
        for i, (v, im) in enumerate(zip(variants, ims)):
            x, y = (i % cols) * 404 + 2, (i // cols) * 258 + 16
            sheet.paste(im, (x, y))
            dr.text((x, y - 14), f"{v}  (mean |diff| vs {base}: {sum(diffs[i]) / 3:.2f})", fill=(255, 255, 255))
            if i == 0:
                dr.rectangle((x + bx, y + by, x + bx + 120, y + by + 72), outline=(255, 255, 0))
        y0 = rows * 258 + 18
        for i, (v, im) in enumerate(zip(variants, ims)):
            x, y = (i % 3) * (zw + 8) + 2, y0 + (i // 3) * (zh + 18) + 14
            if x + zw > W:
                continue
            sheet.paste(im.crop((bx, by, bx + 120, by + 72)).resize((zw, zh), Image.NEAREST), (x, y))
            dr.text((x, y - 13), f"{v} x{zoom}", fill=(255, 255, 0))
        name = f"perf_{scene}_{frame}.png"
        sheet.save(os.path.join(out, name))
        print(name, ' '.join(f"{v}:{sum(df) / 3:.2f}" for v, df in zip(variants, diffs)))
