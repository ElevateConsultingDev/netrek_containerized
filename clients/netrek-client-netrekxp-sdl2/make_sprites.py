#!/usr/bin/env python3
"""Build combined sprite sheet BMPs from individual ship direction BMPs.
Layout: 8 columns (ship types) x 32 rows (directions)
Ship type order: SC DD CA BB AS SB GA AT (indices 0-7)
"""
import os
from PIL import Image

SRCDIR = "../netrek-client-netrekxp/resources/ships"
OUTDIR = "bitmaps/shiplib"
os.makedirs(OUTDIR, exist_ok=True)

TYPES = ["sc", "dd", "ca", "bb", "as", "sb", "ga", "at"]
RACES = ["fed", "rom", "kli", "ori", "ind"]

VARIANTS = {
    "mono":      ("M",  20),
    "color":     ("",   20),
    "color1":    ("1",  20),
    "grayscale": ("G",  20),
    "tinted":    ("T",  20),
    "highres":   ("HR", 80),
}

for race in RACES:
    for dirname, (suffix, cell) in VARIANTS.items():
        indir = os.path.join(SRCDIR, dirname, race)
        outfile = os.path.join(OUTDIR, f"{race}ship{suffix}.bmp")

        if not os.path.isdir(indir):
            print(f"  Skip {indir} (not found)")
            continue

        sheet_w = 8 * cell
        sheet_h = 32 * cell
        sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
        missing = 0

        for d in range(32):
            for t, stype in enumerate(TYPES):
                fname = f"{race}_{stype}{d+1:02d}.bmp"
                fpath = os.path.join(indir, fname)
                if os.path.isfile(fpath):
                    try:
                        img = Image.open(fpath).convert("RGBA")
                        # Make black pixels transparent
                        pixels = img.load()
                        for y in range(img.height):
                            for x in range(img.width):
                                r, g, b, a = pixels[x, y]
                                if r == 0 and g == 0 and b == 0:
                                    pixels[x, y] = (0, 0, 0, 0)
                        sheet.paste(img, (t * cell, d * cell))
                    except Exception as e:
                        missing += 1
                else:
                    missing += 1

        # Save as BMP (24-bit, SDL2_image can load this)
        sheet_rgb = Image.new("RGB", sheet.size, (0, 0, 0))
        sheet_rgb.paste(sheet, mask=sheet.split()[3])
        sheet_rgb.save(outfile)
        print(f"  {outfile}: {sheet_w}x{sheet_h} ({missing} missing)")

print("Done.")
