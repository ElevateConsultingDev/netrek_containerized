"""Distress/RCD system — makedistress() expansion engine.

Ported from COW distress.c / data.c.  Provides:
  - Distress dataclass: snapshot of sender state
  - load_distress(gs): fill from current gamestate
  - makedistress(dist, template, gs): 3-pass %-variable expansion
  - decode_rcd(mesg_bytes, m_from, gs): decode incoming RCD binary message
  - emergency(gs, conn, dist_type): high-level send-distress-to-team
"""
from dataclasses import dataclass
from .constants import (
    MAXPLAYER, MAXPLANETS, PALIVE, PFREE,
    FED, ROM, KLI, ORI, NOBODY,
    MTEAM, ADDRLEN, PFWEP, PFENG, PFCLOAK,
    TEAM_NAMES, TEAMLET, SHIPNOS, SHIP_NAMES,
)
from .protocol import cp_message


@dataclass
class Distress:
    sender: int = 0
    dam: int = 0
    shld: int = 0
    arms: int = 0
    fuel: int = 0
    wtmp: int = 0
    etmp: int = 0
    wtmpflag: int = 0
    etmpflag: int = 0
    close_pl: int = -1
    close_en: int = -1
    tclose_pl: int = -1
    tclose_en: int = -1
    close_fr: int = -1
    tclose_fr: int = -1


# COW data.c dist_defaults[] — default distress templates
DIST_TEMPLATES = {
    "take":         "%T%c->%O (%S) Carrying %a to %l%?%n>4%{ @ %n%}",
    "ogg":          "%T%c->%O Help Ogg %p at %l",
    "bomb":         "%T%c->%O %?%n>4%{bomb %l @ %n%!bomb %l%}",
    "control":      "%T%c->%O %l taken by %T",
    "escorting":    "%T%c->%O Escorting %g to %l",
    "ogging":       "%T%c->%O Ogging %h",
    "bombing":      "%T%c->%O Bombing %l%?%n>4%{ @ %n%}",
    "controlling":  "%T%c->%O Controlling %l%?%n>4%{ @ %n%}",
    "free_beer":    "%T%c->%O %l is free beer (%n armies)",
    "no_gas":       "%T%c->%O %d%% dam, %s%% shd, %f%% fuel%?%W>0%{ Wtemp%!%?%E>0%{ Etemp%}%}",
    "crippled":     "%T%c->%O Crippled (%d%% dam, %s%% shd, %f%% fuel)",
    "pickup":       "%T%c->%O Picking up %a at %l",
    "save_planet":  "%T%c->%O SAVE %l!! (%n armies)",
    "base_ogg":     "%T%c->%O Help! Ogg at %l!",
    "help1":        "%T%c->%O Help at %l!",
    "help2":        "%T%c->%O Need help at %l!",
    "asw":          "%T%c->%O Armies at %l%?%n>0%{ (%n)%}",
    "asbomb":       "%T%c->%O %l under attack!",
    "doing1":       "%T%c->%O Heading for %l%?%n>4%{ (%n armies)%}",
    "doing2":       "%T%c->%O Bombing %l",
    "pop":          "%T%c->%O %l%?%n>4%{ (%n armies)%}",
}


def nearest_planet(gs, x, y):
    """Find nearest planet to (x, y). Returns planet pnum or -1."""
    best_dist = float('inf')
    best = -1
    for pl in gs.planets:
        if not pl.name:
            continue
        d = (pl.x - x) ** 2 + (pl.y - y) ** 2
        if d < best_dist:
            best_dist = d
            best = pl.pnum
    return best


def nearest_enemy(gs, me_pnum, me_team, x, y):
    """Find nearest enemy player to (x, y). Returns player pnum or -1."""
    best_dist = float('inf')
    best = -1
    for p in gs.players:
        if p.pnum == me_pnum:
            continue
        if p.status != PALIVE:
            continue
        if p.team == me_team:
            continue
        d = (p.x - x) ** 2 + (p.y - y) ** 2
        if d < best_dist:
            best_dist = d
            best = p.pnum
    return best


