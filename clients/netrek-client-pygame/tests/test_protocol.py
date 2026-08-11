"""Tests for protocol.py — Packet encode/decode."""
import struct
import pytest

from netrek.protocol import (
    cp_speed, cp_direction, cp_torp, cp_phaser, cp_plasma,
    cp_shield, cp_cloak, cp_repair, cp_orbit, cp_bomb, cp_beam,
    cp_det_torps, cp_det_mytorp, cp_war, cp_refit,
    cp_planlock, cp_playlock, cp_tractor, cp_repress,
    cp_quit, cp_bye, cp_message, cp_coup, cp_practr,
    cp_socket, cp_login, cp_outfit, cp_updates,
    decode_packet,
)
from netrek.constants import (
    CP_SPEED, CP_DIRECTION, CP_TORP, CP_PHASER, CP_PLASMA,
    CP_SHIELD, CP_CLOAK, CP_REPAIR, CP_ORBIT, CP_BOMB, CP_BEAM,
    CP_DET_TORPS, CP_DET_MYTORP, CP_WAR, CP_REFIT,
    CP_PLANLOCK, CP_PLAYLOCK, CP_TRACTOR, CP_REPRESS,
    CP_QUIT, CP_BYE, CP_MESSAGE, CP_COUP, CP_PRACTR,
    CP_SOCKET, CP_LOGIN, CP_OUTFIT, CP_UPDATES,
    MALL, MTEAM, MINDIV,
    SP_PLAYER_INFO, SP_KILLS, SP_YOU, SP_PLANET,
    CRUISER, FED, BATTLESHIP,
)


# ---- cp_speed ----

class TestCpSpeed:
    def test_packet_size(self):
        pkt = cp_speed(5)
        assert len(pkt) == 4

    def test_type_byte(self):
        pkt = cp_speed(5)
        assert pkt[0] == CP_SPEED

    def test_speed_value(self):
        pkt = cp_speed(5)
        assert pkt[1] == 5

    def test_speed_zero(self):
        pkt = cp_speed(0)
        assert pkt[1] == 0

    def test_speed_12(self):
        pkt = cp_speed(12)
        assert pkt[1] == 12

    def test_padding_zeroed(self):
        pkt = cp_speed(5)
        assert pkt[2] == 0 and pkt[3] == 0


# ---- cp_torp ----

class TestCpTorp:
    def test_packet_size(self):
        pkt = cp_torp(128)
        assert len(pkt) == 4

    def test_type_byte(self):
        pkt = cp_torp(128)
        assert pkt[0] == CP_TORP

    def test_direction(self):
        pkt = cp_torp(128)
        assert pkt[1] == 128

    def test_direction_zero(self):
        pkt = cp_torp(0)
        assert pkt[1] == 0

    def test_direction_255(self):
        pkt = cp_torp(255)
        assert pkt[1] == 255

    def test_direction_wraps(self):
        """Direction is masked to 0xFF."""
        pkt = cp_torp(256)
        assert pkt[1] == 0


# ---- cp_phaser ----

class TestCpPhaser:
    def test_type_byte(self):
        pkt = cp_phaser(64)
        assert pkt[0] == CP_PHASER

    def test_direction(self):
        pkt = cp_phaser(64)
        assert pkt[1] == 64


# ---- cp_plasma ----

class TestCpPlasma:
    def test_type_byte(self):
        pkt = cp_plasma(192)
        assert pkt[0] == CP_PLASMA

    def test_direction(self):
        pkt = cp_plasma(192)
        assert pkt[1] == 192


# ---- cp_direction ----

class TestCpDirection:
    def test_type_byte(self):
        pkt = cp_direction(0)
        assert pkt[0] == CP_DIRECTION

    def test_direction(self):
        pkt = cp_direction(32)
        assert pkt[1] == 32


# ---- cp_shield ----

