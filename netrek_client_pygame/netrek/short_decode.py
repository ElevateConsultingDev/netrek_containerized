"""Short packet (SP2) decoders for the Netrek protocol.

Ported from COW client short.c / wtext.h.  Short packets use variable-length
encoding, bit-packing, and 9-bit coordinate compression to reduce bandwidth
by ~50-70%.
"""
import struct

from .constants import (
    SP_S_MESSAGE, SP_S_WARNING, SP_S_YOU, SP_S_YOU_SS, SP_S_PLAYER,
    SP_S_TORP, SP_S_TORP_INFO, SP_S_8_TORP, SP_S_PLANET,
    SP_S_PHASER, SP_S_KILLS, SP_S_REPLY, SP_S_SEQUENCE,
    SP_GPARAM, SP_PARADISE_EXT1,
    PHFREE, PHHIT, PHMISS, PHHIT2,
    SHORT_WARNING, STEXTE_STRING,
    SPWINSIDE, GWIDTH, SCALE, MAXTORP,
    MAXPLAYER, MAXPLANETS,
    TEAMLET, SHIPNOS, SHIP_NAMES, NOBODY,
    KQUIT, KTORP, KPHASER, KPLANET, KSHIP, KDAEMON, KWINNER, KGHOST, KGENOCIDE,
)

# ---------------------------------------------------------------------------
# Lookup tables
# ---------------------------------------------------------------------------

# popcount: number of 1-bits in a byte
_POPCOUNT = bytes([bin(i).count('1') for i in range(256)])

# vtsize[n]: total packet size for SP_S_TORP with n torps present
# n torps need ceil(n*18 bits / 8) bytes of coord data + header
_VTSIZE = [4, 8, 8, 12, 12, 16, 20, 20, 24]

# vtisize[n]: byte offset to torp-info status data in SP_S_TORP_INFO
_VTISIZE = [4, 7, 9, 11, 13, 16, 18, 20, 22]

# Rank names (COW data.c ranks[])
_RANKS = [
    "Ensign", "Lieutenant", "Lt. Cmdr.", "Commander",
    "Captain", "Flt. Capt.", "Commodore", "Rear Adml.",
    "Admiral",
]

