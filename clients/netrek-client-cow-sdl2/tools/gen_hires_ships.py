#!/usr/bin/env python3
"""Generate COW-format hi-res ship sprite strips from the netrekxp highres art.

Source: clients/netrek-client-netrekxp/resources/ships/highres/<race>shipHR.bmp
  Each file is 640x80 = 8 north-facing 80x80 tiles, one per ship type, in COW
  type order: SC DD CA BB AS SB GA AT.

Output: pixmaps-hr/<Race>/<TYPE>.png
  Each a 80x2560 vertical strip = 32 frames of 80x80. Frame v is the base tile
  rotated counterclockwise by v*11.25 deg -- the exact convention COW's 20px
  sprites use (frame 0 = north, 8 = west, 16 = south, 24 = east), verified
  against pixmaps/Fed/CA.png.

Transparency: the HR background is pure black (0,0,0); the darkest ship pixel
  is (32,32,32), so keying exact black to alpha 0 is clean.

One-time asset generator. Re-run after changing source art. Needs Pillow.
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(ROOT, "clients", "netrek-client-netrekxp",
                   "resources", "ships", "highres")
OUT = os.path.join(HERE, "..", "pixmaps-hr")

# HR filename stem -> COW race dir name
RACES = [("fedship", "Fed"), ("romship", "Rom"), ("kliship", "Kli"),
         ("oriship", "Ori"), ("indship", "Ind")]
# tile index -> COW ship file (matches shipfiles[] in sdl2sprite.c)
TYPES = ["SC", "DD", "CA", "BB", "AS", "SB", "GA", "AT"]

TILE = 80
NFRAMES = 32


def key_black(img):
    """Pure-black (0,0,0) -> transparent; everything else opaque."""
    img = img.convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if r == 0 and g == 0 and b == 0:
                px[x, y] = (0, 0, 0, 0)
    return img


def main():
    made = 0
    for stem, race in RACES:
        src_path = os.path.join(SRC, stem + "HR.bmp")
        if not os.path.exists(src_path):
            print("skip (missing):", src_path)
            continue
        sheet = key_black(Image.open(src_path))
        os.makedirs(os.path.join(OUT, race), exist_ok=True)
        for t, name in enumerate(TYPES):
            base = sheet.crop((t * TILE, 0, t * TILE + TILE, TILE))
            strip = Image.new("RGBA", (TILE, TILE * NFRAMES), (0, 0, 0, 0))
            for v in range(NFRAMES):
                # PIL rotate() is CCW-positive; COW frames advance CLOCKWISE
                # (view 8 = east, 24 = west), so negate. N/S look the same
                # either way, which masked this until in-game E/W swapped.
                frame = base.rotate(-v * (360.0 / NFRAMES),
                                    resample=Image.BICUBIC, expand=False)
                strip.paste(frame, (0, v * TILE))
            strip.save(os.path.join(OUT, race, name + ".png"))
            made += 1
    print("wrote", made, "ship strips to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
