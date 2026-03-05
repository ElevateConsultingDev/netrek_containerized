"""Configuration: planet rendering, keymap, buttonmap, rc file parsing.

Implements COW-compatible keymap and buttonmap parsing from netrek.rc files.
"""
import os

STYLE_NAMES = ["Nothing", "Owner", "Standard Resources", "MOO Resources", "Rabbit Ears"]

# COW default key bindings: character -> action name
DEFAULT_KEYMAP = {
    '0': 'speed_0', '1': 'speed_1', '2': 'speed_2', '3': 'speed_3',
    '4': 'speed_4', '5': 'speed_5', '6': 'speed_6', '7': 'speed_7',
    '8': 'speed_8', '9': 'speed_9',
    '%': 'max_speed', '#': 'max_speed',
    '<': 'speed_down', '>': 'speed_up',
    'k': 'course',
    't': 'torp', 'p': 'phaser', 'f': 'plasma',
    'd': 'det_torps', 'D': 'det_own_torp',
    's': 'shields', 'c': 'cloak', 'r': 'repair', 'o': 'orbit',
    'b': 'bomb', 'z': 'beam_up', 'x': 'beam_down',
    'e': 'dock_perm', 'E': 'practice',
    'l': 'planet_lock', ';': 'player_lock',
    'T': 'tractor', 'y': 'pressor', 'Y': 'tractor', '_': 'tractor_off',
    'w': 'war', 'R': 'refit', '^': 'coup',
    'q': 'quit', 'Q': 'fast_quit',
    'V': 'cycle_local_planets', 'B': 'cycle_galactic_planets',
    ' ': 'det_own_torp',
    'h': 'help',
    'i': 'info', 'I': 'info_ext',
    'm': 'message',
    'X': 'macro',
    'A': 'auto_aim',
    'P': 'planet_list',
    'U': 'rank_window',
    'S': 'stat_window',
    '/': 'sort_players',
    # Ctrl+key distress calls (COW input.c RCD bindings)
    '\x14': 'distress_take',        # Ctrl+T
    '\x0f': 'distress_ogg',         # Ctrl+O
    '\x02': 'distress_bomb',        # Ctrl+B
    '\x03': 'distress_control',     # Ctrl+C
    '\x05': 'distress_escorting',   # Ctrl+E
    '\x10': 'distress_ogging',      # Ctrl+P
    '\x0c': 'distress_controlling', # Ctrl+L
    '\x06': 'distress_free_beer',   # Ctrl+F
    '\x0e': 'distress_no_gas',      # Ctrl+N
    '\x08': 'distress_crippled',    # Ctrl+H
    # Ctrl+digit distress calls (COW input.c:2499-2616)
    '\x80': 'distress_pop',          # Ctrl+0
    '\x81': 'distress_save_planet',  # Ctrl+1
    '\x82': 'distress_base_ogg',     # Ctrl+2
    '\x83': 'distress_help1',        # Ctrl+3
    '\x84': 'distress_help2',        # Ctrl+4
    '\x85': 'distress_asw',          # Ctrl+5
    '\x86': 'distress_asbomb',       # Ctrl+6
    '\x87': 'distress_doing1',       # Ctrl+7
    '\x88': 'distress_doing2',       # Ctrl+8
    '\x89': 'distress_pickup',       # Ctrl+9
}

# COW default mouse button bindings: button number -> action name
DEFAULT_BUTTONMAP = {1: 'torp', 2: 'phaser', 3: 'course'}

# Actions that need a mouse direction to fire
DIRECTED_ACTIONS = {'torp', 'phaser', 'plasma', 'course'}


