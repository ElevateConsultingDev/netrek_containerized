"""Tests for pure computation formulas: player ratings, army carry, max speed."""
import pytest

from netrek.gamestate import GameState, Player, ShipCap
from netrek.constants import (
    PALIVE, CRUISER, ASSAULT, STARBASE, BATTLESHIP, SCOUT,
)


# ---- Player list ratings (playerlist._compute_ratings) ----

class TestComputeRatings:
    def _compute(self, p, gs):
        from netrek.playerlist import _compute_ratings
        return _compute_ratings(p, gs)

    def _make_gs(self, **overrides):
        gs = GameState()
        gs.status_timeprod = overrides.get('timeprod', 1)
        gs.status_kills = overrides.get('kills', 1)
        gs.status_losses = overrides.get('losses', 1)
        gs.status_armsbomb = overrides.get('armsbomb', 1)
        gs.status_planets = overrides.get('planets', 1)
        return gs

    def test_zero_tticks_all_zero(self):
        gs = self._make_gs()
        p = Player(st_tticks=0)
        wins, losses, ratio, oR, dR, di = self._compute(p, gs)
        assert oR == 0.0
        assert dR == 0.0
        assert di == 0.0

    def test_starbase_uses_sb_stats(self):
        gs = self._make_gs()
        p = Player(shiptype=STARBASE, st_sbkills=10, st_sblosses=5,
                   st_kills=99, st_losses=99, st_tkills=99, st_tlosses=99,
                   st_tticks=1)
        wins, losses, ratio, oR, dR, di = self._compute(p, gs)
        assert wins == 10
        assert losses == 5
        assert ratio == 2.0

    def test_non_starbase_uses_combined_stats(self):
        gs = self._make_gs()
        p = Player(shiptype=CRUISER, st_kills=3, st_tkills=7,
                   st_losses=2, st_tlosses=8, st_tticks=1)
        wins, losses, ratio, oR, dR, di = self._compute(p, gs)
        assert wins == 10
        assert losses == 10
        assert ratio == 1.0

    def test_zero_losses_ratio_is_wins(self):
        gs = self._make_gs()
        p = Player(shiptype=CRUISER, st_kills=5, st_tkills=5,
                   st_losses=0, st_tlosses=0, st_tticks=1)
        wins, losses, ratio, oR, dR, di = self._compute(p, gs)
        assert losses == 0
        assert ratio == 10.0  # float(wins)

    def test_known_values(self):
        """Hand-computed ratings with known inputs."""
        gs = self._make_gs(timeprod=10000, kills=100, losses=50,
                           armsbomb=200, planets=80)
        p = Player(shiptype=CRUISER,
                   st_kills=5, st_tkills=20,
                   st_losses=3, st_tlosses=10,
                   st_tticks=36000,   # 1 hour
                   st_tarmsbomb=15, st_tplanets=8)
        wins, losses, ratio, oR, dR, di = self._compute(p, gs)

        # oRating = 20 * 10000 / (36000 * 100) = 200000 / 3600000 = 0.05556
        assert abs(oR - 20 * 10000 / (36000 * 100)) < 1e-6

        # dRating = 36000 * 50 / (10 * 10000) = 1800000 / 100000 = 18.0
        assert abs(dR - 36000 * 50 / (10 * 10000)) < 1e-6

        # bRating = 15 * 10000 / (36000 * 200) = 150000 / 7200000
        bR = 15 * 10000 / (36000 * 200)
        # pRating = 8 * 10000 / (36000 * 80) = 80000 / 2880000
        pR = 8 * 10000 / (36000 * 80)
        # DI = (oR + bR + pR) * (36000 / 36000) = ratings * 1.0
        expected_di = (oR + bR + pR) * 1.0
        assert abs(di - expected_di) < 1e-6

    def test_zero_tlosses_drating_fallback(self):
        """When st_tlosses=0, dRating uses tticks * losses / tp (no div-by-zero)."""
        gs = self._make_gs(timeprod=10000, kills=100, losses=50,
                           armsbomb=200, planets=80)
        p = Player(shiptype=CRUISER,
                   st_kills=5, st_tkills=20,
                   st_losses=3, st_tlosses=0,
                   st_tticks=36000,
                   st_tarmsbomb=15, st_tplanets=8)
        wins, losses, ratio, oR, dR, di = self._compute(p, gs)
        # dRating = tticks * gs.status_losses / tp = 36000 * 50 / 10000 = 180.0
        assert abs(dR - 36000 * 50 / 10000) < 1e-6


