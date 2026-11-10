"""Decode profile-build frame captures into PNGs.
Sources: src/app/prof.c (sdmc:/aos2_cap.txt -> sdmc:/cap/f<frame>.raw, scripted "shot" ->
sdmc:/cap/<name>_top.raw / <name>_bot.raw) and conv_scene.c captures.
Raw = a render target colour buffer: 240x400 (top) or 240x320 (bottom) RGBA8 (ABGR bytes), 8x8 Morton tiles.
usage: python tools/capconv.py <cap dir> <out dir> [glob, default *.raw]   (needs Pillow)"""
import sys,glob,os
from PIL import Image
src,dst=sys.argv[1],sys.argv[2]; os.makedirs(dst,exist_ok=True)
pat=sys.argv[3] if len(sys.argv)>3 else '*.raw'
def mort(x,y):
    m=0
    for i in range(3):
        m|=((x>>i)&1)<<(2*i) | ((y>>i)&1)<<(2*i+1)
    return m
M=[[mort(x,y) for x in range(8)] for y in range(8)]
for p in sorted(glob.glob(os.path.join(src,pat))):
    d=open(p,'rb').read()
    W=240; H=len(d)//(W*4)  # target width 240 (screen height), height 400 or 320
    if H not in (400,320): continue
    out=bytearray(W*H*3)
    for ty in range(H//8):
        for tx in range(W//8):
            base=((ty*(W//8))+tx)*64*4
            for y in range(8):
                for x in range(8):
                    o=base+M[y][x]*4
                    a,b,g,r=d[o],d[o+1],d[o+2],d[o+3]
                    py=ty*8+y; px=tx*8+x
                    q=(py*W+px)*3; out[q]=r; out[q+1]=g; out[q+2]=b
    im=Image.frombytes('RGB',(W,H),bytes(out)).transpose(Image.ROTATE_90)
    im.save(os.path.join(dst,os.path.basename(p)[:-4]+'.png'))
