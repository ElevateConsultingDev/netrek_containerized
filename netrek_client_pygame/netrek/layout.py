"""Scalable layout dimensions for native-resolution rendering.

All pixel dimensions are computed from a single scale factor.
At scale=1.0, values match the original COW base resolution
(TWINSIDE=500, GWINSIDE=500). The Layout object is mutated
in-place on resize so all references stay valid.
"""


class Layout:
    def __init__(self, scale=1.0):
        self.scale = 1.0
        self.update(scale)

    def update(self, scale):
        self.scale = scale
        s = scale

        # --- Panel sizes ---
        self.twinside = int(500 * s)
        self.gwinside = int(500 * s)
        self.border = max(1, int(3 * s))
        self.messagesize = int(20 * s)
        self.statsize = self.messagesize * 2 + self.border
        self.playerlist_height = int(200 * s)
        self.review_height = (self.playerlist_height
                              - self.messagesize * 2 - self.border)

        # Total content size
        self.width = self.twinside + self.border + self.gwinside
        left_h = (self.twinside + self.border + self.statsize
                  + self.border + self.playerlist_height)
        right_h = (self.gwinside + self.border + self.messagesize * 2
                   + self.border + self.review_height)
        self.height = max(left_h, right_h)

        # Game coordinate scale (keeps viewed area constant at 20000 game units)
        self.game_scale = 40.0 / s

        # --- Font sizes ---
        self.font_tiny = max(8, int(10 * s))
        self.font_small = max(8, int(11 * s))
        self.font_medium = max(10, int(14 * s))
        self.font_team = max(12, int(16 * s))
        self.font_team_count = max(18, int(36 * s))
        self.font_waiting = max(12, int(20 * s))

        # --- Tactical view ---
        self.planet_radius = max(1, int(15 * s))
        self.planet_name_offset = int(16 * s)
        self.ind_cross = int(15 * s)
        self.ind_cross_end = int(14 * s)
        self.ship_fallback_radius = max(1, int(6 * s))
        self.shield_radius = max(2, int(12 * s))
        self.ship_label_offset = int(12 * s)
        self.phaser_width = max(1, int(2 * s))
        self.tractor_spread = int(12 * s)
        self.trac_dash_len = max(2, int(6 * s))
        self.trac_gap_len = max(2, int(4 * s))
        self.lock_offset = int(20 * s)
        self.lock_size = max(2, int(4 * s))
        self.alert_border_width = max(1, int(3 * s))
        self.plasma_radius = max(1, int(4 * s))
        self.plasma_explode_radius = max(2, int(8 * s))
        self.torp_fallback_radius = max(1, int(20 * s))

        # --- Galactic view ---
        self.gal_planet_radius = max(1, int(5 * s))
        self.gal_halo_radius = max(2, int(8 * s))
        self.gal_name_offset = int(9 * s)
        self.gal_owner_offset_x = int(9 * s)
        self.gal_owner_offset_y = int(4 * s)
        self.gal_ind_cross = int(7 * s)
        self.gal_army_offset_x = int(10 * s)
        self.gal_army_offset_y = int(5 * s)
        self.gal_lock_player_offset = int(6 * s)
        self.gal_lock_planet_offset = int(12 * s)

        # --- Dashboard (COW dashboard.c) ---
        # Bar dimensions; column positions computed dynamically from font width
        self.bar_length = int(56 * s)
        self.bar_h = max(3, int(9 * s))
        self.dash_row_spacing = int(14 * s)

        # --- Player list ---
        self.row_height = int(14 * s)
        self.plist_x = int(2 * s)

        # --- Renderer: login screen ---
        self.login_x = int(20 * s)
        self.login_y_start = int(20 * s)
        self.login_line_h = int(22 * s)
        self.login_gap = int(40 * s)
        self.motd_line_h = int(14 * s)
        self.motd_x = int(10 * s)
        self.motd_gap = int(18 * s)

        # --- Renderer: team select ---
        self.team_corner_size = int(120 * s)
        self.team_corner_far = self.twinside - self.team_corner_size
        self.team_hatch_step = max(2, int(8 * s))
        self.team_name_y_offset = int(15 * s)
        self.team_count_y_offset = int(50 * s)
        self.team_center_y = int(180 * s)
        self.team_ship_y = int(210 * s)
        self.team_legend_y = int(235 * s)
        self.team_enter_y = int(252 * s)
        self.team_wait_y = int(275 * s)
        self.team_motd_start_y = int(310 * s)
        self.team_motd_limit_y = int(370 * s)
        self.team_quit_y = self.twinside - int(20 * s)

        # --- Renderer: help overlay (smaller font, tighter spacing) ---
        self.font_help = max(7, int(9 * s))
        self.help_line_h = max(8, int(11 * s))
        self.help_pad_x = int(6 * s)
        self.help_pad_y = int(4 * s)
        self.help_col_w = self.twinside // 2

        # --- Renderer: info window ---
        self.info_line_h = int(14 * s)
        self.info_margin = int(10 * s)
        self.info_padding = int(8 * s)

        # --- Renderer: war window (drawing positions) ---
        self.war_x = self.twinside + self.border + int(10 * s)
        self.war_y = int(10 * s)
        self.war_w = int(160 * s)
        self.war_row_h = int(18 * s)

        # --- Renderer: message area ---
        self.msg_x_pad = int(2 * s)
        self.msg_y_pad = int(3 * s)