# Static warning texts (COW short.c w_texts[], indices 0-103)
_W_TEXTS = [
    "Tractor beams haven't been invented yet.",
    "Weapons's Officer:  Cannot tractor while cloaked, sir!",
    "Weapon's Officer:  Vessel is out of range of our tractor beam.",
    "You must have one kill to throw a coup",
    "You must orbit your home planet to throw a coup",
    "You already own a planet!!!",
    "You must orbit your home planet to throw a coup",
    "Too many armies on planet to throw a coup",
    "Planet not yet ready for a coup",
    "I cannot allow that.  Pick another team",
    "Please confirm change of teams.  Select the new team again.",
    "That is an illegal ship type.  Try again.",
    "That ship hasn't beed designed yet.  Try again.",
    "Your new starbase is still under construction",
    "Your team is not capable of defending such an expensive ship!",
    "Your team's stuggling economy cannot support such an expenditure!",
    "Your side already has a starbase!",
    "Plasmas haven't been invented yet.",
    "Weapon's Officer:  Captain, this ship is not armed with plasma torpedoes!",
    "Plasma torpedo launch tube has exceeded the maximum safe temperature!",
    "Our fire control system limits us to 1 live torpedo at a time captain!",
    "Our fire control system limits us to 1 live torpedo at a time captain!",
    "We don't have enough fuel to fire a plasma torpedo!",
    "We cannot fire while our vessel is undergoing repairs.",
    "We are unable to fire while in cloak, captain!",
    "Torpedo launch tubes have exceeded maximum safe temperature!",
    "Our computers limit us to having 8 live torpedos at a time captain!",
    "We don't have enough fuel to fire photon torpedos!",
    "We cannot fire while our vessel is in repair mode.",
    "We are unable to fire while in cloak, captain!",
    "We only have forward mounted cannons.",
    "Weapons Officer:  This ship is not armed with phasers, captain!",
    "Phasers have not recharged",
    "Not enough fuel for phaser",
    "Can't fire while repairing",
    "Weapons overheated",
    "Cannot fire while cloaked",
    "Phaser missed!!!",
    "You destroyed the plasma torpedo!",
    "Must be orbiting to bomb",
    "Can't bomb your own armies.  Have you been reading Catch-22 again?",
    "Must declare war first (no Pearl Harbor syndrome allowed here).",
    "Bomb out of T-mode?  Please verify your order to bomb.",
    "Hoser!",
    "Must be orbiting or docked to beam up.",
    "Those aren't our men.",
    "Comm Officer: We're not authorized to beam foriegn troops on board!",
    "Must be orbiting or docked to beam down.",
    "Comm Officer: Starbase refuses permission to beam our troops over.",
    "Pausing ten seconds to re-program battle computers.",
    "You must orbit your HOME planet to apply for command reassignment!",
    "You must orbit your home planet to apply for command reassignment!",
    "Can only refit to starbase on your home planet.",
    "You must dock YOUR starbase to apply for command reassignment!",
    "Must orbit home planet or dock your starbase to apply for command reassignment!",
    "Central Command refuses to accept a ship in this condition!",
    "You must beam your armies down before moving to your new ship",
    "That ship hasn't been designed yet.",
    "Your side already has a starbase!",
    "Your team is not capable of defending such an expensive ship",
    "Your new starbase is still under construction",
    "Your team's stuggling economy cannot support such an expenditure!",
    "You are being transported to your new vessel .... ",
    "Engineering:  Energize. [ SFX: chimes ]",
    "Wait, you forgot your toothbrush!",
    "Nothing like turning in a used ship for a new one.",
    "First officer:  Oh no, not you again... we're doomed!",
    "First officer:  Uh, I'd better run diagnostics on the escape pods.",
    "Shipyard controller:  This time, *please* be more careful, okay?",
    "Weapons officer:  Not again!  This is absurd...",
    "Weapons officer:  ... the whole ship's computer is down?",
    "Weapons officer:  Just to twiddle a few bits of the ship's memory?",
    "Weapons officer:  Bah! [ bangs fist on inoperative console ]",
    "First Officer:  Easy, big guy... it's just one of those mysterious",
    "First Officer:  laws of the universe, like 'tires on the ether'.",
    "First Officer:  laws of the universe, like 'Klingon bitmaps are ugly'.",
    "First Officer:  laws of the universe, like 'all admirals have scummed'.",
    "First Officer:  laws of the universe, like 'Mucus Pig exists'.",
    "First Officer:  laws of the universe, like 'guests advance 5x faster'.",
    "Helmsman: Captain, the maximum safe speed for docking or orbiting is warp 2!",
    "Central Command regulations prohibit you from orbiting foreign planets",
    "Helmsman:  Sensors read no valid targets in range to dock or orbit sir!",
    "No more room on board for armies",
    "You notice everyone on the bridge is staring at you.",
    "Can't send in practice robot with other players in the game.",
    "Self Destruct has been canceled",
    "Be quiet",
    "You are censured.  Message was not sent.",
    "You are ignoring that player.  Message was not sent.",
    "That player is censured.  Message was not sent.",
    "Self destruct initiated",
    "Scanners haven't been invented yet",
    "WARNING: BROKEN mode is enabled",
    "Server can't do that UDP mode",
    "Server will send with TCP only",
    "Server will send with simple UDP",
    "Request for fat UDP DENIED (set to simple)",
    "Request for double UDP DENIED (set to simple)",
    "Update request DENIED (chill out!)",
    "Player lock lost while player dead.",
    "Can only lock on own team.",
    "You can only warp to your own team's planets!",
    "Planet lock lost on change of ownership.",
    " Weapons officer: Finally! systems are back online!",
]

# Parameterized warning texts (COW short.c vari_texts[])
_VARI_TEXTS = [
    "Engineering:  Energizing transporters in %d seconds",
    "Stand By ... Self Destruct in %d seconds",
    "Helmsman:  Docking manuever completed Captain.  All moorings secured at port %d.",
    "Not constructed yet. %d minutes required for completion",
]

# INL daemon texts (COW short.c daemon_texts[])
_DAEMON_TEXTS = [
    "Game is paused.  CONTINUE to continue.",
    "Game is no-longer paused!",
    "Game is paused. Captains CONTINUE to continue.",
    "Game will continue in 10 seconds",
    "Teams chosen.  Game will start in 1 minute.",
    "----------- Game will start in 1 minute -------------",
]

# wtext.h dispatch codes
_TEXTE = 0
_PHASER_HIT_TEXT = 1
_BOMB_INEFFECTIVE = 2
_BOMB_TEXT = 3
_BEAMUP_TEXT = 4
_BEAMUP2_TEXT = 5
_BEAMUPSTARBASE_TEXT = 6
_BEAMDOWNSTARBASE_TEXT = 7
_BEAMDOWNPLANET_TEXT = 8
_SBREPORT = 9
_ONEARG_TEXT = 10
_BEAM_D_PLANET_TEXT = 11
_ARGUMENTS = 12
_BEAM_U_TEXT = 13
_LOCKPLANET_TEXT = 14
_LOCKPLAYER_TEXT = 15
_SBRANK_TEXT = 16
_SBDOCKREFUSE_TEXT = 17
_SBDOCKDENIED_TEXT = 18
_SBLOCKSTRANGER = 19
_SBLOCKMYTEAM = 20
_DMKILL = 21
_KILLARGS = 22
_DMKILLP = 23
_DMBOMB = 24
_DMDEST = 25
_DMTAKE = 26
_DGHOSTKILL = 27
_INLDMKILLP = 28
_INLDMKILL = 29
_INLDRESUME = 30
_INLDTEXTE = 31
_STEXTE = 32
# 33 = SHORT_WARNING, 34 = STEXTE_STRING (handled in get_variable_size)
_KILLARGS2 = 35

