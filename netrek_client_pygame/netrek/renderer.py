"""Window layout coordinator matching COW's newwin.c layout.

COW layout (from newwin.c):
  w (tactical):    0, 0                              TWINSIDE x TWINSIDE
  mapw (galactic): TWINSIDE+BORDER, 0                GWINSIDE x GWINSIDE
  tstatw (dash):   0, TWINSIDE+BORDER                TWINSIDE x STATSIZE
  playerw:         0, TWINSIDE+BORDER+STATSIZE+BORDER  TWINSIDE x PLISTH
  warnw:           TWINSIDE+BORDER, GWINSIDE+BORDER            GWINSIDE x MESSAGESIZE
  messagew:        TWINSIDE+BORDER, GWINSIDE+BORDER+MESSAGESIZE  GWINSIDE x MESSAGESIZE
  reviewWin:       TWINSIDE+BORDER, GWINSIDE+BORDER+MESSAGESIZE*2+BORDER  GWINSIDE x REVIEWH
"""
import pygame
from .constants import (TWINSIDE, GWINSIDE, BORDER, MESSAGESIZE, STATSIZE,
                        TEAM_COLORS, FED, ROM, KLI, ORI, SHIP_NAMES,
                        TEAMLET, SHIPNOS, NOBODY, PALIVE, STARBASE,
                        PLREPAIR, PLFUEL, PLAGRI, PLCORE)
from .playerlist import PLAYERLIST_HEIGHT

# Height of the scrolling message review area on the right
REVIEW_HEIGHT = PLAYERLIST_HEIGHT - MESSAGESIZE * 2 - BORDER


