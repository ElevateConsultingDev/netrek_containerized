"""Window layout coordinator matching COW's newwin.c layout.

COW layout (from newwin.c):
  w (tactical):    0, 0                              TWINSIDE x TWINSIDE
  mapw (galactic): TWINSIDE+BORDER, 0                GWINSIDE x GWINSIDE
  tstatw (dash):   0, TWINSIDE+BORDER                TWINSIDE x STATSIZE
  playerw:         0, TWINSIDE+BORDER+STATSIZE+BORDER  TWINSIDE x PLISTH
  warnw:           TWINSIDE+BORDER, GWINSIDE+BORDER            GWINSIDE x MESSAGESIZE
  messagew:        TWINSIDE+BORDER, GWINSIDE+BORDER+MESSAGESIZE  GWINSIDE x MESSAGESIZE
  reviewWin:       TWINSIDE+BORDER, GWINSIDE+BORDER+MESSAGESIZE*2+BORDER  GWINSIDE x REVIEWH

Native-resolution rendering: all surfaces are created at the window's
native pixel size.  A Layout object computes every dimension from a
single scale factor.  No smoothscale post-processing step.
"""
import pygame
from .constants import (TWINSIDE, GWINSIDE, BORDER, MESSAGESIZE, STATSIZE,
                        TEAM_COLORS, TEAM_NAMES, FED, ROM, KLI, ORI, SHIP_NAMES,
                        TEAMLET, SHIPNOS, NOBODY, PALIVE, STARBASE,
                        PLREPAIR, PLFUEL, PLAGRI, PLCORE,
                        MAXPLAYER, MGOD, MTEAM, MINDIV, MALL)
from .playerlist import PLAYERLIST_HEIGHT, RANK_NAMES, _compute_ratings
from .layout import Layout

# Base (unscaled) dimensions for aspect-ratio calculation
_BASE_WIDTH = TWINSIDE + BORDER + GWINSIDE  # 1003
_LEFT_H = TWINSIDE + BORDER + STATSIZE + BORDER + PLAYERLIST_HEIGHT
_RIGHT_H = (GWINSIDE + BORDER + MESSAGESIZE * 2 + BORDER
            + PLAYERLIST_HEIGHT - MESSAGESIZE * 2 - BORDER)
_BASE_HEIGHT = max(_LEFT_H, _RIGHT_H)  # 749