# Death-reason names for kill messages (COW short.c)
_KILL_REASONS = {
    KQUIT: "self-destructed",
    KTORP: "torpedoed",
    KPHASER: "phasered",
    KPLANET: "killed by planet",
    KSHIP: "killed by ship",
    KDAEMON: "killed by daemon",
    KWINNER: "killed by winner",
    KGHOST: "ghostbusted",
    KGENOCIDE: "genocided",
}

# Server-populated dynamic text slots (filled by STEXTE_STRING messages)
_s_texte = [""] * 256

# Saved multi-packet arguments (ARGUMENTS / KILLARGS / KILLARGS2)
_saved_arg3 = 0
_saved_arg4 = 0
_saved_karg3 = 0
_saved_karg4 = 0
_saved_karg5 = 0


# ---------------------------------------------------------------------------
# Variable-length packet size calculator
# ---------------------------------------------------------------------------

def get_variable_size(buf):
    """Return total size of the short packet starting at buf[0].

    buf must have at least 4 bytes.  Returns size padded to 4-byte boundary,
    or 0 if packet type is unknown.
    """
    ptype = buf[0]

    if ptype == SP_S_MESSAGE:
        # byte 4 = total length
        if len(buf) < 5:
            return 0
        size = buf[4]
        return max((size + 3) & ~3, 8)  # pad to 4, minimum 8

    if ptype == SP_S_WARNING:
        whichmsg = buf[1]
        if whichmsg == STEXTE_STRING or whichmsg == SHORT_WARNING:
            # Variable-length: byte 3 has the length
            size = buf[3]
            return max((size + 3) & ~3, 4)
        return 4  # fixed 4-byte warning

    if ptype == SP_S_PLAYER:
        b1 = buf[1]
        if b1 & 0x80:
            # Extended header: (b1 & 0x3f) player entries * 4 + 4
            return (b1 & 0x3f) * 4 + 4
        if b1 & 0x40:
            # Short header (SP2): player entries + optional flags block
            if len(buf) < 3:
                return 0
            return (b1 & 0x3f) * 4 + 4 + buf[2] * 4
        # Big header: me + other players
        return b1 * 4 + 12

    if ptype == SP_S_TORP:
        nset = _POPCOUNT[buf[1]]
        return _VTSIZE[nset]

    if ptype == SP_S_TORP_INFO:
        nset = _POPCOUNT[buf[1]]
        ninfo = _POPCOUNT[buf[3]]
        size = _VTISIZE[nset] + ninfo
        return (size + 3) & ~3

    if ptype == SP_S_8_TORP:
        # Fixed size: 2-byte header + 18 bytes coords = 20
        # (COW handlers[] has {20, handleVTorp} for SP_S_8_TORP)
        return 20

    if ptype == SP_S_PLANET:
        return buf[1] * 6 + 2

    if ptype == SP_S_PHASER:
        status = buf[1] & 0x0F
        if status in (PHFREE, PHMISS):
            return 4
        if status == PHHIT:
            return 4
        if status == PHHIT2:
            return 8
        return 12  # default (includes x, y, target)

    if ptype == SP_S_KILLS:
        size = buf[1] * 2 + 2
        return (size + 3) & ~3

    if ptype == SP_S_REPLY:
        return 8

    if ptype == SP_S_YOU:
        return 12

    if ptype == SP_S_YOU_SS:
        return 12

    if ptype == SP_S_SEQUENCE:
        return 4

    # Paradise variable-length packets
    if ptype == SP_GPARAM:
        subtype = buf[1]
        # Sizes from netrek-client-paradise gppackets.h
        _gparam_sizes = {0: 64, 1: 64, 2: 768, 3: 8, 4: 1004, 5: 64, 6: 64, 7: 312}
        return _gparam_sizes.get(subtype, 0)

    if ptype == SP_PARADISE_EXT1:
        subtype = buf[1]
        if subtype == 0:  # SP_PE1_MISSING_BITMAP
            return 12
        if subtype == 1:  # SP_PE1_NUM_MISSILES
            return 4
        return 0

    return 0


# Set of all variable-length packet types (short packets + paradise variable)
VARIABLE_PACKET_TYPES = frozenset({
    SP_S_MESSAGE, SP_S_WARNING, SP_S_YOU, SP_S_YOU_SS, SP_S_PLAYER,
    SP_GPARAM, SP_PARADISE_EXT1,
    SP_S_TORP, SP_S_TORP_INFO, SP_S_8_TORP, SP_S_PLANET,
    SP_S_PHASER, SP_S_KILLS, SP_S_REPLY, SP_S_SEQUENCE,
})