def nearest_friendly(gs, me_pnum, me_team, x, y):
    """Find nearest friendly player to (x, y). Returns player pnum or -1."""
    best_dist = float('inf')
    best = -1
    for p in gs.players:
        if p.pnum == me_pnum:
            continue
        if p.status != PALIVE:
            continue
        if p.team != me_team:
            continue
        d = (p.x - x) ** 2 + (p.y - y) ** 2
        if d < best_dist:
            best_dist = d
            best = p.pnum
    return best


def load_distress(gs):
    """Fill a Distress struct from current gamestate (COW loaddistress)."""
    dist = Distress()
    me = gs.me
    if not me:
        return dist

    dist.sender = gs.me_pnum
    cap = gs.ship_cap

    # Compute percentages (COW uses 100 * val / max)
    if cap.s_maxdamage > 0:
        dist.dam = 100 * me.damage // cap.s_maxdamage
    if cap.s_maxshield > 0:
        dist.shld = 100 * me.shield // cap.s_maxshield
    if cap.s_maxfuel > 0:
        dist.fuel = 100 * me.fuel // cap.s_maxfuel
    if cap.s_maxwpntemp > 0:
        dist.wtmp = 100 * me.wtemp // cap.s_maxwpntemp
    if cap.s_maxegntemp > 0:
        dist.etmp = 100 * me.etemp // cap.s_maxegntemp

    dist.arms = me.armies
    dist.wtmpflag = 1 if dist.wtmp > 80 else 0
    dist.etmpflag = 1 if dist.etmp > 80 else 0

    # Nearest objects to me
    dist.close_pl = nearest_planet(gs, me.x, me.y)
    dist.close_en = nearest_enemy(gs, me.pnum, me.team, me.x, me.y)
    dist.close_fr = nearest_friendly(gs, me.pnum, me.team, me.x, me.y)

    # Target nearest objects (same as sender for self-distress)
    dist.tclose_pl = dist.close_pl
    dist.tclose_en = dist.close_en
    dist.tclose_fr = dist.close_fr

    return dist


def _pnum_char(pnum):
    """Convert player number to display character (0-9, a-z)."""
    if 0 <= pnum < len(SHIPNOS):
        return SHIPNOS[pnum]
    return '?'


def _team_letter(gs, pnum):
    """Get team letter for a player."""
    if 0 <= pnum < MAXPLAYER:
        return TEAMLET.get(gs.players[pnum].team, 'I')
    return 'I'


def _planet_name(gs, pl_pnum):
    """Get planet name by pnum."""
    if 0 <= pl_pnum < MAXPLANETS:
        return gs.planets[pl_pnum].name or "???"
    return "???"


def _planet_armies(gs, pl_pnum):
    """Get planet army count."""
    if 0 <= pl_pnum < MAXPLANETS:
        return gs.planets[pl_pnum].armies
    return 0


def makedistress(dist, template, gs):
    """Three-pass expansion of a distress template (COW distress.c makedistress).

    Pass 1: Variable substitution (%X codes)
    Pass 2: Test evaluation (%?expr)
    Pass 3: Conditional blocks (%{true%!false%})
    """
    # Use the sender from the dist struct (matches COW: %T/%c/%S refer to sender)
    sender_pnum = dist.sender
    if 0 <= sender_pnum < MAXPLAYER:
        sender = gs.players[sender_pnum]
    else:
        sender = gs.me
    if not sender:
        return template

    my_team = sender.team
    my_team_letter = TEAMLET.get(my_team, 'I')
    my_team_name = TEAM_NAMES.get(my_team, "Ind")
    my_char = _pnum_char(sender_pnum)
    my_ship = SHIP_NAMES.get(sender.shiptype, "??")

    # Pass 1: variable substitution
    result = []
    i = 0
    while i < len(template):
        if template[i] == '%' and i + 1 < len(template):
            code = template[i + 1]
            i += 2
            val = _expand_var(code, dist, gs, sender, my_team, my_team_letter,
                              my_team_name, my_char, my_ship)
            if val is not None:
                result.append(val)
            else:
                # Not a variable — keep as-is for pass 2/3
                result.append('%')
                result.append(code)
        else:
            result.append(template[i])
            i += 1

    expanded = ''.join(result)

    # Pass 2 & 3: evaluate conditionals
    expanded = _eval_conditionals(expanded)

    return expanded