class Renderer:
    def __init__(self, gamestate, sprite_mgr, config=None, statemachine=None,
                 server_host="localhost", input_handler=None):
        self.gs = gamestate
        self.sprites = sprite_mgr
        self.config = config
        self.sm = statemachine
        self.server_host = server_host
        self.input_handler = input_handler
        self.width = _BASE_WIDTH
        self.height = _BASE_HEIGHT
        self.base_width = _BASE_WIDTH
        self.base_height = _BASE_HEIGHT
        self.screen = None
        self.layout = None
        self.tactical_surface = None
        self.galactic_surface = None
        self.dashboard_surface = None
        self.playerlist_surface = None
        self.tactical_view = None
        self.galactic_view = None
        self.dashboard = None
        self.playerlist = None
        # Font cache: (font_object, size_it_was_created_at)
        self._font = None
        self._font_size = 0
        self._login_font = None
        self._login_font_size = 0
        self._team_font = None
        self._team_font_size = 0
        self._team_count_font = None
        self._team_count_font_size = 0
        # Letterbox offset
        self._ox = 0
        self._oy = 0
        self._scale_factor = 1.0
        self._scale_offset = (0, 0)

    def init(self):
        iw = int(self.base_width * 1.7)
        ih = int(self.base_height * 1.7)
        self.screen = pygame.display.set_mode((iw, ih), pygame.RESIZABLE)
        pygame.display.set_caption("Netrek")
        self._rebuild(iw, ih)

    def _rebuild(self, win_w, win_h):
        """Recompute layout, recreate surfaces, and reassign to views."""
        scale = min(win_w / self.base_width, win_h / self.base_height)
        if self.layout is None:
            self.layout = Layout(scale)
        else:
            self.layout.update(scale)
        lo = self.layout

        self._ox = (win_w - lo.width) // 2
        self._oy = (win_h - lo.height) // 2
        self._scale_factor = scale
        self._scale_offset = (self._ox, self._oy)

        self.tactical_surface = pygame.Surface((lo.twinside, lo.twinside))
        self.galactic_surface = pygame.Surface((lo.gwinside, lo.gwinside))
        self.dashboard_surface = pygame.Surface((lo.twinside, lo.statsize))
        self.playerlist_surface = pygame.Surface((lo.twinside, lo.playerlist_height))

        from .tactical import TacticalView
        from .galactic import GalacticView
        from .dashboard import Dashboard
        from .playerlist import PlayerList

        if self.tactical_view is None:
            self.tactical_view = TacticalView(
                self.tactical_surface, self.gs, self.sprites, self.config, lo)
            self.galactic_view = GalacticView(
                self.galactic_surface, self.gs, self.sprites, self.config, lo)
            self.dashboard = Dashboard(self.dashboard_surface, self.gs, lo)
            self.playerlist = PlayerList(self.playerlist_surface, self.gs, lo)
        else:
            self.tactical_view.surface = self.tactical_surface
            self.galactic_view.surface = self.galactic_surface
            self.dashboard.surface = self.dashboard_surface
            self.playerlist.surface = self.playerlist_surface
            # Invalidate font caches in views
            self.tactical_view.font = None
            self.tactical_view._waiting_font = None
            self.galactic_view.font = None
            self.dashboard.font = None
            self.dashboard.bold_font = None
            self.playerlist.font = None

        # Invalidate renderer font caches
        self._font = None
        self._login_font = None
        self._team_font = None
        self._team_count_font = None

        # Rescale sprites
        self.sprites.rescale(scale)

    def _get_font(self):
        size = self.layout.font_small
        if not self._font or self._font_size != size:
            self._font = pygame.font.SysFont("monospace", size)
            self._font_size = size
        return self._font

    def _get_login_font(self):
        size = self.layout.font_medium
        if not self._login_font or self._login_font_size != size:
            self._login_font = pygame.font.SysFont("monospace", size)
            self._login_font_size = size
        return self._login_font

    def _get_team_font(self):
        size = self.layout.font_team
        if not self._team_font or self._team_font_size != size:
            self._team_font = pygame.font.SysFont("monospace", size, bold=True)
            self._team_font_size = size
        return self._team_font

    def _get_team_count_font(self):
        size = self.layout.font_team_count
        if not self._team_count_font or self._team_count_font_size != size:
            self._team_count_font = pygame.font.SysFont("monospace", size, bold=True)
            self._team_count_font_size = size
        return self._team_count_font

    def handle_resize(self, event):
        """Handle VIDEORESIZE event — rebuild at new native resolution."""
        self.screen = pygame.display.set_mode((event.w, event.h),
                                              pygame.RESIZABLE)
        self._rebuild(event.w, event.h)

    def render(self):
        from .statemachine import State

        self.screen.fill((0, 0, 0))

        state = self.sm.state if self.sm else State.ALIVE

        if state == State.DISCONNECTED:
            self._render_disconnected()
        elif state == State.WAIT_MOTD and self.sm and self.sm.login_ready:
            self._render_login_screen()
        elif state == State.LOGIN:
            self._render_login_screen()
        elif state == State.TEAM_SELECT:
            self._render_team_select()
        elif state == State.OUTFIT:
            self._render_team_select()
        else:
            self._render_gameplay()

        pygame.display.flip()

    # --- Disconnected screen ---

    def _render_disconnected(self):
        lo = self.layout
        font = self._get_login_font()
        cx = self.screen.get_width() // 2
        cy = self.screen.get_height() // 2
        line1 = font.render("Server disconnected.", True, (255, 80, 80))
        line2 = font.render("Reconnecting...", True, (255, 255, 0))
        self.screen.blit(line1, (cx - line1.get_width() // 2, cy - lo.login_line_h))
        self.screen.blit(line2, (cx - line2.get_width() // 2, cy + 4))

    # --- Login screen ---

    def _render_login_screen(self):
        lo = self.layout
        ox, oy = self._ox, self._oy

        tac = self.tactical_surface
        tac.fill((0, 0, 0))
        font = self._get_login_font()
        sm = self.sm
        ls = sm.login_state

        y = lo.login_y_start
        text = font.render("Welcome to Netrek.", True, (255, 255, 255))
        tac.blit(text, (lo.login_x, y))
        y += lo.login_line_h
        text = font.render(f"Connected to server {self.server_host}.", True, (200, 200, 200))
        tac.blit(text, (lo.login_x, y))
        y += lo.login_gap

        if sm.login_error:
            text = font.render(sm.login_error, True, (255, 80, 80))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h

        active_name = ls == "name"
        name_color = (255, 255, 255) if active_name else (160, 160, 160)
        cursor = "_" if active_name else ""
        text = font.render(f"What is your name? : {sm.login_name}{cursor}", True, name_color)
        tac.blit(text, (lo.login_x, y))
        y += lo.login_line_h

        if ls == "query_wait":
            text = font.render("Checking name...", True, (255, 255, 0))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
        elif ls == "password":
            dots = "*" * len(sm.login_password)
            text = font.render(f"What is your password? : {dots}_", True, (0, 255, 255))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
        elif ls == "makepass1":
            text = font.render("You appear to be a new player.", True, (255, 255, 0))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
            dots = "*" * len(sm.login_password)
            text = font.render(f"Enter a password : {dots}_", True, (0, 255, 255))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
        elif ls == "makepass2":
            text = font.render("You appear to be a new player.", True, (255, 255, 0))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
            dots1 = "*" * len(sm.login_password)
            text = font.render(f"Enter a password : {dots1}", True, (100, 160, 160))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
            dots2 = "*" * len(sm.login_password_confirm)
            text = font.render(f"Confirm password : {dots2}_", True, (0, 255, 255))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h
        elif ls == "login_wait":
            text = font.render("Logging in...", True, (255, 255, 0))
            tac.blit(text, (lo.login_x, y))
            y += lo.login_line_h

        y += lo.motd_gap

        motd_font = self._get_font()
        for line in self.gs.motd_lines:
            if y > lo.twinside - lo.motd_line_h:
                break
            text = motd_font.render(line, True, (0, 200, 200))
            tac.blit(text, (lo.login_x, y))
            y += lo.motd_line_h

        self.screen.blit(tac, (ox, oy))

        # Galactic side: server info (COW showValues)
        gal = self.galactic_surface
        gal.fill((0, 0, 0))
        self._draw_server_info(gal, motd_font)
        self.screen.blit(gal, (ox + lo.twinside + lo.border, oy))

        self._draw_borders()

    # --- Team selection screen ---

    def _render_team_select(self):
        lo = self.layout
        ox, oy = self._ox, self._oy

        tac = self.tactical_surface
        tac.fill((0, 0, 0))
        font = self._get_team_font()
        count_font = self._get_team_count_font()
        small_font = self._get_font()

        counts = self.gs.team_counts()
        mask = self.gs.team_mask

        cw = lo.team_corner_size
        far = lo.team_corner_far
        corners = [
            (ROM, "Romulan",    TEAM_COLORS[ROM], 0,   0),
            (KLI, "Klingon",    TEAM_COLORS[KLI], far, 0),
            (FED, "Federation", TEAM_COLORS[FED], 0,   far),
            (ORI, "Orion",      TEAM_COLORS[ORI], far, far),
        ]

        for team_bit, label, color, cx, cy in corners:
            available = bool(mask & team_bit)
            draw_color = color if available else (80, 80, 80)

            for i in range(0, cw + cw, lo.team_hatch_step):
                x0 = cx + min(i, cw)
                y0 = cy + max(0, i - cw)
                x1 = cx + max(0, i - cw)
                y1 = cy + min(i, cw)
                pygame.draw.line(tac,
                                 (draw_color[0] // 4, draw_color[1] // 4, draw_color[2] // 4),
                                 (x0, y0), (x1, y1))

            pygame.draw.rect(tac, draw_color, (cx, cy, cw, cw), 1)

            text = font.render(label, True, draw_color)
            tx = cx + (cw - text.get_width()) // 2
            ty = cy + lo.team_name_y_offset
            tac.blit(text, (tx, ty))

            count_text = count_font.render(str(counts[team_bit]), True, draw_color)
            tx = cx + (cw - count_text.get_width()) // 2
            ty = cy + lo.team_count_y_offset
            tac.blit(count_text, (tx, ty))

        center_font = self._get_login_font()
        msg1 = "Click a team corner to join."
        text = center_font.render(msg1, True, (200, 200, 200))
        tac.blit(text, ((lo.twinside - text.get_width()) // 2, lo.team_center_y))

        ship_name = SHIP_NAMES.get(self.sm.chosen_ship, "??") if self.sm else "CA"
        ship_msg = f"Ship: {ship_name}"
        text = center_font.render(ship_msg, True, (255, 255, 255))
        tac.blit(text, ((lo.twinside - text.get_width()) // 2, lo.team_ship_y))

        legend = "s=SC d=DD c=CA b=BB a=AS g=GA o=SB"
        text = small_font.render(legend, True, (160, 160, 160))
        tac.blit(text, ((lo.twinside - text.get_width()) // 2, lo.team_legend_y))

        msg3 = "Enter/Space = join default team"
        text = small_font.render(msg3, True, (160, 160, 160))
        tac.blit(text, ((lo.twinside - text.get_width()) // 2, lo.team_enter_y))

        from .statemachine import State
        if self.sm and self.sm.state == State.OUTFIT:
            wait_msg = "Waiting for slot..."
            text = center_font.render(wait_msg, True, (255, 255, 0))
            tac.blit(text, ((lo.twinside - text.get_width()) // 2, lo.team_wait_y))

        quit_text = small_font.render("q = Quit", True, (160, 160, 160))
        tac.blit(quit_text, ((lo.twinside - quit_text.get_width()) // 2, lo.team_quit_y))

        motd_y = lo.team_motd_start_y
        for line in self.gs.motd_lines[:8]:
            if motd_y > lo.team_motd_limit_y:
                break
            text = small_font.render(line, True, (0, 160, 160))
            tac.blit(text, (lo.login_x, motd_y))
            motd_y += lo.motd_line_h

        self.screen.blit(tac, (ox, oy))

        # Galactic surface: server info (COW showValues)
        gal = self.galactic_surface
        gal.fill((0, 0, 0))
        self._draw_server_info(gal, small_font)
        self.screen.blit(gal, (ox + lo.twinside + lo.border, oy))

        self._draw_borders()

    def _draw_server_info(self, surface, font):
        """Draw server features and ship caps on galactic (COW showValues)."""
        lo = self.layout
        x = lo.motd_x
        y = lo.motd_x
        lh = lo.motd_line_h
        cyan = (0, 200, 200)
        yellow = (255, 255, 0)
        white = (200, 200, 200)
        max_y = lo.gwinside - lh

        def put(text, color=cyan):
            nonlocal y
            if y > max_y:
                return
            surface.blit(font.render(text, True, color), (x, y))
            y += lh

        put("Server options set:", yellow)
        put("")

        # Negotiated features
        if self.gs.features:
            for name, value in sorted(self.gs.features.items()):
                put(f"  {name}: {value}")
            put("")

        # Ship capabilities table
        ship_abbrevs = {0: 'SC', 1: 'DD', 2: 'CA', 3: 'BB',
                        4: 'AS', 5: 'SB', 6: 'GA', 7: 'AT'}
        put("Ship stats:", yellow)
        put("     Spd  Shd  Dam  Fuel  Arm", white)
        for sc in self.gs.ship_caps:
            name = ship_abbrevs.get(sc.s_type, '??')
            put(f"  {name}  {sc.s_maxspeed:3d}  {sc.s_maxshield:3d}  "
                f"{sc.s_maxdamage:3d}  {sc.s_maxfuel:5d}  {sc.s_maxarmies:2d}",
                white)

    # --- Normal gameplay ---

    def _render_gameplay(self):
        lo = self.layout
        ox, oy = self._ox, self._oy

        self.tactical_view.render()
        # Det circle — always visible (thin red circle showing det range)
        ih = self.input_handler
        cfg = self.config
        if not cfg or cfg.det_circle:
            self.tactical_view.draw_det_circle()
        # Auto-aim visual indicator (drawn on tactical surface before blit)
        if ih and ih.auto_aim and ih.aim_target:
            self.tactical_view.draw_aim_indicator(
                ih.aim_target, ih.aim_intercept)
        self.galactic_view.render()
        self.dashboard.render()
        sort_mode = ih.sort_mode if ih else 0
        self.playerlist.render(sort_mode=sort_mode)

        # Blit sub-surfaces to screen at native-resolution positions
        self.screen.blit(self.tactical_surface, (ox, oy))
        self.screen.blit(self.galactic_surface, (ox + lo.twinside + lo.border, oy))
        self.screen.blit(self.dashboard_surface, (ox, oy + lo.twinside + lo.border))
        plist_y = lo.twinside + lo.border + lo.statsize + lo.border
        self.screen.blit(self.playerlist_surface, (ox, oy + plist_y))

        # Warning text area (below galactic)
        warn_y = oy + lo.gwinside + lo.border
        warn_x = ox + lo.twinside + lo.border
        if self.gs.warning and self.gs.warning_timer > 0:
            font = self._get_font()
            text = font.render(self.gs.warning, True, (255, 255, 0))
            self.screen.blit(text, (warn_x + lo.msg_x_pad, warn_y + lo.msg_y_pad))
            self.gs.warning_timer -= 1

        # Message input line (below warning)
        msg_input_y = warn_y + lo.messagesize
        font = self._get_font()
        ih = self.input_handler
        if ih and ih.messageon:
            outmsg = ih.outmessage
            if outmsg:
                text = font.render(outmsg, True, (255, 255, 255))
                self.screen.blit(text, (warn_x + lo.msg_x_pad, msg_input_y + lo.msg_y_pad))
            else:
                text = font.render("[Message: select recipient] A=all T=team F/R/K/O 0-9=player_", True, (255, 255, 0))
                self.screen.blit(text, (warn_x + lo.msg_x_pad, msg_input_y + lo.msg_y_pad))
        else:
            prompt = "Talk to everyone, click here, type, press enter.  Read and learn."
            text = font.render(prompt, True, (0, 200, 200))
            self.screen.blit(text, (warn_x + lo.msg_x_pad, msg_input_y + lo.msg_y_pad))

        # Scrolling message review area
        review_y = msg_input_y + lo.messagesize + lo.border
        self._draw_messages(warn_x, review_y, lo.gwinside, lo.review_height)

        # Help overlay (covers tactical)
        if ih and ih.help_mode:
            self._draw_help_window()

        # War window overlay
        if ih and ih.war_mode:
            self._draw_war_window(ih)

        # Info window overlay
        if ih and ih.info_target:
            self._draw_info_window(ih)

        # Planet list overlay (COW planetlist.c)
        if ih and ih.planet_list_mode:
            self._draw_planet_list()

        # Rank window overlay (COW ranklist.c)
        if ih and ih.rank_window_mode:
            self._draw_rank_window()

        # Stats window overlay (COW stats.c)
        if ih and ih.stat_window_mode:
            self._draw_stat_window()

        self._draw_borders()

    # Action -> (group, description) for the help window.
    _ACTION_INFO = {
        'course':       ('Movement', 'Set course'),
        'max_speed':    ('Movement', 'Max speed'),
        'speed_down':   ('Movement', 'Speed -1'),
        'speed_up':     ('Movement', 'Speed +1'),
        'torp':         ('Weapons',  'Fire torpedo'),
        'phaser':       ('Weapons',  'Fire phaser'),
        'plasma':       ('Weapons',  'Fire plasma'),
        'det_torps':    ('Weapons',  'Det enemy torps'),
        'det_own_torp': ('Weapons',  'Det own torps'),
        'auto_aim':     ('Weapons',  'Aim indicator'),
        'shields':      ('Defense',  'Shields'),
        'cloak':        ('Defense',  'Cloak'),
        'repair':       ('Defense',  'Repair'),
        'orbit':        ('Ops',      'Orbit'),
        'bomb':         ('Ops',      'Bomb'),
        'beam_up':      ('Ops',      'Beam up armies'),
        'beam_down':    ('Ops',      'Beam down armies'),
        'tractor':      ('Ops',      'Tractor'),
        'pressor':      ('Ops',      'Pressor'),
        'tractor_off':  ('Ops',      'Tractor off'),
        'planet_lock':  ('Ops',      'Lock planet/player'),
        'player_lock':  ('Ops',      'Lock planet/base'),
        'coup':         ('Ops',      'Coup'),
        'dock_perm':    ('Ops',      'Dock permission'),
        'practice':     ('Ops',      'Practice robot'),
        'refit':        ('Ship/Team', 'Refit'),
        'war':          ('Ship/Team', 'War window'),
        'message':      ('Comms',    'Send message'),
        'macro':        ('Comms',    'Macro'),
        'info':         ('Comms',    'Info'),
        'info_ext':     ('Comms',    'Extended info'),
        'planet_list':  ('Display',  'Planet list'),
        'rank_window':  ('Display',  'Rank window'),
        'stat_window':  ('Display',  'Stats window'),
        'sort_players': ('Display',  'Sort player list'),
        'help':         ('Display',  'This help'),
        'cycle_local_planets':    ('Display', 'Cycle local planets'),
        'cycle_galactic_planets': ('Display', 'Cycle galactic planets'),
        'quit':         ('Quit',     'Self destruct'),
        'fast_quit':    ('Quit',     'Quit now'),
    }

    _HELP_GROUPS = [
        'Movement', 'Weapons', 'Defense', 'Ops',
        'Ship/Team', 'Comms', 'Distress', 'Display', 'Quit',
    ]

    @staticmethod
    def _fmt_key(ch):
        if ch == ' ':
            return 'SPC'
        if len(ch) == 1 and ord(ch) < 0x20:
            return '^' + chr(ord(ch) + 64)
        if len(ch) == 1 and 0x80 <= ord(ch) <= 0x89:
            return '^' + str(ord(ch) - 0x80)
        return ch

    def _get_help_font(self):
        size = self.layout.font_help
        k = '_help_font'
        ks = '_help_font_size'
        f = getattr(self, k, None)
        fs = getattr(self, ks, 0)
        if not f or fs != size:
            f = pygame.font.SysFont("monospace", size)
            setattr(self, k, f)
            setattr(self, ks, size)
        return f

    def _draw_help_window(self):
        lo = self.layout
        ox, oy = self._ox, self._oy
        font = self._get_help_font()
        line_h = lo.help_line_h
        pad_x = lo.help_pad_x
        pad_y = lo.help_pad_y
        col_w = lo.help_col_w

        if not self.config:
            return

        # Invert keymap: action -> [display keys]
        action_keys = {}
        for ch, act in self.config.keymap.items():
            action_keys.setdefault(act, []).append(self._fmt_key(ch))

        groups = {g: [] for g in self._HELP_GROUPS}
        speed_keys = {}   # digit -> display key
        distress_keys = []  # collect all distress display keys
        seen_actions = set()

        for act, keys in sorted(action_keys.items()):
            if act.startswith('speed_'):
                digit = act[6:]
                for k in keys:
                    speed_keys[digit] = k
                continue
            if act.startswith('distress_'):
                distress_keys.extend(keys)
                continue
            if act in seen_actions:
                continue
            seen_actions.add(act)
            keys_str = ' '.join(keys)
            info = self._ACTION_INFO.get(act)
            if info:
                grp, desc = info
            else:
                grp = 'Display'
                desc = act.replace('_', ' ')
            groups.setdefault(grp, []).append((keys_str, desc))

        # Consolidated speed line
        if speed_keys:
            digits = []
            extras = []
            for digit, display in sorted(speed_keys.items()):
                if display == digit:
                    digits.append(digit)
                else:
                    extras.append(display)
            parts = []
            if digits:
                parts.append('0-9' if len(digits) == 10 else ''.join(digits))
            parts.extend(extras)
            groups['Movement'].insert(0, (' '.join(parts), 'Set speed'))

        # Consolidated distress line
        if distress_keys:
            groups['Distress'].append((' '.join(distress_keys), 'Distress calls'))

        # Build flat entry list
        entries = []
        for grp_name in self._HELP_GROUPS:
            items = groups.get(grp_name, [])
            if not items:
                continue
            entries.append((grp_name, (0, 255, 255)))
            for keys_str, desc in items:
                entries.append((f" {keys_str:6s} {desc}", (255, 255, 255)))

        # Two-column layout
        max_rows = (lo.twinside - pad_y * 2 - line_h) // line_h
        col1 = entries[:max_rows]
        col2 = entries[max_rows:max_rows * 2]

        # Draw over tactical area
        pygame.draw.rect(self.screen, (0, 0, 0),
                         (ox, oy, lo.twinside, lo.twinside))
        pygame.draw.rect(self.screen, (160, 160, 160),
                         (ox, oy, lo.twinside, lo.twinside), 1)

        title = font.render("Key Bindings  (press h or any key to close)",
                            True, (255, 255, 0))
        self.screen.blit(title, (ox + pad_x, oy + pad_y))

        y0 = oy + pad_y + line_h + 2
        for col_idx, col in enumerate((col1, col2)):
            x = ox + pad_x + col_idx * col_w
            for i, (text, color) in enumerate(col):
                rendered = font.render(text, True, color)
                self.screen.blit(rendered, (x, y0 + i * line_h))

    def _draw_info_window(self, ih):
        lo = self.layout
        ox, oy = self._ox, self._oy
        font = self._get_font()
        line_h = lo.info_line_h
        me = self.gs.me
        if not me:
            return
        ttype, tnum = ih.info_target

        lines = []

        if ttype == 'player' and 0 <= tnum < 36:
            p = self.gs.players[tnum]
            tl = TEAMLET.get(p.team, 'I')
            sn = SHIPNOS[tnum] if tnum < len(SHIPNOS) else '?'
            ship = SHIP_NAMES.get(p.shiptype, "??")
            pcolor = TEAM_COLORS.get(p.team, (0, 255, 0))

            if ih.info_extended:
                lines.append((f"{p.name} ({tl}{sn}):", pcolor))
                lines.append((f"Login   {p.login}", pcolor))
                lines.append(("        Rating    Total", pcolor))
                hours = p.st_tticks / 36000.0 if p.st_tticks else 0.001
                b_rate = p.st_tarmsbomb / hours if hours > 0 else 0
                lines.append((f"Bombing: {b_rate:7.2f}  {p.st_tarmsbomb:5d}", pcolor))
                p_rate = p.st_tplanets / hours if hours > 0 else 0
                lines.append((f"Planets: {p_rate:7.2f}  {p.st_tplanets:5d}", pcolor))
                if p.shiptype == STARBASE:
                    kph = p.st_sbkills / hours if hours > 0 else 0
                    lines.append((f"KPH:     {kph:7.2f}  {p.st_sbkills:5d}", pcolor))
                else:
                    o_rate = p.st_tkills / hours if hours > 0 else 0
                    lines.append((f"Offense: {o_rate:7.2f}  {p.st_tkills:5d}", pcolor))
                if p.shiptype == STARBASE:
                    dph = p.st_sblosses / hours if hours > 0 else 0
                    lines.append((f"DPH:     {dph:7.2f}  {p.st_sblosses:5d}", pcolor))
                else:
                    d_rate = p.st_tlosses / hours if hours > 0 else 0
                    lines.append((f"Defense: {d_rate:7.2f}  {p.st_tlosses:5d}", pcolor))
                lines.append((f"  Maxkills: {p.st_maxkills:6.2f}", pcolor))
                lines.append((f"  Hours:    {hours:6.2f}", pcolor))
            else:
                lines.append((f"{p.name} ({tl}{sn})", pcolor))
                lines.append((f"Speed:   {p.speed}", pcolor))
                lines.append((f"kills:   {p.kills / 100.0:.2f}", pcolor))
                lines.append((f"Ship Type: {ship}", pcolor))
                if p.swar & me.team:
                    lines.append(("WAR", (255, 60, 60)))
                elif p.hostile & me.team:
                    lines.append(("HOSTILE", (255, 255, 0)))
                else:
                    lines.append(("PEACEFUL", (0, 255, 0)))
                lines.append((f"{p.login}", pcolor))

        elif ttype == 'planet' and 0 <= tnum < 40:
            pl = self.gs.planets[tnum]
            owner_l = TEAMLET.get(pl.owner, 'I')
            pcolor = TEAM_COLORS.get(pl.owner, (170, 170, 170))
            lines.append((f"{pl.name} ({owner_l})", pcolor))
            if pl.info & me.team:
                lines.append((f"Armies {pl.armies}", pcolor))
                repair = "REPAIR" if pl.flags & PLREPAIR else "      "
                fuel = "FUEL" if pl.flags & PLFUEL else "    "
                agri = "AGRI" if pl.flags & PLAGRI else "    "
                core = "CORE" if pl.flags & PLCORE else "    "
                f_str = "F" if pl.info & FED else " "
                r_str = "R" if pl.info & ROM else " "
                k_str = "K" if pl.info & KLI else " "
                o_str = "O" if pl.info & ORI else " "
                lines.append((f"{repair} {fuel} {agri} {core} {f_str}{r_str}{k_str}{o_str}", pcolor))
            else:
                lines.append(("No other info", (170, 170, 170)))

        if not lines:
            return

        max_w = max(font.size(t)[0] for t, c in lines) + lo.info_padding
        h = len(lines) * line_h + lo.info_padding // 2
        ix = ox + lo.twinside + lo.border + lo.gwinside - max_w - lo.info_margin
        iy = oy + lo.gwinside // 2 - h // 2

        pygame.draw.rect(self.screen, (0, 0, 0), (ix, iy, max_w, h))
        pygame.draw.rect(self.screen, (160, 160, 160), (ix, iy, max_w, h), 1)
        for i, (text, color) in enumerate(lines):
            rendered = font.render(text, True, color)
            self.screen.blit(rendered, (ix + lo.info_padding // 2, iy + 2 + i * line_h))

    def _draw_war_window(self, ih):
        lo = self.layout
        ox, oy = self._ox, self._oy
        font = self._get_font()
        me = self.gs.me
        if not me:
            return
        wx = ox + lo.war_x
        wy = oy + lo.war_y
        row_h = lo.war_row_h
        w = lo.war_w
        h = row_h * 6

        pygame.draw.rect(self.screen, (0, 0, 0), (wx, wy, w, h))
        pygame.draw.rect(self.screen, (160, 160, 160), (wx, wy, w, h), 1)

        teams = [(FED, "FED"), (ROM, "ROM"), (KLI, "KLI"), (ORI, "ORI")]
        for i, (team_bit, name) in enumerate(teams):
            y = wy + i * row_h
            if me.swar & team_bit:
                color = (255, 60, 60)
                status = "WAR"
            elif ih._war_hostile & team_bit:
                color = (255, 255, 0)
                status = "HOSTILE"
            else:
                color = (0, 255, 0)
                status = "PEACE"
            if me.team == team_bit:
                color = (100, 100, 100)
                status = "(own)"
            text = font.render(f" {name} - {status}", True, color)
            self.screen.blit(text, (wx + 4, y + 2))
            pygame.draw.line(self.screen, (80, 80, 80),
                             (wx, y + row_h), (wx + w, y + row_h))

        y = wy + 4 * row_h
        text = font.render("  Save", True, (255, 255, 255))
        self.screen.blit(text, (wx + 4, y + 2))
        pygame.draw.line(self.screen, (80, 80, 80),
                         (wx, y + row_h), (wx + w, y + row_h))

        y = wy + 5 * row_h
        text = font.render("  Exit - no change", True, (160, 160, 160))
        self.screen.blit(text, (wx + 4, y + 2))

    def _draw_planet_list(self):
        """Draw planet list overlay on tactical area (COW planetlist.c)."""
        lo = self.layout
        ox, oy = self._ox, self._oy
        font = self._get_help_font()
        line_h = lo.help_line_h
        pad_x = lo.help_pad_x
        pad_y = lo.help_pad_y
        me = self.gs.me
        if not me:
            return

        pygame.draw.rect(self.screen, (0, 0, 0),
                         (ox, oy, lo.twinside, lo.twinside))
        pygame.draw.rect(self.screen, (160, 160, 160),
                         (ox, oy, lo.twinside, lo.twinside), 1)

        title = font.render("Planet List  (press P to close)", True, (255, 255, 0))
        self.screen.blit(title, (ox + pad_x, oy + pad_y))

        header = font.render(
            " Planet          Owner  Arm  REPR FUEL AGRI CORE  Info", True, (200, 200, 200))
        self.screen.blit(header, (ox + pad_x, oy + pad_y + line_h))

        y = oy + pad_y + line_h * 2
        for pl in self.gs.planets:
            if y + line_h > oy + lo.twinside - pad_y:
                break
            if not pl.name:
                continue
            owner_l = TEAM_NAMES.get(pl.owner, "Ind")
            color = TEAM_COLORS.get(pl.owner, (170, 170, 170))

            if pl.info & me.team:
                repair = "REPR" if pl.flags & PLREPAIR else "    "
                fuel = "FUEL" if pl.flags & PLFUEL else "    "
                agri = "AGRI" if pl.flags & PLAGRI else "    "
                core = "CORE" if pl.flags & PLCORE else "    "
                armies = f"{pl.armies:4d}"
            else:
                repair = fuel = agri = core = "    "
                armies = "   ?"

            f_str = "F" if pl.info & FED else " "
            r_str = "R" if pl.info & ROM else " "
            k_str = "K" if pl.info & KLI else " "
            o_str = "O" if pl.info & ORI else " "
            info_str = f"{f_str}{r_str}{k_str}{o_str}"

            line = f" {pl.name:16s} {owner_l:3s}  {armies}  {repair} {fuel} {agri} {core}  {info_str}"
            text = font.render(line, True, color)
            self.screen.blit(text, (ox + pad_x, y))
            y += line_h

    def _draw_rank_window(self):
        """Draw rank requirements overlay on tactical area (COW ranklist.c)."""
        lo = self.layout
        ox, oy = self._ox, self._oy
        font = self._get_help_font()
        line_h = lo.help_line_h
        pad_x = lo.help_pad_x
        pad_y = lo.help_pad_y

        # COW rank requirements: (DI, battles, planets, bombing)
        RANK_REQS = [
            (0.0,    0, 0, 0),      # Ensign
            (2.0,    0, 0, 0),      # Lieutenant
            (4.0,    0, 0, 0),      # Lt. Commander
            (8.0,    0, 0, 0),      # Commander
            (15.0,   0, 0, 0),      # Captain
            (30.0,   0, 0, 0),      # Fleet Captain
            (60.0,   0, 0, 0),      # Commodore
            (100.0,  0, 0, 0),      # Rear Admiral
            (200.0,  0, 0, 0),      # Admiral
        ]

        pygame.draw.rect(self.screen, (0, 0, 0),
                         (ox, oy, lo.twinside, lo.twinside))
        pygame.draw.rect(self.screen, (160, 160, 160),
                         (ox, oy, lo.twinside, lo.twinside), 1)

        title = font.render("Rank Requirements  (press U to close)", True, (255, 255, 0))
        self.screen.blit(title, (ox + pad_x, oy + pad_y))

        header = font.render(" Rank             DI Required", True, (200, 200, 200))
        self.screen.blit(header, (ox + pad_x, oy + pad_y + line_h))

        me = self.gs.me
        my_rank = me.rank if me else 0
        my_di = 0.0
        if me:
            _, _, _, _, _, my_di = _compute_ratings(me, self.gs)

        y = oy + pad_y + line_h * 2
        for i, name in enumerate(RANK_NAMES):
            if y + line_h > oy + lo.twinside - pad_y:
                break
            di_req = RANK_REQS[i][0] if i < len(RANK_REQS) else 0
            if i == my_rank:
                color = (255, 255, 0)
                marker = ">>>"
            elif i < my_rank:
                color = (0, 200, 0)
                marker = "   "
            else:
                color = (200, 200, 200)
                marker = "   "
            line = f" {marker} {name:12s}  {di_req:8.1f}"
            text = font.render(line, True, color)
            self.screen.blit(text, (ox + pad_x, y))
            y += line_h

        y += line_h
        if me:
            di_line = f" Your DI: {my_di:.2f}   Rank: {RANK_NAMES[my_rank] if my_rank < len(RANK_NAMES) else '?'}"
            text = font.render(di_line, True, (0, 255, 255))
            self.screen.blit(text, (ox + pad_x, y))

    def _draw_stat_window(self):
        """Draw personal stats overlay on tactical area (COW stats.c)."""
        lo = self.layout
        ox, oy = self._ox, self._oy
        font = self._get_help_font()
        line_h = lo.help_line_h
        pad_x = lo.help_pad_x
        pad_y = lo.help_pad_y
        me = self.gs.me
        if not me:
            return

        wins, losses, ratio, oRating, dRating, di = _compute_ratings(me, self.gs)
        hours = me.st_tticks / 36000.0 if me.st_tticks else 0
        bRating = me.st_tarmsbomb * self.gs.status_timeprod / (
            me.st_tticks * self.gs.status_armsbomb) if me.st_tticks else 0
        pRating = me.st_tplanets * self.gs.status_timeprod / (
            me.st_tticks * self.gs.status_planets) if me.st_tticks else 0

        pygame.draw.rect(self.screen, (0, 0, 0),
                         (ox, oy, lo.twinside, lo.twinside))
        pygame.draw.rect(self.screen, (160, 160, 160),
                         (ox, oy, lo.twinside, lo.twinside), 1)

        title = font.render("Personal Statistics  (press S to close)", True, (255, 255, 0))
        self.screen.blit(title, (ox + pad_x, oy + pad_y))

        stats = [
            (f"Name:     {me.name}", (0, 255, 255)),
            (f"Rank:     {RANK_NAMES[me.rank] if me.rank < len(RANK_NAMES) else '?'}", (0, 255, 255)),
            ("", (0, 0, 0)),
            ("             Rating     Total", (200, 200, 200)),
            (f"Offense:  {oRating:8.2f}   {me.st_tkills:6d} kills", (200, 200, 200)),
            (f"Defense:  {dRating:8.2f}   {me.st_tlosses:6d} losses", (200, 200, 200)),
            (f"Bombing:  {bRating:8.2f}   {me.st_tarmsbomb:6d} armies", (200, 200, 200)),
            (f"Planets:  {pRating:8.2f}   {me.st_tplanets:6d} planets", (200, 200, 200)),
            ("", (0, 0, 0)),
            (f"T-Wins:   {wins:8d}   T-Losses: {losses:6d}   Ratio: {ratio:.2f}", (200, 200, 200)),
            (f"Max Kills: {me.st_maxkills:7.2f}", (200, 200, 200)),
            (f"SB Max:    {me.st_sbmaxkills:7.2f}", (200, 200, 200)),
            ("", (0, 0, 0)),
            (f"Hours:    {hours:8.2f}", (200, 200, 200)),
            (f"DI:       {di:8.2f}", (255, 255, 0)),
        ]

        y = oy + pad_y + line_h + 4
        for text_str, color in stats:
            if y + line_h > oy + lo.twinside - pad_y:
                break
            if text_str:
                text = font.render(text_str, True, color)
                self.screen.blit(text, (ox + pad_x, y))
            y += line_h

    def _draw_borders(self):
        lo = self.layout
        ox, oy = self._ox, self._oy
        plist_y = lo.twinside + lo.border + lo.statsize + lo.border
        warn_y = lo.gwinside + lo.border
        warn_x = lo.twinside + lo.border
        review_y = warn_y + lo.messagesize * 2 + lo.border
        border_color = (100, 100, 100)

        # Vertical divider between tactical and galactic columns
        pygame.draw.line(self.screen, border_color,
                         (ox + lo.twinside, oy),
                         (ox + lo.twinside, oy + lo.height))
        # Below tactical
        pygame.draw.line(self.screen, border_color,
                         (ox, oy + lo.twinside),
                         (ox + lo.twinside, oy + lo.twinside))
        # Above player list
        pygame.draw.line(self.screen, border_color,
                         (ox, oy + plist_y),
                         (ox + lo.twinside, oy + plist_y))
        # Below galactic
        pygame.draw.line(self.screen, border_color,
                         (ox + warn_x, oy + lo.gwinside),
                         (ox + lo.width, oy + lo.gwinside))
        # Between warning and message input
        pygame.draw.line(self.screen, border_color,
                         (ox + warn_x, oy + warn_y + lo.messagesize),
                         (ox + lo.width, oy + warn_y + lo.messagesize))
        # Above review area
        pygame.draw.line(self.screen, border_color,
                         (ox + warn_x, oy + review_y),
                         (ox + lo.width, oy + review_y))

    def _draw_messages(self, x, y, width, height):
        font = self._get_font()
        lo = self.layout
        line_h = lo.motd_line_h
        max_lines = height // line_h
        ih = self.input_handler
        scroll = ih.msg_scroll if ih else 0
        total = len(self.gs.messages)
        end = total - scroll
        start = max(0, end - max_lines)
        msgs = self.gs.messages[start:end] if end > 0 else []

        if not msgs:
            hint = font.render("(messages appear here — PgUp/PgDn to scroll)",
                               True, (60, 60, 60))
            self.screen.blit(hint, (x + lo.msg_x_pad, y + lo.msg_y_pad))
            return

        # Reserve bottom line for scroll indicator when scrolled
        avail_lines = max_lines - 1 if scroll > 0 else max_lines
        cy = y + height - line_h
        if scroll > 0:
            cy -= line_h
        for msg_entry in reversed(msgs[:avail_lines]):
            if cy < y:
                break
            if isinstance(msg_entry, tuple):
                msg_text = msg_entry[0]
            else:
                msg_text = msg_entry
            if not msg_text:
                cy -= line_h
                continue
            color = self._message_color(msg_entry)
            text = font.render(msg_text, True, color)
            self.screen.blit(text, (x + lo.msg_x_pad, cy))
            cy -= line_h

        # Scroll indicator
        if scroll > 0:
            indicator = font.render(f"-- {scroll} more below (PgDn) --", True, (255, 255, 0))
            self.screen.blit(indicator, (x + lo.msg_x_pad, y + height - line_h))

    def _message_color(self, msg_entry):
        if isinstance(msg_entry, tuple):
            text, flags, m_from = msg_entry
        else:
            text, flags, m_from = msg_entry, 0, 0

        if flags & MGOD:
            return (255, 255, 0)
        if flags & MTEAM:
            if 0 <= m_from < MAXPLAYER:
                team = self.gs.players[m_from].team
                return TEAM_COLORS.get(team, (0, 255, 0))
            return (0, 255, 0)
        if flags & MINDIV:
            return (255, 255, 255)
        if flags & MALL:
            return (0, 255, 0)

        if text.startswith("GOD->") or text.startswith("Kathy->"):
            return (255, 255, 0)
        return (0, 255, 0)

    @property
    def scale_info(self):
        """Return (scale_factor, offset_x, offset_y) for mouse coord mapping."""
        return (self._scale_factor, self._ox, self._oy)

    @property
    def tactical_offset(self):
        """Top-left position of the tactical surface on screen."""
        return (0, 0)
