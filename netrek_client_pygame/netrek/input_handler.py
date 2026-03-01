"""Keyboard + mouse -> protocol commands via configurable keymap."""
import math
import pygame
from .constants import *
from .config import STYLE_NAMES, DIRECTED_ACTIONS
from .protocol import (cp_speed, cp_direction, cp_torp, cp_phaser, cp_plasma,
                       cp_shield, cp_cloak, cp_repair, cp_orbit, cp_bomb,
                       cp_beam, cp_det_torps, cp_det_mytorp, cp_practr,
                       cp_quit, cp_refit, cp_coup, cp_message, cp_war,
                       cp_planlock, cp_playlock, cp_tractor, cp_repress)

# COW refit key -> ship type (input.c Key114 / refit switch)
_REFIT_KEYS = {
    's': SCOUT, 'd': DESTROYER, 'c': CRUISER, 'b': BATTLESHIP,
    'a': ASSAULT, 'g': SGALAXY, 'o': STARBASE, '*': ATT,
}

# COW outfit ship keys (newwin.c:1039-1081) — same as refit except ATT is 'X'
_OUTFIT_KEYS = {
    's': SCOUT, 'd': DESTROYER, 'c': CRUISER, 'b': BATTLESHIP,
    'a': ASSAULT, 'g': SGALAXY, 'o': STARBASE, 'X': ATT,
}

# Galactic panel origin in base (unscaled) coordinates
_GAL_X = TWINSIDE + 3  # BORDER = 3
_GAL_Y = 0


def _dir_to_cursor(mx, my, cx, cy):
    """Compute Netrek direction (0-255) from center to mouse position.

    0=north, 64=east, 128=south, 192=west.
    Screen y is inverted (down is positive), so dy is negated.
    """
    dx = mx - cx
    dy = -(my - cy)
    angle = math.atan2(dx, dy)
    direction = int(angle / math.pi * 128) & 0xFF
    return direction


def _window_to_base(wx, wy, scale_info):
    """Map window pixel coords to base (unscaled) coords."""
    s, ox, oy = scale_info
    return (wx - ox) / s, (wy - oy) / s


def _which_panel(bx, by):
    """Determine which panel base coordinates fall in.

    Returns ("tactical", local_x, local_y) or ("galactic", local_x, local_y)
    or None if outside both panels.
    """
    if 0 <= bx < TWINSIDE and 0 <= by < TWINSIDE:
        return ("tactical", bx, by)
    gx = bx - _GAL_X
    gy = by - _GAL_Y
    if 0 <= gx < GWINSIDE and 0 <= gy < GWINSIDE:
        return ("galactic", gx, gy)
    return None


def _panel_to_game(panel, lx, ly, me):
    """Convert panel-local coords to game coords.

    Matches COW's gettarget():
      galactic: g_x = x * GWIDTH / GWINSIDE  (direct mapping)
      tactical: g_x = me.x + (x - TWINSIDE/2) * SCALE  (relative to player)
    """
    if panel == "galactic":
        return int(lx * GWIDTH / GWINSIDE), int(ly * GWIDTH / GWINSIDE)
    else:
        return (me.x + int((lx - TWINSIDE / 2) * SCALE),
                me.y + int((ly - TWINSIDE / 2) * SCALE))


def _getcourse(panel, lx, ly, me):
    """Compute Netrek direction from player to mouse position.

    Matches COW's getcourse(): for galactic, direction is from player's
    galactic position to the click point.  For tactical, it's from screen
    center to the click point.
    """
    if panel == "galactic":
        me_gx = me.x * GWINSIDE / GWIDTH
        me_gy = me.y * GWINSIDE / GWIDTH
        return _dir_to_cursor(lx, ly, me_gx, me_gy)
    else:
        return _dir_to_cursor(lx, ly, TWINSIDE / 2, TWINSIDE / 2)


