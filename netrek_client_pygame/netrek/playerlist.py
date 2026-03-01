"""Player list table matching COW's playerw layout.

COW default plistStyle=1 (Old Style) layout: "nTRNKWLr O D d "
  n=Ship Number (3w), T=Ship Type (3w), R=Rank (11w), N=Name (17w),
  K=Kills (6w), W=Wins (6w), L=Losses (6w), r=Ratio (6w),
  ' '=space, O=Offense (6w), D=Defense (6w), d=DI (8w)

Header: " No Ty Rank       Name              Kills  Wins  Loss Ratio  Offse Defse      DI"
Players colored by team.
"""
import pygame
from .constants import *

PLAYERLIST_HEIGHT = 200
ROW_HEIGHT = 14

# Rank names from COW (index 0-8)
RANK_NAMES = [
    "Ensign", "Lieutn", "Lt.Cmdr", "Commndr",
    "Captain", "Fl.Capt", "Commodre", "Rear Adm",
    "Admiral",
]


class PlayerList:
    def __init__(self, surface, gamestate):
        self.surface = surface
        self.gs = gamestate
        self.font = None

    def _init_font(self):
        if self.font is None:
            self.font = pygame.font.SysFont("monospace", 11)

    def render(self):
        self._init_font()
        self.surface.fill((0, 0, 0))

        # Top border
        pygame.draw.line(self.surface, (100, 100, 100),
                         (0, 0), (self.surface.get_width(), 0))

        # Header matching COW plistStyle=1: "nTRNKWLr O D d "
        header = " No Ty Rank       Name              Kills  Wins  Loss Ratio  Offse Defse      DI"
        self.surface.blit(
            self.font.render(header, True, (200, 200, 200)), (2, 2))

        y = ROW_HEIGHT + 2
        me = self.gs.me

        for p in self.gs.players:
            if p.status == PFREE:
                continue
            if y + ROW_HEIGHT > self.surface.get_height():
                break

            # n: Ship Number - team letter + hex number (alive) or space + number (dead)
            team_l = TEAM_LETTERS.get(p.team, " ")
            pnum_hex = f"{p.pnum % 16:x}"
            if p.status == PALIVE:
                slot = f"{team_l}{pnum_hex}"
            else:
                slot = f" {pnum_hex}"

            # T: Ship Type
            if p.status == PALIVE:
                ship = SHIP_NAMES.get(p.shiptype, "??")
            else:
                ship = "  "

            # R: Rank (10 chars)
            rank = RANK_NAMES[p.rank] if 0 <= p.rank < len(RANK_NAMES) else "Ensign"

            # N: Name (16 chars)
            name = p.name if p.name else ""

            # K: Kills (5 chars, blank if zero)
            kills_val = p.kills / 100.0 if p.kills else 0
            if kills_val > 0:
                kills_str = f"{kills_val:5.2f}"
            else:
                kills_str = "     "

            # W/L/r/O/D/d: We get these from SP_STATS but server may not
            # send full stats for all players. Show zeros for now.
            # COW format: Wins(5), Loss(5), Ratio(5), Offse(5), Defse(5), DI(7)
            line = f" {slot} {ship} {rank:10s} {name:16s} {kills_str}     0     0  0.00   0.00  6.00     0.00"

            color = TEAM_COLORS.get(p.team, (170, 170, 170))
            # Own player in white (COW highlights own line)
            if me and p.pnum == self.gs.me_pnum:
                color = (255, 255, 255)

            self.surface.blit(
                self.font.render(line, True, color), (2, y))
            y += ROW_HEIGHT
