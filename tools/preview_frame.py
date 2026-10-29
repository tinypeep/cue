# Synthetic film frame + subtitle approximating what libass draws with our style.
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W, H, out, font, size, bg, pos = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4], int(sys.argv[5]), int(sys.argv[6]), float(sys.argv[7])
img = Image.new("RGB", (W, H))
d = ImageDraw.Draw(img)
for y in range(H):
    t = y / H
    c = (int(40 + 170 * t**1.6), int(60 + 70 * t), int(110 - 40 * t))
    d.line([(0, y), (W, y)], fill=c)
d.ellipse([W*0.58, H*0.42, W*0.72, H*0.42 + W*0.14], fill=(255, 214, 150))
d.polygon([(0, H*0.72), (W*0.25, H*0.58), (W*0.5, H*0.7), (W*0.8, H*0.55), (W, H*0.66), (W, H), (0, H)], fill=(24, 22, 30))
d.polygon([(0, H*0.85), (W*0.4, H*0.76), (W, H*0.88), (W, H), (0, H)], fill=(10, 10, 14))
img = img.filter(ImageFilter.GaussianBlur(1))
txt = "Você também viu aquela luz no céu?"
px = size * H / 720
f = ImageFont.truetype(font, int(px))
d = ImageDraw.Draw(img, "RGBA")
tw = d.textlength(txt, font=f)
bottom = pos / 100 * H
asc, desc = f.getmetrics()
pad = 6 * H / 720
x = W / 2 - tw / 2; y = bottom - asc - desc
if bg > 0:
    d.rectangle([x - pad, y - pad, x + tw + pad, y + asc + desc + pad], fill=(0, 0, 0, int(255 * bg / 100)))
else:
    for dx in (-2, 0, 2):
        for dy in (-2, 0, 2):
            d.text((x + dx, y + dy), txt, font=f, fill=(0, 0, 0, 200))
d.text((x, y), txt, font=f, fill=(255, 255, 255, 255))
img.convert("RGBA").save(out)