# ---------------------------------------------------------------------------
# 9-bit coordinate unpacking
# ---------------------------------------------------------------------------

def _unpack_9bit_coords(data, offset, bitset):
    """Unpack pairs of 9-bit (dx, dy) for each set bit in bitset.

    Port of COW's handleSTorp / sp2 coordinate encoding.
    Data is a stream of packed 9-bit values: each pair of coordinates
    takes 18 bits = 2.25 bytes.  A shift accumulator tracks the bit offset.

    Returns list of 8 entries: (dx, dy) tuple or None for unset bits.
    """
    results = [None] * 8
    shift = 0
    pos = offset
    for i in range(8):
        if not (bitset & (1 << i)):
            continue
        if pos + 2 >= len(data):
            break
        dx = (data[pos] >> shift) & 0xFF
        pos += 1
        dx |= ((data[pos] << (8 - shift)) & 0x1FF)
        dx &= 0x1FF
        shift += 1
        dy = (data[pos] >> shift) & 0xFF
        pos += 1
        dy |= ((data[pos] << (8 - shift)) & 0x1FF)
        dy &= 0x1FF
        shift += 1
        if shift == 8:
            shift = 0
            pos += 1
        results[i] = (dx, dy)
    return results


# ---------------------------------------------------------------------------
# Individual packet decoders
# ---------------------------------------------------------------------------

def decode_s_reply(data):
    """SP_S_REPLY (40): 8 bytes — short packet negotiation reply."""
    if len(data) < 8:
        return None
    # type(1), repl(1), winside(2), gwidth(4)
    repl = data[1]
    winside = struct.unpack("!H", data[2:4])[0]
    gwidth = struct.unpack("!i", data[4:8])[0]
    return {"type": data[0], "repl": repl, "winside": winside, "gwidth": gwidth}


def decode_s_you(data):
    """SP_S_YOU (43): 12 bytes — my ship status (replaces SP_YOU minus damage/fuel)."""
    if len(data) < 12:
        return None
    # type, pnum, hostile, swar, armies, whydead, whodead, pad, flags(4)
    flags = struct.unpack("!I", data[8:12])[0]
    return {
        "type": data[0], "pnum": data[1], "hostile": data[2], "swar": data[3],
        "armies": data[4], "whydead": data[5], "whodead": data[6],
        "flags": flags,
    }


def decode_s_you_ss(data):
    """SP_S_YOU_SS (44): 12 bytes — my ship shields/damage/fuel/temps."""
    if len(data) < 12:
        return None
    # type, pad, damage(2), shield(2), fuel(2), etemp(2), wtemp(2)
    damage, shield, fuel, etemp, wtemp = struct.unpack("!HHHHH", data[2:12])
    return {
        "type": data[0], "damage": damage, "shield": shield,
        "fuel": fuel, "etemp": etemp, "wtemp": wtemp,
    }


def decode_s_message(data):
    """SP_S_MESSAGE (41): variable — compressed message."""
    if len(data) < 5:
        return None
    # type, m_flags, m_recpt, m_from, length, message_text...
    m_flags = data[1]
    m_recpt = data[2]
    m_from = data[3]
    length = data[4]
    total = (length + 3) & ~3
    text = data[5:total]
    # Use latin-1 to preserve binary RCD distress payload bytes
    mesg = text.split(b'\x00', 1)[0].decode('latin-1')
    return {
        "type": data[0], "m_flags": m_flags, "m_recpt": m_recpt,
        "m_from": m_from, "mesg": mesg,
    }


def decode_s_warning(data, gs=None):
    """SP_S_WARNING (42): 4 bytes (or variable for STEXTE_STRING/SHORT_WARNING).

    gs (GameState) is used for planet/player name lookups in templated warnings.
    """
    if len(data) < 4:
        return None
    global _saved_arg3, _saved_arg4, _saved_karg3, _saved_karg4, _saved_karg5

    whichmsg = data[1]
    arg1 = data[2]
    arg2 = data[3]

    # Variable-length raw text warnings
    if whichmsg == SHORT_WARNING:
        total = (arg2 + 3) & ~3
        text = data[4:total]
        mesg = text.split(b'\x00', 1)[0].decode('ascii', errors='replace')
        return {"type": data[0], "mesg": mesg}

    if whichmsg == STEXTE_STRING:
        total = (arg2 + 3) & ~3
        text = data[4:total]
        mesg = text.split(b'\x00', 1)[0].decode('ascii', errors='replace')
        # Store in dynamic text slot and also display
        _s_texte[arg1] = mesg
        return {"type": data[0], "mesg": mesg}

    # Fixed 4-byte warnings -- dispatch by whichmsg code
    mesg = _decode_warning_text(whichmsg, arg1, arg2, gs)
    return {"type": data[0], "mesg": mesg}