def _expand_var(code, dist, gs, me, my_team, my_team_letter,
                my_team_name, my_char, my_ship):
    """Expand a single %X variable code. Returns string or None if not a var."""
    if code == 'a':
        return str(dist.arms)
    elif code == 'd':
        return str(dist.dam)
    elif code == 's':
        return str(dist.shld)
    elif code == 'f':
        return str(dist.fuel)
    elif code == 'w':
        return str(dist.wtmp)
    elif code == 'e':
        return str(dist.etmp)
    elif code == 'W':
        return str(dist.wtmpflag)
    elif code == 'E':
        return str(dist.etmpflag)
    elif code == 'T':
        return my_team_letter
    elif code == 'c':
        return my_char
    elif code == 'S':
        return my_ship
    elif code == 'l':
        # Nearest planet name (short — 3 chars)
        name = _planet_name(gs, dist.close_pl)
        return name[:3] if len(name) > 3 else name
    elif code == 'L':
        # Nearest planet name (full)
        return _planet_name(gs, dist.close_pl)
    elif code == 'b':
        # Target's nearest planet (short)
        name = _planet_name(gs, dist.tclose_pl)
        return name[:3] if len(name) > 3 else name
    elif code == 'B':
        # Target's nearest planet (full)
        return _planet_name(gs, dist.tclose_pl)
    elif code == 'n':
        # Planet armies at nearest planet
        return str(_planet_armies(gs, dist.close_pl))
    elif code == 'p':
        # Nearest enemy player char
        if dist.close_en >= 0:
            tl = _team_letter(gs, dist.close_en)
            return tl + _pnum_char(dist.close_en)
        return "??"
    elif code == 'h':
        # Same as %p (COW compatibility)
        if dist.close_en >= 0:
            tl = _team_letter(gs, dist.close_en)
            return tl + _pnum_char(dist.close_en)
        return "??"
    elif code == 'g':
        # Nearest friendly player char
        if dist.close_fr >= 0:
            tl = _team_letter(gs, dist.close_fr)
            return tl + _pnum_char(dist.close_fr)
        return "??"
    elif code == 'o':
        # Target's team name (short)
        if dist.close_en >= 0 and 0 <= dist.close_en < MAXPLAYER:
            eteam = gs.players[dist.close_en].team
            return TEAM_NAMES.get(eteam, "Ind")[:3]
        return "???"
    elif code == 'O':
        # My team name
        return my_team_name
    elif code == 'i':
        # Sender name (short)
        if 0 <= dist.sender < MAXPLAYER:
            return gs.players[dist.sender].name[:8]
        return "?"
    elif code == 'I':
        # Sender name (full)
        if 0 <= dist.sender < MAXPLAYER:
            return gs.players[dist.sender].name
        return "?"
    elif code == '%':
        return '%'
    # Not a recognized variable — pass 2/3 markers
    elif code in '?{!}':
        return None
    return None


def _eval_conditionals(text):
    """Evaluate %?test%{true%!false%} conditionals (COW distress.c pass 2+3).

    Simplified: handles %?X>N, %?X<N, %?X=N where X is a digit sequence
    following the ?, and N is also a digit sequence.
    """
    result = []
    i = 0
    while i < len(text):
        if text[i] == '%' and i + 1 < len(text) and text[i + 1] == '?':
            # Parse test expression
            i += 2  # skip %?
            test_result, i = _parse_test(text, i)
            # Now expect %{ ... %}  with optional %! for else
            true_str, false_str, i = _parse_conditional_block(text, i)
            # Recursively evaluate nested conditionals in the chosen branch
            if test_result:
                result.append(_eval_conditionals(true_str))
            else:
                result.append(_eval_conditionals(false_str))
        else:
            result.append(text[i])
            i += 1
    return ''.join(result)


def _parse_test(text, pos):
    """Parse a test expression like 'X>N' starting at pos. Returns (bool, new_pos)."""
    # Read left operand (digits or minus)
    left = []
    while pos < len(text) and (text[pos].isdigit() or text[pos] == '-'):
        left.append(text[pos])
        pos += 1

    if not left or pos >= len(text):
        return False, pos

    op = text[pos]
    pos += 1

    # Read right operand
    right = []
    while pos < len(text) and (text[pos].isdigit() or text[pos] == '-'):
        right.append(text[pos])
        pos += 1

    if not right:
        return False, pos

    try:
        lval = int(''.join(left))
        rval = int(''.join(right))
    except ValueError:
        return False, pos

    if op == '>':
        return lval > rval, pos
    elif op == '<':
        return lval < rval, pos
    elif op == '=':
        return lval == rval, pos
    return False, pos


