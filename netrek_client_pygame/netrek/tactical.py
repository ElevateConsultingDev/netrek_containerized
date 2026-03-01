"""Tactical window (local view) rendering."""
import math
import pygame
from .constants import *

def _netrek_cos(d):
    """X component for Netrek direction d (0=north, 64=east)."""
    return math.sin(d * math.pi / 128.0)

def _netrek_sin(d):
    """Y component for Netrek direction d (0=north, 64=east). Negative = up."""
    return -math.cos(d * math.pi / 128.0)

# COW shield_width / 2 for tractor beam endpoint spread
_TRACTOR_SPREAD = 12


class TacticalView:
    def __init__(self, surface, gamestate, sprite_mgr, config=None):
        self.surface = surface
        self.gs = gamestate
        self.sprites = sprite_mgr
        self.config = config
        self.frame_tick = 0

    def render(self):
        self.surface.fill((0, 0, 0))
        me = self.gs.me
        if not me or me.status not in (PALIVE, PEXPLODE):
            self._draw_waiting()
            self.frame_tick += 1
            return
        self._draw_planets(me)
        self._draw_torps(me)
        self._draw_plasmas(me)
        self._draw_phasers(me)
        self._draw_ships(me)
        self._draw_tractors(me)
        self._draw_lock_triangle(me)
        self._draw_alert_border(me)
        self.frame_tick += 1

    def _to_screen(self, ox, oy, me):
        """Convert game coords to tactical screen coords, centered on me."""
        sx = int((ox - me.render_x) / SCALE) + TWINSIDE // 2
        sy = int((oy - me.render_y) / SCALE) + TWINSIDE // 2
        return sx, sy

    def _in_view(self, sx, sy, margin=40):
        return -margin < sx < TWINSIDE + margin and -margin < sy < TWINSIDE + margin

    def _draw_planets(self, me):
        font = pygame.font.SysFont("monospace", 10)
        cfg = self.config
        showlocal = cfg.showlocal if cfg else 2
        show_ind = cfg.show_ind if cfg else True
        namemode = cfg.namemode if cfg else True
        agri_caps = cfg.agri_caps if cfg else True
        for pl in self.gs.planets:
            if not pl.name:
                continue
            sx, sy = self._to_screen(pl.x, pl.y, me)
            if not self._in_view(sx, sy, 100):
                continue

            icon = self.sprites.get_tactical_planet(pl, me.team, showlocal)
            if icon:
                r = icon.get_rect(center=(sx, sy))
                self.surface.blit(icon, r)
            else:
                color = TEAM_COLORS.get(pl.owner, (170, 170, 170))
                pygame.draw.circle(self.surface, color, (sx, sy), 15, 1)

            # X cross on independent planets (COW showIND)
            if show_ind and (pl.info & me.team) and pl.owner == NOBODY:
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx - 15, sy - 15), (sx + 14, sy + 14))
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx + 14, sy - 15), (sx - 15, sy + 14))

            # Planet name in team color below planet (matches COW)
            if namemode:
                name_color = TEAM_COLORS.get(pl.owner, (170, 170, 170))
                name_text = pl.name
                if agri_caps and (pl.flags & PLAGRI):
                    name_text = name_text.upper()
                label = font.render(name_text, True, name_color)
                self.surface.blit(label, (sx - label.get_width() // 2, sy + 16))

    def _draw_ships(self, me):
        font = pygame.font.SysFont("monospace", 10)
        for p in self.gs.players:
            if p.status not in (PALIVE, PEXPLODE):
                continue
            sx, sy = self._to_screen(p.render_x, p.render_y, me)
            if not self._in_view(sx, sy, 60):
                continue

            if p.status == PEXPLODE:
                exp_frame = self.sprites.get_explosion_frame(
                    (self.frame_tick) % 10)
                if exp_frame:
                    r = exp_frame.get_rect(center=(sx, sy))
                    self.surface.blit(exp_frame, r)
                continue

            # Cloaked enemies: show faint or skip
            if (p.flags & PFCLOAK) and p.pnum != self.gs.me_pnum:
                if p.team != me.team:
                    continue  # fully cloaked enemy invisible

            frame = self.sprites.get_ship_frame(p.team, p.shiptype, p.render_dir)
            if frame:
                # Native 20x20 sprite size matches COW at TWINSIDE=500
                r = frame.get_rect(center=(sx, sy))
                if p.flags & PFCLOAK:
                    frame = frame.copy()
                    frame.set_alpha(80)
                self.surface.blit(frame, r)
            else:
                color = TEAM_COLORS.get(p.team, (170, 170, 170))
                pygame.draw.circle(self.surface, color, (sx, sy), 6)

            # Shield circle (COW uses shield_width/2 + 1 = ~12 radius)
            if p.flags & PFSHIELD:
                color = TEAM_COLORS.get(p.team, (200, 200, 200))
                pygame.draw.circle(self.surface, color, (sx, sy), 12, 1)

            # Player label: team letter + number
            team_l = TEAM_LETTERS.get(p.team, "X")
            label_text = f"{team_l}{p.pnum % 16:x}"
            lbl_color = TEAM_COLORS.get(p.team, (200, 200, 200))
            label = font.render(label_text, True, lbl_color)
            self.surface.blit(label, (sx - label.get_width() // 2, sy + 12))

    def _draw_torps(self, me):
        for i, t in enumerate(self.gs.torps):
            if t.status == TFREE:
                continue
            sx, sy = self._to_screen(t.render_x, t.render_y, me)
            if not self._in_view(sx, sy):
                continue

            owner_pnum = i // MAXTORP
            owner = self.gs.players[owner_pnum]

            if t.status in (TEXPLODE, TDET):
                t.fuse -= 1
                if t.fuse <= 0:
                    t.status = TFREE
                    continue
                frame = t.fuse * 5 // 10  # map fuse to animation frame
                det_frame = self.sprites.get_torp_det_frame(
                    owner.team, frame % 5)
                if det_frame:
                    r = det_frame.get_rect(center=(sx, sy))
                    self.surface.blit(det_frame, r)
            elif t.status in (TMOVE, TSTRAIGHT):
                torp_frame = self.sprites.get_torp_frame(
                    owner.team, self.frame_tick)
                if torp_frame:
                    r = torp_frame.get_rect(center=(sx, sy))
                    self.surface.blit(torp_frame, r)
                else:
                    color = TEAM_COLORS.get(owner.team, (255, 255, 255))
                    pygame.draw.circle(self.surface, color, (sx, sy), 2)

    def _draw_plasmas(self, me):
        for i, pl in enumerate(self.gs.plasmas):
            if pl.status == PTFREE:
                continue
            sx, sy = self._to_screen(pl.render_x, pl.render_y, me)
            if not self._in_view(sx, sy):
                continue
            owner_pnum = i // MAXPLASMA
            owner = self.gs.players[owner_pnum]
            color = TEAM_COLORS.get(owner.team, (255, 255, 255))
            if pl.status in (PTEXPLODE, PTDET):
                pl.fuse -= 1
                if pl.fuse <= 0:
                    pl.status = PTFREE
                    continue
                pygame.draw.circle(self.surface, color, (sx, sy), 8)
            else:
                pygame.draw.circle(self.surface, color, (sx, sy), 4)

    def _draw_phasers(self, me):
        """Draw phasers matching COW local.c.

        PHHIT:  line from firer to target player
        PHHIT2: line from firer to ph.x/ph.y (hit a torp/plasma)
        PHMISS: line from firer to computed endpoint at PHASEDIST using ph.dir
        """
        for i, ph in enumerate(self.gs.phasers):
            if ph.status == PHFREE or ph.fuse <= 0:
                continue
            p = self.gs.players[i]
            if p.status != PALIVE:
                continue
            sx1, sy1 = self._to_screen(p.render_x, p.render_y, me)

            if ph.status == PHHIT and 0 <= ph.target < MAXPLAYER:
                # Hit a player -- draw to target's position
                target = self.gs.players[ph.target]
                sx2, sy2 = self._to_screen(target.render_x, target.render_y, me)
            elif ph.status == PHHIT2:
                # Hit a torp/plasma -- server sends endpoint in ph.x/ph.y
                sx2, sy2 = self._to_screen(ph.x, ph.y, me)
            else:
                # PHMISS -- compute endpoint from direction + PHASEDIST
                # COW: tx = PHASEDIST * phaserdamage/100 * Cos[dir]
                # We don't track per-ship phaserdamage, so use full PHASEDIST
                tx = p.render_x + PHASEDIST * _netrek_cos(ph.dir)
                ty = p.render_y + PHASEDIST * _netrek_sin(ph.dir)
                sx2, sy2 = self._to_screen(tx, ty, me)

            color = TEAM_COLORS.get(p.team, (255, 255, 255))
            if self.frame_tick % 2 == 0:
                pygame.draw.line(self.surface, color, (sx1, sy1), (sx2, sy2), 2)
            else:
                pygame.draw.line(self.surface, (255, 255, 255), (sx1, sy1), (sx2, sy2), 1)

            ph.fuse -= 1

    def _draw_tractors(self, me):
        """Draw tractor/pressor beams (COW local.c:697-767).

        COW draws two lines from the tracting ship to the edges of the tractee,
        perpendicular to the line between them. Green = tractor, yellow = pressor.
        Shows beams for self and (if server supports it) all visible players.
        """
        for j in self.gs.players:
            if j.status != PALIVE:
                continue
            if not (j.flags & (PFTRACT | PFPRESS)):
                continue
            if j.tractor < 0 or j.tractor >= MAXPLAYER:
                continue

            tractee = self.gs.players[j.tractor]
            if tractee.status != PALIVE:
                continue
            # Skip fully cloaked tractees
            if (tractee.flags & PFCLOAK) and tractee.pnum != self.gs.me_pnum:
                if tractee.team != me.team:
                    continue

            dx, dy = self._to_screen(j.render_x, j.render_y, me)
            px, py = self._to_screen(tractee.render_x, tractee.render_y, me)

            if not self._in_view(dx, dy, 60) and not self._in_view(px, py, 60):
                continue

            # Compute perpendicular spread at tractee position (COW local.c:727-737)
            ddx = px - dx
            ddy = py - dy
            dist = math.hypot(ddx, ddy)
            if dist < 1:
                continue
            # Unit perpendicular vector
            perp_x = -ddy / dist * _TRACTOR_SPREAD
            perp_y = ddx / dist * _TRACTOR_SPREAD

            lx0 = int(px + perp_x)
            ly0 = int(py + perp_y)
            lx1 = int(px - perp_x)
            ly1 = int(py - perp_y)

            # COW: pressor = yellow, tractor = green
            color = (255, 255, 0) if (j.flags & PFPRESS) else (0, 255, 0)

            # Draw two dashed lines from ship center to tractee edges
            self._draw_dashed_line(dx, dy, lx0, ly0, color)
            self._draw_dashed_line(dx, dy, lx1, ly1, color)

    def _draw_dashed_line(self, x1, y1, x2, y2, color, dash_len=6, gap_len=4):
        """Draw a dashed line matching COW's W_MakeTractLine."""
        dx = x2 - x1
        dy = y2 - y1
        length = math.hypot(dx, dy)
        if length < 1:
            return
        ux, uy = dx / length, dy / length
        pos = 0.0
        while pos < length:
            end = min(pos + dash_len, length)
            sx = int(x1 + ux * pos)
            sy = int(y1 + uy * pos)
            ex = int(x1 + ux * end)
            ey = int(y1 + uy * end)
            pygame.draw.line(self.surface, color, (sx, sy), (ex, ey))
            pos = end + gap_len

    def _draw_triangle(self, x, y, size, facing, color):
        """Draw a triangle matching COW's W_WriteTriangle.

        facing=0: points up (vertex at top, base below) — planet lock
        facing=1: points down (vertex at bottom, base above) — player lock
        """
        if facing == 0:
            points = [(x, y), (x + size, y - size), (x - size, y - size)]
        else:
            points = [(x, y), (x + size, y + size), (x - size, y + size)]
        pygame.draw.polygon(self.surface, color, points, 1)

    def _draw_lock_triangle(self, me):
        """Draw lock indicator triangle on tactical view (COW showLock & 2).

        Uses client-side lock state for immediate feedback rather than
        waiting for server flag confirmation (PFPLOCK/PFPLLOCK).
        """
        # Player lock
        pnum = self.gs.lock_player
        if 0 <= pnum < MAXPLAYER:
            j = self.gs.players[pnum]
            if j.status == PALIVE and not (j.flags & PFCLOAK):
                sx, sy = self._to_screen(j.render_x, j.render_y, me)
                if self._in_view(sx, sy):
                    self._draw_triangle(sx, sy + 20, 4, 1, (255, 255, 255))
        # Planet lock
        pnum = self.gs.lock_planet
        if 0 <= pnum < MAXPLANETS:
            pl = self.gs.planets[pnum]
            sx, sy = self._to_screen(pl.x, pl.y, me)
            if self._in_view(sx, sy):
                self._draw_triangle(sx, sy - 20, 4, 0, (255, 255, 255))

    def _draw_alert_border(self, me):
        if me.flags & PFRED:
            color = (255, 0, 0)
        elif me.flags & PFYELLOW:
            color = (255, 255, 0)
        else:
            color = (0, 255, 0)
        pygame.draw.rect(self.surface, color, (0, 0, TWINSIDE, TWINSIDE), 3)

    def _draw_waiting(self):
        font = pygame.font.SysFont("monospace", 20)
        text = font.render("Waiting for game...", True, (200, 200, 200))
        self.surface.blit(text, (TWINSIDE // 2 - text.get_width() // 2,
                                 TWINSIDE // 2 - text.get_height() // 2))
