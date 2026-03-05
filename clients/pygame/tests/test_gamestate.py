"""Tests for gamestate.py — GameState, dataclasses, packet handlers."""
import pytest

from netrek.gamestate import GameState, Player, Torp, Plasma, Phaser, Planet, ShipCap
from netrek.constants import (
    MAXPLAYER, MAXPLANETS, MAXTORP, NUM_TYPES, MAXPLASMA,
    PFREE, PALIVE, PEXPLODE, CRUISER, BATTLESHIP, SCOUT,
    TFREE, TMOVE, TEXPLODE, PTFREE, PHFREE,
    FED, ROM, KLI, ORI, NOBODY,
    SP_YOU, SP_PLAYER_INFO, SP_PLANET, SP_PLANET_LOC,
    SP_SHIP_CAP, SP_KILLS, SP_MESSAGE, SP_WARNING,
    SP_PSTATUS, SP_HOSTILE, SP_MASK, SP_FLAGS,
)


# ---- Dataclass defaults ----

class TestDataclassDefaults:
    def test_player_defaults(self):
        p = Player()
        assert p.pnum == 0
        assert p.status == PFREE
        assert p.team == NOBODY
        assert p.shiptype == CRUISER
        assert p.x == 0
        assert p.y == 0
        assert p.kills == 0
        assert p.name == ""

    def test_torp_defaults(self):
        t = Torp()
        assert t.status == TFREE
        assert t.x == 0
        assert t.y == 0

    def test_plasma_defaults(self):
        pl = Plasma()
        assert pl.status == PTFREE

    def test_phaser_defaults(self):
        ph = Phaser()
        assert ph.status == PHFREE

    def test_planet_defaults(self):
        pl = Planet()
        assert pl.pnum == 0
        assert pl.owner == NOBODY
        assert pl.name == ""

    def test_shipcap_defaults(self):
        sc = ShipCap()
        assert sc.s_type == 0
        assert sc.s_maxshield == 100
        assert sc.s_maxspeed == 12


# ---- GameState initialization ----

class TestGameStateInit:
    def test_player_count(self):
        gs = GameState()
        assert len(gs.players) == MAXPLAYER

    def test_player_pnums(self):
        gs = GameState()
        for i, p in enumerate(gs.players):
            assert p.pnum == i

    def test_torp_count(self):
        gs = GameState()
        assert len(gs.torps) == MAXPLAYER * MAXTORP

    def test_plasma_count(self):
        gs = GameState()
        assert len(gs.plasmas) == MAXPLAYER * MAXPLASMA

    def test_phaser_count(self):
        gs = GameState()
        assert len(gs.phasers) == MAXPLAYER

    def test_planet_count(self):
        gs = GameState()
        assert len(gs.planets) == MAXPLANETS

    def test_planet_pnums(self):
        gs = GameState()
        for i, pl in enumerate(gs.planets):
            assert pl.pnum == i

    def test_ship_caps_count(self):
        gs = GameState()
        assert len(gs.ship_caps) == NUM_TYPES

    def test_ship_caps_types(self):
        gs = GameState()
        for i, sc in enumerate(gs.ship_caps):
            assert sc.s_type == i

    def test_me_pnum_initial(self):
        gs = GameState()
        assert gs.me_pnum == -1

    def test_me_returns_none_initially(self):
        gs = GameState()
        assert gs.me is None


# ---- me property ----

class TestMeProperty:
    def test_me_with_valid_pnum(self):
        gs = GameState()
        gs.me_pnum = 5
        assert gs.me is gs.players[5]

    def test_me_with_invalid_pnum(self):
        gs = GameState()
        gs.me_pnum = -1
        assert gs.me is None

    def test_me_with_out_of_range(self):
        gs = GameState()
        gs.me_pnum = MAXPLAYER + 10
        assert gs.me is None


# ---- ship_cap property ----

class TestShipCapProperty:
    def test_ship_cap_for_cruiser(self):
        gs = GameState()
        gs.me_pnum = 0
        gs.players[0].shiptype = CRUISER
        assert gs.ship_cap is gs.ship_caps[CRUISER]

    def test_ship_cap_for_bb(self):
        gs = GameState()
        gs.me_pnum = 0
        gs.players[0].shiptype = BATTLESHIP
        assert gs.ship_cap is gs.ship_caps[BATTLESHIP]

    def test_ship_cap_fallback_no_me(self):
        gs = GameState()
        gs.me_pnum = -1
        assert gs.ship_cap is gs.ship_caps[0]


# ---- team_counts ----