class InputHandler:
    def __init__(self, conn, gamestate, statemachine, config=None, sound=None):
        self.conn = conn
        self.gs = gamestate
        self.sm = statemachine
        self.config = config
        self.sound = sound
        self.refit_mode = False
        # War window state (COW war.c)
        self.war_mode = False     # True when war toggle overlay is visible
        self._war_hostile = 0     # working copy of hostile bits (COW newhostile)
        # Info window state (COW inform.c)
        self.info_target = None   # ('player', pnum) or ('planet', pnum) or None
        self.info_extended = False # True for 'I' (extended stats), False for 'i'
        self.info_timer = 0       # auto-dismiss countdown (frames)
        # Message input state (COW smessage.c: messageon, messpend, outmessage)
        self.messageon = False   # True when all keys route to smessage
        self.messpend = False    # True after recipient selected, typing body
        self._msg_addr = ''      # recipient character (A, F, R, K, O, 0-9, a-z, G)
        self._msg_group = 0      # MALL, MTEAM, MINDIV
        self._msg_recip = 0      # recipient number (team bit or player num)
        self._msg_header = ''    # address header string (e.g. " Fc->ALL")
        self._msg_buf = ''       # message body being typed

    def handle_event(self, event, tactical_offset=(0, 0),
                     scale_info=(1.0, 0, 0)):
        """Process a pygame event. Returns True if game should quit."""
        if event.type == pygame.QUIT:
            return True

        from .statemachine import State
        state = self.sm.state

        # Login screen input
        if state == State.WAIT_MOTD and self.sm.login_ready:
            return self._handle_login_input(event)
        if state == State.LOGIN:
            return self._handle_login_input(event)

        # Team selection input
        if state == State.TEAM_SELECT:
            return self._handle_team_input(event, tactical_offset, scale_info)

        # Outfit waiting — no gameplay input, just quit
        if state == State.OUTFIT:
            if event.type == pygame.KEYDOWN and event.key == pygame.K_q:
                return True
            return False

        # Normal gameplay
        if event.type == pygame.KEYDOWN:
            return self._handle_key(event, scale_info)

        if event.type == pygame.MOUSEBUTTONDOWN:
            return self._handle_mouse(event, scale_info)

        return False

    # --- Login screen input ---

    def _handle_login_input(self, event):
        """Handle text input for login name/password fields.

        Matches COW getname.c state machine:
          name       — typing character name
          query_wait — waiting for server response (no input)
          password   — typing password for existing user
          makepass1  — typing new password (new user)
          makepass2  — confirming new password
          login_wait — waiting for server response (no input)
        """
        if event.type != pygame.KEYDOWN:
            return False

        sm = self.sm
        key = event.key
        ls = sm.login_state

        # No input during wait states
        if ls in ("query_wait", "login_wait"):
            if key == pygame.K_ESCAPE:
                return True
            return False

        if key == pygame.K_ESCAPE:
            return True

        if key == pygame.K_RETURN or key == pygame.K_KP_ENTER:
            if ls == "name":
                sm.submit_name()
            elif ls == "password":
                sm.submit_password()
            elif ls == "makepass1":
                sm.submit_makepass1()
            elif ls == "makepass2":
                sm.submit_makepass2()
            return False

        if key == pygame.K_BACKSPACE:
            if ls == "name":
                sm.login_name = sm.login_name[:-1]
            elif ls in ("password", "makepass1"):
                sm.login_password = sm.login_password[:-1]
            elif ls == "makepass2":
                sm.login_password_confirm = sm.login_password_confirm[:-1]
            return False

        # Printable characters
        ch = event.unicode
        if ch and ch.isprintable() and len(ch) == 1:
            if ls == "name":
                if len(sm.login_name) < 15:
                    sm.login_name += ch
            elif ls in ("password", "makepass1"):
                if len(sm.login_password) < 15:
                    sm.login_password += ch
            elif ls == "makepass2":
                if len(sm.login_password_confirm) < 15:
                    sm.login_password_confirm += ch
            return False

        return False

    # --- Team selection input ---

    def _handle_team_input(self, event, tactical_offset, scale_info):
        """Handle keyboard + mouse for team/ship selection.

        Matches COW newwin.c entrywindow():
          - Ship keys (s/d/c/b/a/g/o/X) set chosen_ship
          - Enter/Space re-uses previous team+ship selection
          - Mouse click on team corner selects team
          - Both team and ship must be set to send CP_OUTFIT
        """
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_q:
                return True

            ch = event.unicode

            # Enter/Space: re-use previous selection (COW newwin.c:1045-1051)
            if event.key in (pygame.K_RETURN, pygame.K_KP_ENTER, pygame.K_SPACE):
                mask = self.gs.team_mask
                for team_bit in (FED, ROM, KLI, ORI):
                    if mask & team_bit:
                        self.sm.select_team(team_bit)
                        return False
                return False

            # Ship selection keys (COW newwin.c:1039-1081)
            if ch and ch in _OUTFIT_KEYS:
                ship = _OUTFIT_KEYS[ch]
                self.sm.chosen_ship = ship
                self.gs.warning = f"Ship: {SHIP_NAMES.get(ship, '??')} — click a team to join"
                self.gs.warning_timer = 150
                return False

            return False

        if event.type == pygame.MOUSEBUTTONDOWN:
            mx, my = _window_to_base(event.pos[0], event.pos[1], scale_info)
            tx, ty = tactical_offset
            rx, ry = mx - tx, my - ty

            # Check if click is within tactical surface
            if not (0 <= rx < TWINSIDE and 0 <= ry < TWINSIDE):
                return False

            # Map click to team quadrant
            team_bit = None
            if rx < 120 and ry < 120:
                team_bit = ROM   # Top-left
            elif rx > 380 and ry < 120:
                team_bit = KLI   # Top-right
            elif rx < 120 and ry > 380:
                team_bit = FED   # Bottom-left
            elif rx > 380 and ry > 380:
                team_bit = ORI   # Bottom-right

            if team_bit:
                self.sm.select_team(team_bit)
            return False

        return False

    # --- Coordinate helpers ---

    def _mouse_panel(self, scale_info):
        """Get current mouse position as (panel, local_x, local_y) or None.

        Determines whether the mouse is over tactical or galactic and returns
        panel-local coordinates.  Matches COW's findMouseInWin + gettarget.
        """
        wx, wy = pygame.mouse.get_pos()
        bx, by = _window_to_base(wx, wy, scale_info)
        return _which_panel(bx, by)

    def _mouse_to_game(self, scale_info):
        """Convert current mouse position to game coordinates.

        Handles both tactical (relative to player) and galactic (absolute)
        coordinate systems, matching COW's gettarget().
        """
        me = self.gs.me
        if not me:
            return None, None
        hit = self._mouse_panel(scale_info)
        if hit is None:
            return None, None
        panel, lx, ly = hit
        return _panel_to_game(panel, lx, ly, me)

    def _dir_to_mouse(self, scale_info):
        """Compute direction from ship center to current mouse position.

        Handles both tactical and galactic windows matching COW's getcourse().
        """
        me = self.gs.me
        if not me:
            return 0
        hit = self._mouse_panel(scale_info)
        if hit is None:
            return 0
        panel, lx, ly = hit
        return _getcourse(panel, lx, ly, me)

    # --- Normal gameplay ---

    def _handle_key(self, event, scale_info):
        from .statemachine import State
        if self.sm.state != State.ALIVE:
            if event.key == pygame.K_q:
                return True
            return False

        ch = event.unicode
        if not ch:
            return False

        # COW input.c:833: if (messageon) smessage(data.key); else keyaction()
        if self.messageon:
            # Map pygame key codes to chars for special keys
            if event.key == pygame.K_BACKSPACE:
                self._smessage('\x08')
            elif event.key == pygame.K_ESCAPE:
                self._smessage('\x1b')
            elif event.key == pygame.K_RETURN or event.key == pygame.K_KP_ENTER:
                self._smessage('\r')
            elif ch:
                self._smessage(ch)
            return False

        # Refit mode: next key selects ship type (COW Key114 + refit switch)
        if self.refit_mode:
            self.refit_mode = False
            if ch == 'R' and self.gs.me:
                # Sturgeon: refit to same ship = enter upgrade menu
                self.conn.send(cp_refit(self.gs.me.shiptype))
                return False
            ship = _REFIT_KEYS.get(ch)
            if ship is not None:
                self.conn.send(cp_refit(ship))
            return False

        action = self.config.keymap.get(ch) if self.config else None
        if action:
            return self._dispatch(action, scale_info)

        return False

    def _dispatch(self, action, scale_info):
        """Execute an action by name. Returns True only for fast_quit."""

        if action.startswith('speed_'):
            speed = int(action[6:])
            self.conn.send(cp_speed(speed))
            return False

        if action == 'max_speed':
            self.conn.send(cp_speed(12))
            return False

        if action in DIRECTED_ACTIONS:
            d = self._dir_to_mouse(scale_info)
            if d is not None:
                self._dispatch_directed(action, d)
            return False

        if action == 'shields':
            me = self.gs.me
            on = 0 if (me and me.flags & PFSHIELD) else 1
            self.conn.send(cp_shield(on))
        elif action == 'cloak':
            me = self.gs.me
            on = 0 if (me and me.flags & PFCLOAK) else 1
            self.conn.send(cp_cloak(on))
        elif action == 'repair':
            self.conn.send(cp_repair(1))
        elif action == 'orbit':
            self.conn.send(cp_orbit(1))
        elif action == 'bomb':
            self.conn.send(cp_bomb(1))
        elif action == 'beam_up':
            self.conn.send(cp_beam(1))
        elif action == 'beam_down':
            self.conn.send(cp_beam(2))

        elif action == 'det_torps':
            self.conn.send(cp_det_torps())
        elif action == 'det_own_torp':
            # COW detmine(): send CP_DET_MYTORP for each of your live torps
            base = self.gs.me_pnum * MAXTORP
            for i in range(MAXTORP):
                t = self.gs.torps[base + i]
                if t.status in (TMOVE, TSTRAIGHT):
                    self.conn.send(cp_det_mytorp(base + i))

        elif action == 'refit':
            me = self.gs.me
            self.refit_mode = True
            if me and (me.flags & PFORBIT):
                self.gs.warning = "s=SC d=DD c=CA b=BB a=AS g=GA o=SB | R=upgrade"
            else:
                self.gs.warning = "s=scout, d=destroyer, c=cruiser, b=battleship, a=assault, g=galaxy, o=starbase"
            self.gs.warning_timer = 150

        elif action == 'practice':
            self.conn.send(cp_practr())
        elif action == 'coup':
            self.conn.send(cp_coup())

        elif action == 'tractor':
            self._do_tractor(scale_info, pressor=False)
        elif action == 'pressor':
            self._do_tractor(scale_info, pressor=True)
        elif action == 'tractor_off':
            self.conn.send(cp_tractor(0, self.gs.me_pnum))

        elif action == 'quit':
            self.conn.send(cp_quit())
            if self.sound:
                self.sound.play("self_destruct")
        elif action == 'fast_quit':
            return True

        elif action == 'cycle_local_planets' and self.config:
            self.config.showlocal = (self.config.showlocal + 1) % 5
            name = STYLE_NAMES[self.config.showlocal]
            self.gs.warning = f"Local planets: {name}"
            self.gs.warning_timer = 90
        elif action == 'cycle_galactic_planets' and self.config:
            self.config.showgalactic = (self.config.showgalactic + 1) % 5
            name = STYLE_NAMES[self.config.showgalactic]
            self.gs.warning = f"Galactic planets: {name}"
            self.gs.warning_timer = 90

        elif action == 'info':
            # COW Key105: toggle info window for nearest object under cursor
            self._toggle_info(scale_info, extended=False)
        elif action == 'info_ext':
            # COW Key73: toggle extended info window
            self._toggle_info(scale_info, extended=True)

        elif action == 'war':
            # COW Key119: toggle war window (war.c warwindow)
            self._toggle_war()

        elif action == 'message':
            # COW Key109: activate message mode (smessage.c message_on)
            if self.sound:
                self.sound.play("message")
            self._message_on()

        elif action == 'planet_lock':
            self._lock_nearest(scale_info)
        elif action == 'player_lock':
            self._lock_nearest_planet_or_base(scale_info)

        return False

    def _dispatch_directed(self, action, direction):
        """Execute a directed action (needs a direction toward mouse)."""
        if action == 'torp':
            self.conn.send(cp_torp(direction))
            if self.sound:
                self.sound.play("fire_torp")
        elif action == 'phaser':
            self.conn.send(cp_phaser(direction))
            # Phaser sound played from sound.tick() on phaser status change
        elif action == 'plasma':
            self.conn.send(cp_plasma(direction))
            if self.sound:
                self.sound.play("fire_plasma")
        elif action == 'course':
            self.conn.send(cp_direction(direction))

    def _do_tractor(self, scale_info, pressor=False):
        """Tractor or pressor nearest player under mouse (COW Key84/Key94).

        If already tractoring/pressing, turn off first, then re-target.
        """
        me = self.gs.me
        if not me:
            return
        # Turn off existing tractor/pressor first (COW behavior)
        if me.flags & (PFTRACT | PFPRESS):
            if pressor:
                self.conn.send(cp_repress(0, me.pnum))
            else:
                self.conn.send(cp_tractor(0, me.pnum))

        # Find nearest player to mouse (COW gettarget with TARG_PLAYER)
        gx, gy = self._mouse_to_game(scale_info)
        if gx is None:
            return
        best_dist = float('inf')
        best_pnum = -1
        for p in self.gs.players:
            if p.status != PALIVE:
                continue
            if p.pnum == self.gs.me_pnum:
                continue
            dist = (p.x - gx) ** 2 + (p.y - gy) ** 2
            if dist < best_dist:
                best_dist = dist
                best_pnum = p.pnum
        if best_pnum < 0:
            return
        me.tractor = best_pnum
        if pressor:
            self.conn.send(cp_repress(1, best_pnum))
        else:
            self.conn.send(cp_tractor(1, best_pnum))

    def _lock_nearest(self, scale_info):
        """Lock onto nearest player or planet under cursor (COW 'l' key).

        Like COW Key108/gettarget: finds the closest object to the mouse
        position and sends the appropriate lock packet.  Works on both
        tactical and galactic windows.
        """
        gx, gy = self._mouse_to_game(scale_info)
        if gx is None:
            return
        self._do_lock(gx, gy, include_players=True, include_planets=True)

    def _lock_nearest_planet_or_base(self, scale_info):
        """Lock onto nearest planet or friendly starbase (COW ';' key)."""
        gx, gy = self._mouse_to_game(scale_info)
        if gx is None:
            return
        self._do_lock(gx, gy, include_players=False, include_planets=True,
                      include_friendly_bases=True)

    def _do_lock(self, gx, gy, include_players=True, include_planets=True,
                 include_friendly_bases=False):
        """Find nearest matching object and send lock packet."""
        best_dist = float('inf')
        best_type = None  # 'player' or 'planet'
        best_num = -1
        me = self.gs.me

        # Check planets
        if include_planets:
            for pl in self.gs.planets:
                if not pl.name:
                    continue
                dist = (pl.x - gx) ** 2 + (pl.y - gy) ** 2
                if dist < best_dist:
                    best_dist = dist
                    best_type = 'planet'
                    best_num = pl.pnum

        # Check players
        if include_players:
            for p in self.gs.players:
                if p.status != PALIVE:
                    continue
                if p.pnum == self.gs.me_pnum:
                    continue
                if p.flags & PFCLOAK:
                    continue
                dist = (p.x - gx) ** 2 + (p.y - gy) ** 2
                if dist < best_dist:
                    best_dist = dist
                    best_type = 'player'
                    best_num = p.pnum

        # Check friendly starbases (for ';' key)
        if include_friendly_bases and me:
            for p in self.gs.players:
                if p.status != PALIVE:
                    continue
                if p.team != me.team:
                    continue
                if p.shiptype != STARBASE:
                    continue
                dist = (p.x - gx) ** 2 + (p.y - gy) ** 2
                if dist < best_dist:
                    best_dist = dist
                    best_type = 'player'
                    best_num = p.pnum

        if best_type == 'player':
            self.conn.send(cp_playlock(best_num))
            self.gs.lock_player = best_num
            self.gs.lock_planet = -1
            p = self.gs.players[best_num]
            tl = TEAM_LETTERS.get(p.team, "X")
            self.gs.warning = f"Locking onto {p.name} ({tl}{best_num % 16:x})"
            self.gs.warning_timer = 90
        elif best_type == 'planet':
            self.conn.send(cp_planlock(best_num))
            self.gs.lock_planet = best_num
            self.gs.lock_player = -1
            pl = self.gs.planets[best_num]
            self.gs.warning = f"Locking onto {pl.name}"
            self.gs.warning_timer = 90

    # --- Info window (COW inform.c) ---

    def _toggle_info(self, scale_info, extended=False):
        """Toggle info window (COW Key105/Key73: inform or destroyInfo)."""
        if self.info_target is not None:
            # Already showing — close it
            self.info_target = None
            self.info_timer = 0
            return
        # Find nearest object under cursor (COW gettarget with TARG_PLAYER|TARG_PLANET)
        gx, gy = self._mouse_to_game(scale_info)
        if gx is None:
            return
        best_dist = float('inf')
        best = None
        # Check planets
        for pl in self.gs.planets:
            if not pl.name:
                continue
            dist = (pl.x - gx) ** 2 + (pl.y - gy) ** 2
            if dist < best_dist:
                best_dist = dist
                best = ('planet', pl.pnum)
        # Check players (closer players override planets, matching COW)
        for p in self.gs.players:
            if p.status != PALIVE:
                continue
            if p.pnum == self.gs.me_pnum and not extended:
                continue  # 'i' skips self, 'I' includes self
            dist = (p.x - gx) ** 2 + (p.y - gy) ** 2
            if dist < best_dist:
                best_dist = dist
                best = ('player', p.pnum)
        if best:
            self.info_target = best
            self.info_extended = extended
            self.info_timer = 90  # ~3 seconds at 30fps (COW keepInfo=15 * ups/5)

    def tick_info(self):
        """Called each frame to auto-dismiss info window (COW opened_info countdown)."""
        if self.info_timer > 0:
            self.info_timer -= 1
            if self.info_timer <= 0:
                self.info_target = None

    # --- War window (COW war.c) ---

    def _toggle_war(self):
        """Toggle war window on/off (COW Key119 / warwindow)."""
        if self.war_mode:
            self.war_mode = False
        else:
            me = self.gs.me
            if me:
                self._war_hostile = me.hostile
            self.war_mode = True

    # War window position in base coords (matches renderer)
    WAR_X = TWINSIDE + BORDER + 10
    WAR_Y = 10
    WAR_W = 160
    WAR_ROW_H = 18

    def _handle_war_click(self, bx, by):
        """Map base coordinates to war menu row and dispatch."""
        rx = bx - self.WAR_X
        ry = by - self.WAR_Y
        if rx < 0 or rx > self.WAR_W or ry < 0:
            # Click outside war window — close it (COW behavior)
            self.war_mode = False
            return
        row = int(ry // self.WAR_ROW_H)
        if row > 5:
            self.war_mode = False
            return
        self._war_click(row)

    def _war_click(self, row):
        """Handle click on war window row (COW war.c waraction).

        Rows 0-3 = FED/ROM/KLI/ORI toggle, 4 = Save, 5 = Exit.
        """
        me = self.gs.me
        if not me:
            return
        team_for_row = [FED, ROM, KLI, ORI]
        if row < 4:
            enemy = team_for_row[row]
            if me.swar & enemy:
                self.gs.warning = "Already at war. Status cannot be changed."
                self.gs.warning_timer = 90
            elif me.team == enemy:
                self.gs.warning = "Can't declare war on your own team, fool."
                self.gs.warning_timer = 90
            else:
                self._war_hostile ^= enemy
        elif row == 4:
            # Save — send CP_WAR and close
            self.conn.send(cp_war(self._war_hostile))
            self.war_mode = False
        elif row == 5:
            # Exit — no change
            self.war_mode = False

    # --- Message input (COW smessage.c) ---

    def _message_on(self):
        """Activate message mode (COW smessage.c message_on)."""
        self.messageon = True
        self.messpend = False

    def _message_off(self):
        """Deactivate message mode (COW smessage.c message_off)."""
        self.messageon = False
        self.messpend = False
        self._msg_buf = ''
        self._msg_header = ''
        self._msg_addr = ''

    @property
    def outmessage(self):
        """Current message display string for renderer (COW outmessage)."""
        if not self.messageon:
            return ''
        if not self.messpend:
            # Waiting for recipient key
            return ''
        return self._msg_header + self._msg_buf + '_'

    def _getaddr(self, who):
        """Resolve recipient char to (group, recip, header_str) or None.

        Matches COW smessage.c getaddr() + getaddr2().
        """
        me = self.gs.me
        my_team_letter = TEAMLET.get(me.team, 'I') if me else 'I'
        my_slot = SHIPNOS[self.gs.me_pnum] if 0 <= self.gs.me_pnum < len(SHIPNOS) else '?'
        prefix = f" {my_team_letter}{my_slot}->"

        def _pad(s):
            return s.ljust(ADDRLEN)

        if who == 'A':
            return (MALL, 0, _pad(f"{prefix}ALL"))
        elif who == 'F':
            return (MTEAM, FED, _pad(f"{prefix}FED"))
        elif who == 'R':
            return (MTEAM, ROM, _pad(f"{prefix}ROM"))
        elif who == 'K':
            return (MTEAM, KLI, _pad(f"{prefix}KLI"))
        elif who == 'O':
            return (MTEAM, ORI, _pad(f"{prefix}ORI"))
        elif who == 'G':
            return (MGOD, 0, _pad(f"{prefix}GOD"))
        elif '0' <= who <= '9':
            pnum = ord(who) - ord('0')
            if pnum >= MAXPLAYER or self.gs.players[pnum].status == PFREE:
                self.gs.warning = "Slot is not alive."
                self.gs.warning_timer = 90
                return None
            p = self.gs.players[pnum]
            tl = TEAMLET.get(p.team, 'I')
            return (MINDIV, pnum, _pad(f"{prefix}{tl}{SHIPNOS[pnum]}"))
        elif 'a' <= who <= 'z':
            pnum = ord(who) - ord('a') + 10
            if pnum >= MAXPLAYER or self.gs.players[pnum].status == PFREE:
                self.gs.warning = "Slot is not alive."
                self.gs.warning_timer = 90
                return None
            p = self.gs.players[pnum]
            tl = TEAMLET.get(p.team, 'I')
            return (MINDIV, pnum, _pad(f"{prefix}{tl}{SHIPNOS[pnum]}"))
        else:
            self.gs.warning = "Not legal recipient"
            self.gs.warning_timer = 90
            return None

    def _smessage(self, ichar):
        """Process one keystroke in message mode (COW smessage.c smessage()).

        Two phases:
          1. messpend=False: first char selects recipient
          2. messpend=True: typing message body
        """
        if not self.messpend:
            # Phase 1: recipient selection
            self.messpend = True

            # T/t = my team (COW: addr = teamlet[me->p_team])
            if ichar in ('t', 'T'):
                me = self.gs.me
                if me:
                    ichar = TEAMLET.get(me.team, 'F')
                else:
                    ichar = 'A'

            result = self._getaddr(ichar)
            if result is None:
                self._message_off()
                return

            self._msg_group, self._msg_recip, self._msg_header = result
            self._msg_addr = ichar
            self._msg_buf = ''
            return

        # Phase 2: typing message body
        # Handle control characters (COW smessage.c)
        if ichar == '\x08' or ichar == '\x7f':
            # Backspace / Delete
            self._msg_buf = self._msg_buf[:-1]
        elif ichar == '\x1b':
            # Escape — abort message
            self._message_off()
        elif ichar == '\r' or ichar == '\n':
            # Enter — send message
            self._send_message()
        elif ichar == '\x15':
            # Ctrl-U — erase to start (stay in message mode)
            self._msg_buf = ''
        elif len(ichar) == 1 and ichar.isprintable():
            # Normal character — append if room
            # COW max is 80 - ADDRLEN = 70 chars of body
            if len(self._msg_buf) < 80 - ADDRLEN:
                self._msg_buf += ichar

    def _send_message(self):
        """Send the composed message and exit message mode (COW pmessage)."""
        text = self._msg_buf.strip()
        if text:
            self.conn.send(cp_message(self._msg_group, self._msg_recip, text))
        self._message_off()

    def _handle_mouse(self, event, scale_info):
        """Handle mouse button clicks on tactical or galactic panels.

        Matches COW: clicks on both panels fire directed actions, with
        coordinate conversion appropriate to each panel.
        COW input.c:1141: if (messageon) message_off() — cancel message on click.
        """
        from .statemachine import State
        if self.sm.state != State.ALIVE:
            return False

        # COW: any mouse event cancels message mode
        if self.messageon:
            self._message_off()

        # War window click intercept (COW war.c waraction)
        if self.war_mode:
            bx, by = _window_to_base(event.pos[0], event.pos[1], scale_info)
            self._handle_war_click(bx, by)
            return False

        me = self.gs.me
        if not me:
            return False

        bx, by = _window_to_base(event.pos[0], event.pos[1], scale_info)
        hit = _which_panel(bx, by)
        if hit is None:
            return False

        panel, lx, ly = hit
        d = _getcourse(panel, lx, ly, me)

        action = self.config.buttonmap.get(event.button) if self.config else None
        if action:
            self._dispatch_directed(action, d)
        else:
            if event.button == 1:
                self.conn.send(cp_torp(d))
                if self.sound:
                    self.sound.play("fire_torp")
            elif event.button == 2:
                self.conn.send(cp_phaser(d))
            elif event.button == 3:
                self.conn.send(cp_direction(d))

        return False