class Config:
    def __init__(self, rc_path=None):
        # Planet rendering settings
        self.showlocal = 2
        self.showgalactic = 2
        self.show_ind = True
        self.show_planet_owner = False
        self.namemode = True
        self.agri_caps = True
        self.planet_bitmap = 0
        self.planet_bitmap_galaxy = 0

        # Tactical display settings (COW defaults)
        self.show_stars = True          # showStars: background star dots
        self.show_tractor_pressor = True  # showTractorPressor: tractor/pressor lines
        self.extra_alert_border = True  # extraAlertBorder: colored border on alert
        self.show_lock = 3              # showLock: 0=none 1=galactic 2=tactical 3=both
        self.vary_shields = True        # varyShields: shield color varies with damage
        self.show_army = 3              # showArmy: 0=none 1=local 2=galactic 3=both
        self.det_circle = True          # detCircle: show det range circle

        # Galactic display settings
        self.owner_halo = True          # ownerhalo: planet owner halo circles
        self.lock_line = True           # lockLine: dashed line to lock target
        self.view_box = True            # viewBox: tactical extent rectangle
        self.weapons_on_map = True      # weaponsOnMap: torps/phasers on galactic
        self.show_visibility_range = True  # showVisRange: enemy scanner circles on galactic

        # Sound
        self.sound_enabled = True       # sound: enable sound output

        # Info window
        self.keep_info = 15             # keepInfo: info window duration (updates, 0=don't remove)

        # Keymap: char -> action (starts as copy of defaults)
        self.keymap = dict(DEFAULT_KEYMAP)

        # Buttonmap: button_number -> action
        self.buttonmap = dict(DEFAULT_BUTTONMAP)

        # Macros: key -> {target_type: template} (from rc file mac.X.T lines)
        self.macros = {}

        # Network settings
        self.try_udp = False

        # RC file path that was loaded
        self.rc_path = None

        if rc_path:
            self._load_rc(rc_path)

    def _load_rc(self, path):
        path = os.path.expanduser(path)
        if not os.path.isfile(path):
            return
        self.rc_path = path
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                # Parse "key: value" or "key:value"
                colon = line.find(':')
                if colon < 0:
                    continue
                key = line[:colon].strip()
                value = line[colon + 1:].strip()
                self._apply_setting(key, value)

    @staticmethod
    def _parse_bool(value):
        return value.lower() in ('on', 'true', '1', 'yes')

    def _apply_setting(self, key, value):
        if key == 'keymap':
            self._parse_keymap(value)
        elif key == 'buttonmap':
            self._parse_buttonmap(value)
        elif key == 'showIND':
            self.show_ind = self._parse_bool(value)
        elif key == 'showPlanetOwner':
            self.show_planet_owner = self._parse_bool(value)
        elif key == 'showPlanetNames':
            self.namemode = self._parse_bool(value)
        elif key == 'agriCAPS':
            self.agri_caps = self._parse_bool(value)
        elif key == 'planetBitmap':
            try:
                self.planet_bitmap = int(value)
            except ValueError:
                pass
        elif key == 'planetBitmapGalaxy':
            try:
                self.planet_bitmap_galaxy = int(value)
            except ValueError:
                pass
        elif key == 'tryUdp':
            self.try_udp = self._parse_bool(value)
        # Tactical display
        elif key == 'showStars':
            self.show_stars = self._parse_bool(value)
        elif key == 'showTractorPressor':
            self.show_tractor_pressor = self._parse_bool(value)
        elif key == 'extraAlertBorder':
            self.extra_alert_border = self._parse_bool(value)
        elif key == 'showLock':
            try:
                self.show_lock = max(0, min(3, int(value)))
            except ValueError:
                pass
        elif key == 'varyShields':
            self.vary_shields = self._parse_bool(value)
        elif key == 'showArmy':
            try:
                self.show_army = max(0, min(3, int(value)))
            except ValueError:
                pass
        elif key == 'detCircle':
            self.det_circle = self._parse_bool(value)
        # Galactic display
        elif key == 'ownerhalo':
            self.owner_halo = self._parse_bool(value)
        elif key == 'lockLine':
            self.lock_line = self._parse_bool(value)
        elif key == 'viewBox':
            self.view_box = self._parse_bool(value)
        elif key == 'weaponsOnMap':
            self.weapons_on_map = self._parse_bool(value)
        elif key == 'showVisRange':
            self.show_visibility_range = self._parse_bool(value)
        # Sound
        elif key == 'sound':
            self.sound_enabled = self._parse_bool(value)
        # Info window
        elif key == 'keepInfo':
            try:
                self.keep_info = max(0, int(value))
            except ValueError:
                pass
        elif key.startswith('mac.') and len(key) >= 5:
            # COW macro: mac.X.T: template  (X=trigger key, T=target type)
            parts = key.split('.')
            if len(parts) == 3 and len(parts[1]) == 1 and len(parts[2]) == 1:
                macro_key = parts[1]
                target_type = parts[2]
                if macro_key not in self.macros:
                    self.macros[macro_key] = {}
                self.macros[macro_key][target_type] = value

    def _parse_keymap(self, s):
        """Parse COW keymap string: pairs of (new_key, old_key).

        Each pair means "give new_key the DEFAULT action of old_key."
        The old key's binding is NOT removed. Lookup always uses
        DEFAULT_KEYMAP, not the current (partially remapped) state.
        """
        i = 0
        while i + 1 < len(s):
            new_key = s[i]
            old_key = s[i + 1]
            action = DEFAULT_KEYMAP.get(old_key)
            if action:
                self.keymap[new_key] = action
            i += 2

    def _parse_buttonmap(self, s):
        """Parse COW buttonmap string: pairs of (button_digit, key_char).

        The key_char is resolved through the CURRENT keymap (after remaps).
        Button digits: 1=left, 2=middle, 3=right, 4-9=extended/wheel.
        """
        i = 0
        while i + 1 < len(s):
            btn_char = s[i]
            key_char = s[i + 1]
            if btn_char.isdigit():
                btn = int(btn_char)
                action = self.keymap.get(key_char)
                if action:
                    self.buttonmap[btn] = action
            i += 2
