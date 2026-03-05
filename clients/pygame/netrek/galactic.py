"""Galaxy map (strategic view) rendering."""
import math
import pygame
from .constants import *


class GalacticView:
    def __init__(self, surface, gamestate, sprite_mgr, config=None, layout=None):
        self.surface = surface
        self.gs = gamestate
        self.sprites = sprite_mgr
        self.config = config
        self.layout = layout
        self.font = None
        self._font_size = 0

    def _get_font(self):
        size = self.layout.font_tiny
        if not self.font or self._font_size != size:
            self.font = pygame.font.SysFont("monospace", size)
            self._font_size = size
        return self.font

    def render(self):
        self.surface.fill((0, 0, 0))
        self._draw_grid()
        cfg = self.config
        if cfg and cfg.show_visibility_range:
            self._draw_visibility_ranges()
        self._draw_planets()
        if not cfg or cfg.weapons_on_map:
            self._draw_weapons()
        self._draw_players()
        if not cfg or cfg.view_box:
            self._draw_view_box()
        if not cfg or cfg.lock_line:
            self._draw_lock_line()
        show_lock = cfg.show_lock if cfg else 3
        if show_lock in (1, 3):  # 1=galactic only, 3=both
            self._draw_lock_triangle()

    def _to_screen(self, gx, gy):
        """Convert game coords to galactic screen coords."""
        gw = self.layout.gwinside
        sx = int(gx * gw // GWIDTH)
        sy = int(gy * gw // GWIDTH)
        return sx, sy

    def _draw_grid(self):
        lo = self.layout
        color = (60, 60, 60)
        step = lo.gwinside // 5
        for i in range(1, 5):
            pygame.draw.line(self.surface, color,
                             (i * step, 0), (i * step, lo.gwinside))
            pygame.draw.line(self.surface, color,
                             (0, i * step), (lo.gwinside, i * step))

    def _draw_visibility_ranges(self):
        """Draw scanner range circle around own ship on galactic.

        From daemon.c udplayersight():
          uncloaked: enemies within GWIDTH/3 can see you
          cloaked:   enemies within GWIDTH/7 can see you
        Single circle around your ship — if an enemy is inside, they see you.
        """
        me = self.gs.me
        if not me or me.status != PALIVE:
            return
        lo = self.layout
        gw = lo.gwinside
        if me.flags & PFCLOAK:
            r = int(GWIDTH // 7 * gw // GWIDTH)
        else:
            r = int(GWIDTH // 3 * gw // GWIDTH)
        sx, sy = self._to_screen(me.render_x, me.render_y)
        pygame.draw.circle(self.surface, (40, 40, 40), (sx, sy), r, 1)

    def _draw_planets(self):
        lo = self.layout
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

            if my_team and not (pl.info & my_team):
                display_owner = NOBODY
            else:
                display_owner = pl.owner

            icon = self.sprites.get_galactic_planet(pl, my_team, showgalactic)
            if icon:
                r = icon.get_rect(center=(sx, sy))
                self.surface.blit(icon, r)
            else:
                color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                pygame.draw.circle(self.surface, color, (sx, sy),
                                   lo.gal_planet_radius)

            if display_owner != NOBODY and (not cfg or cfg.owner_halo):
                halo_color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                pygame.draw.circle(self.surface, halo_color, (sx, sy),
                                   lo.gal_halo_radius, 1)

            if namemode:
                short = pl.name[:3]
                if agri_caps and (pl.flags & PLAGRI):
                    short = short.upper()
                name_color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                label = font.render(short, True, name_color)
                self.surface.blit(label, (sx - label.get_width() // 2,
                                          sy + lo.gal_name_offset))

            if show_planet_owner and display_owner != NOBODY:
                letter = TEAM_LETTERS.get(display_owner, "x").lower()
                owner_color = TEAM_COLORS.get(display_owner, (170, 170, 170))
                owner_lbl = font.render(letter, True, owner_color)
                self.surface.blit(owner_lbl, (sx + lo.gal_owner_offset_x,
                                              sy - lo.gal_owner_offset_y))

            if show_ind and my_team and (pl.info & my_team) and pl.owner == NOBODY:
                cr = lo.gal_ind_cross
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx - cr, sy - cr), (sx + cr, sy + cr))
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx + cr, sy - cr), (sx - cr, sy + cr))

            show_army = cfg.show_army if cfg else 3
            if (show_army & 2) and me and (pl.info & me.team) and pl.armies > 0:
                army_lbl = font.render(str(pl.armies), True, (200, 200, 200))
                self.surface.blit(army_lbl, (sx + lo.gal_army_offset_x,
                                             sy - lo.gal_army_offset_y))

    def _draw_weapons(self):
        """Draw torps, plasmas, and phasers on galactic map."""
        lo = self.layout
        torp_r = 1
        plasma_r = max(1, int(2 * lo.scale))

        for i, t in enumerate(self.gs.torps):
            if t.status not in (TMOVE, TSTRAIGHT):
                continue
            sx, sy = self._to_screen(t.x, t.y)
            owner = self.gs.players[i // MAXTORP]
            color = TEAM_COLORS.get(owner.team, (200, 200, 200))
            pygame.draw.circle(self.surface, color, (sx, sy), torp_r)

        for i, pl in enumerate(self.gs.plasmas):
            if pl.status != PTMOVE:
                continue
            sx, sy = self._to_screen(pl.x, pl.y)
            owner = self.gs.players[i // MAXPLASMA]
            color = TEAM_COLORS.get(owner.team, (200, 200, 200))
            pygame.draw.circle(self.surface, color, (sx, sy), plasma_r)

        for i, ph in enumerate(self.gs.phasers):
            if ph.status == PHFREE or ph.fuse <= 0:
                continue
            p = self.gs.players[i]
            if p.status != PALIVE:
                continue
            sx1, sy1 = self._to_screen(p.render_x, p.render_y)
            if ph.status == PHHIT and 0 <= ph.target < MAXPLAYER:
                target = self.gs.players[ph.target]
                sx2, sy2 = self._to_screen(target.render_x, target.render_y)
            elif ph.status == PHHIT2:
                sx2, sy2 = self._to_screen(ph.x, ph.y)
            else:
                tx = p.render_x + PHASEDIST * math.sin(ph.dir * math.pi / 128.0)
                ty = p.render_y + PHASEDIST * -math.cos(ph.dir * math.pi / 128.0)
                sx2, sy2 = self._to_screen(tx, ty)
            color = TEAM_COLORS.get(p.team, (200, 200, 200))
            pygame.draw.line(self.surface, color, (sx1, sy1), (sx2, sy2))

    def _draw_players(self):
        lo = self.layout
        font = self._get_font()
        me = self.gs.me
        for p in self.gs.players:
            if p.status not in (PALIVE, PEXPLODE):
                continue
            sx, sy = self._to_screen(p.render_x, p.render_y)

            # COW map.c: skip enemy cloaked ships; friendly cloaked show normally
            if me and (p.flags & PFCLOAK) and p.pnum != self.gs.me_pnum:
                if p.team != me.team:
                    continue

            color = TEAM_COLORS.get(p.team, (170, 170, 170))
            team_l = TEAM_LETTERS.get(p.team, "X")
            label_text = f"{team_l}{p.pnum % 16:x}"

            # Friendly cloaked ships shown dimmer on galactic
            if p.flags & PFCLOAK:
                color = (color[0] // 3, color[1] // 3, color[2] // 3)

            label = font.render(label_text, True, color)
            self.surface.blit(label, (sx - label.get_width() // 2,
                                      sy - label.get_height() // 2))

            # Shield indicator: small box around shielded ships
            if p.flags & PFSHIELD:
                lw = label.get_width()
                lh = label.get_height()
                pygame.draw.rect(self.surface, color,
                                 (sx - lw // 2 - 1, sy - lh // 2 - 1,
                                  lw + 2, lh + 2), 1)

    def _draw_triangle(self, x, y, size, facing, color):
        if facing == 0:
            points = [(x, y), (x + size, y - size), (x - size, y - size)]
        else:
            points = [(x, y), (x + size, y + size), (x - size, y + size)]
        pygame.draw.polygon(self.surface, color, points, 1)

    def _draw_view_box(self):
        """Draw tactical view extent on galactic map (Paradise viewBox)."""
        me = self.gs.me
        if not me:
            return
        half_view = (TWINSIDE // 2) * SCALE  # 10000 game coords
        x1, y1 = self._to_screen(me.render_x - half_view, me.render_y - half_view)
        x2, y2 = self._to_screen(me.render_x + half_view, me.render_y + half_view)
        gw = self.layout.gwinside
        x1 = max(0, min(x1, gw - 1))
        y1 = max(0, min(y1, gw - 1))
        x2 = max(0, min(x2, gw - 1))
        y2 = max(0, min(y2, gw - 1))
        color = (100, 100, 100)
        pygame.draw.rect(self.surface, color, (x1, y1, x2 - x1, y2 - y1), 1)

    def _draw_dashed_line(self, x1, y1, x2, y2, color, dash=6, gap=4):
        dx = x2 - x1
        dy = y2 - y1
        length = math.hypot(dx, dy)
        if length < 1:
            return
        ux, uy = dx / length, dy / length
        pos = 0.0
        while pos < length:
            end = min(pos + dash, length)
            pygame.draw.line(self.surface, color,
                             (int(x1 + ux * pos), int(y1 + uy * pos)),
                             (int(x1 + ux * end), int(y1 + uy * end)))
            pos = end + gap

    def _draw_lock_line(self):
        """Draw dashed green line from ship to lock target (Paradise lockLine)."""
        me = self.gs.me
        if not me:
            return
        mx, my = self._to_screen(me.render_x, me.render_y)
        color = (0, 255, 0)

        if me.flags & PFPLOCK:
            pnum = self.gs.lock_player
            if 0 <= pnum < MAXPLAYER:
                j = self.gs.players[pnum]
                if j.status == PALIVE and not (j.flags & PFCLOAK):
                    tx, ty = self._to_screen(j.render_x, j.render_y)
                    self._draw_dashed_line(mx, my, tx, ty, color)
        elif me.flags & PFPLLOCK:
            pnum = self.gs.lock_planet
            if 0 <= pnum < MAXPLANETS:
                pl = self.gs.planets[pnum]
                tx, ty = self._to_screen(pl.x, pl.y)
                self._draw_dashed_line(mx, my, tx, ty, color)

    def _draw_lock_triangle(self):
        lo = self.layout
        me = self.gs.me
        if not me:
            return

        if (me.flags & PFPLOCK):
            pnum = self.gs.lock_player
            if 0 <= pnum < MAXPLAYER:
                j = self.gs.players[pnum]
                if j.status == PALIVE and not (j.flags & PFCLOAK):
                    sx, sy = self._to_screen(j.render_x, j.render_y)
                    self._draw_triangle(sx, sy + lo.gal_lock_player_offset,
                                        lo.lock_size, 1, (255, 255, 255))
        if (me.flags & PFPLLOCK):
            pnum = self.gs.lock_planet
            if 0 <= pnum < MAXPLANETS:
                pl = self.gs.planets[pnum]
                sx, sy = self._to_screen(pl.x, pl.y)
                self._draw_triangle(sx, sy - lo.gal_lock_planet_offset,
                                    lo.lock_size, 0, (255, 255, 255))
