/*
 * Netrek Client of Mac (COM)
 * Copyright (C) 2026 Dave King / Elevate Consulting
 *
 * Part of Netrek Client of Mac, a native SDL2 fork of netrek-client-cow.
 * The SDL2 backend and Mac-specific code in this file are original work.
 * Licensed under the GNU General Public License, version 2 or later.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* sdl2camera.c - Screenshot via SDL2
 *
 * Replaces camera.c (Imlib2-based) for the SDL2 backend.
 */

#include "config.h"

#ifdef CAMERA
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "defs.h"

extern SDL_Renderer *sdl_renderer;

static int frame = 0;

void camera_snap_sdl2(void)
{
    int w, h;
    SDL_GetRendererOutputSize(sdl_renderer, &w, &h);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
        SDL_PIXELFORMAT_RGBA8888);
    if (!surface) {
        fprintf(stderr, "camera_snap: cannot create surface: %s\n", SDL_GetError());
        return;
    }

    if (SDL_RenderReadPixels(sdl_renderer, NULL, surface->format->format,
                             surface->pixels, surface->pitch) != 0) {
        fprintf(stderr, "camera_snap: cannot read pixels: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    char name[80];
    snprintf(name, sizeof(name), "netrek-%03d.png", frame++);
    IMG_SavePNG(surface, name);
    fprintf(stderr, "camera_snap: %s\n", name);
    SDL_FreeSurface(surface);
}
#endif
