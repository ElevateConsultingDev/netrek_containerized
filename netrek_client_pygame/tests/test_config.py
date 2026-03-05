"""Tests for config.py — Config, keymap, buttonmap, rc file parsing."""
import os
import tempfile
import pytest

from netrek.config import Config, DEFAULT_KEYMAP, DEFAULT_BUTTONMAP


# ---- DEFAULT_KEYMAP sanity ----

class TestDefaultKeymap:
    def test_h_maps_to_help(self):
        assert DEFAULT_KEYMAP['h'] == 'help'

    def test_t_maps_to_torp(self):
        assert DEFAULT_KEYMAP['t'] == 'torp'

    def test_s_maps_to_shields(self):
        assert DEFAULT_KEYMAP['s'] == 'shields'

    def test_speed_keys(self):
        for i in range(10):
            assert DEFAULT_KEYMAP[str(i)] == f'speed_{i}'

    def test_q_maps_to_quit(self):
        assert DEFAULT_KEYMAP['q'] == 'quit'


# ---- Config defaults (no rc file) ----

class TestConfigDefaults:
    def test_keymap_equals_default(self):
        cfg = Config()
        assert cfg.keymap == DEFAULT_KEYMAP

    def test_buttonmap_equals_default(self):
        cfg = Config()
        assert cfg.buttonmap == DEFAULT_BUTTONMAP

    def test_no_macros(self):
        cfg = Config()
        assert cfg.macros == {}

    def test_default_showlocal(self):
        cfg = Config()
        assert cfg.showlocal == 2

    def test_default_planet_flags(self):
        cfg = Config()
        assert cfg.show_ind is True
        assert cfg.namemode is True
        assert cfg.agri_caps is True

    def test_rc_path_is_none(self):
        cfg = Config()
        assert cfg.rc_path is None

    def test_try_udp_default_off(self):
        cfg = Config()
        assert cfg.try_udp is False


# ---- _parse_keymap ----

class TestParseKeymap:
    def test_remap_single_pair(self):
        """'qs' means: give 'q' the DEFAULT action of 's' (shields)."""
        cfg = Config()
        cfg._parse_keymap("qs")
        assert cfg.keymap['q'] == 'shields'

    def test_original_key_unchanged(self):
        """Remapping FROM 's' does not remove 's' binding."""
        cfg = Config()
        cfg._parse_keymap("qs")
        assert cfg.keymap['s'] == 'shields'

    def test_multiple_pairs(self):
        """Two pairs: 'qs' + 'et' → q=shields, e=torp."""
        cfg = Config()
        cfg._parse_keymap("qset")
        assert cfg.keymap['q'] == 'shields'
        assert cfg.keymap['e'] == 'torp'

    def test_uses_default_not_current(self):
        """Lookup always uses DEFAULT_KEYMAP, not partially-remapped state.

        If we remap 'q' to shields, then 'xq' should give 'x' the DEFAULT
        action of 'q' (quit), NOT the remapped shields.
        """
        cfg = Config()
        cfg._parse_keymap("qs")  # q -> shields
        cfg._parse_keymap("xq")  # x -> DEFAULT['q'] = quit
        assert cfg.keymap['x'] == 'quit'

    def test_odd_length_string_ignores_trailing(self):
        """Odd-length string: last char is ignored (no pair)."""
        cfg = Config()
        cfg._parse_keymap("qsz")
        assert cfg.keymap['q'] == 'shields'
        # 'z' has no pair, keymap['z'] stays default (beam_up)
        assert cfg.keymap['z'] == 'beam_up'

    def test_unknown_old_key_ignored(self):
        """If old_key is not in DEFAULT_KEYMAP, the pair is skipped."""
        cfg = Config()
        cfg._parse_keymap("q!")  # '!' not in default
        assert cfg.keymap['q'] == 'quit'  # unchanged


# ---- _parse_buttonmap ----