class TestTeamCounts:
    def test_empty_game(self):
        gs = GameState()
        counts = gs.team_counts()
        assert counts == {FED: 0, ROM: 0, KLI: 0, ORI: 0}

    def test_one_player_per_team(self):
        gs = GameState()
        gs.players[0].status = PALIVE
        gs.players[0].team = FED
        gs.players[1].status = PALIVE
        gs.players[1].team = ROM
        counts = gs.team_counts()
        assert counts[FED] == 1
        assert counts[ROM] == 1
        assert counts[KLI] == 0

    def test_free_players_not_counted(self):
        gs = GameState()
        gs.players[0].status = PFREE
        gs.players[0].team = FED
        counts = gs.team_counts()
        assert counts[FED] == 0


# ---- SP_YOU handler ----

class TestHandleSpYou:
    def test_sets_me_pnum(self):
        gs = GameState()
        gs.handle_packet(SP_YOU, {
            "pnum": 3, "hostile": 0, "swar": 0, "armies": 0,
            "tractor": 0, "flags": 0, "damage": 50, "shield": 80,
            "fuel": 9000, "etemp": 100, "wtemp": 200,
            "whydead": 0, "whodead": 0,
        })
        assert gs.me_pnum == 3

    def test_updates_player_fields(self):
        gs = GameState()
        gs.me_pnum = 3
        gs.handle_packet(SP_YOU, {
            "pnum": 3, "hostile": 0x0F, "swar": 0x02, "armies": 5,
            "tractor": 0, "flags": 0x0001, "damage": 50, "shield": 80,
            "fuel": 9000, "etemp": 100, "wtemp": 200,
            "whydead": 0, "whodead": 0,
        })
        p = gs.players[3]
        assert p.hostile == 0x0F
        assert p.swar == 0x02
        assert p.armies == 5
        assert p.damage == 50
        assert p.shield == 80
        assert p.fuel == 9000

    def test_out_of_range_pnum_no_crash(self):
        gs = GameState()
        gs.handle_packet(SP_YOU, {
            "pnum": 999, "hostile": 0, "swar": 0, "armies": 0,
            "tractor": 0, "flags": 0, "damage": 0, "shield": 0,
            "fuel": 0, "etemp": 0, "wtemp": 0,
            "whydead": 0, "whodead": 0,
        })
        # Should not crash; me_pnum unchanged
        assert gs.me_pnum == -1

    def test_negative_pnum_no_crash(self):
        gs = GameState()
        gs.handle_packet(SP_YOU, {
            "pnum": -5, "hostile": 0, "swar": 0, "armies": 0,
            "tractor": 0, "flags": 0, "damage": 0, "shield": 0,
            "fuel": 0, "etemp": 0, "wtemp": 0,
            "whydead": 0, "whodead": 0,
        })
        assert gs.me_pnum == -1

    def test_tractor_with_active_bit(self):
        gs = GameState()
        gs.me_pnum = 0
        gs.handle_packet(SP_YOU, {
            "pnum": 0, "hostile": 0, "swar": 0, "armies": 0,
            "tractor": 0x40 | 7,  # active, target pnum 7
            "flags": 0, "damage": 0, "shield": 0,
            "fuel": 0, "etemp": 0, "wtemp": 0,
            "whydead": 0, "whodead": 0,
        })
        assert gs.players[0].tractor == 7


# ---- SP_PLAYER_INFO handler ----

class TestHandleSpPlayerInfo:
    def test_updates_shiptype(self):
        gs = GameState()
        gs.handle_packet(SP_PLAYER_INFO, {
            "pnum": 5, "shiptype": BATTLESHIP, "team": FED,
        })
        assert gs.players[5].shiptype == BATTLESHIP
        assert gs.players[5].team == FED

    def test_out_of_range_pnum(self):
        gs = GameState()
        gs.handle_packet(SP_PLAYER_INFO, {
            "pnum": MAXPLAYER + 1, "shiptype": CRUISER, "team": FED,
        })
        # No crash

    def test_nobody_team_not_overwrite_alive(self):
        """Don't overwrite team with NOBODY for alive players."""
        gs = GameState()
        gs.players[5].team = ROM
        gs.players[5].status = PALIVE
        gs.handle_packet(SP_PLAYER_INFO, {
            "pnum": 5, "shiptype": CRUISER, "team": NOBODY,
        })
        assert gs.players[5].team == ROM  # preserved


# ---- SP_PLANET handler ----

class TestHandleSpPlanet:
    def test_updates_planet(self):
        gs = GameState()
        gs.handle_packet(SP_PLANET, {
            "pnum": 10, "owner": FED, "info": 0x0F, "flags": 0x070, "armies": 12,
        })
        pl = gs.planets[10]
        assert pl.owner == FED
        assert pl.info == 0x0F
        assert pl.flags == 0x070
        assert pl.armies == 12

    def test_out_of_range_pnum(self):
        gs = GameState()
        gs.handle_packet(SP_PLANET, {
            "pnum": MAXPLANETS + 5, "owner": FED, "info": 0, "flags": 0, "armies": 0,
        })
        # No crash

    def test_planet_loc(self):
        gs = GameState()
        gs.handle_packet(SP_PLANET_LOC, {
            "pnum": 0, "x": 50000, "y": 60000, "name": "Earth",
        })
        pl = gs.planets[0]
        assert pl.x == 50000
        assert pl.y == 60000
        assert pl.name == "Earth"