class TestCpShield:
    def test_shield_on(self):
        pkt = cp_shield(1)
        assert pkt[0] == CP_SHIELD
        assert pkt[1] == 1

    def test_shield_off(self):
        pkt = cp_shield(0)
        assert pkt[1] == 0

    def test_packet_size(self):
        assert len(cp_shield(1)) == 4


# ---- cp_cloak ----

class TestCpCloak:
    def test_cloak_on(self):
        pkt = cp_cloak(1)
        assert pkt[0] == CP_CLOAK
        assert pkt[1] == 1

    def test_cloak_off(self):
        pkt = cp_cloak(0)
        assert pkt[1] == 0


# ---- cp_repair, cp_orbit, cp_bomb, cp_beam ----

class TestSimpleToggles:
    def test_repair(self):
        pkt = cp_repair(1)
        assert pkt[0] == CP_REPAIR
        assert len(pkt) == 4

    def test_orbit(self):
        pkt = cp_orbit(1)
        assert pkt[0] == CP_ORBIT

    def test_bomb(self):
        pkt = cp_bomb(1)
        assert pkt[0] == CP_BOMB

    def test_beam_up(self):
        pkt = cp_beam(1)
        assert pkt[0] == CP_BEAM
        assert pkt[1] == 1

    def test_beam_down(self):
        pkt = cp_beam(2)
        assert pkt[0] == CP_BEAM
        assert pkt[1] == 2


# ---- cp_det_torps, cp_det_mytorp ----

class TestDetTorps:
    def test_det_torps(self):
        pkt = cp_det_torps()
        assert pkt[0] == CP_DET_TORPS
        assert len(pkt) == 4

    def test_det_mytorp(self):
        pkt = cp_det_mytorp(42)
        assert pkt[0] == CP_DET_MYTORP
        tnum = struct.unpack("!h", pkt[2:4])[0]
        assert tnum == 42


# ---- cp_war, cp_refit ----

class TestWarRefit:
    def test_war(self):
        pkt = cp_war(0x0E)
        assert pkt[0] == CP_WAR
        assert pkt[1] == 0x0E

    def test_refit(self):
        pkt = cp_refit(BATTLESHIP)
        assert pkt[0] == CP_REFIT
        assert pkt[1] == BATTLESHIP


# ---- cp_planlock, cp_playlock ----

class TestLocks:
    def test_planlock(self):
        pkt = cp_planlock(10)
        assert pkt[0] == CP_PLANLOCK
        assert pkt[1] == 10

    def test_playlock(self):
        pkt = cp_playlock(3)
        assert pkt[0] == CP_PLAYLOCK
        assert pkt[1] == 3


# ---- cp_tractor, cp_repress ----

class TestTractorRepress:
    def test_tractor_on(self):
        pkt = cp_tractor(1, 5)
        assert pkt[0] == CP_TRACTOR
        assert pkt[1] == 1
        assert pkt[2] == 5

    def test_tractor_off(self):
        pkt = cp_tractor(0, 5)
        assert pkt[1] == 0

    def test_repress(self):
        pkt = cp_repress(1, 7)
        assert pkt[0] == CP_REPRESS
        assert pkt[1] == 1
        assert pkt[2] == 7


# ---- cp_quit, cp_bye ----

class TestQuitBye:
    def test_quit(self):
        pkt = cp_quit()
        assert pkt[0] == CP_QUIT
        assert len(pkt) == 4

    def test_bye(self):
        pkt = cp_bye()
        assert pkt[0] == CP_BYE
        assert len(pkt) == 4


# ---- cp_message ----

class TestCpMessage:
    def test_message_all(self):
        pkt = cp_message(MALL, 0, "hello")
        assert pkt[0] == CP_MESSAGE
        assert pkt[1] == MALL
        assert pkt[2] == 0  # indiv
        assert len(pkt) == 84  # 4 header + 80 body

    def test_message_team(self):
        pkt = cp_message(MTEAM, FED, "attack!")
        assert pkt[1] == MTEAM
        assert pkt[2] == FED

    def test_message_text_encoded(self):
        pkt = cp_message(MALL, 0, "hello")
        text = pkt[4:84]
        assert text[:5] == b'hello'
        assert text[5] == 0  # null terminated

    def test_message_long_truncated(self):
        long_msg = "x" * 100
        pkt = cp_message(MALL, 0, long_msg)
        assert len(pkt) == 84