class TestParseButtonmap:
    def test_remap_button(self):
        """'1p' means button 1 → action of current keymap['p'] (phaser)."""
        cfg = Config()
        cfg._parse_buttonmap("1p")
        assert cfg.buttonmap[1] == 'phaser'

    def test_uses_current_keymap(self):
        """Buttonmap resolves through current keymap (after remaps)."""
        cfg = Config()
        cfg._parse_keymap("ps")  # p -> shields
        cfg._parse_buttonmap("1p")  # button1 -> keymap['p'] = shields
        assert cfg.buttonmap[1] == 'shields'

    def test_nondigit_button_ignored(self):
        """Non-digit button character is ignored."""
        cfg = Config()
        cfg._parse_buttonmap("xp")  # 'x' is not a digit
        assert cfg.buttonmap == DEFAULT_BUTTONMAP

    def test_unknown_key_ignored(self):
        """If key_char not in keymap, button is not remapped."""
        cfg = Config()
        cfg._parse_buttonmap("1!")  # '!' not in keymap
        assert cfg.buttonmap[1] == 'torp'  # unchanged


# ---- RC file loading ----

class TestRcFile:
    def test_nonexistent_file(self):
        cfg = Config(rc_path="/tmp/nonexistent_netrekrc_test_xyz")
        assert cfg.rc_path is None

    def test_keymap_from_rc(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("keymap: qs\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.keymap['q'] == 'shields'
                assert cfg.rc_path == f.name
            finally:
                os.unlink(f.name)

    def test_buttonmap_from_rc(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("buttonmap: 1p\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.buttonmap[1] == 'phaser'
            finally:
                os.unlink(f.name)

    def test_show_ind_setting(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("showIND: off\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.show_ind is False
            finally:
                os.unlink(f.name)

    def test_planet_bitmap_setting(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("planetBitmap: 3\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.planet_bitmap == 3
            finally:
                os.unlink(f.name)

    def test_planet_bitmap_bad_value(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("planetBitmap: notanumber\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.planet_bitmap == 0  # unchanged from default
            finally:
                os.unlink(f.name)

    def test_try_udp_on(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("tryUdp: on\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.try_udp is True
            finally:
                os.unlink(f.name)

    def test_comments_and_blank_lines(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("# comment\n\nshowIND: off\n# another comment\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.show_ind is False
            finally:
                os.unlink(f.name)


# ---- Macro parsing ----

class TestMacroParsing:
    def test_single_macro(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("mac.z.T: help me at %l\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert 'z' in cfg.macros
                assert cfg.macros['z']['T'] == 'help me at %l'
            finally:
                os.unlink(f.name)

    def test_multiple_macros_same_key(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("mac.z.T: help team\nmac.z.A: help all\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.macros['z']['T'] == 'help team'
                assert cfg.macros['z']['A'] == 'help all'
            finally:
                os.unlink(f.name)

    def test_different_macro_keys(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("mac.z.T: help\nmac.x.T: ogg\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert 'z' in cfg.macros
                assert 'x' in cfg.macros
            finally:
                os.unlink(f.name)

    def test_invalid_macro_key_too_long(self):
        """mac.ab.T is invalid (trigger key must be single char)."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("mac.ab.T: nope\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.macros == {}
            finally:
                os.unlink(f.name)

    def test_combined_rc(self):
        """Full rc file with keymap, buttonmap, macros, and settings."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc',
                                         delete=False) as f:
            f.write("keymap: qs\n")
            f.write("buttonmap: 1p\n")
            f.write("showPlanetOwner: on\n")
            f.write("mac.z.T: help at %l\n")
            f.flush()
            try:
                cfg = Config(rc_path=f.name)
                assert cfg.keymap['q'] == 'shields'
                assert cfg.buttonmap[1] == 'phaser'
                assert cfg.show_planet_owner is True
                assert cfg.macros['z']['T'] == 'help at %l'
            finally:
                os.unlink(f.name)


# ---- New COW display/sound/info options ----

class TestNewConfigDefaults:
    """Verify defaults for newly-added COW options."""

    def test_tactical_defaults(self):
        cfg = Config()
        assert cfg.show_stars is True
        assert cfg.show_tractor_pressor is True
        assert cfg.extra_alert_border is True
        assert cfg.show_lock == 3
        assert cfg.vary_shields is True
        assert cfg.show_army == 3
        assert cfg.det_circle is True

    def test_galactic_defaults(self):
        cfg = Config()
        assert cfg.owner_halo is True
        assert cfg.lock_line is True
        assert cfg.view_box is True
        assert cfg.weapons_on_map is True

    def test_sound_default(self):
        cfg = Config()
        assert cfg.sound_enabled is True

    def test_keep_info_default(self):
        cfg = Config()
        assert cfg.keep_info == 15


class TestNewConfigParsing:
    """Verify netrekrc parsing for newly-added COW options."""

    def _make_rc(self, content):
        f = tempfile.NamedTemporaryFile(mode='w', suffix='.netrekrc', delete=False)
        f.write(content)
        f.flush()
        f.close()
        return f.name

    def test_show_stars_off(self):
        path = self._make_rc("showStars: off\n")
        try:
            assert Config(rc_path=path).show_stars is False
        finally:
            os.unlink(path)

    def test_show_tractor_pressor_off(self):
        path = self._make_rc("showTractorPressor: off\n")
        try:
            assert Config(rc_path=path).show_tractor_pressor is False
        finally:
            os.unlink(path)

    def test_extra_alert_border_off(self):
        path = self._make_rc("extraAlertBorder: off\n")
        try:
            assert Config(rc_path=path).extra_alert_border is False
        finally:
            os.unlink(path)

    def test_show_lock_values(self):
        for val in (0, 1, 2, 3):
            path = self._make_rc(f"showLock: {val}\n")
            try:
                assert Config(rc_path=path).show_lock == val
            finally:
                os.unlink(path)

    def test_show_lock_clamped(self):
        path = self._make_rc("showLock: 9\n")
        try:
            assert Config(rc_path=path).show_lock == 3
        finally:
            os.unlink(path)

    def test_vary_shields_off(self):
        path = self._make_rc("varyShields: off\n")
        try:
            assert Config(rc_path=path).vary_shields is False
        finally:
            os.unlink(path)

    def test_show_army_values(self):
        for val in (0, 1, 2, 3):
            path = self._make_rc(f"showArmy: {val}\n")
            try:
                assert Config(rc_path=path).show_army == val
            finally:
                os.unlink(path)

    def test_det_circle_off(self):
        path = self._make_rc("detCircle: off\n")
        try:
            assert Config(rc_path=path).det_circle is False
        finally:
            os.unlink(path)

    def test_owner_halo_off(self):
        path = self._make_rc("ownerhalo: off\n")
        try:
            assert Config(rc_path=path).owner_halo is False
        finally:
            os.unlink(path)

    def test_lock_line_off(self):
        path = self._make_rc("lockLine: off\n")
        try:
            assert Config(rc_path=path).lock_line is False
        finally:
            os.unlink(path)

    def test_view_box_off(self):
        path = self._make_rc("viewBox: off\n")
        try:
            assert Config(rc_path=path).view_box is False
        finally:
            os.unlink(path)

    def test_weapons_on_map_off(self):
        path = self._make_rc("weaponsOnMap: off\n")
        try:
            assert Config(rc_path=path).weapons_on_map is False
        finally:
            os.unlink(path)

    def test_sound_off(self):
        path = self._make_rc("sound: off\n")
        try:
            assert Config(rc_path=path).sound_enabled is False
        finally:
            os.unlink(path)

    def test_keep_info_value(self):
        path = self._make_rc("keepInfo: 30\n")
        try:
            assert Config(rc_path=path).keep_info == 30
        finally:
            os.unlink(path)

    def test_keep_info_zero(self):
        path = self._make_rc("keepInfo: 0\n")
        try:
            assert Config(rc_path=path).keep_info == 0
        finally:
            os.unlink(path)

    def test_combined_new_options(self):
        content = (
            "showStars: off\n"
            "showLock: 1\n"
            "ownerhalo: off\n"
            "sound: off\n"
            "keepInfo: 0\n"
            "showArmy: 2\n"
        )
        path = self._make_rc(content)
        try:
            cfg = Config(rc_path=path)
            assert cfg.show_stars is False
            assert cfg.show_lock == 1
            assert cfg.owner_halo is False
            assert cfg.sound_enabled is False
            assert cfg.keep_info == 0
            assert cfg.show_army == 2
        finally:
            os.unlink(path)