# ---- SP_SHIP_CAP handler ----

class TestHandleSpShipCap:
    def test_stores_to_correct_slot(self):
        gs = GameState()
        gs.handle_packet(SP_SHIP_CAP, {
            "s_type": BATTLESHIP, "s_maxspeed": 9, "s_maxfuel": 14000,
            "s_maxshield": 130, "s_maxdamage": 130, "s_maxwpntemp": 1000,
            "s_maxegntemp": 1000, "s_maxarmies": 6, "s_phaserrange": 6300,
            "s_torpspeed": 12,
        })
        sc = gs.ship_caps[BATTLESHIP]
        assert sc.s_type == BATTLESHIP
        assert sc.s_maxshield == 130
        assert sc.s_maxspeed == 9

    def test_bb_shipcap_via_property(self):
        """When me is BB, ship_cap returns BB's entry."""
        gs = GameState()
        gs.me_pnum = 0
        gs.players[0].shiptype = BATTLESHIP
        gs.handle_packet(SP_SHIP_CAP, {
            "s_type": BATTLESHIP, "s_maxspeed": 9, "s_maxfuel": 14000,
            "s_maxshield": 130, "s_maxdamage": 130, "s_maxwpntemp": 1000,
            "s_maxegntemp": 1000, "s_maxarmies": 6, "s_phaserrange": 6300,
            "s_torpspeed": 12,
        })
        assert gs.ship_cap.s_maxshield == 130

    def test_out_of_range_stype(self):
        gs = GameState()
        gs.handle_packet(SP_SHIP_CAP, {
            "s_type": NUM_TYPES + 5, "s_maxspeed": 9, "s_maxfuel": 14000,
            "s_maxshield": 130, "s_maxdamage": 130, "s_maxwpntemp": 1000,
            "s_maxegntemp": 1000, "s_maxarmies": 6, "s_phaserrange": 6300,
            "s_torpspeed": 12,
        })
        # No crash, no change


# ---- SP_KILLS handler ----

class TestHandleSpKills:
    def test_updates_kills(self):
        gs = GameState()
        gs.handle_packet(SP_KILLS, {"pnum": 2, "kills": 350})
        assert gs.players[2].kills == 350


# ---- SP_PSTATUS handler ----

class TestHandleSpPstatus:
    def test_updates_status(self):
        gs = GameState()
        gs.handle_packet(SP_PSTATUS, {"pnum": 0, "status": PALIVE})
        assert gs.players[0].status == PALIVE


# ---- SP_HOSTILE handler ----

class TestHandleSpHostile:
    def test_updates_war_hostile(self):
        gs = GameState()
        gs.handle_packet(SP_HOSTILE, {"pnum": 1, "war": 0x06, "hostile": 0x0E})
        assert gs.players[1].war == 0x06
        assert gs.players[1].hostile == 0x0E


# ---- SP_MASK handler ----

class TestHandleSpMask:
    def test_updates_team_mask(self):
        gs = GameState()
        gs.handle_packet(SP_MASK, {"mask": 0x0F})
        assert gs.team_mask == 0x0F


# ---- SP_FLAGS handler ----

class TestHandleSpFlags:
    def test_updates_flags(self):
        gs = GameState()
        gs.handle_packet(SP_FLAGS, {"pnum": 0, "tractor": 0, "flags": 0x0001})
        assert gs.players[0].flags == 0x0001


# ---- SP_MESSAGE handler ----

class TestHandleSpMessage:
    def test_appends_message(self):
        gs = GameState()
        gs.handle_packet(SP_MESSAGE, {"mesg": "hello", "m_flags": 0x08, "m_from": 0})
        assert len(gs.messages) == 1
        assert gs.messages[0] == ("hello", 0x08, 0)

    def test_message_cap(self):
        gs = GameState()
        for i in range(25):
            gs.handle_packet(SP_MESSAGE, {"mesg": f"msg{i}", "m_flags": 0, "m_from": 0})
        assert len(gs.messages) == 20


# ---- SP_WARNING handler ----

class TestHandleSpWarning:
    def test_sets_warning(self):
        gs = GameState()
        gs.handle_packet(SP_WARNING, {"mesg": "Watch out!"})
        assert gs.warning == "Watch out!"
        assert gs.warning_timer == 90


# ---- Unknown packet type ----

class TestUnknownPacket:
    def test_unknown_type_no_crash(self):
        gs = GameState()
        gs.handle_packet(9999, {"foo": "bar"})
        # No crash
