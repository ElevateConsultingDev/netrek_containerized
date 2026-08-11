"""Tests for speed_up / speed_down dispatch ordering and keymap."""
import pytest

from netrek.config import Config, DEFAULT_KEYMAP
from netrek.gamestate import GameState
from netrek.constants import PALIVE, CRUISER, CP_SPEED


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


SI = (1.0, 0, 0)  # default scale_info


class TestSpeedDown:
    def test_speed_down_from_5(self):
        ih, conn, gs = make_handler()
        gs.players[0].speed = 5
        ih._dispatch('speed_down', SI)
        assert len(conn.sent) == 1
        assert conn.sent[0][0] == CP_SPEED
        assert conn.sent[0][1] == 4

    def test_speed_down_at_zero_no_packet(self):
        ih, conn, gs = make_handler()
        gs.players[0].speed = 0
        ih._dispatch('speed_down', SI)
        assert len(conn.sent) == 0


class TestSpeedUp:
    def test_speed_up_from_5(self):
        ih, conn, gs = make_handler()
        gs.players[0].speed = 5
        ih._dispatch('speed_up', SI)
        assert len(conn.sent) == 1
        assert conn.sent[0][0] == CP_SPEED
        assert conn.sent[0][1] == 6

    def test_speed_up_no_upper_bound_check(self):
        """speed_up sends speed+1 unconditionally (server enforces limits)."""
        ih, conn, gs = make_handler()
        gs.players[0].speed = 99
        ih._dispatch('speed_up', SI)
        assert conn.sent[0][0] == CP_SPEED
        assert conn.sent[0][1] == 100


class TestMaxSpeed:
    def test_max_speed_sends_12(self):
        ih, conn, gs = make_handler()
        ih._dispatch('max_speed', SI)
        assert len(conn.sent) == 1
        assert conn.sent[0][0] == CP_SPEED
        assert conn.sent[0][1] == 12


class TestSpeedKeymap:
    def test_less_than_is_speed_down(self):
        assert DEFAULT_KEYMAP['<'] == 'speed_down'

    def test_greater_than_is_speed_up(self):
        assert DEFAULT_KEYMAP['>'] == 'speed_up'

    def test_percent_is_max_speed(self):
        assert DEFAULT_KEYMAP['%'] == 'max_speed'

    def test_hash_is_max_speed(self):
        assert DEFAULT_KEYMAP['#'] == 'max_speed'
