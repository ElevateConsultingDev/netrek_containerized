"""Packet encode/decode for the Netrek protocol.

Ported from lurk.py (Python 2) to Python 3, extended with all packet
types from packets.h py-struct comments.
"""
import struct
from .constants import *

# --- Server packet definitions ---
# Maps SP type number -> (name, struct_format, field_names)
# Formats use network byte order (!) per packets.h py-struct comments.

SP_PACKETS = {
    SP_MESSAGE:     ("SP_MESSAGE",     "!bBBB80s",        ("type", "m_flags", "m_recpt", "m_from", "mesg")),
    SP_PLAYER_INFO: ("SP_PLAYER_INFO", "!bbbb",           ("type", "pnum", "shiptype", "team")),
    SP_KILLS:       ("SP_KILLS",       "!bbxxI",          ("type", "pnum", "kills")),
    SP_PLAYER:      ("SP_PLAYER",      "!bbBbll",         ("type", "pnum", "dir", "speed", "x", "y")),
    SP_TORP_INFO:   ("SP_TORP_INFO",   "!bbbxhxx",        ("type", "war", "status", "tnum")),
    SP_TORP:        ("SP_TORP",        "!bBhll",          ("type", "dir", "tnum", "x", "y")),
    SP_PHASER:      ("SP_PHASER",      "!bbbBlll",        ("type", "pnum", "status", "dir", "x", "y", "target")),
    SP_PLASMA_INFO: ("SP_PLASMA_INFO", "!bbbxhxx",        ("type", "war", "status", "pnum")),
    SP_PLASMA:      ("SP_PLASMA",      "!bxhll",          ("type", "pnum", "x", "y")),
    SP_WARNING:     ("SP_WARNING",     "!bxxx80s",        ("type", "mesg",)),
    SP_MOTD:        ("SP_MOTD",        "!bxxx80s",        ("type", "line",)),
    SP_YOU:         ("SP_YOU",         "!bbbbbbxxIlllhhhh", ("type", "pnum", "hostile", "swar", "armies", "tractor", "flags", "damage", "shield", "fuel", "etemp", "wtemp", "whydead", "whodead")),
    SP_QUEUE:       ("SP_QUEUE",       "!bxh",            ("type", "pos",)),
    SP_STATUS:      ("SP_STATUS",      "!bbxxIIIIIL",     ("type", "tourn", "armsbomb", "planets", "kills", "losses", "time", "timeprod")),
    SP_PLANET:      ("SP_PLANET",      "!bbbbhxxl",       ("type", "pnum", "owner", "info", "flags", "armies")),
    SP_PICKOK:      ("SP_PICKOK",      "!bbxx",           ("type", "state",)),
    SP_LOGIN:       ("SP_LOGIN",       "!bbxxl96s",       ("type", "accept", "flags", "keymap")),
    SP_FLAGS:       ("SP_FLAGS",       "!bbbxI",          ("type", "pnum", "tractor", "flags")),
    SP_MASK:        ("SP_MASK",        "!bbxx",           ("type", "mask",)),
    SP_PSTATUS:     ("SP_PSTATUS",     "!bbbx",           ("type", "pnum", "status")),
    SP_BADVERSION:  ("SP_BADVERSION",  "!bbxx",           ("type", "why",)),
    SP_HOSTILE:     ("SP_HOSTILE",     "!bbbb",           ("type", "pnum", "war", "hostile")),
    SP_STATS:       ("SP_STATS",       "!bbxx13l",        ("type", "pnum", "tkills", "tlosses", "kills", "losses", "tticks", "tplanets", "tarmies", "sbkills", "sblosses", "armies", "planets", "maxkills", "sbmaxkills")),
    SP_PL_LOGIN:    ("SP_PL_LOGIN",    "!bbbx16s16s16s",  ("type", "pnum", "rank", "name", "monitor", "login")),
    SP_RESERVED:    ("SP_RESERVED",    "!bxxx16s",        ("type", "data",)),
    SP_PLANET_LOC:  ("SP_PLANET_LOC",  "!bbxxll16s",      ("type", "pnum", "x", "y", "name")),
    SP_SCAN:        ("SP_SCAN",        "!bbbbiiiiii",     ("type", "pnum", "success", "pad1", "p_fuel", "p_armies", "p_shield", "p_damage", "p_etemp", "p_wtemp")),
    SP_UDP_REPLY:   ("SP_UDP_REPLY",   "!bbxxi",          ("type", "reply", "port")),
    SP_SEQUENCE:    ("SP_SEQUENCE",    "!bBH",            ("type", "flag8", "sequence")),
    SP_SC_SEQUENCE: ("SP_SC_SEQUENCE", "!bxH",            ("type", "sequence",)),
    SP_RSA_KEY:     ("SP_RSA_KEY",     "!bbbb32s",        ("type", "pad1", "pad2", "pad3", "data")),
    SP_MOTD_PIC:    ("SP_MOTD_PIC",    "!bbhhhhh1016s",   ("type", "pad1", "x", "y", "page", "width", "height", "bits")),
    SP_STATS2:      ("SP_STATS2",      "!bbxx22i",        ("type", "pnum", "genocides", "maxkills", "di", "kills", "losses", "armsbomb", "resbomb", "dooshes", "planets", "tticks", "sbkills", "sblosses", "sbticks", "sbmaxkills", "wbkills", "wblosses", "wbticks", "wbmaxkills", "jsplanets", "jsticks", "rank", "royal")),
    SP_STATUS2:     ("SP_STATUS2",     "!bbxx16I",        ("type", "tourn", "dooshes", "armsbomb", "resbomb", "planets", "kills", "losses", "sbkills", "sblosses", "sbtime", "wbkills", "wblosses", "wbtime", "jsplanets", "jstime", "time", "timeprod")),
    SP_PLANET2:     ("SP_PLANET2",     "!bbbbiii",        ("type", "pnum", "owner", "info", "flags", "timestamp", "armies")),
    SP_NEW_MOTD:    ("SP_NEW_MOTD",    "!bbxx",           ("type", "pad1",)),
    SP_THINGY:      ("SP_THINGY",      "!bBhii",          ("type", "dir", "tnum", "x", "y")),
    SP_THINGY_INFO: ("SP_THINGY_INFO", "!bbhhh",          ("type", "war", "shape", "tnum", "owner")),
    SP_SHIP_CAP:    ("SP_SHIP_CAP",    "!bbHHHiiiiiiHHH1sx16s2sH", ("type", "operation", "s_type", "s_torpspeed", "s_phaserrange", "s_maxspeed", "s_maxfuel", "s_maxshield", "s_maxdamage", "s_maxwpntemp", "s_maxegntemp", "s_width", "s_height", "s_maxarmies", "s_letter", "s_name", "s_desig", "s_bitmap")),
    SP_PING:        ("SP_PING",        "!bBHBBBB",        ("type", "number", "lag", "tloss_sc", "tloss_cs", "iloss_sc", "iloss_cs")),
    SP_FEATURE:     ("SP_FEATURE",     "!bcbbi80s",       ("type", "feature_type", "arg1", "arg2", "value", "name")),
    SP_TERRAIN2:    ("SP_TERRAIN2",    "!bBBB128s",       ("type", "sequence", "total_pkts", "length", "terrain_type")),
    SP_TERRAIN_INFO2: ("SP_TERRAIN_INFO2", "!bbHHH",    ("type", "pad", "pad2", "xdim", "ydim")),
    SP_S_STATS:     ("SP_S_STATS",     "!bb5H3I4HI",     ("type", "pnum", "tplanets", "tkills", "tlosses", "kills", "losses", "tticks", "tarmies", "maxkills", "sbkills", "sblosses", "armies", "planets", "sbmaxkills")),
}

