"""Mutable world state updated from server packets."""
import time
from dataclasses import dataclass, field
from .constants import *
from . import short_decode
from .distress import decode_rcd


@dataclass
class Player:
    pnum: int = 0
    status: int = PFREE
    team: int = NOBODY
    shiptype: int = CRUISER
    x: int = 0
    y: int = 0
    dir: int = 0
    speed: int = 0
    flags: int = 0
    kills: int = 0  # kills * 100
    hostile: int = 0
    swar: int = 0
    armies: int = 0
    tractor: int = 0
    damage: int = 0
    shield: int = 0
    fuel: int = 0
    etemp: int = 0
    wtemp: int = 0
    whydead: int = 0
    whodead: int = 0
    name: str = ""
    login: str = ""
    rank: int = 0
    war: int = 0
    # Stats (from SP_STATS / SP_S_STATS, matches COW struct stats)
    st_tkills: int = 0
    st_tlosses: int = 0
    st_kills: int = 0
    st_losses: int = 0
    st_tticks: int = 0
    st_tplanets: int = 0
    st_tarmsbomb: int = 0
    st_sbkills: int = 0
    st_sblosses: int = 0
    st_armsbomb: int = 0
    st_planets: int = 0
    st_maxkills: float = 0.0
    st_sbmaxkills: float = 0.0
    # Interpolation state
    prev_x: int = 0
    prev_y: int = 0
    prev_dir: int = 0
    render_x: float = 0.0
    render_y: float = 0.0
    render_dir: int = 0
    _update_time: float = 0.0
    # Cloak animation phase: 0=fully visible, 7=fully cloaked
    cloak_phase: int = 0


@dataclass
class Torp:
    status: int = TFREE
    x: int = 0
    y: int = 0
    dir: int = 0
    war: int = 0
    fuse: int = 0  # client-side explosion countdown
    # Interpolation state
    prev_x: int = 0
    prev_y: int = 0
    render_x: float = 0.0
    render_y: float = 0.0
    _update_time: float = 0.0


@dataclass
class Plasma:
    status: int = PTFREE
    x: int = 0
    y: int = 0
    war: int = 0
    fuse: int = 0  # client-side explosion countdown
    # Interpolation state
    prev_x: int = 0
    prev_y: int = 0
    render_x: float = 0.0
    render_y: float = 0.0
    _update_time: float = 0.0


@dataclass
class Phaser:
    status: int = PHFREE
    dir: int = 0
    x: int = 0
    y: int = 0
    target: int = 0
    fuse: int = 0  # countdown for display


@dataclass
class ShipCap:
    s_type: int = 0
    s_maxspeed: int = 12
    s_maxfuel: int = 10000
    s_maxshield: int = 100
    s_maxdamage: int = 100
    s_maxwpntemp: int = 1000
    s_maxegntemp: int = 1000
    s_maxarmies: int = 0
    s_phaserrange: int = 6000
    s_torpspeed: int = 12


@dataclass
class Planet:
    pnum: int = 0
    owner: int = NOBODY
    info: int = 0
    flags: int = 0
    armies: int = 0
    x: int = 0
    y: int = 0
    name: str = ""


