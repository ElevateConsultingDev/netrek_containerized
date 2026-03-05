"""Tests for ctrl distress bindings, galaxy edge visibility, det circle."""
import pytest

from netrek.config import DEFAULT_KEYMAP, Config
from netrek.constants import (
    TWINSIDE, SCALE, GWIDTH, DETDIST,
    PALIVE, CRUISER, CP_DET_TORPS,
)
from netrek.distress import DIST_TEMPLATES
from netrek.gamestate import GameState


# ---- Ctrl distress bindings (config.py keymap) ----

class TestCtrlDistressBindings:
    """Verify all Ctrl+letter and Ctrl+digit distress bindings."""

    # Ctrl+letter bindings: Ctrl+X = chr(X - A + 1)
    CTRL_LETTER_BINDINGS = {
        '\x14': 'distress_take',        # Ctrl+T
        '\x0f': 'distress_ogg',         # Ctrl+O
        '\x02': 'distress_bomb',        # Ctrl+B
        '\x03': 'distress_control',     # Ctrl+C
        '\x05': 'distress_escorting',   # Ctrl+E
        '\x10': 'distress_ogging',      # Ctrl+P
        '\x0c': 'distress_controlling', # Ctrl+L
        '\x06': 'distress_free_beer',   # Ctrl+F
        '\x0e': 'distress_no_gas',      # Ctrl+N
        '\x08': 'distress_crippled',    # Ctrl+H
    }

    def test_ctrl_t_is_distress_take(self):
        assert DEFAULT_KEYMAP['\x14'] == 'distress_take'

    def test_all_ctrl_letter_bindings_present(self):
        for ch, action in self.CTRL_LETTER_BINDINGS.items():
            assert ch in DEFAULT_KEYMAP, f"Missing binding for {action}"
            assert DEFAULT_KEYMAP[ch] == action

    def test_ctrl_letter_count(self):
        """Exactly 10 Ctrl+letter distress bindings."""
        assert len(self.CTRL_LETTER_BINDINGS) == 10

    # Ctrl+digit bindings: Ctrl+K maps to chr(0x80 + K_digit - K_0)
    # Since K_0..K_9 are consecutive, this is chr(0x80 + digit)
    CTRL_DIGIT_BINDINGS = {
        '\x80': 'distress_pop',          # Ctrl+0
        '\x81': 'distress_save_planet',  # Ctrl+1
        '\x82': 'distress_base_ogg',     # Ctrl+2
        '\x83': 'distress_help1',        # Ctrl+3
        '\x84': 'distress_help2',        # Ctrl+4
        '\x85': 'distress_asw',          # Ctrl+5
        '\x86': 'distress_asbomb',       # Ctrl+6
        '\x87': 'distress_doing1',       # Ctrl+7
        '\x88': 'distress_doing2',       # Ctrl+8
        '\x89': 'distress_pickup',       # Ctrl+9
    }

    def test_ctrl_0_is_distress_pop(self):
        assert DEFAULT_KEYMAP['\x80'] == 'distress_pop'

    def test_all_ctrl_digit_bindings_present(self):
        for ch, action in self.CTRL_DIGIT_BINDINGS.items():
            assert ch in DEFAULT_KEYMAP, f"Missing binding for {action}"
            assert DEFAULT_KEYMAP[ch] == action

    def test_ctrl_digit_count(self):
        """Exactly 10 Ctrl+digit distress bindings."""
        assert len(self.CTRL_DIGIT_BINDINGS) == 10

    def test_ctrl_digit_encoding_math(self):
        """Verify chr(0x80 + K_digit - K_0) produces the expected character.

        In the input handler, Ctrl+0..9 synthesize chr(0x80 + event.key - K_0).
        Since K_0..K_9 are consecutive, Ctrl+0 -> chr(0x80), Ctrl+5 -> chr(0x85).
        """
        import pygame
        for digit in range(10):
            expected_char = chr(0x80 + digit)
            # Simulate: event.key = K_0 + digit
            synthesized = chr(0x80 + (pygame.K_0 + digit) - pygame.K_0)
            assert synthesized == expected_char

    def test_all_distress_actions_have_templates(self):
        """Every distress_* action in the keymap must have a DIST_TEMPLATES entry."""
        for ch, action in DEFAULT_KEYMAP.items():
            if action.startswith('distress_'):
                dist_type = action[9:]  # strip "distress_" prefix
                assert dist_type in DIST_TEMPLATES, \
                    f"Action '{action}' has no DIST_TEMPLATES['{dist_type}']"


# ---- Galaxy edge visibility (tactical._draw_galaxy_edges logic) ----