def _planet_name(pnum, gs):
    """Get planet name from gamestate, or fallback."""
    if gs and 0 <= pnum < MAXPLANETS:
        name = gs.planets[pnum].name
        if name:
            return name
    return f"planet #{pnum}"


def _player_name(pnum, gs):
    """Get player name from gamestate, or fallback."""
    if gs and 0 <= pnum < MAXPLAYER:
        return gs.players[pnum].name or f"player {pnum}"
    return f"player {pnum}"


def _player_mapchars(pnum, gs):
    """Return (teamletter, shipnumber) for a player."""
    if gs and 0 <= pnum < MAXPLAYER:
        p = gs.players[pnum]
        tl = TEAMLET.get(p.team, 'X')
        sn = SHIPNOS[pnum] if pnum < len(SHIPNOS) else '?'
        return tl, sn
    return 'X', '?'


def _decode_warning_text(whichmsg, arg1, arg2, gs):
    """Decode a fixed-length SP_S_WARNING into a text string."""
    global _saved_arg3, _saved_arg4, _saved_karg3, _saved_karg4, _saved_karg5

    if whichmsg == _TEXTE:
        idx = arg1 | (arg2 << 8)
        if 0 <= idx < len(_W_TEXTS):
            return _W_TEXTS[idx]
        return f"Unknown warning #{idx}"

    if whichmsg == _PHASER_HIT_TEXT:
        name = _player_name(arg1, gs)
        damage = arg2
        return f"Phaser burst hit {name} for {damage} points"

    if whichmsg == _BOMB_INEFFECTIVE:
        return f"Bombing is ineffective. Only {arg2} armies defending"

    if whichmsg == _BOMB_TEXT:
        name = _planet_name(arg1, gs)
        return f"Bombarding {name}...  Sensors read {arg2} armies left"

    if whichmsg == _BEAMUP_TEXT:
        name = _planet_name(arg1, gs)
        return f"{name}: Too few armies to beam up"

    if whichmsg == _BEAMUP2_TEXT:
        return f"Beaming up. ({arg1}/{arg2})"

    if whichmsg == _BEAMUPSTARBASE_TEXT:
        name = _player_name(arg1, gs)
        return f"Starbase {name}: Too few armies to beam up"

    if whichmsg == _BEAMDOWNSTARBASE_TEXT:
        name = _player_name(arg1, gs)
        return f"No more armies to beam down to Starbase {name}"

    if whichmsg == _BEAMDOWNPLANET_TEXT:
        name = _planet_name(arg1, gs)
        return f"No more armies to beam down to {name}"

    if whichmsg == _SBREPORT:
        name = _player_name(arg1, gs)
        return f"Transporter Room: Starbase {name} reports all troop bunkers full!"

    if whichmsg == _ONEARG_TEXT:
        if 0 <= arg1 < len(_VARI_TEXTS):
            return _VARI_TEXTS[arg1] % arg2
        return f"Variable warning #{arg1} ({arg2})"

    if whichmsg == _ARGUMENTS:
        _saved_arg3 = arg1
        _saved_arg4 = arg2
        return ""  # no display, just stores args for next message

    if whichmsg == _BEAM_D_PLANET_TEXT:
        name = _planet_name(arg1, gs)
        return f"Beaming down. ({_saved_arg3}/{_saved_arg4}) {name} has {arg2} armies left"

    if whichmsg == _BEAM_U_TEXT:
        name = _player_name(arg1, gs)
        return f"Transferring ground units. ({_saved_arg3}/{_saved_arg4}) Starbase {name} has {arg2} left"

    if whichmsg == _LOCKPLANET_TEXT:
        name = _planet_name(arg1, gs)
        return f"Locking onto {name}"

    if whichmsg == _LOCKPLAYER_TEXT:
        name = _player_name(arg1, gs)
        return f"Locking onto {name}"

    if whichmsg == _SBRANK_TEXT:
        rank = _RANKS[arg1] if 0 <= arg1 < len(_RANKS) else f"rank {arg1}"
        return f"You need a rank of {rank} or higher to command a starbase!"

    if whichmsg == _SBDOCKREFUSE_TEXT:
        name = _player_name(arg1, gs)
        return f"Starbase {name} refusing us docking permission captain"

    if whichmsg == _SBDOCKDENIED_TEXT:
        name = _player_name(arg1, gs)
        return f"Starbase {name}: Permission to dock denied, all bays occupied"

    if whichmsg == _SBLOCKSTRANGER:
        name = _player_name(arg1, gs)
        tl, sn = _player_mapchars(arg1, gs)
        return f"Locking onto {name} ({tl}{sn})"

    if whichmsg == _SBLOCKMYTEAM:
        name = _player_name(arg1, gs)
        tl, sn = _player_mapchars(arg1, gs)
        dock_str = "enabled" if arg2 else "disabled"
        return f"Locking onto {name} ({tl}{sn}) (docking is {dock_str})"

    if whichmsg == _KILLARGS:
        _saved_karg3 = arg1
        _saved_karg4 = arg2
        return ""

    if whichmsg == _KILLARGS2:
        _saved_karg5 = arg1
        return ""

    if whichmsg == _DMKILL:
        # arg1 = killer pnum, arg2 = killed pnum
        # karg3 = killer armies, karg4 = kills (raw), karg5 = why-dead
        killer = _player_name(arg1, gs)
        killed = _player_name(arg2, gs)
        ktl, ksn = _player_mapchars(arg1, gs)
        dtl, dsn = _player_mapchars(arg2, gs)
        kills = (_saved_karg4 * 256 + _saved_karg3) / 100.0
        reason = _KILL_REASONS.get(_saved_karg5, "")
        armies_str = f"+{_saved_karg3} armies" if _saved_karg3 else ""
        return f"{killed} ({dtl}{dsn}) was killed by {killer} ({ktl}{ksn}) {kills:.2f} kills {armies_str}"

    if whichmsg == _DMKILLP:
        # arg1 = killed pnum, arg2 = planet pnum
        killed = _player_name(arg1, gs)
        dtl, dsn = _player_mapchars(arg1, gs)
        planet = _planet_name(arg2, gs)
        return f"{killed} ({dtl}{dsn}) was killed by {planet}"

    if whichmsg == _DMBOMB:
        name = _planet_name(arg1, gs)
        return f"{name} being BOMBED by {_player_name(arg2, gs)}"

    if whichmsg == _DMDEST:
        name = _planet_name(arg1, gs)
        return f"{name} destroyed by {_player_name(arg2, gs)}"

    if whichmsg == _DMTAKE:
        name = _planet_name(arg1, gs)
        return f"{name} taken over by {_player_name(arg2, gs)}"

    if whichmsg == _DGHOSTKILL:
        killed = _player_name(arg1, gs)
        dtl, dsn = _player_mapchars(arg1, gs)
        return f"{killed} ({dtl}{dsn}) was ghostbusted"

    if whichmsg == _INLDMKILLP:
        killed = _player_name(arg1, gs)
        dtl, dsn = _player_mapchars(arg1, gs)
        planet = _planet_name(arg2, gs)
        return f"{killed} ({dtl}{dsn}) was killed by {planet}"

    if whichmsg == _INLDMKILL:
        killer = _player_name(arg1, gs)
        killed = _player_name(arg2, gs)
        ktl, ksn = _player_mapchars(arg1, gs)
        dtl, dsn = _player_mapchars(arg2, gs)
        return f"{killed} ({dtl}{dsn}) was killed by {killer} ({ktl}{ksn})"

    if whichmsg == _INLDRESUME:
        return f"Game will resume in {arg1} seconds"

    if whichmsg == _INLDTEXTE:
        if 0 <= arg1 < len(_DAEMON_TEXTS):
            return _DAEMON_TEXTS[arg1]
        return f"Daemon message #{arg1}"

    if whichmsg == _STEXTE:
        if _s_texte[arg1]:
            return _s_texte[arg1]
        return f"Server text #{arg1}"

    return f"Unknown warning code {whichmsg} ({arg1}, {arg2})"