# ---- Army carry capacity (dashboard formula) ----

class TestArmyCarry:
    @staticmethod
    def carry_cap(kills_100, shiptype, max_armies):
        """Replicate the dashboard army carry formula."""
        kills = kills_100 / 100.0 if kills_100 else 0
        if shiptype == ASSAULT:
            return min(int(kills * 3), max_armies)
        elif shiptype == STARBASE:
            return max_armies
        else:
            return min(int(kills * 2), max_armies)

    def test_assault_kills_2_5(self):
        # kills=2.5 → int(2.5 * 3) = int(7.5) = 7
        assert self.carry_cap(250, ASSAULT, 20) == 7

    def test_assault_capped_at_max(self):
        # kills=10 → int(10 * 3) = 30, capped at max_armies=20
        assert self.carry_cap(1000, ASSAULT, 20) == 20

    def test_starbase_always_max(self):
        assert self.carry_cap(0, STARBASE, 25) == 25
        assert self.carry_cap(100, STARBASE, 25) == 25

    def test_cruiser_kills_1(self):
        # kills=1.0 → int(1.0 * 2) = 2
        assert self.carry_cap(100, CRUISER, 10) == 2

    def test_cruiser_capped_at_max(self):
        # kills=20 → int(20 * 2) = 40, capped at max_armies=10
        assert self.carry_cap(2000, CRUISER, 10) == 10

    def test_zero_kills_non_starbase(self):
        assert self.carry_cap(0, CRUISER, 10) == 0
        assert self.carry_cap(0, ASSAULT, 20) == 0
        assert self.carry_cap(0, BATTLESHIP, 6) == 0

    def test_battleship_kills_1_5(self):
        # kills=1.5 → int(1.5 * 2) = int(3.0) = 3
        assert self.carry_cap(150, BATTLESHIP, 6) == 3


# ---- Damage-reduced max speed (dashboard formula) ----

class TestDamageMaxSpeed:
    @staticmethod
    def cur_max_speed(max_speed, damage, max_damage):
        """Replicate the dashboard damage-reduced max speed formula."""
        if max_damage > 0:
            val = int((max_speed + 2) - (max_speed + 1) * damage / max_damage)
            return max(0, min(val, max_speed))
        return max_speed

    def test_zero_damage(self):
        # int((9 + 2) - (9 + 1) * 0 / 100) = int(11) = 11, clamped to 9
        assert self.cur_max_speed(9, 0, 100) == 9

    def test_max_damage(self):
        # int((9 + 2) - (9 + 1) * 100 / 100) = int(11 - 10) = 1
        assert self.cur_max_speed(9, 100, 100) == 1

    def test_half_damage_ca(self):
        # CA: max_speed=9, max_damage=100, damage=50
        # int((9 + 2) - (9 + 1) * 50 / 100) = int(11 - 5.0) = int(6.0) = 6
        assert self.cur_max_speed(9, 50, 100) == 6

    def test_over_damage_clamped_to_zero(self):
        # Extreme damage beyond max (theoretically shouldn't happen, but test clamp)
        # int((9 + 2) - (9 + 1) * 200 / 100) = int(11 - 20) = int(-9) = -9, clamped to 0
        assert self.cur_max_speed(9, 200, 100) == 0

    def test_zero_max_damage_returns_max_speed(self):
        assert self.cur_max_speed(9, 50, 0) == 9

    def test_scout_half_damage(self):
        # Scout: max_speed=12, max_damage=75, damage=37
        # int((12 + 2) - (12 + 1) * 37 / 75) = int(14 - 6.4133) = int(7.5867) = 7
        assert self.cur_max_speed(12, 37, 75) == 7
