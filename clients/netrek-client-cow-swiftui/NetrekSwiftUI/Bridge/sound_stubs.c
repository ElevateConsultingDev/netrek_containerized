/* sound_stubs.c - Stub sound functions for SwiftUI backend (Phase 1)
 *
 * COW references these functions from various places.
 * With SOUND undefined in config.h, most callsites are compiled out,
 * but a few unconditional references remain.
 */

#include "config.h"

void Init_Sound(void) {}
void Play_Sound(int type) { (void)type; }
void Abort_Sound(int type) { (void)type; }
void Sound_Init(void) {}

/* Kill the intro sound when entering a ship */
void Kill_Sound(void) {}
