"""Turns the layer folders written by tools/brand_images.cpp into layered PSDs (Photoshop).
   pip install psd-tools
   python3 tools/brand_psd.py cue-cover-layers cue-cover.psd
Each layer keeps its name and position; pixels stay exact (no smoothing)."""
import sys
from PIL import Image
from psd_tools import PSDImage
from psd_tools.api.layers import PixelLayer

src, out = sys.argv[1], sys.argv[2]
names = [l.rstrip("\n").split("\t") for l in open(f"{src}/layers.txt", encoding="utf-8") if l.strip()]
first = Image.open(f"{src}/{names[0][0]}.png")
psd = PSDImage.new("RGB", first.size)
for num, name in names:  # bottom to top
    im = Image.open(f"{src}/{num}.png").convert("RGBA")
    box = im.getchannel("A").getbbox()
    if not box:
        continue
    layer = PixelLayer.frompil(im.crop(box), psd, name, box[1], box[0])
    psd.append(layer)
psd.save(out)
print("ok", out, len(psd), "layers")