# ---- cp_socket, cp_login, cp_outfit, cp_updates ----

class TestSessionPackets:
    def test_socket(self):
        pkt = cp_socket()
        assert pkt[0] == CP_SOCKET
        assert len(pkt) == 8

    def test_login(self):
        pkt = cp_login(0, "guest", "", "pygame")
        assert pkt[0] == CP_LOGIN
        assert len(pkt) == 52  # 4 + 16 + 16 + 16

    def test_outfit(self):
        pkt = cp_outfit(0, CRUISER)
        assert pkt[0] == CP_OUTFIT
        assert pkt[1] == 0   # team
        assert pkt[2] == CRUISER

    def test_updates(self):
        pkt = cp_updates(100000)
        assert pkt[0] == CP_UPDATES

    def test_practr(self):
        pkt = cp_practr()
        assert pkt[0] == CP_PRACTR

    def test_coup(self):
        pkt = cp_coup()
        assert pkt[0] == CP_COUP


# ---- decode_packet ----

class TestDecodePacket:
    def test_decode_player_info(self):
        raw = struct.pack("!bbbb", SP_PLAYER_INFO, 5, CRUISER, FED)
        pkt = decode_packet(SP_PLAYER_INFO, raw)
        assert pkt is not None
        assert pkt["pnum"] == 5
        assert pkt["shiptype"] == CRUISER
        assert pkt["team"] == FED

    def test_decode_kills(self):
        raw = struct.pack("!bbxxI", SP_KILLS, 2, 350)
        pkt = decode_packet(SP_KILLS, raw)
        assert pkt["pnum"] == 2
        assert pkt["kills"] == 350

    def test_decode_unknown_type(self):
        pkt = decode_packet(9999, b'\x00' * 20)
        assert pkt is None

    def test_decode_truncated_data(self):
        pkt = decode_packet(SP_PLAYER_INFO, b'\x02')
        assert pkt is None

    def test_round_trip_speed(self):
        """Encode cp_speed then verify struct fields manually."""
        pkt = cp_speed(7)
        ptype, speed = struct.unpack("!bb", pkt[:2])
        assert ptype == CP_SPEED
        assert speed == 7

    def test_round_trip_torp(self):
        pkt = cp_torp(200)
        ptype, direction = struct.unpack("!bB", pkt[:2])
        assert ptype == CP_TORP
        assert direction == 200

    def test_round_trip_message(self):
        pkt = cp_message(MALL, 0, "test msg")
        ptype, group, indiv = struct.unpack("!bBB", pkt[:3])
        assert ptype == CP_MESSAGE
        assert group == MALL
        text = pkt[4:84].split(b'\x00', 1)[0].decode('ascii')
        assert text == "test msg"


# ---- All cp_* return 4-byte aligned packets ----

class TestPacketAlignment:
    """All client packets must be 4-byte aligned per protocol spec."""
    def test_all_packets_aligned(self):
        packets = [
            cp_speed(0), cp_direction(0), cp_torp(0), cp_phaser(0),
            cp_plasma(0), cp_shield(0), cp_cloak(0), cp_repair(0),
            cp_orbit(0), cp_bomb(0), cp_beam(1), cp_det_torps(),
            cp_det_mytorp(0), cp_war(0), cp_refit(0), cp_planlock(0),
            cp_playlock(0), cp_tractor(0, 0), cp_repress(0, 0),
            cp_quit(), cp_bye(), cp_message(0, 0, ""), cp_practr(),
            cp_coup(), cp_socket(), cp_login(0, "x", "", "x"),
            cp_outfit(0, 0), cp_updates(100000),
        ]
        for pkt in packets:
            assert len(pkt) % 4 == 0, f"Packet type {pkt[0]} not 4-byte aligned: {len(pkt)} bytes"