def decode_s_torp(data):
    """SP_S_TORP (47) / SP_S_8_TORP (49): variable — compressed torp positions.

    Returns dict with 'torps' list of (tnum, dx, dy) for each present torp,
    and 'whichtorps' (the player slot whose torps these are * MAXTORP).
    """
    if len(data) < 3:
        return None
    ptype = data[0]
    if ptype == SP_S_8_TORP:
        bitset = 0xFF
        whichtorps = data[1]
        coord_offset = 2
    else:
        bitset = data[1]
        whichtorps = data[2]
        coord_offset = 3

    coords = _unpack_9bit_coords(data, coord_offset, bitset)
    base_tnum = whichtorps * MAXTORP

    torps = []
    for i in range(8):
        if coords[i] is not None:
            dx, dy = coords[i]
            torps.append((base_tnum + i, dx, dy))
    return {"type": ptype, "whichtorps": whichtorps, "bitset": bitset, "torps": torps}


def decode_s_torp_info(data):
    """SP_S_TORP_INFO (48): variable — torp positions + status nibbles.

    Returns dict with 'torps' list of (tnum, dx, dy, war, status).
    The bitset at byte 1 controls which torps have position data.
    The infobitset at byte 3 controls which torps have status updates.
    """
    if len(data) < 4:
        return None
    bitset = data[1]
    whichtorps = data[2]
    infobitset = data[3]

    nset = _POPCOUNT[bitset]
    coords = _unpack_9bit_coords(data, 4, bitset)

    # Status data starts at vtisize[nset]
    info_offset = _VTISIZE[nset]
    base_tnum = whichtorps * MAXTORP

    torps = []
    info_pos = info_offset
    for i in range(8):
        dx, dy, war, status = None, None, 0, None
        if coords[i] is not None:
            dx, dy = coords[i]
        if infobitset & (1 << i):
            if info_pos < len(data):
                b = data[info_pos]
                war = b & 0x0F
                status = (b >> 4) & 0x0F
                info_pos += 1
        torps.append((base_tnum + i, dx, dy, war, status))
    return {"type": data[0], "whichtorps": whichtorps, "bitset": bitset, "torps": torps}


