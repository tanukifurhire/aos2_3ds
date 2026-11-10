"""HOME-menu art for the CIA (tools/make_cia.sh), cut from the extracted PC assets.

usage: make_art.py <extracted dir> <out dir>
writes banner.png (256x128, top-screen banner), icon.png (48x48), banner.wav (banner jingle)
"""
import shutil
import sys

from PIL import Image, ImageDraw

src, out = sys.argv[1], sys.argv[2]
BG = (34, 36, 58, 255)

# banner: title logo on a dark backdrop
logo = Image.open(f"{src}/texture/menuMdl/ttl_logo.png").convert("RGBA")
logo = logo.crop(logo.getbbox())
logo.thumbnail((244, 116), Image.LANCZOS)
banner = Image.new("RGBA", (256, 128), BG)
banner.alpha_composite(logo, ((256 - logo.width) // 2, (128 - logo.height) // 2))
banner.save(f"{out}/banner.png")

# icon: one portrait circle of the select-screen sheet (4x4 cells of 256 px)
CELL = (1, 1)  # (col, row)
sheet = Image.open(f"{src}/texture/selMdl/sel_icon00.png").convert("RGBA")
x, y = CELL[0] * 256, CELL[1] * 256
face = sheet.crop((x + 18, y + 18, x + 238, y + 238)).resize((256, 256), Image.LANCZOS)  # circle edge to edge
icon = Image.new("RGBA", (256, 256), BG)
icon.alpha_composite(face)
mask = Image.new("L", (256, 256), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, 255, 255), 40, fill=255)
icon.putalpha(mask)
icon.resize((48, 48), Image.LANCZOS).save(f"{out}/icon.png")

# banner jingle: the menu "decide" sound (~2 s, banner limit 3 s)
shutil.copy(f"{src}/se/se/SH_DECIDE.WAV", f"{out}/banner.wav")