def _parse_conditional_block(text, pos):
    """Parse %{true%!false%} block. Returns (true_str, false_str, new_pos)."""
    # Expect %{
    if pos + 1 < len(text) and text[pos] == '%' and text[pos + 1] == '{':
        pos += 2
    else:
        return '', '', pos

    # Read true branch until %! or %}
    true_parts = []
    depth = 0
    while pos < len(text):
        if text[pos] == '%' and pos + 1 < len(text):
            if text[pos + 1] == '{':
                depth += 1
                true_parts.append('%{')
                pos += 2
            elif text[pos + 1] == '}' and depth > 0:
                depth -= 1
                true_parts.append('%}')
                pos += 2
            elif text[pos + 1] == '!' and depth == 0:
                pos += 2
                break
            elif text[pos + 1] == '}' and depth == 0:
                pos += 2
                return ''.join(true_parts), '', pos
            else:
                true_parts.append(text[pos])
                pos += 1
        else:
            true_parts.append(text[pos])
            pos += 1

    true_str = ''.join(true_parts)

    # Read false branch until %}
    false_parts = []
    depth = 0
    while pos < len(text):
        if text[pos] == '%' and pos + 1 < len(text):
            if text[pos + 1] == '{':
                depth += 1
                false_parts.append('%{')
                pos += 2
            elif text[pos + 1] == '}' and depth > 0:
                depth -= 1
                false_parts.append('%}')
                pos += 2
            elif text[pos + 1] == '}' and depth == 0:
                pos += 2
                break
            else:
                false_parts.append(text[pos])
                pos += 1
        else:
            false_parts.append(text[pos])
            pos += 1

    return true_str, ''.join(false_parts), pos


# COW data.c dist_defaults[] — receive-side templates indexed by distype.
# Index 0 is a dummy ("no zero"); indices 1–23 match COW's RCD encoding.
_RCD_TEMPLATES = [
    "",                                                                      # 0: unused
    " %T%c->%O (%S) Carrying %a to %l%?%n>-1%{ @ %n%}",                    # 1: taking
    " %T%c->%O Help Ogg %p at %l",                                          # 2: ogg
    " %T%c->%O %?%n>4%{bomb %l @ %n%!bomb %l%}",                            # 3: bomb
    " %T%c->%O Help Control at %L",                                          # 4: space_control
    " %T%c->%O Emergency at %L!!!!",                                         # 5: save_planet
    " %T%c->%O Sync with --]> %g <[-- OGG ogg OGG base!!",                  # 6: base_ogg
    " %T%c->%O Help me! %d%% dam, %s%% shd, %f%% fuel %a armies.",          # 7: help1
    " %T%c->%O Help me! %d%% dam, %s%% shd, %f%% fuel %a armies.",          # 8: help2
    " %T%c->%O ESCORTING %g (%d%%D %s%%S %f%%F)",                           # 9: escorting
    " %T%c->%O Ogging %h",                                                  # 10: ogging
    " %T%c->%O Bombing %l @ %n",                                            # 11: bombing
    " %T%c->%O Controlling at %l",                                           # 12: controlling
    " %T%c->%O Anti-bombing %p near %b.",                                    # 13: asw
    " %T%c->%O DON'T BOMB %l. Let me bomb it (%S)",                         # 14: asbomb
    " %T%c->%O (%i)%?%a>0%{ has %a arm%?%a=1%{y%!ies%}%} at %l.  (%d%% dam, %s%% shd, %f%% fuel)",  # 15: doing1
    " %T%c->%O (%i)%?%a>0%{ has %a arm%?%a=1%{y%!ies%}%} at %l.  (%d%% dam, %s%% shd, %f%% fuel)",  # 16: doing2
    " %T%c->%O %p is free beer",                                            # 17: free_beer
    " %T%c->%O %p @ %l has no gas",                                         # 18: no_gas
    " %T%c->%O %p @ %l crippled",                                           # 19: crippled
    " %T%c->%O %p++ @ %l",                                                  # 20: pickup
    " %T%c->%O %l%?%n>-1%{ @ %n%}!",                                        # 21: pop
    " %T%c->%O %?%S=SB%{Your Starbase is c%!C%}arrying %?%a>0%{%a%!NO%} arm%?%a=1%{y%!ies%}.",  # 22: carrying
    " %T%c->%O (%i)%?%a>0%{ has %a arm%?%a=1%{y%!ies%}%} at %l. (%d%%D, %s%%S, %f%%F)",  # 23: other1
]