def decode_s_player(data, my_x=0, my_y=0):
    """SP_S_PLAYER (45): variable — compressed player positions.

    Three sub-formats:
    - Big header (bits 6,7 clear): 12-byte header with my dir/speed/x/y + flags,
      then 4-byte entries for other players
    - Short header (bit6 set): 4-byte header, optional flags, then 4-byte entries
    - Extended header (bit7 set): 4-byte header + 4-byte entries (pnum+32 range)

    my_x, my_y: current player position in game coords, needed for local-to-global
    coordinate conversion in short header packets (COW uses global my_x/my_y).

    Returns dict with 'players' list and optionally 'me' dict.
    """
    if len(data) < 2:
        return None
    b1 = data[1]

    if b1 & 0x80:
        return _decode_s_player_extended(data, b1)
    if b1 & 0x40:
        return _decode_s_player_short(data, b1, my_x, my_y)
    return _decode_s_player_big(data, b1)


def _decode_s_player_big(data, b1):
    """Big header (SP2 player_s2_spacket): 12-byte header + b1*4 other players.

    bytes 0:    type (SP_S_PLAYER)
    bytes 1:    num_others (low 6 bits)
    bytes 2:    my dir
    bytes 3:    my speed
    bytes 4-5:  my x (signed short, divide by SCALE already done by server)
    bytes 6-7:  my y (signed short, divide by SCALE already done by server)
    bytes 8-11: packed 2-bit flags for players 0-15 (NOT traditional pflags!)
                Same format as short header flag words / COW new_flags().
    """
    num_others = b1
    my_dir = data[2]
    my_speed = data[3]
    my_x = struct.unpack("!h", data[4:6])[0]  # signed short
    my_y = struct.unpack("!h", data[6:8])[0]  # signed short
    packed_flags = struct.unpack("!I", data[8:12])[0]

    my_game_x = SCALE * my_x
    my_game_y = SCALE * my_y

    me = {
        "dir": my_dir, "speed": my_speed,
        "x": my_game_x, "y": my_game_y,
    }

    # Decode packed 2-bit-per-player flags (same as short header / COW new_flags)
    flag_updates = []
    tmp = packed_flags
    for i in range(16):
        pnum = i
        if pnum >= MAXPLAYER:
            break
        code = tmp & 0x03
        tmp >>= 2
        flag_updates.append({"pnum": pnum, "code": code})

    players = _decode_player_entries(data, 12, num_others, my_game_x, my_game_y)
    return {"type": data[0], "me": me, "players": players,
            "flag_updates": flag_updates}


def _decode_s_player_short(data, b1, my_x=0, my_y=0):
    """Short header (SP2): 4 bytes + optional flags + player entries.

    SP2 flag encoding (COW short.c new_flags): each 32-bit word packs
    2 bits per player for 16 players.  The 2-bit codes are:
      0 = dead/exploding (clear cloak)
      1 = alive + cloaked
      2 = alive + shields (clear cloak)
      3 = alive + no shields/cloak
    byte 2 = number of 4-byte flag words (0, 1, or 2)
    byte 3 = index: which group of 16 players the first word covers

    my_x, my_y: current "me" position in game coords for local coordinate
    conversion (COW uses global my_x/my_y set by the big header path).
    """
    num_players = b1 & 0x3f
    num_flag_words = data[2]
    flag_index = data[3]  # 0 = players 0-15, 1 = players 16-31

    offset = 4
    # Decode packed 2-bit-per-player flag words (COW new_flags(word, which))
    # Each word packs 16 players * 2 bits.  `which` selects the group:
    # which=0 → players 0-15, which=1 → players 16-31.
    # First word uses flag_index, subsequent words increment from there.
    flag_updates = []  # list of {"pnum": int, "code": int} where code is 0-3
    for w in range(num_flag_words):
        if offset + 4 > len(data):
            break
        word = struct.unpack("!I", data[offset:offset + 4])[0]
        which = flag_index + w
        base_pnum = which * 16
        tmp = word
        for i in range(16):
            pnum = base_pnum + i
            if pnum >= MAXPLAYER:
                break
            code = tmp & 0x03
            tmp >>= 2
            flag_updates.append({"pnum": pnum, "code": code})
        offset += 4

    players = _decode_player_entries(data, offset, num_players, my_x, my_y)
    return {"type": data[0], "me": None, "players": players,
            "flag_updates": flag_updates}


