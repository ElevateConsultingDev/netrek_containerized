#!/usr/bin/env python3
"""Extract planet XBM bitmaps from COW source header files and save as PNGs.

Parses XBM bitmap definitions from oldbitmaps.h, moobitmaps.h, and
rabbitbitmaps.h, rendering white-on-transparent PNGs using pygame.
"""

import os
import re
import sys

# Ensure pygame doesn't open a display window
os.environ["SDL_VIDEODRIVER"] = "dummy"

import pygame

BASE = os.path.dirname(os.path.abspath(__file__))


def parse_xbm_bitmaps(header_path):
    """Parse all XBM bitmap definitions from a C header file.

    Returns a dict: name -> (width, height, bytes_list)
    For bitmaps without explicit width/height defines, they are
    inferred later.
    """
    with open(header_path, "r") as f:
        text = f.read()

    # Collect all #define NAME_width N and NAME_height N
    widths = {}
    heights = {}
    for m in re.finditer(r"#define\s+(\w+)_width\s+(\d+)", text):
        widths[m.group(1)] = int(m.group(2))
    for m in re.finditer(r"#define\s+(\w+)_height\s+(\d+)", text):
        heights[m.group(1)] = int(m.group(2))

    # Collect all static char NAME_bits[] = { ... };
    bitmaps = {}
    pattern = re.compile(
        r"static\s+(?:unsigned\s+)?char\s+(\w+)_bits\[\]\s*=\s*\{([^}]+)\}",
        re.DOTALL,
    )
    for m in pattern.finditer(text):
        name = m.group(1)
        data_str = m.group(2)
        # Parse hex bytes
        byte_values = []
        for hx in re.findall(r"0x([0-9a-fA-F]+)", data_str):
            byte_values.append(int(hx, 16))

        w = widths.get(name)
        h = heights.get(name)
        bitmaps[name] = (w, h, byte_values)

    return bitmaps


