"""Player list table matching COW's playerw layout.

COW default plistStyle=1 (Old Style) layout: "nTRNKWLr O D d "
  n=Ship Number (3w), T=Ship Type (3w), R=Rank (11w), N=Name (17w),
  K=Kills (6w), W=Wins (6w), L=Losses (6w), r=Ratio (6w),
  ' '=space, O=Offense (6w), D=Defense (6w), d=DI (8w)

Header: " No Ty Rank       Name              Kills  Wins  Loss Ratio  Offse Defse      DI"
Players colored by team.

Rating formulas from COW defs.h:205-212:
  offenseRating = st_tkills * timeprod / (st_tticks * kills)
  defenseRating = st_tticks * losses / (st_tlosses * timeprod)
  bombingRating = st_tarmsbomb * timeprod / (st_tticks * armsbomb)
  planetRating  = st_tplanets * timeprod / (st_tticks * planets)
  Ratings = offense + planet + bombing
  DI = Ratings * (st_tticks / 36000)
"""
import pygame
from .constants import *

PLAYERLIST_HEIGHT = 200

# Rank names from COW (index 0-8)
RANK_NAMES = [
    "Ensign", "Lieutn", "Lt.Cmdr", "Commndr",
    "Captain", "Fl.Capt", "Commodre", "Rear Adm",
    "Admiral",
]


def _compute_ratings(p, gs):
    """Compute COW player ratings.  Returns (wins, losses, ratio, oRating, dRating, di)."""
    if p.shiptype == STARBASE:
        wins = p.st_sbkills
        losses = p.st_sblosses
    else:
        wins = p.st_kills + p.st_tkills
        losses = p.st_losses + p.st_tlosses

    if losses == 0:
        ratio = float(wins)
    else:
        ratio = wins / losses

    tticks = p.st_tticks
    tp = gs.status_timeprod

    if tticks == 0:
        return (wins, losses, ratio, 0.0, 0.0, 0.0)

    oRating = p.st_tkills * tp / (tticks * gs.status_kills)
    bRating = p.st_tarmsbomb * tp / (tticks * gs.status_armsbomb)
    pRating = p.st_tplanets * tp / (tticks * gs.status_planets)

    if p.st_tlosses > 0:
        dRating = tticks * gs.status_losses / (p.st_tlosses * tp)
    else:
        dRating = tticks * gs.status_losses / tp

    ratings = oRating + pRating + bRating
    hours = tticks / 36000.0
    di = ratings * hours

    return (wins, losses, ratio, oRating, dRating, di)


class PlayerList:
    def __init__(self, surface, gamestate, layout=None):
        self.surface = surface
        self.gs = gamestate
        self.layout = layout
        self.font = None
        self._font_size = 0

    def _init_font(self):
        size = self.layout.font_small
        if self.font is None or self._font_size != size:
            self.font = pygame.font.SysFont("monospace", size)
            self._font_size = size

    def render(self, sort_mode=0):
        self._init_font()
        self.surface.fill((0, 0, 0))

        lo = self.layout

        # Top border
        pygame.draw.line(self.surface, (100, 100, 100),
                         (0, 0), (self.surface.get_width(), 0))

        # Header
        header = " No Ty Rank       Name              Kills  Wins  Loss Ratio  Offse Defse      DI"
        self.surface.blit(
            self.font.render(header, True, (200, 200, 200)),
            (lo.plist_x, lo.plist_x))

        y = lo.row_height + lo.plist_x
        me = self.gs.me

        players = [p for p in self.gs.players if p.status != PFREE]
        if sort_mode == 1:
            players.sort(key=lambda p: (p.team, p.pnum))
        elif sort_mode == 2:
            players.sort(key=lambda p: -p.kills)
        elif sort_mode == 3:
            players.sort(key=lambda p: p.name.lower())

        for p in players:
            if y + lo.row_height > self.surface.get_height():
                break

            team_l = TEAM_LETTERS.get(p.team, " ")
            pnum_hex = f"{p.pnum % 16:x}"
            if p.status == PALIVE:
                slot = f"{team_l}{pnum_hex}"
            else:
                slot = f" {pnum_hex}"

            if p.status == PALIVE:
                ship = SHIP_NAMES.get(p.shiptype, "??")
            else:
                ship = "  "

            rank = RANK_NAMES[p.rank] if 0 <= p.rank < len(RANK_NAMES) else "Ensign"
            name = p.name if p.name else ""
            kills_val = p.kills / 100.0 if p.kills else 0
            if kills_val > 0:
                kills_str = f"{kills_val:5.2f}"
            else:
                kills_str = "     "

            wins, losses, ratio, oRating, dRating, di = _compute_ratings(p, self.gs)

            line = (f" {slot} {ship} {rank:10s} {name:16s} {kills_str}"
                    f"{wins:6d}{losses:6d}{ratio:6.2f}"
                    f"  {oRating:5.2f}{dRating:6.2f}{di:10.2f}")

            color = TEAM_COLORS.get(p.team, (170, 170, 170))
            if me and p.pnum == self.gs.me_pnum:
                color = (255, 255, 255)

            self.surface.blit(
                self.font.render(line, True, color), (lo.plist_x, y))
            y += lo.row_height
