"""Login/outfit/alive/dead state machine for the Netrek client.

Login sub-states (matching COW getname.c):
  name        — typing character name
  query_wait  — sent name query, waiting for SP_LOGIN response
  password    — typing password for existing user
  makepass1   — typing new password (new user)
  makepass2   — confirming new password
  login_wait  — sent actual login, waiting for SP_LOGIN + SP_MASK
"""
from enum import Enum, auto
from .constants import *
from .protocol import (cp_socket, cp_login, cp_updates, cp_outfit,
                       cp_ping_response, cp_quit, cp_bye, cp_s_req)


class State(Enum):
    CONNECTING = auto()
    WAIT_MOTD = auto()
    LOGIN = auto()
    TEAM_SELECT = auto()
    OUTFIT = auto()
    ALIVE = auto()
    EXPLODING = auto()
    DEAD = auto()


class StateMachine:
    def __init__(self, conn, gamestate, name="guest", password="", login="pygame",
                 sound=None, config=None):
        self.conn = conn
        self.gs = gamestate
        self.config = config
        self.state = State.CONNECTING
        self.name = name
        self.password = password
        self.login = login
        self.sound = sound
        self.login_sent = False
        self.outfit_sent = False
        self.explode_timer = 0
        self.dead_timer = 0

        # Interactive login fields
        self.login_name = name
        self.login_password = password
        self.login_ready = False    # True once first SP_YOU received
        self.login_accepted = None  # None/True/False from SP_LOGIN

        # Two-step login sub-state (COW getname.c states)
        self.login_state = "name"  # name/query_wait/password/makepass1/makepass2/login_wait
        self.login_password_confirm = ""  # second password entry for new users
        self.login_error = ""       # error message to display
        self.login_error_timer = 0  # frames to show error
        self._short_requested = False  # True once CP_S_REQ(SPK_VON) sent
        # Ship selection (COW newwin.c: chosen_ship, default CRUISER)
        self.chosen_ship = CRUISER  # remembered across deaths (COW previous_ship)

    def start(self):
        """Send the initial CP_SOCKET packet."""
        self.conn.send(cp_socket())
        self.state = State.WAIT_MOTD

    def handle_packet(self, ptype, pkt):
        """Process a packet and advance the state machine."""
        # Always respond to pings
        if ptype == SP_PING:
            self.conn.send(cp_ping_response())
            return

        # Handle SP_S_REPLY: on SPK_VON confirmation, request initial full update
        if ptype == SP_S_REPLY:
            self.gs.handle_packet(ptype, pkt)
            # pkt is raw bytes for short packets; check repl byte
            if isinstance(pkt, (bytes, bytearray)) and len(pkt) >= 2 and pkt[1] == SPK_VON:
                self.conn.send(cp_s_req(SPK_SALL, SHORTVERSION))
            return

        # Update game state first
        self.gs.handle_packet(ptype, pkt)

        if self.state == State.WAIT_MOTD:
            if ptype == SP_YOU and not self.login_ready:
                # First SP_YOU — mark login screen as ready to show
                self.login_ready = True
                if self.sound:
                    self.sound.play("intro")
            elif ptype == SP_LOGIN and self.login_state == "query_wait":
                # Response to our name-existence query
                accept = bool(pkt.get("accept", 0))
                if accept:
                    # Name exists — prompt for password
                    self.login_state = "password"
                    self.login_password = ""
                else:
                    # New user — prompt to create password
                    self.login_state = "makepass1"
                    self.login_password = ""

        elif self.state == State.LOGIN:
            if ptype == SP_LOGIN:
                accept = bool(pkt.get("accept", 0))
                self.login_accepted = accept
                if not accept:
                    # Bad password or login refused
                    self.login_error = "Bad password!"
                    self.login_error_timer = 90  # ~3 seconds
                    self.login_state = "password"
                    self.login_password = ""
                    self.login_sent = False
                    self.state = State.WAIT_MOTD
            if ptype == SP_MASK and self.state == State.LOGIN:
                self.state = State.TEAM_SELECT

        elif self.state == State.TEAM_SELECT:
            if ptype == SP_MASK:
                pass  # update mask (already stored in gs)
            elif ptype == SP_PICKOK:
                if pkt.get("state", 0) == 1:
                    self._enter_alive()

        elif self.state == State.OUTFIT:
            if ptype == SP_MASK:
                pass
            elif ptype == SP_PICKOK:
                if pkt.get("state", 0) == 1:
                    self._enter_alive()

        elif self.state == State.ALIVE:
            if ptype == SP_PSTATUS:
                me = self.gs.me
                if me and pkt["pnum"] == self.gs.me_pnum:
                    if pkt["status"] == PEXPLODE:
                        self.state = State.EXPLODING
                        self.explode_timer = 30  # ~1 second at 30fps

        elif self.state == State.EXPLODING:
            if ptype == SP_PSTATUS:
                if pkt["pnum"] == self.gs.me_pnum:
                    if pkt["status"] == PDEAD:
                        self.state = State.DEAD
                        self.dead_timer = 15
                    elif pkt["status"] == POUTFIT:
                        self._enter_death_screen()

        elif self.state == State.DEAD:
            if ptype == SP_PSTATUS:
                if pkt["pnum"] == self.gs.me_pnum and pkt["status"] == POUTFIT:
                    self._enter_death_screen()

    def _enter_alive(self):
        """Transition to ALIVE state (COW cowmain.c enter-ship sequence).

        COW resets alert flags and sets shields on before entering the game,
        then plays ENTER_SHIP_SOUND.  UDP negotiation happens on first entry
        (COW isFirstEntry) since the server only accepts CP_UDP_REQ when
        the player is ALIVE.
        """
        self.state = State.ALIVE
        self.outfit_sent = False
        if self.sound:
            self.sound.play("enter_ship")
        # Start UDP on first entry (COW: isFirstEntry + tryUdp)
        try_udp = self.config.try_udp if self.config else True
        if try_udp and not self.conn.udp_active and self.conn._udp_state == "off":
            self.conn.start_udp_negotiation()
        # Request short packets (COW: sendShortReq(SPK_VON) on first entry)
        if not self._short_requested:
            self.conn.send(cp_s_req(SPK_VON, SHORTVERSION))
            self._short_requested = True
            print("Short packets: requested SPK_VON")

    def _enter_death_screen(self):
        """Transition to team select after death (COW death.c).

        COW resets sound_flags = PFSHIELD to avoid false sound triggers
        on the next respawn.
        """
        self.state = State.TEAM_SELECT
        self.outfit_sent = False
        # Clear lock state on death
        self.gs.lock_planet = -1
        self.gs.lock_player = -1
        if self.sound:
            self.sound.on_death()

    def tick(self):
        """Called each frame for timer-based transitions."""
        self.conn.check_udp_timeout()

        if self.state == State.EXPLODING:
            self.explode_timer -= 1
            if self.explode_timer <= 0:
                self.state = State.DEAD
                self.dead_timer = 15

        elif self.state == State.DEAD:
            self.dead_timer -= 1
            if self.dead_timer <= 0:
                self._enter_death_screen()

        # Tick login error display timer
        if self.login_error_timer > 0:
            self.login_error_timer -= 1
            if self.login_error_timer <= 0:
                self.login_error = ""

    def submit_name(self):
        """User pressed Enter on name field. Start the login flow.

        Guest: send login directly (no query needed).
        Other: send query to check if name exists on server.
        """
        name = self.login_name.strip()
        if not name:
            return

        if name.lower() == "guest":
            # Guest login — send directly, no password needed
            self.conn.send(cp_login(0, name, "", self.login))
            self.conn.send(cp_updates(100000))
            self.login_sent = True
            self.login_state = "login_wait"
            self.state = State.LOGIN
        else:
            # Send query: does this name exist? (query=1)
            self.conn.send(cp_login(1, name, "", self.login))
            self.login_state = "query_wait"

    def submit_password(self):
        """User pressed Enter on password field. Send actual login."""
        self.conn.send(cp_login(0, self.login_name, self.login_password, self.login))
        self.conn.send(cp_updates(100000))
        self.login_sent = True
        self.login_state = "login_wait"
        self.state = State.LOGIN

    def submit_makepass1(self):
        """User pressed Enter on first new-password field. Move to confirm."""
        if not self.login_password:
            self.login_error = "Password must not be empty."
            self.login_error_timer = 90
            return
        self.login_password_confirm = ""
        self.login_state = "makepass2"

    def submit_makepass2(self):
        """User pressed Enter on confirm-password field. Check match."""
        if self.login_password_confirm == self.login_password:
            # Passwords match — send actual login
            self.submit_password()
        else:
            # Mismatch — start over
            self.login_error = "Passwords do not match, starting again!"
            self.login_error_timer = 90
            self.login_password = ""
            self.login_password_confirm = ""
            self.login_state = "name"

    def select_team(self, team_bit):
        """Send CP_OUTFIT for the chosen team + ship, transition to OUTFIT.

        COW newwin.c:937-941: sends sendTeamReq(chosen_team, chosen_ship)
        when both are set.
        """
        if self.outfit_sent:
            return
        mask = self.gs.team_mask
        if not (mask & team_bit):
            return  # team not available
        team_idx = {FED: 0, ROM: 1, KLI: 2, ORI: 3}[team_bit]
        self.conn.send(cp_outfit(team_idx, self.chosen_ship))
        self.outfit_sent = True
        self.state = State.OUTFIT

    def quit(self):
        self.conn.send(cp_quit())
        self.conn.send(cp_bye())
