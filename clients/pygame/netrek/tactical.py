"""Tactical window (local view) rendering."""
import math
import random
import pygame
from .constants import *

# Background stars: fixed random positions in game coords (Paradise planets.c:191-204)
_NUM_STARS = 1600
_rng = random.Random(42)  # deterministic seed so all clients see the same stars
_STAR_X = [((i % 40) * 5000 + _rng.randint(0, 4999)) for i in range(_NUM_STARS)]
_STAR_Y = [((i // 40) * 5000 + _rng.randint(0, 4999)) for i in range(_NUM_STARS)]

def _netrek_cos(d):
    """X component for Netrek direction d (0=north, 64=east)."""
    return math.sin(d * math.pi / 128.0)

def _netrek_sin(d):
    """Y component for Netrek direction d (0=north, 64=east). Negative = up."""
    return -math.cos(d * math.pi / 128.0)


class TacticalView:
    def __init__(self, surface, gamestate, sprite_mgr, config=None, layout=None):
        self.surface = surface
        self.gs = gamestate
        self.sprites = sprite_mgr
        self.config = config
        self.layout = layout
        self.frame_tick = 0
        self.font = None
        self._font_size = 0
        self._waiting_font = None
        self._waiting_font_size = 0

    def _get_font(self):
        size = self.layout.font_tiny
        if not self.font or self._font_size != size:
            self.font = pygame.font.SysFont("monospace", size)
            self._font_size = size
        return self.font

    def _get_waiting_font(self):
        size = self.layout.font_waiting
        if not self._waiting_font or self._waiting_font_size != size:
            self._waiting_font = pygame.font.SysFont("monospace", size)
            self._waiting_font_size = size
        return self._waiting_font

    def render(self):
        self.surface.fill((0, 0, 0))
        me = self.gs.me
        if not me or me.status not in (PALIVE, PEXPLODE):
            self._draw_waiting()
            self.frame_tick += 1
            return
        cfg = self.config
        if not cfg or cfg.show_stars:
            self._draw_stars(me)
        self._draw_planets(me)
        self._draw_torps(me)
        self._draw_plasmas(me)
        self._draw_phasers(me)
        self._draw_ships(me)
        if not cfg or cfg.show_tractor_pressor:
            self._draw_tractors(me)
        show_lock = cfg.show_lock if cfg else 3
        if show_lock >= 2:  # 2=tactical only, 3=both
            self._draw_lock_triangle(me)
        self._draw_galaxy_edges(me)
        if not cfg or cfg.extra_alert_border:
            self._draw_alert_border(me)
        self.frame_tick += 1

    def _to_screen(self, ox, oy, me):
        """Convert game coords to tactical screen coords, centered on me."""
        lo = self.layout
        sx = int((ox - me.render_x) / lo.game_scale) + lo.twinside // 2
        sy = int((oy - me.render_y) / lo.game_scale) + lo.twinside // 2
        return sx, sy

    def _in_view(self, sx, sy, margin=0):
        lo = self.layout
        m = margin if margin else int(40 * lo.scale)
        return -m < sx < lo.twinside + m and -m < sy < lo.twinside + m

    def _draw_stars(self, me):
        """Draw background star dots (Paradise showStars)."""
        lo = self.layout
        tw = lo.twinside
        gs = lo.game_scale
        hw = tw // 2
        mx, my = me.render_x, me.render_y
        color = (80, 80, 80)
        for i in range(_NUM_STARS):
            sx = int((_STAR_X[i] - mx) / gs) + hw
            sy = int((_STAR_Y[i] - my) / gs) + hw
            if 0 <= sx < tw and 0 <= sy < tw:
                self.surface.set_at((sx, sy), color)

    def _draw_planets(self, me):
        lo = self.layout
        font = self._get_font()
        cfg = self.config
        showlocal = cfg.showlocal if cfg else 2
        show_ind = cfg.show_ind if cfg else True
        namemode = cfg.namemode if cfg else True
        agri_caps = cfg.agri_caps if cfg else True
        for pl in self.gs.planets:
            if not pl.name:
                continue
            sx, sy = self._to_screen(pl.x, pl.y, me)
            if not self._in_view(sx, sy, int(100 * lo.scale)):
                continue

            icon = self.sprites.get_tactical_planet(pl, me.team, showlocal)
            if icon:
                r = icon.get_rect(center=(sx, sy))
                self.surface.blit(icon, r)
            else:
                color = TEAM_COLORS.get(pl.owner, (170, 170, 170))
                pygame.draw.circle(self.surface, color, (sx, sy), lo.planet_radius, 1)

            if show_ind and (pl.info & me.team) and pl.owner == NOBODY:
                cr = lo.ind_cross
                ce = lo.ind_cross_end
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx - cr, sy - cr), (sx + ce, sy + ce))
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (sx + ce, sy - cr), (sx - cr, sy + ce))

            if namemode:
                name_color = TEAM_COLORS.get(pl.owner, (170, 170, 170))
                name_text = pl.name
                if agri_caps and (pl.flags & PLAGRI):
                    name_text = name_text.upper()
                label = font.render(name_text, True, name_color)
                self.surface.blit(label, (sx - label.get_width() // 2,
                                          sy + lo.planet_name_offset))

            # Army count (COW local.c: show armies if known, showArmy bit 0)
            show_army = cfg.show_army if cfg else 3
            if (show_army & 1) and (pl.info & me.team) and pl.armies > 0:
                army_lbl = font.render(str(pl.armies), True, (200, 200, 200))
                self.surface.blit(army_lbl, (sx + lo.planet_radius + 2,
                                              sy - army_lbl.get_height() // 2))

    def _draw_ships(self, me):
        lo = self.layout
        font = self._get_font()
        max_cloak_phase = 7
        for p in self.gs.players:
            if p.status not in (PALIVE, PEXPLODE):
                p.cloak_phase = 0
                continue
            sx, sy = self._to_screen(p.render_x, p.render_y, me)
            if not self._in_view(sx, sy, int(60 * lo.scale)):
                continue

            if p.status == PEXPLODE:
                p.cloak_phase = 0
                if p.shiptype == STARBASE:
                    nef = self.sprites.num_sb_explosion_frames or 1
                    exp_frame = self.sprites.get_sb_explosion_frame(
                        self.frame_tick % nef)
                else:
                    nef = self.sprites.num_explosion_frames or 1
                    exp_frame = self.sprites.get_explosion_frame(
                        self.frame_tick % nef)
                if exp_frame:
                    r = exp_frame.get_rect(center=(sx, sy))
                    self.surface.blit(exp_frame, r)
                continue

            # Update cloak phase animation (COW local.c:197-222)
            if p.flags & PFCLOAK:
                if p.cloak_phase < max_cloak_phase:
                    p.cloak_phase += 1
            else:
                if p.cloak_phase > 0:
                    p.cloak_phase -= 1

            if (p.flags & PFCLOAK) and p.pnum != self.gs.me_pnum:
                if p.team != me.team:
                    if p.cloak_phase >= max_cloak_phase:
                        # Fully cloaked enemy: draw "??" marker (COW cloakChars)
                        clk_label = font.render("??", True, (100, 100, 100))
                        self.surface.blit(clk_label,
                                          (sx - clk_label.get_width() // 2,
                                           sy - clk_label.get_height() // 2))
                        continue
                    # Still phasing — fall through to draw ship with alpha

            # Ship sprite alpha: fully visible=255, fully cloaked=40
            ship_alpha = 255 - p.cloak_phase * (215 // max_cloak_phase)

            frame = self.sprites.get_ship_frame(p.team, p.shiptype, p.render_dir)
            if frame:
                r = frame.get_rect(center=(sx, sy))
                if p.cloak_phase > 0:
                    frame = frame.copy()
                    frame.set_alpha(ship_alpha)
                self.surface.blit(frame, r)
            else:
                color = TEAM_COLORS.get(p.team, (170, 170, 170))
                pygame.draw.circle(self.surface, color, (sx, sy),
                                   lo.ship_fallback_radius)

            if p.flags & PFSHIELD:
                cfg = self.config
                if p.pnum == self.gs.me_pnum and (not cfg or cfg.vary_shields):
                    # Own ship: color by shield % (COW local.c:422-434, varyShields)
                    max_sh = self.gs.ship_cap.s_maxshield or 100
                    pct = 100 * me.shield // max_sh
                    if pct < 34:
                        sh_color = (255, 0, 0)
                    elif pct < 67:
                        sh_color = (255, 255, 0)
                    else:
                        sh_color = (0, 255, 0)
                else:
                    sh_color = TEAM_COLORS.get(p.team, (200, 200, 200))
                pygame.draw.circle(self.surface, sh_color, (sx, sy),
                                   lo.shield_radius, 1)

            team_l = TEAM_LETTERS.get(p.team, "X")
            # showMySpeed/showOtherSpeed: "{slot},{speed}" (Paradise local.c:670)
            label_text = f"{team_l}{p.pnum % 16:x},{p.speed}"
            # Own ship ID color matches alert status (COW local.c:489-503)
            if p.pnum == self.gs.me_pnum:
                lbl_color = self._alert_color(me)
            else:
                lbl_color = TEAM_COLORS.get(p.team, (200, 200, 200))
            label = font.render(label_text, True, lbl_color)
            self.surface.blit(label, (sx - label.get_width() // 2,
                                      sy + lo.ship_label_offset))

    def _draw_torps(self, me):
        lo = self.layout
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
                frame = t.fuse * 5 // 10
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
                    pygame.draw.circle(self.surface, color, (sx, sy),
                                       lo.torp_fallback_radius)

    def _draw_plasmas(self, me):
        lo = self.layout
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
                frame = pl.fuse * 5 // 10
                det_frame = self.sprites.get_plasma_det_frame(
                    owner.team, frame % 5)
                if det_frame:
                    r = det_frame.get_rect(center=(sx, sy))
                    self.surface.blit(det_frame, r)
                else:
                    pygame.draw.circle(self.surface, color, (sx, sy),
                                       lo.plasma_explode_radius)
            else:
                plasma_frame = self.sprites.get_plasma_frame(
                    owner.team, self.frame_tick)
                if plasma_frame:
                    r = plasma_frame.get_rect(center=(sx, sy))
                    self.surface.blit(plasma_frame, r)
                else:
                    pygame.draw.circle(self.surface, color, (sx, sy),
                                       lo.plasma_radius)

    def _draw_phasers(self, me):
        lo = self.layout
        for i, ph in enumerate(self.gs.phasers):
            if ph.status == PHFREE or ph.fuse <= 0:
                continue
            p = self.gs.players[i]
            if p.status != PALIVE:
                continue
            sx1, sy1 = self._to_screen(p.render_x, p.render_y, me)

            if ph.status == PHHIT and 0 <= ph.target < MAXPLAYER:
                target = self.gs.players[ph.target]
                sx2, sy2 = self._to_screen(target.render_x, target.render_y, me)
            elif ph.status == PHHIT2:
                sx2, sy2 = self._to_screen(ph.x, ph.y, me)
            else:
                tx = p.render_x + PHASEDIST * _netrek_cos(ph.dir)
                ty = p.render_y + PHASEDIST * _netrek_sin(ph.dir)
                sx2, sy2 = self._to_screen(tx, ty, me)

            # Phaser shrink: source moves toward target as fuse decreases
            # (COW local.c:604-615, phaserShrink)
            initial_fuse = 10
            shrink_frac = (initial_fuse - ph.fuse) / initial_fuse
            sx1 = int(sx1 + (sx2 - sx1) * shrink_frac)
            sy1 = int(sy1 + (sy2 - sy1) * shrink_frac)

            # Hit phasers use team color; misses use dim grey (COW local.c)
            if ph.status == PHMISS:
                color = (100, 100, 100)
                alt_color = (60, 60, 60)
            else:
                color = TEAM_COLORS.get(p.team, (255, 255, 255))
                alt_color = (255, 255, 255)
            if self.frame_tick % 2 == 0:
                pygame.draw.line(self.surface, color,
                                 (sx1, sy1), (sx2, sy2), lo.phaser_width)
            else:
                pygame.draw.line(self.surface, alt_color,
                                 (sx1, sy1), (sx2, sy2), max(1, lo.phaser_width // 2))

            ph.fuse -= 1

    def _draw_tractors(self, me):
        lo = self.layout
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
            if (tractee.flags & PFCLOAK) and tractee.pnum != self.gs.me_pnum:
                if tractee.team != me.team:
                    continue

            dx, dy = self._to_screen(j.render_x, j.render_y, me)
            px, py = self._to_screen(tractee.render_x, tractee.render_y, me)

            if not self._in_view(dx, dy, int(60 * lo.scale)) and not self._in_view(px, py, int(60 * lo.scale)):
                continue

            ddx = px - dx
            ddy = py - dy
            dist = math.hypot(ddx, ddy)
            if dist < 1:
                continue
            spread = lo.tractor_spread
            perp_x = -ddy / dist * spread
            perp_y = ddx / dist * spread

            lx0 = int(px + perp_x)
            ly0 = int(py + perp_y)
            lx1 = int(px - perp_x)
            ly1 = int(py - perp_y)

            color = (255, 255, 0) if (j.flags & PFPRESS) else (0, 255, 0)

            self._draw_dashed_line(dx, dy, lx0, ly0, color)
            self._draw_dashed_line(dx, dy, lx1, ly1, color)

    def _draw_dashed_line(self, x1, y1, x2, y2, color):
        lo = self.layout
        dash_len = lo.trac_dash_len
        gap_len = lo.trac_gap_len
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
        if facing == 0:
            points = [(x, y), (x + size, y - size), (x - size, y - size)]
        else:
            points = [(x, y), (x + size, y + size), (x - size, y + size)]
        pygame.draw.polygon(self.surface, color, points, 1)

    def _draw_lock_triangle(self, me):
        lo = self.layout
        if (me.flags & PFPLOCK):
            pnum = self.gs.lock_player
            if 0 <= pnum < MAXPLAYER:
                j = self.gs.players[pnum]
                if j.status == PALIVE and not (j.flags & PFCLOAK):
                    sx, sy = self._to_screen(j.render_x, j.render_y, me)
                    if self._in_view(sx, sy):
                        self._draw_triangle(sx, sy + lo.lock_offset,
                                            lo.lock_size, 1, (255, 255, 255))
        if (me.flags & PFPLLOCK):
            pnum = self.gs.lock_planet
            if 0 <= pnum < MAXPLANETS:
                pl = self.gs.planets[pnum]
                sx, sy = self._to_screen(pl.x, pl.y, me)
                if self._in_view(sx, sy):
                    self._draw_triangle(sx, sy - lo.lock_offset,
                                        lo.lock_size, 0, (255, 255, 255))

    def _alert_color(self, me):
        if me.flags & PFRED:
            return (255, 0, 0)
        elif me.flags & PFYELLOW:
            return (255, 255, 0)
        return (0, 255, 0)

    def _draw_galaxy_edges(self, me):
        """Draw galaxy boundary lines when near the edge (COW local.c:1099-1173).

        Uses the alert color (warningColor) matching COW behavior.
        """
        lo = self.layout
        tw = lo.twinside
        color = self._alert_color(me)
        half_view = (TWINSIDE // 2) * SCALE  # 10000 game coords

        # Left edge (x=0)
        if me.render_x < half_view:
            dx, _ = self._to_screen(0, 0, me)
            _, sy = self._to_screen(0, 0, me)
            _, ey = self._to_screen(0, GWIDTH, me)
            sy = max(0, sy)
            ey = min(tw - 1, ey)
            if 0 <= dx < tw:
                pygame.draw.line(self.surface, color, (dx, sy), (dx, ey))

        # Right edge (x=GWIDTH)
        if me.render_x > GWIDTH - half_view:
            dx, _ = self._to_screen(GWIDTH, 0, me)
            _, sy = self._to_screen(GWIDTH, 0, me)
            _, ey = self._to_screen(GWIDTH, GWIDTH, me)
            sy = max(0, sy)
            ey = min(tw - 1, ey)
            if 0 <= dx < tw:
                pygame.draw.line(self.surface, color, (dx, sy), (dx, ey))

        # Top edge (y=0)
        if me.render_y < half_view:
            _, dy = self._to_screen(0, 0, me)
            sx, _ = self._to_screen(0, 0, me)
            ex, _ = self._to_screen(GWIDTH, 0, me)
            sx = max(0, sx)
            ex = min(tw - 1, ex)
            if 0 <= dy < tw:
                pygame.draw.line(self.surface, color, (sx, dy), (ex, dy))

        # Bottom edge (y=GWIDTH)
        if me.render_y > GWIDTH - half_view:
            _, dy = self._to_screen(0, GWIDTH, me)
            sx, _ = self._to_screen(0, GWIDTH, me)
            ex, _ = self._to_screen(GWIDTH, GWIDTH, me)
            sx = max(0, sx)
            ex = min(tw - 1, ex)
            if 0 <= dy < tw:
                pygame.draw.line(self.surface, color, (sx, dy), (ex, dy))

    def draw_det_circle(self):
        """Draw det range circle around own ship (always visible, thin line)."""
        me = self.gs.me
        if not me or me.status != PALIVE:
            return
        lo = self.layout
        cx = lo.twinside // 2
        cy = lo.twinside // 2
        radius = int(DETDIST / lo.game_scale)
        pygame.draw.circle(self.surface, (255, 0, 0), (cx, cy), radius, 1)

    def _draw_alert_border(self, me):
        lo = self.layout
        color = self._alert_color(me)
        pygame.draw.rect(self.surface, color,
                         (0, 0, lo.twinside, lo.twinside),
                         lo.alert_border_width)

    def draw_aim_indicator(self, target, intercept):
        """Draw auto-aim visual: line from ship to intercept + crosshair.

        target: the enemy Player being tracked
        intercept: (gx, gy) intercept point in game coords, or None
        """
        me = self.gs.me
        if not me:
            return
        lo = self.layout
        cx = lo.twinside // 2
        cy = lo.twinside // 2
        color = (0, 255, 128)
        cross_color = (255, 255, 0)

        if intercept:
            ix, iy = intercept
            sx, sy = self._to_screen(ix, iy, me)

            # Line from ship center to intercept point
            pygame.draw.line(self.surface, color, (cx, cy), (sx, sy), 1)

            # Crosshair at intercept point (if on screen)
            if self._in_view(sx, sy):
                cr = int(6 * lo.scale)
                pygame.draw.line(self.surface, cross_color,
                                 (sx - cr, sy), (sx + cr, sy))
                pygame.draw.line(self.surface, cross_color,
                                 (sx, sy - cr), (sx, sy + cr))
                pygame.draw.circle(self.surface, cross_color,
                                   (sx, sy), cr, 1)
        else:
            # No finite intercept — draw line toward target position
            tx, ty = self._to_screen(target.x, target.y, me)
            pygame.draw.line(self.surface, color, (cx, cy), (tx, ty), 1)

    def _draw_waiting(self):
        lo = self.layout
        font = self._get_waiting_font()
        text = font.render("Waiting for game...", True, (200, 200, 200))
        cx = lo.twinside // 2
        cy = lo.twinside // 2
        self.surface.blit(text, (cx - text.get_width() // 2,
                                 cy - text.get_height() // 2))
        # Queue position
        if self.gs.queue_pos > 0:
            q_text = font.render(f"Queue position: {self.gs.queue_pos}",
                                 True, (255, 255, 0))
            self.surface.blit(q_text, (cx - q_text.get_width() // 2,
                                       cy + text.get_height()))
