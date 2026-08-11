"""Dashboard HUD panel matching COW's tstatw layout (dashboard.c db_redraw_COW).

COW dashboard is TWINSIDE x STATSIZE pixels below the tactical window.
Three bar columns computed from font character width (COW W_Textwidth=6):
  Flags | Sp/Sh/Da (3-digit) | Ar/Wt/Et (3-digit) | Fu (5-digit) + Kills/Torps
"""
import pygame
from .constants import *

DASH_HEIGHT = 46
_frame_counter = 0  # module-level frame counter for flashing


class Dashboard:
    def __init__(self, surface, gamestate, layout=None):
        self.surface = surface
        self.gs = gamestate
        self.layout = layout
        self.font = None
        self.bold_font = None
        self._font_size = 0
        self._cw = 6  # char width, updated when font changes

    def _init_fonts(self):
        size = self.layout.font_small
        if self.font is None or self._font_size != size:
            self.font = pygame.font.SysFont("monospace", size)
            self.bold_font = pygame.font.SysFont("monospace", size, bold=True)
            self._font_size = size
            self._cw = self.font.size("A")[0]

    def render(self):
        global _frame_counter
        _frame_counter += 1
        self._init_fonts()
        self.surface.fill((0, 0, 0))
        me = self.gs.me
        if not me:
            return

        lo = self.layout
        sc = self.gs.ship_cap
        cw = self._cw
        bar_len = lo.bar_length

        # Column positions computed from font metrics (matches COW W_Textwidth math)
        # COW: flags=12 chars, gap, then 3 columns of bars
        flags_w = 14 * cw
        gap = cw  # small gap between columns
        col3_w = 11 * cw + bar_len  # DB_3DIGITS
        col5_w = 15 * cw + bar_len  # DB_5DIGITS

        # Lay out right-to-left from TWINSIDE to avoid overflow
        tw = self.surface.get_width()
        col3_x = tw - col5_w - gap       # Fu column (5-digit)
        col2_x = col3_x - col3_w - gap   # Ar/Wt/Et column (3-digit)
        col1_x = col2_x - col3_w - gap   # Sp/Sh/Da column (3-digit)
        flags_x = max(0, int(2 * lo.scale))

        row_h = lo.dash_row_spacing
        y0 = int(3 * lo.scale)
        y1 = y0 + row_h
        y2 = y1 + row_h

        max_speed = sc.s_maxspeed or 12
        max_shield = sc.s_maxshield or 100
        max_damage = sc.s_maxdamage or 100
        max_fuel = sc.s_maxfuel or 10000
        max_wtemp = sc.s_maxwpntemp or 1000
        max_etemp = sc.s_maxegntemp or 1000
        max_armies = sc.s_maxarmies or 1
        kills = me.kills / 100.0 if me.kills else 0

        # --- Flags ---
        self._draw_flags(me, flags_x, y0, row_h)

        # --- Column 1: Sp, Sh, Da (DB_3DIGITS) ---
        if max_damage > 0:
            cur_max_spd = int((max_speed + 2) - (max_speed + 1) * me.damage / max_damage)
            cur_max_spd = max(0, min(cur_max_spd, max_speed))
        else:
            cur_max_spd = max_speed
        spd_color = (255, 255, 0) if me.speed >= max_speed - 2 else (200, 200, 200)
        self._draw_bar3(col1_x, y0, "Sp", me.speed, cur_max_spd, max_speed, spd_color)

        if max_shield > 0:
            spct = 100 * me.shield // max_shield
        else:
            spct = 100
        sh_color = (255, 0, 0) if spct <= 50 else (255, 255, 0) if spct < 90 else (200, 200, 200)
        self._draw_bar3(col1_x, y1, "Sh", max_shield - me.shield, max_shield, max_shield, sh_color)

        if max_damage > 0:
            dpct = 100 * me.damage // max_damage
        else:
            dpct = 0
        da_color = (255, 0, 0) if dpct > 50 else (255, 255, 0) if dpct > 10 else (200, 200, 200)
        # Flash damage label when critical (COW dashboard.c)
        if dpct >= 90 and (_frame_counter % 10) < 5:
            da_color = (255, 255, 255)
        self._draw_bar3(col1_x, y2, "Da", me.damage, max_damage, max_damage, da_color)

        # --- Column 2: Ar, Wt, Et (DB_3DIGITS, temps /10) ---
        if me.shiptype == ASSAULT:
            carry_cap = min(int(kills * 3), max_armies)
        elif me.shiptype == STARBASE:
            carry_cap = max_armies
        else:
            carry_cap = min(int(kills * 2), max_armies)
        ar_color = (200, 200, 200) if me.armies <= 3 else (255, 0, 0) if me.armies > 5 else (255, 255, 0)
        self._draw_bar3(col2_x, y0, "Ar", me.armies, carry_cap, max_armies, ar_color)

        wt_d, wt_m = me.wtemp // 10, max_wtemp // 10
        wpct = 100 * me.wtemp // max_wtemp if max_wtemp > 0 else 0
        wt_color = (255, 0, 0) if wpct > 50 else (200, 200, 200) if wpct <= 20 else (255, 255, 0)
        if me.flags & PFWEP and (_frame_counter % 10) < 5:
            wt_color = (255, 255, 255)
        self._draw_bar3(col2_x, y1, "Wt", wt_d, wt_m, wt_m, wt_color)

        et_d, et_m = me.etemp // 10, max_etemp // 10
        epct = 100 * me.etemp // max_etemp if max_etemp > 0 else 0
        et_color = (255, 0, 0) if epct >= 75 else (200, 200, 200) if epct <= 25 else (255, 255, 0)
        if me.flags & PFENG and (_frame_counter % 10) < 5:
            et_color = (255, 255, 255)
        self._draw_bar3(col2_x, y2, "Et", et_d, et_m, et_m, et_color)

        # --- Column 3: Fu (DB_5DIGITS), Kills, Torps ---
        fpct = 100 * me.fuel // max_fuel if max_fuel > 0 else 100
        fu_color = (255, 0, 0) if fpct <= 50 else (200, 200, 200) if fpct > 90 else (255, 255, 0)
        # Flash fuel label when critically low (COW dashboard.c)
        if fpct <= 10 and (_frame_counter % 10) < 5:
            fu_color = (255, 255, 255)
        self._draw_bar5(col3_x, y0, "Fu", me.fuel, max_fuel, max_fuel, fu_color)

        if kills > 0:
            self.surface.blit(
                self.font.render(f"Kills: {kills:.2f}", True, (200, 200, 200)),
                (col3_x, y1))

        torp_count = 0
        if self.gs.me_pnum is not None:
            base = self.gs.me_pnum * MAXTORP
            for i in range(MAXTORP):
                if self.gs.torps[base + i].status in (TMOVE, TSTRAIGHT):
                    torp_count += 1
        if torp_count > 0:
            self.surface.blit(
                self.font.render(f"Torps: {torp_count}", True, (200, 200, 200)),
                (col3_x, y2))

        # Tournament mode + ping stats (third row of flags area)
        info_parts = []
        # Observer mode indicator
        if me.flags & PFOBSERV:
            info_parts.append(("OBS", (255, 255, 0)))
        if self.gs.status_tourn:
            info_parts.append(("T", (255, 0, 0)))
        ping_lag = self.gs.ping_lag
        if ping_lag > 0:
            ping_color = (255, 0, 0) if ping_lag > 500 else (255, 255, 0) if ping_lag > 200 else (0, 200, 0)
            info_parts.append((f" {ping_lag}ms", ping_color))
            loss = self.gs.ping_tloss_sc
            if loss > 0:
                info_parts.append((f" {loss}%", ping_color))
        if info_parts:
            ix = flags_x
            for txt, col in info_parts:
                lbl = self.font.render(txt, True, col)
                self.surface.blit(lbl, (ix, y2))
                ix += lbl.get_width()

        # Top border
        pygame.draw.line(self.surface, (100, 100, 100),
                         (0, 0), (self.surface.get_width(), 0))

    def _draw_flags(self, me, x, y, row_h):
        self.surface.blit(
            self.font.render("Flags", True, (200, 200, 200)), (x, y))

        buf = ""
        buf += "S" if me.flags & PFSHIELD else " "
        if me.flags & PFGREEN:
            buf += "G"
        elif me.flags & PFYELLOW:
            buf += "Y"
        else:
            buf += "R"
        buf += "L" if me.flags & (PFPLLOCK | PFPLOCK) else " "
        buf += "R" if me.flags & PFREPAIR else " "
        buf += "B" if me.flags & PFBOMB else " "
        buf += "O" if me.flags & PFORBIT else " "
        buf += "D" if me.flags & PFDOCK else " "
        buf += "C" if me.flags & PFCLOAK else " "
        buf += "W" if me.flags & PFWEP else " "
        buf += "E" if me.flags & PFENG else " "
        if me.flags & PFPRESS:
            buf += "P"
        elif me.flags & PFTRACT:
            buf += "T"
        else:
            buf += " "
        if me.flags & PFBEAMUP:
            buf += "u"
        elif me.flags & PFBEAMDOWN:
            buf += "d"
        else:
            buf += " "
        self.surface.blit(
            self.font.render(buf, True, (200, 200, 200)), (x, y + row_h))

    def _draw_bar3(self, x, y, label, value, tmpmax, maxval, color):
        """DB_3DIGITS: La[VVV/MMM]|bar| — 11 chars."""
        self._draw_bar(x, y, f"{label}[{value:3d}/{tmpmax:3d}]",
                       value, tmpmax, maxval, color)

    def _draw_bar5(self, x, y, label, value, tmpmax, maxval, color):
        """DB_5DIGITS: La[VVVVV/MMMMM]|bar| — 15 chars."""
        self._draw_bar(x, y, f"{label}[{value:5d}/{tmpmax:5d}]",
                       value, tmpmax, maxval, color)

    def _draw_bar(self, x, y, txt, value, tmpmax, maxval, color):
        lo = self.layout
        cw = self._cw
        tc = len(txt)

        # COW rendering: label(3 chars regular) + middle(bold) + tail(regular)
        mid_end = tc // 2 + 1
        self.surface.blit(
            self.font.render(txt[:3], True, (200, 200, 200)), (x, y))
        self.surface.blit(
            self.bold_font.render(txt[3:mid_end], True, (200, 200, 200)),
            (x + cw * 3, y))
        self.surface.blit(
            self.font.render(txt[mid_end:], True, (200, 200, 200)),
            (x + cw * mid_end, y))

        bar_x = x + cw * tc
        bar_h = lo.bar_h
        bar_len = lo.bar_length

        pygame.draw.rect(self.surface, (200, 200, 200),
                         (bar_x, y + 1, bar_len, bar_h), 1)

        if maxval > 0:
            fill_w = max(0, min(int(bar_len * value / maxval), bar_len))
            if fill_w > 0:
                pygame.draw.rect(self.surface, color,
                                 (bar_x + 1, y + 2, fill_w - 1, bar_h - 2))
            if tmpmax < maxval:
                tick_x = max(0, min(int(bar_len * tmpmax / maxval), bar_len))
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (bar_x + tick_x, y + 1),
                                 (bar_x + tick_x, y + bar_h))
