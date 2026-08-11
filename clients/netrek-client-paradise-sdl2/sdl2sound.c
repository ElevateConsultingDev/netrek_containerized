/*
 * sdl2sound.c - SDL2_mixer replacement for Paradise's fork/exec sound system
 *
 * Replaces sound.c from the submodule. Provides the same API:
 *   init_sound(), play_sound(), maybe_play_sound(), sound_completed(), kill_sound()
 *
 * Sound indices (from sound.h):
 *   0=EXPLOSION 1=CLOAK 2=FIRETORP 3=PHASER 4=PLASMA
 *   5=SHIELD 6=TORPHIT 7=EXP_SB 8=PARADISE 9=THERMAL 10=REDALERT
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL_mixer.h>
#include "data.h"

#define NUM_SOUNDS 11

static Mix_Chunk *sounds[NUM_SOUNDS];
static char sound_flags[20];
static int audioOK = 0;

/* Map Paradise sound indices to WAV filenames */
static const char *sound_files[NUM_SOUNDS] = {
    "nt_explosion.wav",      /* 0: SND_EXPLOSION */
    "nt_cloaked.wav",        /* 1: SND_CLOAK */
    "nt_fire_torp.wav",      /* 2: SND_FIRETORP */
    "nt_phaser.wav",         /* 3: SND_PHASER */
    "nt_fire_plasma.wav",    /* 4: SND_PLASMA */
    "nt_shield_up.wav",      /* 5: SND_SHIELD */
    "nt_torp_hit.wav",       /* 6: SND_TORPHIT */
    "nt_sbexplosion.wav",    /* 7: SND_EXP_SB */
    "nt_intro.wav",          /* 8: SND_PARADISE */
    "nt_warning.wav",        /* 9: SND_THERMAL */
    "nt_red_alert.wav",      /* 10: SND_REDALERT */
};

void
init_sound(void)
{
    int i;
    char path[512];

    /* Default to "sounds" directory if not configured */
    if (!unixSoundPath || unixSoundPath[0] == '?')
        unixSoundPath = "sounds";

    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 1024) < 0) {
        fprintf(stderr, "SDL_mixer: %s\n", Mix_GetError());
        audioOK = 0;
        return;
    }

    Mix_AllocateChannels(16);

    int loaded = 0;
    for (i = 0; i < NUM_SOUNDS; i++) {
        snprintf(path, sizeof(path), "%s/%s", unixSoundPath, sound_files[i]);
        sounds[i] = Mix_LoadWAV(path);
        if (!sounds[i]) {
            /* Try without directory prefix */
            snprintf(path, sizeof(path), "sounds/%s", sound_files[i]);
            sounds[i] = Mix_LoadWAV(path);
        }
        if (sounds[i]) loaded++;
        else fprintf(stderr, "sdl2sound: FAILED to load %s\n", sound_files[i]);
    }

    for (i = 0; i < 19; i++) sound_flags[i] = 0;

    audioOK = 1;
    fprintf(stderr, "sdl2sound: initialized (%s), %d/%d sounds loaded\n", unixSoundPath, loaded, NUM_SOUNDS);
}

void
play_sound(int k)
{
    if (!playSounds || !audioOK) return;
    if (k < 0 || k >= NUM_SOUNDS) return;
    if (!sounds[k]) return;

    Mix_PlayChannel(-1, sounds[k], 0);
}

void
maybe_play_sound(int k)
{
    if (k < 0 || k >= NUM_SOUNDS) return;
    if (sound_flags[k] & 1) return;

    sound_flags[k] |= 1;
    play_sound(k);
}

void
sound_completed(int k)
{
    if (k >= 0 && k < 20)
        sound_flags[k] &= ~1;
}

void
kill_sound(void)
{
    /* Paradise calls kill_sound() when entering the game to stop intro music.
     * The original fork/exec design just killed the child process temporarily.
     * We just halt all channels but keep the mixer alive. */
    if (!audioOK) return;
    Mix_HaltChannel(-1);
}
