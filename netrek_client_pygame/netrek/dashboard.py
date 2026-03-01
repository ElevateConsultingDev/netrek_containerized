"""Dashboard HUD panel matching COW's tstatw layout.

COW dashboard is TWINSIDE x STATSIZE (500x43) pixels below the tactical window.
Layout (from dashboard.c):
  Left section:   Flags display (status letters)
  Middle section: Sp/Sh/Da/Fu/Wt/Et bars with label[value/max]████
  Right section:  Kills count, Torps count
"""
import pygame
from .constants import *

DASH_HEIGHT = 46
BAR_LENGTH = 56


class Dashboard:
    def __init__(self, surface, gamestate):
        self.surface = surface
        self.gs = gamestate
        self.font = None
        self.bold_font = None

    def _init_fonts(self):
        if self.font is None:
            self.font = pygame.font.SysFont("monospace", 11)
            self.bold_font = pygame.font.SysFont("monospace", 11, bold=True)

    def render(self):
        self._init_fonts()
        self.surface.fill((0, 0, 0))
        me = self.gs.me
        if not me:
            return

        self._draw_flags(me, 2, 3)
        self._draw_bars(me, 90)
        self._draw_kills(me, 346)

        # Top border
        pygame.draw.line(self.surface, (100, 100, 100),
                         (0, 0), (self.surface.get_width(), 0))

    def _draw_flags(self, me, x, y):
        flags_line1 = "Flags"
        self.font.render(flags_line1, True, (200, 200, 200))
        self.surface.blit(
            self.font.render(flags_line1, True, (200, 200, 200)), (x, y))

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
            self.font.render(buf, True, (200, 200, 200)), (x, y + 14))

    def _draw_bar(self, label, x, y, value, tmpmax, maxval, color):
        """Draw a COW-style status bar: La[value/max]████"""
        # Format: "La[  123/  456]"
        txt = f"{label}[{value:5d}/{tmpmax:5d}]"
        # Label in normal color
        self.surface.blit(
            self.font.render(txt[:2], True, (200, 200, 200)), (x, y))
        # Numbers in bold
        self.surface.blit(
            self.bold_font.render(txt[2:], True, (200, 200, 200)),
            (x + self.font.size("La")[0], y))

        # Bar background (outline)
        tw = self.font.size(txt)[0] + 4
        bar_x = x + tw
        bar_h = 9
        pygame.draw.rect(self.surface, (200, 200, 200),
                         (bar_x, y + 1, BAR_LENGTH, bar_h), 1)

        # Filled portion
        if maxval > 0:
            fill_w = int(BAR_LENGTH * value / maxval)
            fill_w = max(0, min(fill_w, BAR_LENGTH))
            if fill_w > 0:
                pygame.draw.rect(self.surface, color,
                                 (bar_x + 1, y + 2, fill_w - 1, bar_h - 2))

            # Tmpmax line (current effective max)
            if tmpmax < maxval:
                line_x = int(BAR_LENGTH * tmpmax / maxval)
                line_x = max(0, min(line_x, BAR_LENGTH))
                pygame.draw.line(self.surface, (200, 200, 200),
                                 (bar_x + line_x, y + 1),
                                 (bar_x + line_x, y + bar_h))

    def _draw_bars(self, me, x):
        # Ship max values from SP_SHIP_CAP (with fallback defaults)
        sc = self.gs.ship_cap
        max_speed = sc.s_maxspeed or 12
        max_shield = sc.s_maxshield or 100
        max_damage = sc.s_maxdamage or 100
        max_fuel = sc.s_maxfuel or 10000
        max_wtemp = sc.s_maxwpntemp or 1000
        max_etemp = sc.s_maxegntemp or 1000

        # Speed bar
        spd_color = (255, 0, 0) if me.speed >= max_speed - 2 else (0, 255, 0)
        self._draw_bar("Sp", x, 3, me.speed, max_speed, max_speed, spd_color)

        # Shield bar
        if max_shield > 0:
            pct = 100 * me.shield // max_shield
        else:
            pct = 100
        sh_color = (255, 0, 0) if pct <= 16 else (255, 255, 0) if pct <= 66 else (0, 255, 0)
        self._draw_bar("Sh", x, 17, me.shield, max_shield, max_shield, sh_color)

        # Damage bar
        if max_damage > 0:
            hull_pct = 100 * (max_damage - me.damage) // max_damage
        else:
            hull_pct = 100
        da_color = (255, 0, 0) if hull_pct <= 16 else (255, 255, 0) if hull_pct <= 66 else (0, 255, 0)
        self._draw_bar("Da", x, 31, me.damage, max_damage, max_damage, da_color)

        # Fuel bar
        x2 = x + 230
        if max_fuel > 0:
            fpct = 100 * me.fuel // max_fuel
        else:
            fpct = 100
        fu_color = (255, 0, 0) if fpct <= 16 else (255, 255, 0) if fpct <= 66 else (0, 255, 0)
        self._draw_bar("Fu", x2, 3, me.fuel, max_fuel, max_fuel, fu_color)

        # Weapon temp
        if max_wtemp > 0:
            wpct = 100 * me.wtemp // max_wtemp
        else:
            wpct = 0
        wt_color = (255, 0, 0) if wpct >= 80 else (255, 255, 0) if wpct >= 50 else (0, 255, 0)
        self._draw_bar("Wt", x2, 17, me.wtemp, max_wtemp, max_wtemp, wt_color)

        # Engine temp
        if max_etemp > 0:
            epct = 100 * me.etemp // max_etemp
        else:
            epct = 0
        et_color = (255, 0, 0) if epct >= 80 else (255, 255, 0) if epct >= 50 else (0, 255, 0)
        self._draw_bar("Et", x2, 31, me.etemp, max_etemp, max_etemp, et_color)

    def _draw_kills(self, me, x):
        kills = me.kills / 100.0 if me.kills else 0
        if kills > 0:
            txt = f"Kills: {kills:.2f}"
            self.surface.blit(
                self.font.render(txt, True, (200, 200, 200)), (x, 17))

        armies = me.armies
        if armies > 0:
            txt = f"Armies: {armies}"
            self.surface.blit(
                self.font.render(txt, True, (200, 200, 200)), (x, 31))
