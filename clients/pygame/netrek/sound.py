"""Sound effects manager for the Netrek pygame client.

Sound detection matches COW local.c:
  - Flag diffs (shield, cloak, red alert) compared each frame via sound_flags
  - Explosions detected on p_explode == 1 (COW local.c:518-532)
  - Phaser sounds on ph.sound_phaser flag (COW local.c:572-574)
  - Torp/plasma sounds on server-confirmed launch (status transitions)
  - death.c resets sound_flags = PFSHIELD to avoid false triggers on respawn
  - enter_ship / self_destruct played directly from statemachine / input
"""
import os
import time
import pygame
from .constants import (PFSHIELD, PFCLOAK, PFGREEN, PFYELLOW, PFRED,
                        PFENG, PFWEP,
                        PEXPLODE, PALIVE, STARBASE, PHFREE,
                        TFREE, TMOVE, TSTRAIGHT, MAXTORP,
                        PTFREE, MAXPLASMA)

_ALERT_MASK = PFGREEN | PFYELLOW | PFRED


class SoundManager:
    def __init__(self):
        self._sounds = {}
        # COW: static unsigned int sound_flags = PFSHIELD (death.c resets to this)
        self._sound_flags = PFSHIELD
        # COW data.c: int oldalert = PFGREEN (separate from sound_flags)
        self._oldalert = PFGREEN
        self._red_alert_time = 0.0   # last time klaxon played (cooldown)
        self._prev_status = {}
        self._prev_msg_count = 0
        self._prev_warning_timer = 0
        self._prev_torp_count = 0    # my active torp count last frame
        self._prev_plasma_count = 0  # my active plasma count last frame

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

    def stop(self, name):
        """Stop a playing sound by name."""
        snd = self._sounds.get(name)
        if snd:
            snd.stop()

    def on_death(self):
        """Reset sound state on death (COW death.c)."""
        self._sound_flags = PFSHIELD
        # COW death.c: oldalert = PFGREEN + Abort_Sound(REDALERT_SOUND)
        self._oldalert = PFGREEN
        self.stop("red_alert")

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
                            self.play("base_explosion")
                        else:
                            self.play("explosion")
                    else:
                        if p.shiptype == STARBASE:
                            self.play("base_explosion")
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

        # --- Torp fire sound (detect new torps from server) ---
        base = me_pnum * MAXTORP
        torp_count = 0
        for i in range(MAXTORP):
            t = gs.torps[base + i]
            if t.status in (TMOVE, TSTRAIGHT):
                torp_count += 1
        if torp_count > self._prev_torp_count:
            self.play("fire_torp")
        self._prev_torp_count = torp_count

        # --- Plasma fire sound (detect new plasmas from server) ---
        pbase = me_pnum * MAXPLASMA
        plasma_count = 0
        for i in range(MAXPLASMA):
            p = gs.plasmas[pbase + i]
            if p.status != PTFREE:
                plasma_count += 1
        if plasma_count > self._prev_plasma_count:
            self.play("fire_plasma")
        self._prev_plasma_count = plasma_count

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

        # --- Engine overheat sound (NetrekXP redraw.c:183-190) ---
        if (me.flags & PFENG) and not (self._sound_flags & PFENG):
            self.play("enginemelt")
        elif not (me.flags & PFENG) and (self._sound_flags & PFENG):
            self.play("engineok")

        # --- Weapon overheat sound (NetrekXP, symmetric with engine) ---
        if (me.flags & PFWEP) and not (self._sound_flags & PFWEP):
            self.play("buzzer")

        # --- Alert sound (COW local.c:1178-1215, data.c oldalert) ---
        # Cooldown prevents retrigger when alert flickers at threshold distance.
        new_alert = me.flags & _ALERT_MASK
        if new_alert != self._oldalert:
            self._oldalert = new_alert
            if new_alert == PFRED:
                now = time.monotonic()
                if now - self._red_alert_time > 10.0:
                    self.play("red_alert")
                    self._red_alert_time = now

        # Update saved flags (COW local.c:1229)
        self._sound_flags = me.flags

        # --- Warning onset (COW BUZZER_SOUND) ---
        # COW's BUZZER_WAV is never loaded in SDL builds, but we provide nt_buzzer.wav.
        # nt_warning.wav is the klaxon (REDALERT_WAV) — do NOT use it here.
        wt = getattr(gs, 'warning_timer', 0)
        if wt > 0 and self._prev_warning_timer == 0:
            self.play("buzzer")
        self._prev_warning_timer = wt

        # --- New messages (COW input.c:2294) ---
        msg_count = len(gs.messages)
        if msg_count > self._prev_msg_count and self._prev_msg_count > 0:
            self.play("message")
        self._prev_msg_count = msg_count
