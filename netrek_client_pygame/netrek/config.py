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
    'k': 'course',
    't': 'torp', 'p': 'phaser', 'f': 'plasma',
    'd': 'det_torps', 'D': 'det_own_torp',
    's': 'shields', 'c': 'cloak', 'r': 'repair', 'o': 'orbit',
    'b': 'bomb', 'z': 'beam_up', 'x': 'beam_down',
    'e': 'practice',
    'l': 'planet_lock', ';': 'player_lock',
    'T': 'tractor', 'y': 'pressor', 'Y': 'tractor', '_': 'tractor_off',
    'w': 'war', 'R': 'refit', '^': 'coup',
    'q': 'quit', 'Q': 'fast_quit',
    'V': 'cycle_local_planets', 'B': 'cycle_galactic_planets',
    ' ': 'det_own_torp',
    'i': 'info', 'I': 'info_ext',
    'm': 'message',
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

        # Keymap: char -> action (starts as copy of defaults)
        self.keymap = dict(DEFAULT_KEYMAP)

        # Buttonmap: button_number -> action
        self.buttonmap = dict(DEFAULT_BUTTONMAP)

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

    def _apply_setting(self, key, value):
        if key == 'keymap':
            self._parse_keymap(value)
        elif key == 'buttonmap':
            self._parse_buttonmap(value)
        elif key == 'showIND':
            self.show_ind = value.lower() in ('on', 'true', '1', 'yes')
        elif key == 'showPlanetOwner':
            self.show_planet_owner = value.lower() in ('on', 'true', '1', 'yes')
        elif key == 'showPlanetNames':
            self.namemode = value.lower() in ('on', 'true', '1', 'yes')
        elif key == 'agriCAPS':
            self.agri_caps = value.lower() in ('on', 'true', '1', 'yes')
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
            self.try_udp = value.lower() in ('on', 'true', '1', 'yes')

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