class Renderer:
    def __init__(self, gamestate, sprite_mgr, config=None, statemachine=None,
                 server_host="localhost", input_handler=None):
        self.gs = gamestate
        self.sprites = sprite_mgr
        self.config = config
        self.sm = statemachine
        self.server_host = server_host
        self.input_handler = input_handler
        # Total window dimensions matching COW with player list + messages
        self.width = TWINSIDE + BORDER + GWINSIDE
        left_h = TWINSIDE + BORDER + STATSIZE + BORDER + PLAYERLIST_HEIGHT
        right_h = GWINSIDE + BORDER + MESSAGESIZE * 2 + BORDER + REVIEW_HEIGHT
        self.height = max(left_h, right_h)
        self.screen = None
        self.tactical_surface = None
        self.galactic_surface = None
        self.dashboard_surface = None
        self.playerlist_surface = None
        self.tactical_view = None
        self.galactic_view = None
        self.dashboard = None
        self.playerlist = None
        self.font = None
        self._login_font = None
        self._team_font = None
        self._team_count_font = None

    def init(self):
        iw = int(self.width * 1.7)
        ih = int(self.height * 1.7)
        self.screen = pygame.display.set_mode((iw, ih), pygame.RESIZABLE)
        pygame.display.set_caption("Netrek")
        self.base_width = self.width
        self.base_height = self.height
        self.render_surface = pygame.Surface((self.width, self.height))
        self.tactical_surface = pygame.Surface((TWINSIDE, TWINSIDE))
        self.galactic_surface = pygame.Surface((GWINSIDE, GWINSIDE))
        self.dashboard_surface = pygame.Surface((TWINSIDE, STATSIZE))
        self.playerlist_surface = pygame.Surface((TWINSIDE, PLAYERLIST_HEIGHT))

        from .tactical import TacticalView
        from .galactic import GalacticView
        from .dashboard import Dashboard
        from .playerlist import PlayerList
        self.tactical_view = TacticalView(self.tactical_surface, self.gs, self.sprites, self.config)
        self.galactic_view = GalacticView(self.galactic_surface, self.gs, self.sprites, self.config)
        self.dashboard = Dashboard(self.dashboard_surface, self.gs)
        self.playerlist = PlayerList(self.playerlist_surface, self.gs)

    def _get_font(self):
        if not self.font:
            self.font = pygame.font.SysFont("monospace", 11)
        return self.font

    def _get_login_font(self):
        if not self._login_font:
            self._login_font = pygame.font.SysFont("monospace", 14)
        return self._login_font

    def _get_team_font(self):
        if not self._team_font:
            self._team_font = pygame.font.SysFont("monospace", 16, bold=True)
        return self._team_font

    def _get_team_count_font(self):
        if not self._team_count_font:
            self._team_count_font = pygame.font.SysFont("monospace", 36, bold=True)
        return self._team_count_font

    def handle_resize(self, event):
        """Handle VIDEORESIZE event."""
        self.screen = pygame.display.set_mode((event.w, event.h),
                                              pygame.RESIZABLE)

    def render(self):
        from .statemachine import State

        surf = self.render_surface
        surf.fill((0, 0, 0))

        state = self.sm.state if self.sm else State.ALIVE

        if state == State.WAIT_MOTD and self.sm and self.sm.login_ready:
            self._render_login_screen(surf)
        elif state == State.LOGIN:
            self._render_login_screen(surf)
        elif state == State.TEAM_SELECT:
            self._render_team_select(surf)
        elif state == State.OUTFIT:
            self._render_team_select(surf)
        else:
            self._render_gameplay(surf)

        # Scale to window preserving aspect ratio (letterboxed)
        win_w, win_h = self.screen.get_size()
        if (win_w, win_h) == (self.base_width, self.base_height):
            self._scale_offset = (0, 0)
            self._scale_factor = 1.0
            self.screen.blit(surf, (0, 0))
        else:
            self.screen.fill((0, 0, 0))
            scale = min(win_w / self.base_width, win_h / self.base_height)
            sw = int(self.base_width * scale)
            sh = int(self.base_height * scale)
            ox = (win_w - sw) // 2
            oy = (win_h - sh) // 2
            self._scale_offset = (ox, oy)
            self._scale_factor = scale
            scaled = pygame.transform.smoothscale(surf, (sw, sh))
            self.screen.blit(scaled, (ox, oy))

        pygame.display.flip()

    # --- Login screen ---

    def _render_login_screen(self, surf):
        """Draw login prompts overlaid on the MOTD on the tactical surface.

        Renders different prompts based on sm.login_state, matching COW's
        getname.c flow: name -> query -> password/makepass -> login.
        """
        tac = self.tactical_surface
        tac.fill((0, 0, 0))
        font = self._get_login_font()
        sm = self.sm
        ls = sm.login_state

        y = 20
        # Welcome header
        text = font.render("Welcome to Netrek.", True, (255, 255, 255))
        tac.blit(text, (20, y))
        y += 22
        text = font.render(f"Connected to server {self.server_host}.", True, (200, 200, 200))
        tac.blit(text, (20, y))
        y += 40

        # Error message (red, above prompts)
        if sm.login_error:
            text = font.render(sm.login_error, True, (255, 80, 80))
            tac.blit(text, (20, y))
            y += 22

        # Name prompt (always shown, editable only in "name" state)
        active_name = ls == "name"
        name_color = (255, 255, 255) if active_name else (160, 160, 160)
        cursor = "_" if active_name else ""
        text = font.render(f"What is your name? : {sm.login_name}{cursor}", True, name_color)
        tac.blit(text, (20, y))
        y += 22

        # State-dependent prompts below the name
        if ls == "query_wait":
            text = font.render("Checking name...", True, (255, 255, 0))
            tac.blit(text, (20, y))
            y += 22

        elif ls == "password":
            dots = "*" * len(sm.login_password)
            text = font.render(f"What is your password? : {dots}_", True, (0, 255, 255))
            tac.blit(text, (20, y))
            y += 22

        elif ls == "makepass1":
            text = font.render("You appear to be a new player.", True, (255, 255, 0))
            tac.blit(text, (20, y))
            y += 22
            dots = "*" * len(sm.login_password)
            text = font.render(f"Enter a password : {dots}_", True, (0, 255, 255))
            tac.blit(text, (20, y))
            y += 22

        elif ls == "makepass2":
            text = font.render("You appear to be a new player.", True, (255, 255, 0))
            tac.blit(text, (20, y))
            y += 22
            dots1 = "*" * len(sm.login_password)
            text = font.render(f"Enter a password : {dots1}", True, (100, 160, 160))
            tac.blit(text, (20, y))
            y += 22
            dots2 = "*" * len(sm.login_password_confirm)
            text = font.render(f"Confirm password : {dots2}_", True, (0, 255, 255))
            tac.blit(text, (20, y))
            y += 22

        elif ls == "login_wait":
            text = font.render("Logging in...", True, (255, 255, 0))
            tac.blit(text, (20, y))
            y += 22

        y += 18  # spacing before MOTD

        # MOTD lines
        motd_font = self._get_font()
        for line in self.gs.motd_lines:
            if y > TWINSIDE - 14:
                break
            text = motd_font.render(line, True, (0, 200, 200))
            tac.blit(text, (20, y))
            y += 14

        surf.blit(tac, (0, 0))

        # Galactic side: show MOTD
        gal = self.galactic_surface
        gal.fill((0, 0, 0))
        gy = 10
        motd_font = self._get_font()
        header = motd_font.render("Message of the Day:", True, (255, 255, 0))
        gal.blit(header, (10, gy))
        gy += 18
        for line in self.gs.motd_lines:
            if gy > GWINSIDE - 14:
                break
            text = motd_font.render(line, True, (0, 200, 200))
            gal.blit(text, (10, gy))
            gy += 14
        surf.blit(gal, (TWINSIDE + BORDER, 0))

        # Draw border
        self._draw_borders(surf)

    # --- Team selection screen ---

    def _render_team_select(self, surf):
        """Draw team selection corners with player counts."""
        tac = self.tactical_surface
        tac.fill((0, 0, 0))
        font = self._get_team_font()
        count_font = self._get_team_count_font()
        small_font = self._get_font()

        counts = self.gs.team_counts()
        mask = self.gs.team_mask

        # Four team corners: (team_bit, label, color, x_range, y_range)
        corners = [
            (ROM, "Romulan",    TEAM_COLORS[ROM], 0,   0),
            (KLI, "Klingon",    TEAM_COLORS[KLI], 380, 0),
            (FED, "Federation", TEAM_COLORS[FED], 0,   380),
            (ORI, "Orion",      TEAM_COLORS[ORI], 380, 380),
        ]

        corner_w, corner_h = 120, 120

        for team_bit, label, color, cx, cy in corners:
            available = bool(mask & team_bit)
            draw_color = color if available else (80, 80, 80)

            # Diagonal hatch fill
            for i in range(0, corner_w + corner_h, 8):
                x0 = cx + min(i, corner_w)
                y0 = cy + max(0, i - corner_w)
                x1 = cx + max(0, i - corner_h)
                y1 = cy + min(i, corner_h)
                pygame.draw.line(tac, (draw_color[0] // 4, draw_color[1] // 4, draw_color[2] // 4),
                                 (x0, y0), (x1, y1))

            # Border
            pygame.draw.rect(tac, draw_color, (cx, cy, corner_w, corner_h), 1)

            # Team name
            text = font.render(label, True, draw_color)
            tx = cx + (corner_w - text.get_width()) // 2
            ty = cy + 15
            tac.blit(text, (tx, ty))

            # Player count
            count_text = count_font.render(str(counts[team_bit]), True, draw_color)
            tx = cx + (corner_w - count_text.get_width()) // 2
            ty = cy + 50
            tac.blit(count_text, (tx, ty))

        # Center text
        center_font = self._get_login_font()
        msg1 = "Click a team corner to join."
        text = center_font.render(msg1, True, (200, 200, 200))
        tac.blit(text, ((TWINSIDE - text.get_width()) // 2, 180))

        # Current ship selection (COW: chosen_ship, default CRUISER)
        ship_name = SHIP_NAMES.get(self.sm.chosen_ship, "??") if self.sm else "CA"
        ship_msg = f"Ship: {ship_name}"
        text = center_font.render(ship_msg, True, (255, 255, 255))
        tac.blit(text, ((TWINSIDE - text.get_width()) // 2, 210))

        # Ship key legend (COW newwin.c outfit keys)
        legend = "s=SC d=DD c=CA b=BB a=AS g=GA o=SB"
        text = small_font.render(legend, True, (160, 160, 160))
        tac.blit(text, ((TWINSIDE - text.get_width()) // 2, 235))

        msg3 = "Enter/Space = join default team"
        text = small_font.render(msg3, True, (160, 160, 160))
        tac.blit(text, ((TWINSIDE - text.get_width()) // 2, 252))

        from .statemachine import State
        if self.sm and self.sm.state == State.OUTFIT:
            wait_msg = "Waiting for slot..."
            text = center_font.render(wait_msg, True, (255, 255, 0))
            tac.blit(text, ((TWINSIDE - text.get_width()) // 2, 275))

        # Quit label at bottom center
        quit_text = small_font.render("q = Quit", True, (160, 160, 160))
        tac.blit(quit_text, ((TWINSIDE - quit_text.get_width()) // 2, TWINSIDE - 20))

        # MOTD excerpt in center area
        motd_y = 310
        for line in self.gs.motd_lines[:8]:
            if motd_y > 370:
                break
            text = small_font.render(line, True, (0, 160, 160))
            tac.blit(text, (20, motd_y))
            motd_y += 14

        surf.blit(tac, (0, 0))

        # Galactic surface: server info
        gal = self.galactic_surface
        gal.fill((0, 0, 0))
        gy = 10
        header = small_font.render("Server options set:", True, (255, 255, 0))
        gal.blit(header, (10, gy))
        gy += 18
        for line in self.gs.motd_lines:
            if gy > GWINSIDE - 14:
                break
            text = small_font.render(line, True, (0, 200, 200))
            gal.blit(text, (10, gy))
            gy += 14
        surf.blit(gal, (TWINSIDE + BORDER, 0))

        self._draw_borders(surf)

    # --- Normal gameplay ---

    def _render_gameplay(self, surf):
        """Render normal tactical/galactic/dashboard/messages."""
        # Render sub-views
        self.tactical_view.render()
        self.galactic_view.render()
        self.dashboard.render()
        self.playerlist.render()

        # Blit to offscreen surface at COW positions
        surf.blit(self.tactical_surface, (0, 0))
        surf.blit(self.galactic_surface, (TWINSIDE + BORDER, 0))
        surf.blit(self.dashboard_surface, (0, TWINSIDE + BORDER))
        plist_y = TWINSIDE + BORDER + STATSIZE + BORDER
        surf.blit(self.playerlist_surface, (0, plist_y))

        # Warning text area (below galactic) - COW uses yellow for warnings
        warn_y = GWINSIDE + BORDER
        warn_x = TWINSIDE + BORDER
        if self.gs.warning and self.gs.warning_timer > 0:
            font = self._get_font()
            text = font.render(self.gs.warning, True, (255, 255, 0))
            surf.blit(text, (warn_x + 2, warn_y + 3))
            self.gs.warning_timer -= 1

        # Message input line (below warning) — COW's messagew
        msg_input_y = warn_y + MESSAGESIZE
        font = self._get_font()
        ih = self.input_handler
        if ih and ih.messageon:
            outmsg = ih.outmessage
            if outmsg:
                # Active message composition — show in white (COW textColor)
                text = font.render(outmsg, True, (255, 255, 255))
                surf.blit(text, (warn_x + 2, msg_input_y + 3))
            else:
                # messageon but no recipient yet — waiting for address key
                text = font.render("[Message: select recipient] A=all T=team F/R/K/O 0-9=player_", True, (255, 255, 0))
                surf.blit(text, (warn_x + 2, msg_input_y + 3))
        else:
            # COW default prompt when not in message mode (smessage.c DisplayMessage)
            prompt = "Talk to everyone, click here, type, press enter.  Read and learn."
            text = font.render(prompt, True, (0, 200, 200))
            surf.blit(text, (warn_x + 2, msg_input_y + 3))

        # Scrolling message review area (below message input)
        review_y = msg_input_y + MESSAGESIZE + BORDER
        self._draw_messages(warn_x, review_y, GWINSIDE, REVIEW_HEIGHT)

        # War window overlay (COW war.c)
        if ih and ih.war_mode:
            self._draw_war_window(surf, ih)

        # Info window overlay (COW inform.c)
        if ih and ih.info_target:
            self._draw_info_window(surf, ih)

        self._draw_borders(surf)

    def _draw_info_window(self, surf, ih):
        """Draw player or planet info popup (COW inform.c inform()).

        Quick info ('i'): name, speed, kills, ship type, war status, login
        Extended info ('I'): name, login, rating/total stats table
        Planet info: name(owner), armies, flags, team knowledge
        """
        font = self._get_font()
        line_h = 14
        me = self.gs.me
        if not me:
            return
        ttype, tnum = ih.info_target

        lines = []  # list of (text, color)

        if ttype == 'player' and 0 <= tnum < 36:
            p = self.gs.players[tnum]
            tl = TEAMLET.get(p.team, 'I')
            sn = SHIPNOS[tnum] if tnum < len(SHIPNOS) else '?'
            ship = SHIP_NAMES.get(p.shiptype, "??")
            pcolor = TEAM_COLORS.get(p.team, (0, 255, 0))

            if ih.info_extended:
                # Extended stats (COW 'I' — 10 lines)
                lines.append((f"{p.name} ({tl}{sn}):", pcolor))
                lines.append((f"Login   {p.login}", pcolor))
                lines.append(("        Rating    Total", pcolor))
                # Bombing
                hours = p.st_tticks / 36000.0 if p.st_tticks else 0.001
                b_rate = p.st_tarmsbomb / hours if hours > 0 else 0
                lines.append((f"Bombing: {b_rate:7.2f}  {p.st_tarmsbomb:5d}", pcolor))
                # Planets
                p_rate = p.st_tplanets / hours if hours > 0 else 0
                lines.append((f"Planets: {p_rate:7.2f}  {p.st_tplanets:5d}", pcolor))
                # Offense
                if p.shiptype == STARBASE:
                    kph = p.st_sbkills / hours if hours > 0 else 0
                    lines.append((f"KPH:     {kph:7.2f}  {p.st_sbkills:5d}", pcolor))
                else:
                    o_rate = p.st_tkills / hours if hours > 0 else 0
                    lines.append((f"Offense: {o_rate:7.2f}  {p.st_tkills:5d}", pcolor))
                # Defense
                if p.shiptype == STARBASE:
                    dph = p.st_sblosses / hours if hours > 0 else 0
                    lines.append((f"DPH:     {dph:7.2f}  {p.st_sblosses:5d}", pcolor))
                else:
                    d_rate = p.st_tlosses / hours if hours > 0 else 0
                    lines.append((f"Defense: {d_rate:7.2f}  {p.st_tlosses:5d}", pcolor))
                lines.append((f"  Maxkills: {p.st_maxkills:6.2f}", pcolor))
                lines.append((f"  Hours:    {hours:6.2f}", pcolor))
            else:
                # Quick info (COW 'i' — 6 lines)
                lines.append((f"{p.name} ({tl}{sn})", pcolor))
                lines.append((f"Speed:   {p.speed}", pcolor))
                lines.append((f"kills:   {p.kills / 100.0:.2f}", pcolor))
                lines.append((f"Ship Type: {ship}", pcolor))
                # War status (COW inform.c: check p_swar & my team)
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

        # Compute popup size and position (near galactic top-right)
        max_w = max(font.size(t)[0] for t, c in lines) + 8
        h = len(lines) * line_h + 4
        # Position: near mouse cursor on tactical, or galactic side
        ix = TWINSIDE + BORDER + GWINSIDE - max_w - 10
        iy = GWINSIDE // 2 - h // 2

        pygame.draw.rect(surf, (0, 0, 0), (ix, iy, max_w, h))
        pygame.draw.rect(surf, (160, 160, 160), (ix, iy, max_w, h), 1)
        for i, (text, color) in enumerate(lines):
            rendered = font.render(text, True, color)
            surf.blit(rendered, (ix + 4, iy + 2 + i * line_h))

    def _draw_war_window(self, surf, ih):
        """Draw the war declaration menu overlay (COW war.c warrefresh).

        6 rows: FED/ROM/KLI/ORI toggles, Save, Exit.
        Colors: red=war(swar), yellow=hostile, green=peace.
        """
        font = self._get_font()
        me = self.gs.me
        if not me:
            return
        wx = ih.WAR_X
        wy = ih.WAR_Y
        row_h = ih.WAR_ROW_H
        w = ih.WAR_W
        h = row_h * 6

        # Background
        pygame.draw.rect(surf, (0, 0, 0), (wx, wy, w, h))
        pygame.draw.rect(surf, (160, 160, 160), (wx, wy, w, h), 1)

        teams = [(FED, "FED"), (ROM, "ROM"), (KLI, "KLI"), (ORI, "ORI")]
        for i, (team_bit, name) in enumerate(teams):
            y = wy + i * row_h
            # Determine status color (COW war.c: red=war, yellow=hostile, green=peace)
            if me.swar & team_bit:
                color = (255, 60, 60)    # red — sticky war
                status = "WAR"
            elif ih._war_hostile & team_bit:
                color = (255, 255, 0)    # yellow — hostile (pending)
                status = "HOSTILE"
            else:
                color = (0, 255, 0)      # green — peace
                status = "PEACE"
            if me.team == team_bit:
                color = (100, 100, 100)  # grey — own team
                status = "(own)"
            text = font.render(f" {name} - {status}", True, color)
            surf.blit(text, (wx + 4, y + 2))
            pygame.draw.line(surf, (80, 80, 80), (wx, y + row_h), (wx + w, y + row_h))

        # Save row
        y = wy + 4 * row_h
        text = font.render("  Save", True, (255, 255, 255))
        surf.blit(text, (wx + 4, y + 2))
        pygame.draw.line(surf, (80, 80, 80), (wx, y + row_h), (wx + w, y + row_h))

        # Exit row
        y = wy + 5 * row_h
        text = font.render("  Exit - no change", True, (160, 160, 160))
        surf.blit(text, (wx + 4, y + 2))

    def _draw_borders(self, surf):
        """Draw COW-style thin borders between panels."""
        plist_y = TWINSIDE + BORDER + STATSIZE + BORDER
        warn_y = GWINSIDE + BORDER
        warn_x = TWINSIDE + BORDER
        review_y = warn_y + MESSAGESIZE * 2 + BORDER
        border_color = (100, 100, 100)

        pygame.draw.line(surf, border_color,
                         (TWINSIDE, 0), (TWINSIDE, self.base_height))
        pygame.draw.line(surf, border_color,
                         (0, TWINSIDE), (TWINSIDE, TWINSIDE))
        pygame.draw.line(surf, border_color,
                         (0, plist_y), (TWINSIDE, plist_y))
        pygame.draw.line(surf, border_color,
                         (warn_x, GWINSIDE), (self.base_width, GWINSIDE))
        pygame.draw.line(surf, border_color,
                         (warn_x, warn_y + MESSAGESIZE),
                         (self.base_width, warn_y + MESSAGESIZE))
        pygame.draw.line(surf, border_color,
                         (warn_x, review_y),
                         (self.base_width, review_y))

    def _draw_messages(self, x, y, width, height):
        """Draw scrolling message review area matching COW's reviewWin."""
        font = self._get_font()
        line_h = 14
        max_lines = height // line_h
        msgs = self.gs.messages[-max_lines:] if self.gs.messages else []

        cy = y + height - line_h  # Start from bottom, scroll up
        for msg in reversed(msgs):
            if cy < y:
                break
            color = self._message_color(msg)
            text = font.render(msg, True, color)
            self.render_surface.blit(text, (x + 2, cy))
            cy -= line_h

    def _message_color(self, msg):
        """Determine message color based on sender prefix, matching COW."""
        if msg.startswith("GOD->") or msg.startswith("Kathy->"):
            return (255, 255, 0)  # Yellow for server/god messages
        prefix = msg[:2] if len(msg) >= 2 else ""
        if prefix and prefix[0] in "FRKO":
            team_map = {"F": 0x1, "R": 0x2, "K": 0x4, "O": 0x8}
            team = team_map.get(prefix[0], 0)
            if team:
                return TEAM_COLORS.get(team, (0, 255, 0))
        return (0, 255, 0)  # Default green

    @property
    def scale_info(self):
        """Return (scale_factor, offset_x, offset_y) for mouse coord mapping."""
        s = getattr(self, '_scale_factor', 1.0)
        ox, oy = getattr(self, '_scale_offset', (0, 0))
        return (s, ox, oy)

    @property
    def tactical_offset(self):
        """Top-left position of the tactical surface on screen."""
        return (0, 0)