# Compute packet sizes from struct formats
PACKET_SIZES = {}
for ptype, (name, fmt, fields) in SP_PACKETS.items():
    PACKET_SIZES[ptype] = struct.calcsize(fmt)


def decode_packet(ptype, data):
    """Decode a raw packet bytes into a dict of field values."""
    if ptype not in SP_PACKETS:
        return None
    name, fmt, fields = SP_PACKETS[ptype]
    try:
        values = struct.unpack(fmt, data)
    except struct.error:
        return None
    result = {}
    for i, fname in enumerate(fields):
        val = values[i]
        if isinstance(val, bytes) and fname in ("mesg", "line", "name", "monitor", "login", "data", "keymap", "s_name"):
            val = val.split(b'\x00', 1)[0].decode('ascii', errors='replace')
        result[fname] = val
    return result


# --- Client packet encoders ---

def cp_socket():
    return struct.pack("!bbbxI", CP_SOCKET, SOCKVERSION, UDPVERSION, 0)

def cp_login(query, name, password, login):
    # pad2=0x69, pad3=0x42: paradise "blk_flag" magic — tells server
    # this is a paradise-aware client so tournamentMask() works.
    return struct.pack("!bbbb16s16s16s", CP_LOGIN, query, 0x69, 0x42,
                       name.encode('ascii')[:16].ljust(16, b'\x00'),
                       password.encode('ascii')[:16].ljust(16, b'\x00'),
                       login.encode('ascii')[:16].ljust(16, b'\x00'))