class GameState:
    def __init__(self):
        self.players = [Player(pnum=i) for i in range(MAXPLAYER)]
        self.torps = [Torp() for _ in range(MAXPLAYER * MAXTORP)]
        self.plasmas = [Plasma() for _ in range(MAXPLAYER * MAXPLASMA)]
        self.phasers = [Phaser() for _ in range(MAXPLAYER)]
        self.planets = [Planet(pnum=i) for i in range(MAXPLANETS)]
        self.conn = None   # set by main.py after construction
        self.me_pnum = -1  # my player number
        self.team_mask = 0  # available teams from SP_MASK
        self.motd_lines = []
        self.messages = []  # recent messages: (text, m_flags, m_from) tuples
        self.warning = ""
        self.warning_timer = 0
        self.features = {}  # negotiated features from SP_FEATURE
        self.login_accept = None  # None until SP_LOGIN received, then True/False
        self.ship_caps = [ShipCap(s_type=i) for i in range(NUM_TYPES)]
        # Galaxy status from SP_STATUS (used for rating calculations)
        self.status_tourn = 0
        self.status_armsbomb = 1  # init to 1 to avoid div-by-zero (COW enter.c:59)
        self.status_planets = 1
        self.status_kills = 1
        self.status_losses = 1
        self.status_time = 0
        self.status_timeprod = 1
        # Short packets state
        self._short_winside = 0  # set by SP_S_REPLY
        self._short_gwidth = 0
        # Client-side lock targets (set when we send lock requests)
        self.lock_planet = -1   # planet pnum we locked onto
        self.lock_player = -1   # player pnum we locked onto
        # Ping stats (SP_PING)
        self.ping_lag = 0       # round-trip lag in ms
        self.ping_tloss_sc = 0  # total loss server->client %
        self.ping_tloss_cs = 0  # total loss client->server %
        self.ping_iloss_sc = 0  # interval loss server->client %
        self.ping_iloss_cs = 0  # interval loss client->server %
        # Queue position (SP_QUEUE)
        self.queue_pos = -1     # -1 = not in queue

    def reset(self):
        """Reset all transient state for reconnection. Preserves object identity."""
        for i, p in enumerate(self.players):
            self.players[i] = Player(pnum=i)
        for i in range(len(self.torps)):
            self.torps[i] = Torp()
        for i in range(len(self.plasmas)):
            self.plasmas[i] = Plasma()
        for i in range(len(self.phasers)):
            self.phasers[i] = Phaser()
        # Keep planet names/positions — server will resend them
        self.me_pnum = -1
        self.team_mask = 0
        self.motd_lines.clear()
        self.messages.clear()
        self.warning = ""
        self.warning_timer = 0
        self.features.clear()
        self.login_accept = None
        self.lock_planet = -1
        self.lock_player = -1
        self.ping_lag = 0
        self.ping_tloss_sc = 0
        self.ping_tloss_cs = 0
        self.ping_iloss_sc = 0
        self.ping_iloss_cs = 0
        self.queue_pos = -1
        self._short_winside = 0
        self._short_gwidth = 0

    @property
    def me(self):
        if 0 <= self.me_pnum < MAXPLAYER:
            return self.players[self.me_pnum]
        return None

    @property
    def ship_cap(self):
        """ShipCap for my current ship type."""
        me = self.me
        if me and 0 <= me.shiptype < NUM_TYPES:
            return self.ship_caps[me.shiptype]
        return self.ship_caps[0]

    def team_counts(self):
        """Count active players per team. Returns {FED: n, ROM: n, KLI: n, ORI: n}."""
        counts = {FED: 0, ROM: 0, KLI: 0, ORI: 0}
        for p in self.players:
            if p.status != PFREE and p.team in counts:
                counts[p.team] += 1
        return counts

    def interpolate(self):
        """Compute render positions by lerping between prev and current.

        Called once per frame before rendering.  Server updates arrive at
        UPDATE_RATE (10/sec = 100ms apart).  We blend from prev toward
        current over that interval so movement looks smooth at 30fps.
        """
        now = time.monotonic()
        interval = 1.0 / UPDATE_RATE  # 0.1 seconds

        for p in self.players:
            if p.status not in (PALIVE, PEXPLODE) or p._update_time == 0.0:
                p.render_x = float(p.x)
                p.render_y = float(p.y)
                p.render_dir = p.dir
                continue
            # Don't interpolate local player -- snap to server position
            # so input feels immediate (viewport centers on render_x/y).
            if p.pnum == self.me_pnum:
                p.render_x = float(p.x)
                p.render_y = float(p.y)
                p.render_dir = p.dir
                continue
            t = min((now - p._update_time) / interval, 1.0)
            p.render_x = p.prev_x + (p.x - p.prev_x) * t
            p.render_y = p.prev_y + (p.y - p.prev_y) * t
            # Direction: shortest arc on 0-255 circle
            dd = (p.dir - p.prev_dir + 128) % 256 - 128
            p.render_dir = int(p.prev_dir + dd * t) & 0xFF

        for t in self.torps:
            if t.status == TFREE or t._update_time == 0.0:
                t.render_x = float(t.x)
                t.render_y = float(t.y)
                continue
            frac = min((now - t._update_time) / interval, 1.0)
            t.render_x = t.prev_x + (t.x - t.prev_x) * frac
            t.render_y = t.prev_y + (t.y - t.prev_y) * frac

        for pl in self.plasmas:
            if pl.status == PTFREE or pl._update_time == 0.0:
                pl.render_x = float(pl.x)
                pl.render_y = float(pl.y)
                continue
            frac = min((now - pl._update_time) / interval, 1.0)
            pl.render_x = pl.prev_x + (pl.x - pl.prev_x) * frac
            pl.render_y = pl.prev_y + (pl.y - pl.prev_y) * frac

    def handle_packet(self, ptype, pkt):
        handler = self._handlers.get(ptype)
        if handler:
            handler(self, pkt)

    def _handle_sp_player_info(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        shiptype = pkt["shiptype"]
        if 0 <= shiptype < NUM_TYPES:
            p.shiptype = shiptype
        # Don't overwrite a known team with NOBODY for alive players —
        # the server briefly sends team=0 during slot transitions and the
        # SP_S_PLAYER flag update can set PALIVE before the real team arrives.
        new_team = pkt["team"]
        if new_team not in (NOBODY, FED, ROM, KLI, ORI):
            return
        if new_team != NOBODY or p.status not in (PALIVE, PEXPLODE):
            p.team = new_team

    def _handle_sp_kills(self, pkt):
        pnum = pkt["pnum"]
        if 0 <= pnum < MAXPLAYER:
            self.players[pnum].kills = pkt["kills"]

    def _handle_sp_player(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        p.prev_x = p.x
        p.prev_y = p.y
        p.prev_dir = p.dir
        p._update_time = time.monotonic()
        p.dir = pkt["dir"]
        p.speed = pkt["speed"]
        p.x = pkt["x"]
        p.y = pkt["y"]

    def _handle_sp_torp_info(self, pkt):
        tnum = pkt["tnum"]
        if 0 <= tnum < len(self.torps):
            t = self.torps[tnum]
            new_status = pkt["status"]
            if new_status != t.status and new_status in (TEXPLODE, TDET):
                t.fuse = 10  # NUMDETFRAMES(5) * server_ups(10) / 5
            t.status = new_status
            t.war = pkt["war"]

    def _handle_sp_torp(self, pkt):
        tnum = pkt["tnum"]
        if 0 <= tnum < len(self.torps):
            t = self.torps[tnum]
            t.prev_x = t.x
            t.prev_y = t.y
            t._update_time = time.monotonic()
            t.dir = pkt["dir"]
            t.x = pkt["x"]
            t.y = pkt["y"]

    def _handle_sp_phaser(self, pkt):
        pnum = pkt["pnum"]
        if 0 <= pnum < MAXPLAYER:
            ph = self.phasers[pnum]
            ph.status = pkt["status"]
            ph.dir = pkt["dir"]
            ph.x = pkt["x"]
            ph.y = pkt["y"]
            ph.target = pkt["target"]
            if ph.status != PHFREE:
                ph.fuse = 10  # display for ~10 frames

    def _handle_sp_plasma_info(self, pkt):
        pnum = pkt["pnum"]
        if 0 <= pnum < len(self.plasmas):
            pl = self.plasmas[pnum]
            new_status = pkt["status"]
            if new_status != pl.status and new_status in (PTEXPLODE, PTDET):
                pl.fuse = 10
            pl.status = new_status
            pl.war = pkt["war"]

    def _handle_sp_plasma(self, pkt):
        pnum = pkt["pnum"]
        if 0 <= pnum < len(self.plasmas):
            pl = self.plasmas[pnum]
            pl.prev_x = pl.x
            pl.prev_y = pl.y
            pl._update_time = time.monotonic()
            pl.x = pkt["x"]
            pl.y = pkt["y"]

    def _handle_sp_you(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        if self.me_pnum < 0:
            self.me_pnum = pnum
        p = self.players[pnum]
        p.hostile = pkt["hostile"]
        p.swar = pkt["swar"]
        p.armies = pkt["armies"]
        tractor_raw = pkt["tractor"]
        if tractor_raw & 0x40:
            p.tractor = tractor_raw & ~0x40
        p.flags = pkt["flags"]
        p.damage = pkt["damage"]
        p.shield = pkt["shield"]
        p.fuel = pkt["fuel"]
        p.etemp = pkt["etemp"]
        p.wtemp = pkt["wtemp"]
        p.whydead = pkt["whydead"]
        p.whodead = pkt["whodead"]

    def _handle_sp_queue(self, pkt):
        self.queue_pos = pkt["pos"]

    def _handle_sp_status(self, pkt):
        self.status_tourn = pkt["tourn"]
        self.status_armsbomb = max(1, pkt["armsbomb"])
        self.status_planets = max(1, pkt["planets"])
        self.status_kills = max(1, pkt["kills"])
        self.status_losses = max(1, pkt["losses"])
        self.status_time = pkt["time"]
        self.status_timeprod = max(1, pkt["timeprod"])

    def _handle_sp_planet(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLANETS):
            return
        pl = self.planets[pnum]
        pl.owner = pkt["owner"]
        pl.info = pkt["info"]
        pl.flags = pkt["flags"]
        pl.armies = pkt["armies"]

    def _handle_sp_planet_loc(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLANETS):
            return
        pl = self.planets[pnum]
        pl.x = pkt["x"]
        pl.y = pkt["y"]
        pl.name = pkt["name"]

    def _handle_sp_pickok(self, pkt):
        pass  # handled by state machine

    def _handle_sp_login(self, pkt):
        self.login_accept = bool(pkt.get("accept", 0))

    def _handle_sp_flags(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        p.flags = pkt["flags"]
        # COW: tractor field bit 0x40 means active, lower bits = target pnum
        tractor_raw = pkt["tractor"]
        if tractor_raw & 0x40:
            p.tractor = tractor_raw & ~0x40

    def _handle_sp_mask(self, pkt):
        self.team_mask = pkt["mask"]

    def _handle_sp_pstatus(self, pkt):
        pnum = pkt["pnum"]
        if 0 <= pnum < MAXPLAYER:
            self.players[pnum].status = pkt["status"]

    def _handle_sp_hostile(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        p.war = pkt["war"]
        p.hostile = pkt["hostile"]

    def _handle_sp_stats(self, pkt):
        """Store player stats (COW socket.c handleStats)."""
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        p.st_tkills = pkt["tkills"]
        p.st_tlosses = pkt["tlosses"]
        p.st_kills = pkt["kills"]
        p.st_losses = pkt["losses"]
        p.st_tticks = pkt["tticks"]
        p.st_tplanets = pkt["tplanets"]
        p.st_tarmsbomb = pkt["tarmies"]
        p.st_sbkills = pkt["sbkills"]
        p.st_sblosses = pkt["sblosses"]
        p.st_armsbomb = pkt["armies"]
        p.st_planets = pkt["planets"]
        p.st_maxkills = pkt["maxkills"] / 100.0
        p.st_sbmaxkills = pkt["sbmaxkills"] / 100.0

    def _handle_sp_pl_login(self, pkt):
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        p.name = pkt["name"]
        p.login = pkt["login"]
        p.rank = pkt["rank"]

    def _handle_sp_reserved(self, pkt):
        pass  # respond with cp_reserved if needed

    def _handle_sp_motd(self, pkt):
        self.motd_lines.append(pkt["line"])

    def _handle_sp_warning(self, pkt):
        self.warning = pkt["mesg"]
        self.warning_timer = 90  # display for ~3 seconds at 30fps

    def _handle_sp_message(self, pkt):
        mesg = pkt["mesg"]
        flags = pkt["m_flags"]
        m_from = pkt["m_from"]

        # Decode RCD (binary distress) messages (COW dmessage.c)
        if flags == (MTEAM | MDISTR | MVALID):
            decoded = decode_rcd(mesg, m_from, self)
            if decoded:
                mesg = decoded
                flags ^= MDISTR  # strip MDISTR, keep MTEAM|MVALID

        self.messages.append((mesg, flags, m_from))
        if len(self.messages) > 200:
            self.messages.pop(0)

    def _handle_sp_ping(self, pkt):
        self.ping_lag = pkt["lag"]
        self.ping_tloss_sc = pkt["tloss_sc"]
        self.ping_tloss_cs = pkt["tloss_cs"]
        self.ping_iloss_sc = pkt["iloss_sc"]
        self.ping_iloss_cs = pkt["iloss_cs"]

    def _handle_sp_feature(self, pkt):
        name = pkt["name"].strip('\x00')
        self.features[name] = pkt["value"]

    def _handle_sp_badversion(self, pkt):
        print(f"SP_BADVERSION: why={pkt['why']}")

    def _handle_sp_ship_cap(self, pkt):
        stype = pkt["s_type"]
        if not (0 <= stype < NUM_TYPES):
            return
        sc = self.ship_caps[stype]
        sc.s_type = stype
        sc.s_maxspeed = pkt["s_maxspeed"]
        sc.s_maxfuel = pkt["s_maxfuel"]
        sc.s_maxshield = pkt["s_maxshield"]
        sc.s_maxdamage = pkt["s_maxdamage"]
        sc.s_maxwpntemp = pkt["s_maxwpntemp"]
        sc.s_maxegntemp = pkt["s_maxegntemp"]
        sc.s_maxarmies = pkt["s_maxarmies"]
        sc.s_phaserrange = pkt["s_phaserrange"]
        sc.s_torpspeed = pkt["s_torpspeed"]

    def _handle_sp_rsa_key(self, pkt):
        pass  # RSA key challenge not implemented

    def _handle_sp_motd_pic(self, pkt):
        pass  # MOTD bitmap pictures not rendered yet

    def _handle_sp_scan(self, pkt):
        pass  # scan data not used yet

    def _handle_sp_stats2(self, pkt):
        pass  # paradise extended stats not used yet

    def _handle_sp_status2(self, pkt):
        pass  # paradise galaxy status not used yet

    def _handle_sp_planet2(self, pkt):
        """SP_PLANET2: Paradise planet update (replaces SP_PLANET)."""
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLANETS):
            return
        pl = self.planets[pnum]
        pl.owner = pkt["owner"]
        pl.info = pkt["info"]
        pl.flags = pkt["flags"]
        pl.armies = pkt["armies"]

    def _handle_sp_new_motd(self, pkt):
        pass

    def _handle_sp_thingy(self, pkt):
        pass  # drones/thingies not rendered yet

    def _handle_sp_thingy_info(self, pkt):
        pass  # drones/thingies not rendered yet

    def _handle_sp_terrain2(self, pkt):
        pass  # terrain not rendered yet

    def _handle_sp_terrain_info2(self, pkt):
        pass  # terrain not rendered yet

    def _handle_sp_udp_reply(self, pkt):
        if self.conn:
            self.conn.handle_udp_reply(pkt)

    def _handle_sp_sequence(self, pkt):
        pass

    def _handle_sp_sc_sequence(self, pkt):
        pass

    # ------------------------------------------------------------------
    # Short packet handlers (SP_S_*)
    # These receive raw bytes (not dicts) from network.py.
    # ------------------------------------------------------------------

    def _handle_sp_s_reply(self, raw):
        pkt = short_decode.decode_s_reply(raw)
        if pkt is None:
            return
        repl = pkt["repl"]
        if repl == SPK_VON:
            self._short_winside = pkt["winside"]
            self._short_gwidth = pkt["gwidth"]
            print(f"Short packets: active (WINSIDE={pkt['winside']}, GWIDTH={pkt['gwidth']})")
        else:
            print(f"SP_S_REPLY: repl={repl}")

    def _handle_sp_s_you(self, raw):
        pkt = short_decode.decode_s_you(raw)
        if pkt is None:
            return
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        if self.me_pnum < 0:
            self.me_pnum = pnum
        p = self.players[pnum]
        p.hostile = pkt["hostile"]
        p.swar = pkt["swar"]
        p.armies = pkt["armies"]
        p.flags = pkt["flags"]
        p.whydead = pkt["whydead"]
        p.whodead = pkt["whodead"]

    def _handle_sp_s_you_ss(self, raw):
        pkt = short_decode.decode_s_you_ss(raw)
        if pkt is None:
            return
        me = self.me
        if me:
            me.damage = pkt["damage"]
            me.shield = pkt["shield"]
            me.fuel = pkt["fuel"]
            me.etemp = pkt["etemp"]
            me.wtemp = pkt["wtemp"]

    def _handle_sp_s_torp(self, raw):
        pkt = short_decode.decode_s_torp(raw)
        if pkt is None:
            return
        me = self.me
        if not me:
            return
        now = time.monotonic()
        bitset = pkt["bitset"]
        base = pkt["whichtorps"] * MAXTORP

        # Build lookup of decoded coords by tnum
        coord_map = {tn: (dx, dy) for tn, dx, dy in pkt["torps"]}

        # COW handleVTorp: iterate all 8 torp slots for this player.
        # Bit set = alive (update position), bit clear = TFREE.
        for i in range(8):
            tnum = base + i
            if not (0 <= tnum < len(self.torps)):
                continue
            t = self.torps[tnum]
            if bitset & (1 << i):
                # Torp present — update position
                if tnum in coord_map:
                    dx, dy = coord_map[tnum]
                    game_x = me.x + (dx - SPWINSIDE // 2) * SCALE
                    game_y = me.y + (dy - SPWINSIDE // 2) * SCALE
                    t.prev_x = t.x
                    t.prev_y = t.y
                    t._update_time = now
                    t.x = game_x
                    t.y = game_y
                if t.status == TFREE:
                    t.status = TMOVE
            else:
                # Bit not set — torp is gone (COW: "We got a TFREE")
                if t.status and t.status != TEXPLODE:
                    t.status = TFREE

    def _handle_sp_s_torp_info(self, raw):
        pkt = short_decode.decode_s_torp_info(raw)
        if pkt is None:
            return
        me = self.me
        now = time.monotonic()
        bitset = pkt.get("bitset", 0)
        base = pkt.get("whichtorps", 0) * MAXTORP

        for tnum, dx, dy, war, status in pkt["torps"]:
            if not (0 <= tnum < len(self.torps)):
                continue
            t = self.torps[tnum]
            i = tnum - base
            has_pos = bitset & (1 << i) if 0 <= i < 8 else False
            has_info = status is not None

            if has_pos and dx is not None and dy is not None and me:
                game_x = me.x + (dx - SPWINSIDE // 2) * SCALE
                game_y = me.y + (dy - SPWINSIDE // 2) * SCALE
                t.prev_x = t.x
                t.prev_y = t.y
                t._update_time = now
                t.x = game_x
                t.y = game_y
                # COW: position present but no info — guess TMOVE
                if not has_info and t.status == TFREE:
                    t.status = TMOVE
            elif not has_pos and not has_info:
                # COW: no position, no info — TFREE
                if t.status and t.status != TEXPLODE:
                    t.status = TFREE

            if has_info:
                if status == TEXPLODE and t.status == TFREE:
                    continue  # COW: redundant explosion, skip
                if status != t.status and status in (TEXPLODE, TDET):
                    t.fuse = 10
                t.status = status
                t.war = war

    def _handle_sp_s_player(self, raw):
        # Pass current "me" position for local-to-global coord conversion
        # in short header packets (COW uses global my_x/my_y)
        me = self.me
        mx = me.x if me else 0
        my = me.y if me else 0
        pkt = short_decode.decode_s_player(raw, my_x=mx, my_y=my)
        if pkt is None:
            return
        now = time.monotonic()

        # Big header includes my position directly (but NOT traditional flags —
        # flags are packed 2-bit-per-player, handled in the flag_updates loop below)
        me_data = pkt.get("me")
        if me_data and self.me_pnum >= 0:
            p = self.players[self.me_pnum]
            p.prev_x = p.x
            p.prev_y = p.y
            p.prev_dir = p.dir
            p._update_time = now
            p.dir = me_data["dir"]
            p.speed = me_data["speed"]
            p.x = me_data["x"]
            p.y = me_data["y"]

        # Update other players
        for pd in pkt["players"]:
            pnum = pd["pnum"]
            if 0 <= pnum < MAXPLAYER:
                p = self.players[pnum]
                p.prev_x = p.x
                p.prev_y = p.y
                p.prev_dir = p.dir
                p._update_time = now
                p.dir = pd["dir"]
                p.speed = pd["speed"]
                p.x = pd["x"]
                p.y = pd["y"]

        # SP2 packed 2-bit flag updates (COW new_flags):
        #   0 = dead/exploding, clear cloak
        #   1 = alive + cloaked
        #   2 = alive + shields, clear cloak
        #   3 = alive + no shields/cloak
        for fu in pkt.get("flag_updates", []):
            pnum = fu["pnum"]
            if 0 <= pnum < MAXPLAYER:
                p = self.players[pnum]
                code = fu["code"]
                if code == 0:
                    p.flags &= ~PFCLOAK
                    if p.status == PALIVE:
                        p.status = PEXPLODE
                elif code == 1:
                    p.status = PALIVE
                    p.flags |= PFCLOAK
                elif code == 2:
                    p.status = PALIVE
                    p.flags |= PFSHIELD
                    p.flags &= ~PFCLOAK
                elif code == 3:
                    p.status = PALIVE
                    p.flags &= ~(PFSHIELD | PFCLOAK)

    def _handle_sp_s_planet(self, raw):
        pkt = short_decode.decode_s_planet(raw)
        if pkt is None:
            return
        for pd in pkt["planets"]:
            pnum = pd["pnum"]
            if 0 <= pnum < MAXPLANETS:
                pl = self.planets[pnum]
                pl.owner = pd["owner"]
                pl.info = pd["info"]
                pl.armies = pd["armies"]
                pl.flags = pd["flags"]

    def _handle_sp_s_phaser(self, raw):
        pkt = short_decode.decode_s_phaser(raw)
        if pkt is None:
            return
        pnum = pkt["pnum"]
        if 0 <= pnum < MAXPLAYER:
            ph = self.phasers[pnum]
            ph.status = pkt["status"]
            ph.dir = pkt["dir"]
            ph.x = pkt["x"]
            ph.y = pkt["y"]
            ph.target = pkt["target"]
            if ph.status != PHFREE:
                ph.fuse = 10

    def _handle_sp_s_kills(self, raw):
        pkt = short_decode.decode_s_kills(raw)
        if pkt is None:
            return
        for kd in pkt["kills"]:
            pnum = kd["pnum"]
            if 0 <= pnum < MAXPLAYER:
                self.players[pnum].kills = kd["kills"]

    def _handle_sp_s_message(self, raw):
        pkt = short_decode.decode_s_message(raw)
        if pkt is None:
            return
        mesg = pkt["mesg"]
        flags = pkt["m_flags"]
        m_from = pkt["m_from"]

        if flags == (MTEAM | MDISTR | MVALID):
            decoded = decode_rcd(mesg, m_from, self)
            if decoded:
                mesg = decoded
                flags ^= MDISTR

        self.messages.append((mesg, flags, m_from))
        if len(self.messages) > 200:
            self.messages.pop(0)

    def _handle_sp_s_warning(self, raw):
        pkt = short_decode.decode_s_warning(raw, gs=self)
        if pkt is None:
            return
        mesg = pkt.get("mesg", "")
        if mesg:
            self.warning = mesg
            self.warning_timer = 90

    def _handle_sp_s_stats(self, raw):
        """Store player stats from short packet (COW short.c handle_s_Stats).

        SP_S_STATS is listed in SP_PACKETS so network.py decodes it as a dict
        when it arrives as a fixed-size packet.  However if short packets are
        active it may arrive as raw bytes via the variable-length path.
        Handle both cases.
        """
        if isinstance(raw, dict):
            pkt = raw
        else:
            from .protocol import decode_packet
            pkt = decode_packet(SP_S_STATS, raw)
            if pkt is None:
                return
        pnum = pkt["pnum"]
        if not (0 <= pnum < MAXPLAYER):
            return
        p = self.players[pnum]
        p.st_tkills = pkt["tkills"]
        p.st_tlosses = pkt["tlosses"]
        p.st_kills = pkt["kills"]
        p.st_losses = pkt["losses"]
        p.st_tticks = pkt["tticks"]
        p.st_tplanets = pkt["tplanets"]
        p.st_tarmsbomb = pkt["tarmies"]
        p.st_sbkills = pkt["sbkills"]
        p.st_sblosses = pkt["sblosses"]
        p.st_armsbomb = pkt["armies"]
        p.st_planets = pkt["planets"]
        p.st_maxkills = pkt["maxkills"] / 100.0
        p.st_sbmaxkills = pkt["sbmaxkills"] / 100.0

    def _handle_sp_s_sequence(self, raw):
        pass  # handled by network layer

    def _handle_sp_gparam(self, raw):
        pass  # paradise game parameters not used yet

    def _handle_sp_paradise_ext1(self, raw):
        pass  # paradise extensions not used yet

    _handlers = {
        SP_PLAYER_INFO: _handle_sp_player_info,
        SP_KILLS:       _handle_sp_kills,
        SP_PLAYER:      _handle_sp_player,
        SP_TORP_INFO:   _handle_sp_torp_info,
        SP_TORP:        _handle_sp_torp,
        SP_PHASER:      _handle_sp_phaser,
        SP_PLASMA_INFO: _handle_sp_plasma_info,
        SP_PLASMA:      _handle_sp_plasma,
        SP_YOU:         _handle_sp_you,
        SP_QUEUE:       _handle_sp_queue,
        SP_STATUS:      _handle_sp_status,
        SP_PLANET:      _handle_sp_planet,
        SP_PLANET_LOC:  _handle_sp_planet_loc,
        SP_PICKOK:      _handle_sp_pickok,
        SP_LOGIN:       _handle_sp_login,
        SP_FLAGS:       _handle_sp_flags,
        SP_MASK:        _handle_sp_mask,
        SP_PSTATUS:     _handle_sp_pstatus,
        SP_HOSTILE:     _handle_sp_hostile,
        SP_STATS:       _handle_sp_stats,
        SP_PL_LOGIN:    _handle_sp_pl_login,
        SP_RESERVED:    _handle_sp_reserved,
        SP_MOTD:        _handle_sp_motd,
        SP_WARNING:     _handle_sp_warning,
        SP_MESSAGE:     _handle_sp_message,
        SP_PING:        _handle_sp_ping,
        SP_FEATURE:     _handle_sp_feature,
        SP_BADVERSION:  _handle_sp_badversion,
        SP_SHIP_CAP:    _handle_sp_ship_cap,
        SP_RSA_KEY:     _handle_sp_rsa_key,
        SP_MOTD_PIC:    _handle_sp_motd_pic,
        SP_SCAN:        _handle_sp_scan,
        SP_STATS2:      _handle_sp_stats2,
        SP_STATUS2:     _handle_sp_status2,
        SP_PLANET2:     _handle_sp_planet2,
        SP_NEW_MOTD:    _handle_sp_new_motd,
        SP_THINGY:      _handle_sp_thingy,
        SP_THINGY_INFO: _handle_sp_thingy_info,
        SP_TERRAIN2:    _handle_sp_terrain2,
        SP_TERRAIN_INFO2: _handle_sp_terrain_info2,
        SP_UDP_REPLY:   _handle_sp_udp_reply,
        SP_SEQUENCE:    _handle_sp_sequence,
        SP_SC_SEQUENCE: _handle_sp_sc_sequence,
        # Short packets (SP_S_*)
        SP_S_REPLY:     _handle_sp_s_reply,
        SP_S_YOU:       _handle_sp_s_you,
        SP_S_YOU_SS:    _handle_sp_s_you_ss,
        SP_S_TORP:      _handle_sp_s_torp,
        SP_S_TORP_INFO: _handle_sp_s_torp_info,
        SP_S_8_TORP:    _handle_sp_s_torp,
        SP_S_PLAYER:    _handle_sp_s_player,
        SP_S_PLANET:    _handle_sp_s_planet,
        SP_S_PHASER:    _handle_sp_s_phaser,
        SP_S_KILLS:     _handle_sp_s_kills,
        SP_S_MESSAGE:   _handle_sp_s_message,
        SP_S_WARNING:   _handle_sp_s_warning,
        SP_S_STATS:     _handle_sp_s_stats,
        SP_S_SEQUENCE:  _handle_sp_s_sequence,
        SP_GPARAM:      _handle_sp_gparam,
        SP_PARADISE_EXT1: _handle_sp_paradise_ext1,
    }