class TestGalaxyEdgeVisibility:
    """Test the pure-math proximity conditions for galaxy edge drawing."""

    HALF_VIEW = (TWINSIDE // 2) * SCALE  # 250 * 40 = 10000

    def test_half_view_value(self):
        assert self.HALF_VIEW == 10000

    def test_center_no_edges(self):
        """At map center (50000, 50000), no edges are visible."""
        rx, ry = 50000, 50000
        assert not (rx < self.HALF_VIEW)          # left
        assert not (rx > GWIDTH - self.HALF_VIEW)  # right
        assert not (ry < self.HALF_VIEW)           # top
        assert not (ry > GWIDTH - self.HALF_VIEW)  # bottom

    def test_corner_left_top_visible(self):
        """Near top-left corner (5000, 5000), left and top edges visible."""
        rx, ry = 5000, 5000
        assert rx < self.HALF_VIEW        # left edge visible
        assert ry < self.HALF_VIEW        # top edge visible
        assert not (rx > GWIDTH - self.HALF_VIEW)  # right not visible
        assert not (ry > GWIDTH - self.HALF_VIEW)  # bottom not visible

    def test_right_edge_visible(self):
        """Near right edge (95000, 50000), only right edge visible."""
        rx, ry = 95000, 50000
        assert not (rx < self.HALF_VIEW)
        assert rx > GWIDTH - self.HALF_VIEW
        assert not (ry < self.HALF_VIEW)
        assert not (ry > GWIDTH - self.HALF_VIEW)

    def test_bottom_right_corner(self):
        """Near bottom-right (98000, 97000), right and bottom edges visible."""
        rx, ry = 98000, 97000
        assert rx > GWIDTH - self.HALF_VIEW
        assert ry > GWIDTH - self.HALF_VIEW

    def test_exactly_at_threshold(self):
        """At exactly half_view, the edge is NOT visible (condition is strict <)."""
        rx = self.HALF_VIEW  # 10000
        assert not (rx < self.HALF_VIEW)

    def test_one_below_threshold(self):
        rx = self.HALF_VIEW - 1  # 9999
        assert rx < self.HALF_VIEW

    def test_to_screen_math(self):
        """Verify _to_screen pixel position calculation.

        _to_screen(ox, oy, me):
          sx = int((ox - me.render_x) / game_scale) + twinside // 2
        At scale=1.0, game_scale=40, twinside=500.
        """
        from netrek.layout import Layout
        lo = Layout(scale=1.0)
        # Simulate: me at render_x=5000, converting game x=0 to screen
        render_x = 5000
        ox = 0
        sx = int((ox - render_x) / lo.game_scale) + lo.twinside // 2
        # = int((0 - 5000) / 40) + 250 = int(-125) + 250 = 125
        assert sx == 125


# ---- Det circle (input_handler.det_circle state) ----

class MockConn:
    def __init__(self):
        self.sent = []

    def send(self, data):
        self.sent.append(data)


class MockStateMachine:
    def __init__(self):
        from netrek.statemachine import State
        self.state = State.ALIVE


def make_handler():
    from netrek.input_handler import InputHandler
    conn = MockConn()
    gs = GameState()
    gs.me_pnum = 0
    gs.players[0].status = PALIVE
    gs.players[0].shiptype = CRUISER
    sm = MockStateMachine()
    cfg = Config()
    ih = InputHandler(conn, gs, sm, config=cfg)
    return ih, conn, gs


SI = (1.0, 0, 0)


class TestDetCircle:
    def test_det_circle_starts_at_zero(self):
        ih, conn, gs = make_handler()
        assert ih.det_circle == 0

    def test_det_torps_sets_det_circle(self):
        ih, conn, gs = make_handler()
        ih._dispatch('det_torps', SI)
        assert ih.det_circle == 10

    def test_det_torps_sends_packet(self):
        ih, conn, gs = make_handler()
        ih._dispatch('det_torps', SI)
        assert len(conn.sent) == 1
        assert conn.sent[0][0] == CP_DET_TORPS

    def test_det_radius_formula(self):
        """Det circle radius = int(DETDIST / game_scale).

        At scale=1.0, game_scale=40: radius = int(1700 / 40) = 42.
        """
        from netrek.layout import Layout
        lo = Layout(scale=1.0)
        radius = int(DETDIST / lo.game_scale)
        assert radius == 42  # 1700 / 40 = 42.5 → int = 42

    def test_det_radius_at_scale_2(self):
        """At scale=2.0, game_scale=20: radius = int(1700 / 20) = 85."""
        from netrek.layout import Layout
        lo = Layout(scale=2.0)
        radius = int(DETDIST / lo.game_scale)
        assert radius == 85