def cp_outfit(team, ship=0):
    return struct.pack("!bbbx", CP_OUTFIT, team, ship)

def cp_speed(speed):
    return struct.pack("!bbxx", CP_SPEED, speed)

def cp_direction(direction):
    return struct.pack("!bBxx", CP_DIRECTION, direction & 0xFF)

def cp_phaser(direction):
    return struct.pack("!bBxx", CP_PHASER, direction & 0xFF)

def cp_torp(direction):
    return struct.pack("!bBxx", CP_TORP, direction & 0xFF)

def cp_plasma(direction):
    return struct.pack("!bBxx", CP_PLASMA, direction & 0xFF)

def cp_shield(state):
    return struct.pack("!bbxx", CP_SHIELD, state)

def cp_cloak(state):
    return struct.pack("!bbxx", CP_CLOAK, state)

def cp_repair(state):
    return struct.pack("!bbxx", CP_REPAIR, state)

def cp_orbit(state):
    return struct.pack("!bbxx", CP_ORBIT, state)

def cp_bomb(state):
    return struct.pack("!bbxx", CP_BOMB, state)

def cp_beam(state):
    """state: 1=beam up, 2=beam down"""
    return struct.pack("!bbxx", CP_BEAM, state)

def cp_det_torps():
    return struct.pack("!bxxx", CP_DET_TORPS)

def cp_det_mytorp(tnum):
    return struct.pack("!bxh", CP_DET_MYTORP, tnum)

def cp_war(newmask):
    return struct.pack("!bbxx", CP_WAR, newmask)

def cp_refit(ship):
    return struct.pack("!bbxx", CP_REFIT, ship)

def cp_planlock(pnum):
    return struct.pack("!bbxx", CP_PLANLOCK, pnum)

def cp_playlock(pnum):
    return struct.pack("!bbxx", CP_PLAYLOCK, pnum)

def cp_tractor(state, pnum):
    return struct.pack("!bbbx", CP_TRACTOR, state, pnum)

def cp_repress(state, pnum):
    return struct.pack("!bbbx", CP_REPRESS, state, pnum)

def cp_quit():
    return struct.pack("!bxxx", CP_QUIT)

def cp_bye():
    return struct.pack("!bxxx", CP_BYE)

def cp_updates(usecs):
    return struct.pack("!bxxxI", CP_UPDATES, usecs)

def cp_message(group, indiv, mesg):
    return struct.pack("!bBBx80s", CP_MESSAGE, group, indiv,
                       mesg.encode('ascii')[:80].ljust(80, b'\x00'))

def cp_ping_response(number=0, pingme=1, cp_sent=0, cp_recv=0):
    return struct.pack("!bBbxll", CP_PING_RESPONSE, number, pingme, cp_sent, cp_recv)

def cp_reserved(data, resp):
    return struct.pack("!bxxx16s16s", CP_RESERVED, data, resp)

def cp_feature(feature_type, arg1, arg2, value, name):
    return struct.pack("!bcbbi80s", CP_FEATURE,
                       feature_type.encode('ascii'),
                       arg1, arg2, value,
                       name.encode('ascii')[:80].ljust(80, b'\x00'))

def cp_practr():
    return struct.pack("!bxxx", CP_PRACTR)

def cp_coup():
    return struct.pack("!bxxx", CP_COUP)

def cp_udp_req(request, connmode=1, port=0):
    return struct.pack("!bbbbi", CP_UDP_REQ, request, connmode, 0, port)

def cp_sequence(sequence):
    return struct.pack("!bxH", CP_SEQUENCE, sequence & 0xFFFF)

def cp_s_req(req, version=SHORTVERSION):
    return struct.pack("!bbbb", CP_S_REQ, req, version, 0)

def cp_s_thrs(thresh):
    return struct.pack("!bbH", CP_S_THRS, 0, thresh)
