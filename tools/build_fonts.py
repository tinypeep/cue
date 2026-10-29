"""Builds the three subtitle fonts shipped with Cue.
Omni   -> Omni Mono (from the Omni design system woff2), family "Cue Omni"
Clean  -> Inter 500 (OFL 1.1), merged script subsets, family "Cue Clean"
Readable -> Atkinson Hyperlegible 400 (OFL 1.1), family "Cue Readable"
Renaming avoids clashing with copies the user may have installed."""
import sys, glob, os
from fontTools.ttLib import TTFont
from fontTools.merge import Merger, Options

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def rename(font, family, style="Regular"):
    name = font["name"]
    for rec in list(name.names):
        if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22):
            name.removeNames(nameID=rec.nameID)
    full = f"{family}" if style == "Regular" else f"{family} {style}"
    ps = full.replace(" ", "")
    for nid, val in ((1, family), (2, style), (3, ps + ";cue"), (4, full), (6, ps)):
        name.setName(val, nid, 3, 1, 0x409)
        name.setName(val, nid, 1, 0, 0)
    if "CFF " in font:
        font["CFF "].cff.fontNames = [ps]

def load(path):
    f = TTFont(path)
    f.flavor = None
    return f

def merged(paths, tmp):
    fonts = []
    for i, p in enumerate(paths):
        f = load(p); t = f"{tmp}_{i}.ttf"; f.save(t); fonts.append(t)
    opts = Options(drop_tables=["vhea", "vmtx", "GSUB", "GPOS", "GDEF"])
    m = Merger(options=opts).merge(fonts)
    for t in fonts: os.remove(t)
    return m

src = sys.argv[2]
omni = load(f"{src}/omni.woff2"); rename(omni, "Cue Omni"); omni.save(f"{out}/CueOmni.ttf")
inter = sorted(glob.glob(f"{src}/fontsource-inter-*/package/files/inter-*-500-normal.woff2"))
m = merged(inter, f"{out}/_i"); rename(m, "Cue Clean"); m.save(f"{out}/CueClean.ttf")
atk = sorted(glob.glob(f"{src}/fontsource-atkinson-*/package/files/atkinson-hyperlegible-*-400-normal.woff2"))
m = merged(atk, f"{out}/_a"); rename(m, "Cue Readable"); m.save(f"{out}/CueReadable.ttf")
for f in sorted(os.listdir(out)):
    t = TTFont(f"{out}/{f}")
    print(f, os.path.getsize(f"{out}/{f}"), "glyphs", len(t.getGlyphOrder()), "cmap", len(t.getBestCmap()), t["name"].getDebugName(1))
