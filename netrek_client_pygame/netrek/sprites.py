"""PNG sprite sheet loader + frame cache."""
import os
import pygame
from .constants import (
    TEAM_SPRITE_DIR, SHIP_NAMES, SCOUT, DESTROYER, CRUISER, BATTLESHIP,
    ASSAULT, STARBASE, SGALAXY, ATT, NOBODY, FED, ROM, KLI, ORI,
    PLFUEL, PLREPAIR, TEAM_COLORS,
)

ASSET_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "assets")
PLANET_DIR = os.path.join(ASSET_DIR, "pixmaps", "Planets")


def _load_strip(path):
    """Load a vertical strip PNG and split into square frames."""
    if not os.path.exists(path):
        return []
    img = pygame.image.load(path).convert_alpha()
    w = img.get_width()
    h = img.get_height()
    nframes = h // w
    frames = []
    for i in range(nframes):
        frame = img.subsurface(pygame.Rect(0, i * w, w, w))
        frames.append(frame)
    return frames


def _rosette(direction, nframes):
    """Convert a 0-255 direction to a frame index.

    direction: 0=north, 64=east, 128=south, 192=west
    """
    step = 256 // nframes
    return ((direction + step // 2) // step) % nframes


def _load_png(path):
    """Load a single PNG or return None if missing."""
    if os.path.exists(path):
        return pygame.image.load(path).convert_alpha()
    return None


def _planet_resource_index(planet):
    """Compute 3-bit resource index: (armies>4?4:0) | (PLREPAIR?2:0) | (PLFUEL?1:0)."""
    i = 0
    if planet.armies > 4:
        i |= 4
    if planet.flags & PLREPAIR:
        i |= 2
    if planet.flags & PLFUEL:
        i |= 1
    return i


def _tint(mask, color):
    """Return a copy of mask tinted by color (multiply white -> team color)."""
    tinted = mask.copy()
    tinted.fill(color + (0,), special_flags=pygame.BLEND_RGB_MULT)
    return tinted


def _scale_surface(surf, scale):
    """Scale a surface by the given factor using smooth interpolation."""
    if scale == 1.0:
        return surf
    w = max(1, int(surf.get_width() * scale))
    h = max(1, int(surf.get_height() * scale))
    return pygame.transform.smoothscale(surf, (w, h))


class SpriteManager:
    def __init__(self):
        self._ship_frames = {}  # (team, shiptype) -> list of Surface
        self._planet_icons = {}  # name -> Surface (16x16 galactic, legacy Map/)
        self._torp_frames = {}  # team -> list of Surface
        self._torp_det_frames = {}
        self._plasma_frames = {}
        self._plasma_det_frames = {}
        self._explosion_frames = []
        self._sb_explosion_frames = []
        self._cloak_frames = []

        # Planet bitmap sets (white masks, tinted at render time)
        # Tactical (30x30)
        self._tac_owner = {}      # team -> Surface
        self._tac_standard = {}   # 0..7 -> Surface
        self._tac_moo = {}        # 0..7 -> Surface
        self._tac_rabbit = {}     # 0..7 -> Surface
        self._tac_noinfo = None   # "?" planet
        # Galactic (16x16)
        self._gal_owner = {}      # team -> Surface
        self._gal_unknown = None  # continents for unknown planet
        self._gal_standard = {}   # 0..7 -> Surface
        self._gal_moo = {}        # 0..7 -> Surface
        self._gal_rabbit = {}     # 0..7 -> Surface

        self._planet_tint_cache = {}  # (style, index_key, color) -> Surface
        self._loaded = False
        self._raw_stored = False
        self._pending_scale = 1.0

    def load(self):
        if self._loaded:
            return
        self._loaded = True

        self._load_ships()
        self._load_projectiles()
        self._load_explosions()
        self._load_legacy_planet_icons()
        self._load_planet_bitmaps()
        self._store_raw()

    def _store_raw(self):
        """Snapshot loaded frames as raw originals for later rescaling."""
        self._raw_ship_frames = {k: list(v) for k, v in self._ship_frames.items()}
        self._raw_torp_frames = {k: list(v) for k, v in self._torp_frames.items()}
        self._raw_torp_det_frames = {k: list(v) for k, v in self._torp_det_frames.items()}
        self._raw_plasma_frames = {k: list(v) for k, v in self._plasma_frames.items()}
        self._raw_plasma_det_frames = {k: list(v) for k, v in self._plasma_det_frames.items()}
        self._raw_explosion_frames = list(self._explosion_frames)
        self._raw_sb_explosion_frames = list(self._sb_explosion_frames)
        self._raw_cloak_frames = list(self._cloak_frames)
        self._raw_tac_owner = dict(self._tac_owner)
        self._raw_tac_standard = dict(self._tac_standard)
        self._raw_tac_moo = dict(self._tac_moo)
        self._raw_tac_rabbit = dict(self._tac_rabbit)
        self._raw_tac_noinfo = self._tac_noinfo
        self._raw_gal_owner = dict(self._gal_owner)
        self._raw_gal_unknown = self._gal_unknown
        self._raw_gal_standard = dict(self._gal_standard)
        self._raw_gal_moo = dict(self._gal_moo)
        self._raw_gal_rabbit = dict(self._gal_rabbit)
        self._raw_stored = True
        # Apply any rescale that was requested before load completed
        if self._pending_scale != 1.0:
            self.rescale(self._pending_scale)
            self._pending_scale = 1.0

    def rescale(self, scale):
        """Scale all raw frames to display size and clear tint cache."""
        if not self._raw_stored:
            self._pending_scale = scale
            return
        self._planet_tint_cache.clear()

        def scale_frame_dict(raw_dict):
            return {k: [_scale_surface(f, scale) for f in v]
                    for k, v in raw_dict.items()}

        def scale_mask_dict(raw_dict):
            return {k: _scale_surface(v, scale) for k, v in raw_dict.items()}

        self._ship_frames = scale_frame_dict(self._raw_ship_frames)
        # 10x torp size for god-mode visibility
        torp_scale = scale * 10
        def scale_frame_dict_custom(raw_dict, s):
            return {k: [_scale_surface(f, s) for f in v]
                    for k, v in raw_dict.items()}
        self._torp_frames = scale_frame_dict_custom(self._raw_torp_frames, torp_scale)
        self._torp_det_frames = scale_frame_dict_custom(self._raw_torp_det_frames, torp_scale)
        self._plasma_frames = scale_frame_dict(self._raw_plasma_frames)
        self._plasma_det_frames = scale_frame_dict(self._raw_plasma_det_frames)
        self._explosion_frames = [_scale_surface(f, scale)
                                  for f in self._raw_explosion_frames]
        self._sb_explosion_frames = [_scale_surface(f, scale)
                                     for f in self._raw_sb_explosion_frames]
        self._cloak_frames = [_scale_surface(f, scale)
                              for f in self._raw_cloak_frames]

        self._tac_owner = scale_mask_dict(self._raw_tac_owner)
        self._tac_standard = scale_mask_dict(self._raw_tac_standard)
        self._tac_moo = scale_mask_dict(self._raw_tac_moo)
        self._tac_rabbit = scale_mask_dict(self._raw_tac_rabbit)
        self._tac_noinfo = (_scale_surface(self._raw_tac_noinfo, scale)
                            if self._raw_tac_noinfo else None)
        self._gal_owner = scale_mask_dict(self._raw_gal_owner)
        self._gal_unknown = (_scale_surface(self._raw_gal_unknown, scale)
                             if self._raw_gal_unknown else None)
        self._gal_standard = scale_mask_dict(self._raw_gal_standard)
        self._gal_moo = scale_mask_dict(self._raw_gal_moo)
        self._gal_rabbit = scale_mask_dict(self._raw_gal_rabbit)

    def _load_ships(self):
        for team in (FED, ROM, KLI, ORI, NOBODY):
            team_dir = os.path.join(ASSET_DIR, "pixmaps", TEAM_SPRITE_DIR[team])
            if not os.path.isdir(team_dir):
                continue
            for shiptype, shipname in SHIP_NAMES.items():
                path32 = os.path.join(team_dir, f"{shipname}32.png")
                path_default = os.path.join(team_dir, f"{shipname}.png")
                if os.path.exists(path32):
                    frames = _load_strip(path32)
                elif os.path.exists(path_default):
                    frames = _load_strip(path_default)
                else:
                    frames = []
                if frames:
                    self._ship_frames[(team, shiptype)] = frames

    def _load_projectiles(self):
        for team in (FED, ROM, KLI, ORI, NOBODY):
            team_dir = os.path.join(ASSET_DIR, "pixmaps", TEAM_SPRITE_DIR[team])
            if not os.path.isdir(team_dir):
                continue
            self._torp_frames[team] = _load_strip(os.path.join(team_dir, "torp.png"))
            self._torp_det_frames[team] = _load_strip(os.path.join(team_dir, "torp_det.png"))
            self._plasma_frames[team] = _load_strip(os.path.join(team_dir, "plasma.png"))
            self._plasma_det_frames[team] = _load_strip(os.path.join(team_dir, "plasma_det.png"))

    def _load_explosions(self):
        misc_dir = os.path.join(ASSET_DIR, "pixmaps", "Misc")
        self._explosion_frames = _load_strip(os.path.join(misc_dir, "explosion.png"))
        self._sb_explosion_frames = _load_strip(os.path.join(misc_dir, "sbexplosion.png"))
        self._cloak_frames = _load_strip(os.path.join(misc_dir, "cloak.png"))

    def _load_legacy_planet_icons(self):
        """Load legacy 16x16 Map/ icons (used as fallback only)."""
        planet_dir = os.path.join(ASSET_DIR, "pixmaps", "Planets", "Map")
        if os.path.isdir(planet_dir):
            for fname in os.listdir(planet_dir):
                if fname.endswith(".png"):
                    name = fname[:-4]
                    path = os.path.join(planet_dir, fname)
                    self._planet_icons[name] = pygame.image.load(path).convert_alpha()

    def _load_planet_bitmaps(self):
        """Load all 5-style planet bitmap sets for tactical and galactic."""
        tac = os.path.join(PLANET_DIR, "Tactical")
        gal = os.path.join(PLANET_DIR, "Galactic")

        # --- Tactical owner (style 1) ---
        team_files = {NOBODY: "ind", FED: "fed", ROM: "rom", KLI: "kli", ORI: "ori"}
        for team, fname in team_files.items():
            s = _load_png(os.path.join(tac, "owner", f"{fname}.png"))
            if s:
                self._tac_owner[team] = s

        # --- Tactical noinfo ---
        self._tac_noinfo = _load_png(os.path.join(tac, "noinfo.png"))

        # --- Tactical standard (style 2): indices 1-7 from files, 0 = owner/ind ---
        ind_tac = self._tac_owner.get(NOBODY)
        if ind_tac:
            self._tac_standard[0] = ind_tac
        for i in range(1, 8):
            bits = f"{i:03b}"
            s = _load_png(os.path.join(tac, "standard", f"planet_{bits}.png"))
            if s:
                self._tac_standard[i] = s

        # --- Tactical moo (style 3): all 8 from files ---
        for i in range(8):
            bits = f"{i:03b}"
            s = _load_png(os.path.join(tac, "moo", f"myplanet_{bits}.png"))
            if s:
                self._tac_moo[i] = s

        # --- Tactical rabbit (style 4): all 8 from files ---
        for i in range(8):
            bits = f"{i:03b}"
            s = _load_png(os.path.join(tac, "rabbit", f"rmyplanet_{bits}.png"))
            if s:
                self._tac_rabbit[i] = s

        # --- Galactic owner (style 1) ---
        for team, fname in team_files.items():
            s = _load_png(os.path.join(gal, "owner", f"{fname}.png"))
            if s:
                self._gal_owner[team] = s

        # --- Galactic unknown (continents bitmap) ---
        self._gal_unknown = _load_png(os.path.join(gal, "owner", "planet.png"))

        # --- Galactic standard (style 2): indices 1-7, 0 = owner/ind ---
        ind_gal = self._gal_owner.get(NOBODY)
        if ind_gal:
            self._gal_standard[0] = ind_gal
        for i in range(1, 8):
            bits = f"{i:03b}"
            s = _load_png(os.path.join(gal, "standard", f"mplanet_{bits}.png"))
            if s:
                self._gal_standard[i] = s

        # --- Galactic moo (style 3): indices 0-3 from files, 4-7 reuse standard ---
        s = _load_png(os.path.join(gal, "moo", "myindmplanet.png"))
        if s:
            self._gal_moo[0] = s
        for i in range(1, 4):
            bits = f"{i:03b}"
            s = _load_png(os.path.join(gal, "moo", f"mymplanet_{bits}.png"))
            if s:
                self._gal_moo[i] = s
        for i in range(4, 8):
            if i in self._gal_standard:
                self._gal_moo[i] = self._gal_standard[i]

        # --- Galactic rabbit (style 4): indices 0-3 from files, 4-7 reuse standard ---
        s = _load_png(os.path.join(gal, "rabbit", "rmyindmplanet.png"))
        if s:
            self._gal_rabbit[0] = s
        for i in range(1, 4):
            bits = f"{i:03b}"
            s = _load_png(os.path.join(gal, "rabbit", f"rmymplanet_{bits}.png"))
            if s:
                self._gal_rabbit[i] = s
        for i in range(4, 8):
            if i in self._gal_standard:
                self._gal_rabbit[i] = self._gal_standard[i]

    # ---- Ship sprites ----

    def get_ship_frame(self, team, shiptype, direction):
        """Get the ship Surface for the given team, type, and direction (0-255)."""
        frames = self._ship_frames.get((team, shiptype))
        if not frames:
            frames = self._ship_frames.get((NOBODY, shiptype))
        if not frames:
            return None
        idx = _rosette(direction, len(frames))
        return frames[idx]

    # ---- Planet rendering (style-aware) ----

    def _planet_color(self, planet, my_team):
        """Determine planet color: team color if we have info, else grey."""
        if my_team and not (planet.info & my_team):
            return TEAM_COLORS.get(NOBODY, (170, 170, 170))
        return TEAM_COLORS.get(planet.owner, (170, 170, 170))

    def _tint_cached(self, cache_key, mask, color):
        """Return tinted surface, using cache."""
        if cache_key in self._planet_tint_cache:
            return self._planet_tint_cache[cache_key]
        tinted = _tint(mask, color)
        self._planet_tint_cache[cache_key] = tinted
        return tinted

    def get_tactical_planet(self, planet, my_team=0, showlocal=2):
        """Get 30x30 tactical planet bitmap for the given style.

        Style 0: Nothing — always ind circle outline
        Style 1: Owner — team emblem (fed/rom/kli/ori/ind)
        Style 2: Standard — resource-encoded planet_XXX
        Style 3: MOO — diamond/circle myplanet_XXX
        Style 4: Rabbit — circle+flags rmyplanet_XXX
        """
        color = self._planet_color(planet, my_team)
        has_info = not my_team or bool(planet.info & my_team)

        # Unknown planet: show noinfo ("?") bitmap
        if not has_info:
            mask = self._tac_noinfo or self._tac_owner.get(NOBODY)
            if not mask:
                return None
            key = ("tac_noinfo", color)
            return self._tint_cached(key, mask, color)

        if showlocal == 0:
            # Nothing: always ind circle
            mask = self._tac_owner.get(NOBODY)
        elif showlocal == 1:
            # Owner: team emblem
            mask = self._tac_owner.get(planet.owner, self._tac_owner.get(NOBODY))
        elif showlocal == 2:
            # Standard resources
            idx = _planet_resource_index(planet)
            mask = self._tac_standard.get(idx, self._tac_owner.get(NOBODY))
        elif showlocal == 3:
            # MOO
            idx = _planet_resource_index(planet)
            mask = self._tac_moo.get(idx, self._tac_owner.get(NOBODY))
        elif showlocal == 4:
            # Rabbit
            idx = _planet_resource_index(planet)
            mask = self._tac_rabbit.get(idx, self._tac_owner.get(NOBODY))
        else:
            mask = self._tac_owner.get(NOBODY)

        if not mask:
            return None

        cache_key = ("tac", showlocal, planet.owner, planet.flags, planet.armies > 4, color)
        return self._tint_cached(cache_key, mask, color)

    def get_galactic_planet(self, planet, my_team=0, showgalactic=2):
        """Get 16x16 galactic planet bitmap for the given style.

        Style 0: Nothing — always ind small circle
        Style 1: Owner — team mini-emblem
        Style 2: Standard — resource mplanet_XXX
        Style 3: MOO — myindmplanet / mymplanet_XXX (4-7 reuse standard)
        Style 4: Rabbit — rmyindmplanet / rmymplanet_XXX (4-7 reuse standard)
        """
        color = self._planet_color(planet, my_team)
        has_info = not my_team or bool(planet.info & my_team)

        # Unknown planet: show continents bitmap
        if not has_info:
            mask = self._gal_unknown or self._gal_owner.get(NOBODY)
            if not mask:
                return None
            key = ("gal_unknown", color)
            return self._tint_cached(key, mask, color)

        if showgalactic == 0:
            mask = self._gal_owner.get(NOBODY)
        elif showgalactic == 1:
            mask = self._gal_owner.get(planet.owner, self._gal_owner.get(NOBODY))
        elif showgalactic == 2:
            idx = _planet_resource_index(planet)
            mask = self._gal_standard.get(idx, self._gal_owner.get(NOBODY))
        elif showgalactic == 3:
            idx = _planet_resource_index(planet)
            mask = self._gal_moo.get(idx, self._gal_owner.get(NOBODY))
        elif showgalactic == 4:
            idx = _planet_resource_index(planet)
            mask = self._gal_rabbit.get(idx, self._gal_owner.get(NOBODY))
        else:
            mask = self._gal_owner.get(NOBODY)

        if not mask:
            return None

        cache_key = ("gal", showgalactic, planet.owner, planet.flags, planet.armies > 4, color)
        return self._tint_cached(cache_key, mask, color)

    # ---- Legacy accessors (kept for compatibility) ----

    def get_planet_icon(self, planet, my_team=0):
        """Legacy 16x16 Map/ icons — prefer get_galactic_planet() instead."""
        return self.get_galactic_planet(planet, my_team, showgalactic=2)

    # ---- Projectile sprites ----

    def get_torp_frame(self, team, frame_tick):
        frames = self._torp_frames.get(team, [])
        if not frames:
            frames = self._torp_frames.get(FED, [])
        if frames:
            return frames[frame_tick % len(frames)]
        return None

    def get_plasma_frame(self, team, frame_tick):
        frames = self._plasma_frames.get(team, [])
        if not frames:
            frames = self._plasma_frames.get(FED, [])
        if frames:
            return frames[frame_tick % len(frames)]
        return None

    def get_torp_det_frame(self, team, frame_tick):
        frames = self._torp_det_frames.get(team, [])
        if not frames:
            frames = self._torp_det_frames.get(FED, [])
        if frames and frame_tick < len(frames):
            return frames[frame_tick]
        return None

    def get_plasma_det_frame(self, team, frame_tick):
        frames = self._plasma_det_frames.get(team, [])
        if not frames:
            frames = self._plasma_det_frames.get(FED, [])
        if frames and frame_tick < len(frames):
            return frames[frame_tick]
        return None

    @property
    def num_explosion_frames(self):
        return len(self._explosion_frames)

    @property
    def num_sb_explosion_frames(self):
        return len(self._sb_explosion_frames)

    def get_explosion_frame(self, frame_tick):
        if frame_tick < len(self._explosion_frames):
            return self._explosion_frames[frame_tick]
        return None

    def get_sb_explosion_frame(self, frame_tick):
        if frame_tick < len(self._sb_explosion_frames):
            return self._sb_explosion_frames[frame_tick]
        return None
