"""Sound effects manager for the Netrek pygame client.

Sound detection matches COW local.c:
  - Flag diffs (shield, cloak, red alert) compared each frame via sound_flags
  - Explosions detected on p_explode == 1 (COW local.c:518-532)
  - Phaser sounds on ph.sound_phaser flag (COW local.c:572-574)
  - death.c resets sound_flags = PFSHIELD to avoid false triggers on respawn
  - enter_ship / self_destruct played directly from statemachine / input
  - Weapon fire sounds (torp/phaser/plasma) triggered from input_handler
    since we lack COW's p_ntorp/p_nplasmatorp per-player counts
"""
import os
import pygame
from .constants import (PFSHIELD, PFCLOAK, PFRED, PFYELLOW,
                        PEXPLODE, PALIVE, STARBASE, PHFREE)


class SoundManager:
    def __init__(self):
        self._sounds = {}
        # COW: static unsigned int sound_flags = PFSHIELD (death.c resets to this)
        self._sound_flags = PFSHIELD
        self._prev_status = {}
        self._prev_msg_count = 0
        self._prev_warning_timer = 0

    def load(self):
        """Load all WAVs from assets/sounds/. Mixer must already be init'd."""
        sound_dir = os.path.join(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))), "assets", "sounds")
        if not os.path.isdir(sound_dir):
            print(f"Sound directory not found: {sound_dir}")
            return
        for filename in os.listdir(sound_dir):
            if not filename.endswith(".wav"):
                continue
            name = filename
            if name.startswith("nt_"):
                name = name[3:]
            name = name[:-4]  # strip .wav
            path = os.path.join(sound_dir, filename)
            try:
                self._sounds[name] = pygame.mixer.Sound(path)
            except Exception as e:
                print(f"Failed to load sound {filename}: {e}")
        print(f"Loaded {len(self._sounds)} sounds")

    def play(self, name):
        """Play a sound by name. Silently ignores unknown names."""
        snd = self._sounds.get(name)
        if snd:
            snd.play()

    def on_death(self):
        """Reset sound state on death (COW death.c: sound_flags = PFSHIELD)."""
        self._sound_flags = PFSHIELD

    def tick(self, gs, me_pnum):
        """Detect state changes each frame and play sounds (COW local.c)."""
        if me_pnum is None or me_pnum < 0:
            return

        me = gs.players[me_pnum] if me_pnum < len(gs.players) else None
        if not me:
            return

        # --- Per-player loop: explosions (COW local.c:516-532) ---
        for p in gs.players:
            if p.status == PEXPLODE:
                old_st = self._prev_status.get(p.pnum)
                if old_st != PEXPLODE:
                    if p.pnum == me_pnum:
                        if p.shiptype == STARBASE:
                            self.play("sbexplosion")
                        else:
                            self.play("explosion")
                    else:
                        self.play("explosion_other")
            self._prev_status[p.pnum] = p.status

        # --- Phaser sounds (COW local.c:572-574) ---
        # COW uses php->sound_phaser flag set when phaser packet arrives.
        # We detect new phasers by checking status != PHFREE with fuse at max.
        for i, ph in enumerate(gs.phasers):
            if ph.status != PHFREE and getattr(ph, '_sound_played', False) is False:
                if i == me_pnum:
                    self.play("phaser")
                else:
                    self.play("phaser_other")
                ph._sound_played = True
            elif ph.status == PHFREE:
                ph._sound_played = False

        # --- Shield sound (COW local.c:400-407) ---
        if (self._sound_flags & PFSHIELD) and not (me.flags & PFSHIELD):
            self.play("shield_down")
        elif not (self._sound_flags & PFSHIELD) and (me.flags & PFSHIELD):
            self.play("shield_up")

        # --- Cloak sound (COW local.c:199-216) ---
        if (me.flags & PFCLOAK) and not (self._sound_flags & PFCLOAK):
            self.play("cloaked")
        elif not (me.flags & PFCLOAK) and (self._sound_flags & PFCLOAK):
            self.play("uncloak")

        # --- Alert sound (COW local.c:1205-1213) ---
        if (me.flags & PFRED) and not (self._sound_flags & PFRED):
            self.play("red_alert")

        # Update saved flags (COW local.c:1229)
        self._sound_flags = me.flags

        # --- Warning onset ---
        wt = getattr(gs, 'warning_timer', 0)
        if wt > 0 and self._prev_warning_timer == 0:
            self.play("warning")
        self._prev_warning_timer = wt

        # --- New messages (COW input.c:2294) ---
        msg_count = len(gs.messages)
        if msg_count > self._prev_msg_count and self._prev_msg_count > 0:
            self.play("message")
        self._prev_msg_count = msg_count