def xbm_to_surface(width, height, data):
    """Convert XBM byte data to a pygame Surface.

    XBM format: LSB first within each byte, rows padded to byte boundary.
    Bit=1 -> white pixel (255,255,255,255), bit=0 -> transparent (0,0,0,0).
    """
    row_bytes = (width + 7) // 8
    surf = pygame.Surface((width, height), pygame.SRCALPHA)
    surf.fill((0, 0, 0, 0))

    for y in range(height):
        for x in range(width):
            byte_idx = y * row_bytes + (x // 8)
            bit_idx = x % 8
            if byte_idx < len(data):
                if data[byte_idx] & (1 << bit_idx):
                    surf.set_at((x, y), (255, 255, 255, 255))

    return surf


def save_bitmap(bitmaps, name, output_path, default_width=None, default_height=None):
    """Extract a named bitmap and save as PNG. Returns True on success."""
    if name not in bitmaps:
        print(f"  MISSING: {name} not found in header")
        return False

    w, h, data = bitmaps[name]
    if w is None:
        w = default_width
    if h is None:
        h = default_height
    if w is None or h is None:
        print(f"  MISSING: {name} has no width/height and no default provided")
        return False

    full_path = os.path.join(BASE, output_path)
    os.makedirs(os.path.dirname(full_path), exist_ok=True)

    surf = xbm_to_surface(w, h, data)
    pygame.image.save(surf, full_path)
    print(f"  OK: {name} ({w}x{h}) -> {output_path}")
    return True


def main():
    pygame.init()
    # Need a display for Surface operations
    pygame.display.set_mode((1, 1))

    success = 0
    fail = 0

    # ── Parse all header files ──────────────────────────────────────────
    old = parse_xbm_bitmaps("/tmp/oldbitmaps.h")
    moo = parse_xbm_bitmaps("/tmp/moobitmaps.h")
    rabbit = parse_xbm_bitmaps("/tmp/rabbitbitmaps.h")

    # ── oldbitmaps.h: TACTICAL 30x30 owner planets ─────────────────────
    print("\n=== oldbitmaps.h: Tactical 30x30 owner planets ===")
    tactical_owner = {
        "indplanet": "assets/pixmaps/Planets/Tactical/owner/ind.png",
        "fedplanet": "assets/pixmaps/Planets/Tactical/owner/fed.png",
        "romplanet": "assets/pixmaps/Planets/Tactical/owner/rom.png",
        "kliplanet": "assets/pixmaps/Planets/Tactical/owner/kli.png",
        "oriplanet": "assets/pixmaps/Planets/Tactical/owner/ori.png",
    }
    for name, path in tactical_owner.items():
        if save_bitmap(old, name, path):
            success += 1
        else:
            fail += 1

    # Generic planet (continents) - no explicit width/height, 30x30
    print("\n=== oldbitmaps.h: Tactical 30x30 generic planet ===")
    if save_bitmap(old, "planet", "assets/pixmaps/Planets/Tactical/owner/planet.png",
                   default_width=30, default_height=30):
        success += 1
    else:
        fail += 1

    # ── oldbitmaps.h: TACTICAL 30x30 resource planets ──────────────────
    print("\n=== oldbitmaps.h: Tactical 30x30 resource planets (001-111) ===")
    for i in range(1, 8):  # 001 through 111 in binary = 1..7
        suffix = f"{i:03b}"
        name = f"planet{suffix}"
        path = f"assets/pixmaps/Planets/Tactical/standard/planet_{suffix}.png"
        if save_bitmap(old, name, path):
            success += 1
        else:
            fail += 1

    # ── oldbitmaps.h: GALACTIC 16x16 owner planets ─────────────────────
    print("\n=== oldbitmaps.h: Galactic 16x16 owner planets ===")
    galactic_owner = {
        "indmplanet": "assets/pixmaps/Planets/Galactic/owner/ind.png",
        "fedmplanet": "assets/pixmaps/Planets/Galactic/owner/fed.png",
        "rommplanet": "assets/pixmaps/Planets/Galactic/owner/rom.png",
        "klimplanet": "assets/pixmaps/Planets/Galactic/owner/kli.png",
        "orimplanet": "assets/pixmaps/Planets/Galactic/owner/ori.png",
    }
    for name, path in galactic_owner.items():
        if save_bitmap(old, name, path):
            success += 1
        else:
            fail += 1

    # Generic mplanet (continents) - no explicit width/height, 16x16
    print("\n=== oldbitmaps.h: Galactic 16x16 generic mplanet ===")
    if save_bitmap(old, "mplanet", "assets/pixmaps/Planets/Galactic/owner/planet.png",
                   default_width=16, default_height=16):
        success += 1
    else:
        fail += 1

    # ── oldbitmaps.h: GALACTIC 16x16 resource planets ──────────────────
    print("\n=== oldbitmaps.h: Galactic 16x16 resource planets (001-111) ===")
    for i in range(1, 8):  # 001 through 111
        suffix = f"{i:03b}"
        name = f"mplanet{suffix}"
        path = f"assets/pixmaps/Planets/Galactic/standard/mplanet_{suffix}.png"
        if save_bitmap(old, name, path):
            success += 1
        else:
            fail += 1

    # ── moobitmaps.h: TACTICAL 30x30 moo planets ──────────────────────
    print("\n=== moobitmaps.h: Tactical 30x30 moo planets (000-111) ===")
    for i in range(0, 8):  # 000 through 111
        suffix = f"{i:03b}"
        name = f"myplanet{suffix}"
        path = f"assets/pixmaps/Planets/Tactical/moo/myplanet_{suffix}.png"
        if save_bitmap(moo, name, path):
            success += 1
        else:
            fail += 1

    # noinfo planet
    print("\n=== moobitmaps.h: Tactical 30x30 noinfo planet ===")
    if save_bitmap(moo, "noinfo", "assets/pixmaps/Planets/Tactical/noinfo.png"):
        success += 1
    else:
        fail += 1

    # ── moobitmaps.h: GALACTIC 16x16 moo planets ──────────────────────
    print("\n=== moobitmaps.h: Galactic 16x16 moo planets ===")
    if save_bitmap(moo, "myindmplanet", "assets/pixmaps/Planets/Galactic/moo/myindmplanet.png"):
        success += 1
    else:
        fail += 1

    for i in range(1, 4):  # 001, 010, 011
        suffix = f"{i:03b}"
        name = f"mymplanet{suffix}"
        path = f"assets/pixmaps/Planets/Galactic/moo/mymplanet_{suffix}.png"
        if save_bitmap(moo, name, path):
            success += 1
        else:
            fail += 1

    # ── rabbitbitmaps.h: TACTICAL 30x30 rabbit planets ─────────────────
    print("\n=== rabbitbitmaps.h: Tactical 30x30 rabbit planets (000-111) ===")
    for i in range(0, 8):  # 000 through 111
        suffix = f"{i:03b}"
        name = f"rmyplanet{suffix}"
        path = f"assets/pixmaps/Planets/Tactical/rabbit/rmyplanet_{suffix}.png"
        if save_bitmap(rabbit, name, path):
            success += 1
        else:
            fail += 1

    # ── rabbitbitmaps.h: GALACTIC 16x16 rabbit planets ─────────────────
    print("\n=== rabbitbitmaps.h: Galactic 16x16 rabbit planets ===")
    if save_bitmap(rabbit, "rmyindmplanet", "assets/pixmaps/Planets/Galactic/rabbit/rmyindmplanet.png"):
        success += 1
    else:
        fail += 1

    for i in range(1, 4):  # 001, 010, 011
        suffix = f"{i:03b}"
        name = f"rmymplanet{suffix}"
        path = f"assets/pixmaps/Planets/Galactic/rabbit/rmymplanet_{suffix}.png"
        if save_bitmap(rabbit, name, path):
            success += 1
        else:
            fail += 1

    # ── Summary ────────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    print(f"TOTAL: {success} succeeded, {fail} failed, {success+fail} total")
    print(f"{'='*60}")

    pygame.quit()
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