def _decode_s_player_extended(data, b1):
    """Extended header: pnum+32 range (rarely used on small servers)."""
    num_players = b1 & 0x3f
    players = _decode_player_entries(data, 4, num_players, 0, 0,
                                     pnum_offset=32)
    return {"type": data[0], "me": None, "players": players}


def _decode_player_entries(data, offset, count, my_x, my_y, pnum_offset=0):
    """Decode 4-byte player position entries.

    Each entry: byte0 bits[0:5]=pnum, bit5=galactic, bit6=x_high, bit7=y_high
                byte1 bits[0:4]=speed, bits[4:8]=dir_nibble
                byte2=x_low, byte3=y_low

    my_x, my_y are the current "me" position in game coords, used for
    converting local (tactical-window-relative) coordinates to global.
    """
    players = []
    for _ in range(count):
        if offset + 4 > len(data):
            break
        b0 = data[offset]
        b1 = data[offset + 1]
        x_low = data[offset + 2]
        y_low = data[offset + 3]
        offset += 4

        pnum = (b0 & 0x1F) + pnum_offset
        galactic = bool(b0 & 0x20)
        x_high = (b0 >> 6) & 1
        y_high = (b0 >> 7) & 1
        speed = b1 & 0x0F
        dir_val = (b1 >> 4) * 16  # 4-bit direction * 16

        x = x_low | (x_high << 8)
        y = y_low | (y_high << 8)

        if galactic:
            game_x = x * GWIDTH // SPWINSIDE
            game_y = y * GWIDTH // SPWINSIDE
        else:
            # Local coords relative to my position
            game_x = my_x + (x - SPWINSIDE // 2) * SCALE
            game_y = my_y + (y - SPWINSIDE // 2) * SCALE

        players.append({
            "pnum": pnum, "dir": dir_val, "speed": speed,
            "x": game_x, "y": game_y,
        })
    return players


def decode_s_planet(data):
    """SP_S_PLANET (50): variable — compressed planet updates.

    byte 0: type, byte 1: count
    Then count * 6-byte entries: pnum, owner, info, armies(u8), flags(u16 BE)
    """
    if len(data) < 2:
        return None
    count = data[1]
    planets = []
    offset = 2
    for _ in range(count):
        if offset + 6 > len(data):
            break
        pnum = data[offset]
        owner = data[offset + 1]
        info = data[offset + 2]
        armies = data[offset + 3]
        flags = struct.unpack("!H", data[offset + 4:offset + 6])[0]
        planets.append({
            "pnum": pnum, "owner": owner, "info": info,
            "armies": armies, "flags": flags,
        })
        offset += 6
    return {"type": data[0], "planets": planets}


def decode_s_phaser(data):
    """SP_S_PHASER (57): variable (4, 8, or 12 bytes) — compressed phaser.

    byte 0: type, byte 1: status (low 4 bits), byte 2: pnum (low 6 bits)
    """
    if len(data) < 4:
        return None
    status = data[1] & 0x0F
    pnum = data[2] & 0x3F

    result = {"type": data[0], "pnum": pnum, "status": status,
              "dir": 0, "x": 0, "y": 0, "target": 0}

    if status == PHFREE:
        return result
    if status == PHMISS:
        result["dir"] = data[3]  # direction for miss
        return result
    if status == PHHIT:
        result["target"] = data[3] & 0x3F
        return result
    if status == PHHIT2:
        result["target"] = data[3] & 0x3F
        x, y = struct.unpack("!hh", data[4:8])
        result["x"] = x * SCALE
        result["y"] = y * SCALE
        return result
    # Default: full 12-byte format
    if len(data) >= 12:
        result["target"] = data[3] & 0x3F
        result["dir"] = data[4]
        x, y = struct.unpack("!hh", data[6:10])
        result["x"] = x * SCALE
        result["y"] = y * SCALE
    return result


def decode_s_kills(data):
    """SP_S_KILLS (58): variable — compressed kill counts.

    byte 0: type, byte 1: count
    Then count * 2-byte entries:
      byte 0 = kills_low (8 bits)
      byte 1 = bits[0:1]=kills_high (bits 8-9), bits[2:7]=pnum
    kills = 10-bit value / 100.0 stored as kills*100 integer
    """
    if len(data) < 2:
        return None
    count = data[1]
    kills = []
    offset = 2
    for _ in range(count):
        if offset + 2 > len(data):
            break
        kills_low = data[offset]
        b = data[offset + 1]
        kills_high = b & 0x03
        pnum = (b >> 2) & 0x3F
        kills_val = kills_low | (kills_high << 8)  # 10-bit * 100
        kills.append({"pnum": pnum, "kills": kills_val})
        offset += 2
    return {"type": data[0], "kills": kills}


def decode_s_sequence(data):
    """SP_S_SEQUENCE (56): 4 bytes — short packet sequence number."""
    seq = struct.unpack("!H", data[2:4])[0]
    return {"type": data[0], "sequence": seq}