def decode_rcd(mesg, m_from, gs):
    """Decode an incoming MDISTR-flagged message into readable text.

    COW dmessage.c: HandleGenDistr() unpacks binary payload, then
    makedistress() expands it through the dist_defaults template.

    Args:
        mesg: the 80-byte message string (already decoded to str)
        m_from: sender player number
        gs: GameState

    Returns:
        Expanded text string, or None if decoding fails.
    """
    # The message text includes a 10-char address header ("O3->ORI  ")
    # followed by binary distress data.  Re-encode to get raw bytes.
    try:
        raw = mesg.encode('latin-1')
    except (UnicodeEncodeError, AttributeError):
        if isinstance(mesg, bytes):
            raw = mesg
        else:
            return None

    if len(raw) <= ADDRLEN:
        return None

    mtext = raw[ADDRLEN:]
    if len(mtext) < 16:
        return None

    dist = Distress()
    dist.sender = m_from
    dist.distype = mtext[0] & 0x1F
    dist.fuel = mtext[1] & 0x7F
    dist.dam = mtext[2] & 0x7F
    dist.shld = mtext[3] & 0x7F
    dist.etmp = mtext[4] & 0x7F
    dist.wtmp = mtext[5] & 0x7F
    dist.arms = mtext[6] & 0x1F
    sts = mtext[7] & 0x7F
    dist.wtmpflag = 1 if (sts & PFWEP) else 0
    dist.etmpflag = 1 if (sts & PFENG) else 0
    dist.close_pl = mtext[8] & 0x7F
    dist.close_en = mtext[9] & 0x7F
    dist.tclose_pl = mtext[10] & 0x7F
    dist.tclose_en = mtext[11] & 0x7F
    # mtext[12] = tclose_j (target closest player)
    # mtext[13] = close_j (closest player)
    dist.tclose_fr = mtext[14] & 0x7F
    dist.close_fr = mtext[15] & 0x7F

    # Pick the template for this distress type
    if dist.distype < 0 or dist.distype >= len(_RCD_TEMPLATES):
        return None
    template = _RCD_TEMPLATES[dist.distype]
    if not template:
        return None

    # Check for appended/prepended text (after CC list)
    extra = ""
    i = 0
    while i < 6 and (16 + i) < len(mtext):
        if (mtext[16 + i] & 0xC0) == 0xC0:
            i += 1
        else:
            break
    if (16 + i) < len(mtext):
        terminator = mtext[16 + i]
        pre_app = 1 if terminator == 0x80 else 0
        text_start = 16 + i + 1
        if text_start < len(mtext) and mtext[text_start] != 0:
            extra = mtext[text_start:].split(b'\x00', 1)[0].decode('ascii', errors='replace')

    expanded = makedistress(dist, template, gs)

    if extra:
        if pre_app:
            expanded = extra + expanded
        else:
            expanded = expanded + extra

    return expanded.strip()


def emergency(gs, conn, dist_type):
    """Send an emergency distress message to team (COW emergency).

    Args:
        gs: GameState
        conn: network connection (has .send())
        dist_type: template key from DIST_TEMPLATES (e.g. "take", "ogg")
    """
    template = DIST_TEMPLATES.get(dist_type)
    if not template:
        gs.warning = f"Unknown distress: {dist_type}"
        gs.warning_timer = 90
        return

    me = gs.me
    if not me:
        return

    dist = load_distress(gs)
    text = makedistress(dist, template, gs)

    # Send to own team (MTEAM)
    team_num = {FED: 0, ROM: 1, KLI: 2, ORI: 3}.get(me.team, 0)
    conn.send(cp_message(MTEAM, me.team, text))
