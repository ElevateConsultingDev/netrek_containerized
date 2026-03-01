"""Galaxy map (strategic view) rendering."""
import pygame
from .constants import *


class GalacticView:
    def __init__(self, surface, gamestate, sprite_mgr, config=None):
        self.surface = surface
        self.gs = gamestate
        self.sprites = sprite_mgr
        self.config = config
        self.font = None

    def _get_font(self):
        if self.font is None:
            self.font = pygame.font.SysFont("monospace", 10)
        return self.font

    def render(self):
        self.surface.fill((0, 0, 0))
        self._draw_grid()
        self._draw_planets()
        self._draw_players()
        self._draw_lock_triangle()

    def _to_screen(self, gx, gy):
        """Convert game coords to galactic screen coords."""
        sx = int(gx * GWINSIDE // GWIDTH)
        sy = int(gy * GWINSIDE // GWIDTH)
        return sx, sy

    def _draw_grid(self):
        # COW draws visible gray grid lines for 5x5 sectors
        color = (60, 60, 60)
        step = GWINSIDE // 5
        for i in range(1, 5):
            pygame.draw.line(self.surface, color, (i * step, 0), (i * step, GWINSIDE))
            pygame.draw.line(self.surface, color, (0, i * step), (GWINSIDE, i * step))

    def _draw_planets(self):
        font = self._get_font()
        me = self.gs.me
        cfg = self.config
        showgalactic = cfg.showgalactic if cfg else 2
        show_ind = cfg.show_ind if cfg else True
        show_planet_owner = cfg.show_planet_owner if cfg else False
        namemode = cfg.namemode if cfg else True
        agri_caps = cfg.agri_caps if cfg else True
        for pl in self.gs.planets:
            if not pl.name:
                continue
            sx, sy = self._to_screen(pl.x, pl.y)
            my_team = me.team if me else 0

            # Determine display owner (unscanned -> NOBODY)
            if my_team and not (pl.info & my_team):
                display_owner = NOBODY
            else:
                display_owner = pl.owner

            icon = self.sprites.get_galactic_planet(pl, my_team, showgalactic)
            if icon:
                self.surface.blit(icon, (sx - 8, sy - 8))
            else:
                color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                pygame.draw.circle(self.surface, color, (sx, sy), 5)

            # W_Halo: COW draws a team-colored circle outline around
            # owned planets on the galactic map (not Ind/grey)
            if display_owner != NOBODY:
                halo_color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                pygame.draw.circle(self.surface, halo_color, (sx, sy), 8, 1)

            # Planet name in team color (COW style)
            if namemode:
                short = pl.name[:3]
                if agri_caps and (pl.flags & PLAGRI):
                    short = short.upper()
                name_color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                label = font.render(short, True, name_color)
                self.surface.blit(label, (sx - label.get_width() // 2, sy + 9))

            # Lowercase team letter to right of planet (COW showPlanetOwner)
            if show_planet_owner and display_owner != NOBODY:
                letter = TEAM_LETTERS.get(display_owner, "x").lower()
                owner_color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                owner_lbl = font.render(letter, True, owner_color)
                self.surface.blit(owner_lbl, (sx + 9, sy - 4))

            # X cross on independent planets (COW showIND)
            if show_ind and my_team and (pl.info & my_team) and pl.owner == NOBODY:
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx - 7, sy - 7), (sx + 7, sy + 7))
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx + 7, sy - 7), (sx - 7, sy + 7))

            # Army count if known (to the right of planet)
            if me and (pl.info & me.team) and pl.armies > 0:
                army_lbl = font.render(str(pl.armies), True, (200, 200, 200))
                self.surface.blit(army_lbl, (sx + 10, sy - 5))

    def _draw_players(self):
        font = self._get_font()
        for p in self.gs.players:
            if p.status not in (PALIVE, PEXPLODE):
                continue
            sx, sy = self._to_screen(p.render_x, p.render_y)

            # Cloaked enemies: skip on galactic
            me = self.gs.me
            if me and (p.flags & PFCLOAK) and p.pnum != self.gs.me_pnum:
                if p.team != me.team:
                    continue

            color = TEAM_COLORS.get(p.team, (170, 170, 170))

            # COW draws players as team letter + slot number, centered
            team_l = TEAM_LETTERS.get(p.team, "X")
            label_text = f"{team_l}{p.pnum % 16:x}"
            label = font.render(label_text, True, color)
            self.surface.blit(label, (sx - label.get_width() // 2,
                                      sy - label.get_height() // 2))

    def _draw_triangle(self, x, y, size, facing, color):
        """Draw a triangle matching COW's W_WriteTriangle.

        facing=0: points up — planet lock
        facing=1: points down — player lock
        """
        if facing == 0:
            points = [(x, y), (x + size, y - size), (x - size, y - size)]
        else:
            points = [(x, y), (x + size, y + size), (x - size, y + size)]
        pygame.draw.polygon(self.surface, color, points, 1)

    def _draw_lock_triangle(self):
        """Draw lock indicator triangle on galactic view (COW showLock & 1).

        Uses client-side lock state for immediate feedback.
        """
        me = self.gs.me
        if not me:
            return

        # Player lock
        pnum = self.gs.lock_player
        if 0 <= pnum < MAXPLAYER:
            j = self.gs.players[pnum]
            if j.status == PALIVE and not (j.flags & PFCLOAK):
                sx, sy = self._to_screen(j.render_x, j.render_y)
                self._draw_triangle(sx, sy + 6, 4, 1, (255, 255, 255))
        # Planet lock
        pnum = self.gs.lock_planet
        if 0 <= pnum < MAXPLANETS:
            pl = self.gs.planets[pnum]
            sx, sy = self._to_screen(pl.x, pl.y)
            self._draw_triangle(sx, sy - 8 - 4, 4, 0, (255, 255, 255))
