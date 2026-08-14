/*
 * sprite_preview.c - standalone Netrek COM sprite viewer
 *
 * Loads the real pixmaps (ships, planets, weapons) and renders them at native
 * size and upscaled -- nearest vs smooth -- so we can see how the art holds up
 * when the game scales it onto a large/retina window, without running a server.
 *
 * Build:
 *   cc sprite_preview.c -o /tmp/sprite_preview \
 *      $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf)
 * Run (from clients/netrek-client-cow-sdl2/):
 *   /tmp/sprite_preview                 # uses ./pixmaps and ./pixmaps-hr
 *   /tmp/sprite_preview <pixmap_dir>
 *
 * Keys: N = nearest, S = smooth (bilinear), 1..6 = zoom, ESC = quit.
 *
 * Copyright (C) 2026 Dave King / Elevate Consulting. GPL-2.0-or-later.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>

static SDL_Renderer *R;
static TTF_Font *F;
static int panx = 0, pany = 0;   /* view pan offset */

static SDL_Texture *load(const char *path) {
    SDL_Surface *s = IMG_Load(path);
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
    SDL_FreeSurface(s);
    return t;
}

static void label(int x, int y, const char *txt) {
    if (!F) return;
    SDL_Color c = {125, 145, 165, 255};
    SDL_Surface *s = TTF_RenderText_Blended(F, txt, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
    SDL_Rect d = {x + panx, y + pany, s->w, s->h};
    SDL_RenderCopy(R, t, NULL, &d);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

/* draw one sprite: native, then upscaled to `big` px with current filter */
static void show(SDL_Texture *t, const char *name, int *x, int y, int big) {
    if (!t) return;
    int w, h; SDL_QueryTexture(t, NULL, NULL, &w, &h);
    int frame = (h > w && h % w == 0) ? w : h;     /* ship strips: 1st frame */
    SDL_Rect src = {0, 0, w, frame > 0 ? frame : h};
    /* native */
    SDL_Rect n = {*x + panx, y + pany, src.w, src.h};
    SDL_RenderCopy(R, t, &src, &n);
    /* upscaled */
    SDL_Rect u = {*x + src.w + 10 + panx, y + pany, big, big};
    SDL_RenderCopy(R, t, &src, &u);
    label(*x, y + (big > 44 ? big : 44) + 4, name);
    *x += src.w + 10 + big + 34;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "pixmaps";
    const char *hr  = "pixmaps-hr";
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();
    const char *fonts[] = {"/System/Library/Fonts/Menlo.ttc",
                           "/System/Library/Fonts/Monaco.dfont", NULL};
    for (const char **f = fonts; *f && !F; f++) F = TTF_OpenFont(*f, 13);

    SDL_Window *win = SDL_CreateWindow("Netrek COM - sprite preview",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 760,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    char smooth = 0; int big = 80;
    char p[1024];
    /* what to show: {subpath, label} */
    struct { const char *sub, *name; } planets[] = {
        {"Planets/Map/ROCK.png","rock"}, {"Planets/Map/AGRI.png","agri"},
        {"Planets/Map/Fed.png","Fed"}, {"Planets/Map/Rom.png","Rom"},
        {"Planets/Map/Kli.png","Kli"}, {"Planets/Map/Ori.png","Ori"},
        {"Planets/Map/Ind.png","Ind"}, {"Planets/Map/UNKN.png","unkn"},
        {"Planets/Map/army.png","army"}, {"Planets/Map/repair.png","rpr"},
        {"Planets/Map/fuel.png","fuel"}, {NULL,NULL}};
    struct { const char *sub, *name; } cplanets[] = {
        {"Planets/Color/rock1.png","rock1"}, {"Planets/Color/rock2.png","rock2"},
        {"Planets/Color/agri1.png","agri1"}, {"Planets/Color/agri2.png","agri2"},
        {"Planets/Color/earth.png","earth"}, {"Planets/Color/klingus.png","klingus"},
        {"Planets/Color/romulus.png","romulus"}, {"Planets/Color/orion.png","orion"},
        {"Planets/Color/unknown.png","unknown"}, {NULL,NULL}};
    struct { const char *sub, *name; } ships[] = {
        {"Fed/SC.png","SC 20px"}, {"Fed/DD.png","DD"}, {"Fed/CA.png","CA"},
        {"Fed/BB.png","BB"}, {"Fed/SB.png","SB"}, {NULL,NULL}};
    struct { const char *sub, *name; } weps[] = {
        {"Fed/torp.png","torp"}, {"Fed/plasma.png","plasma"},
        {"Misc/explosion.png","explode"}, {NULL,NULL}};

    int run = 1;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = 0;
            if (e.type == SDL_MOUSEWHEEL) {
                big += e.wheel.y * 12;
            }
            if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                panx += e.motion.xrel; pany += e.motion.yrel;   /* drag to pan */
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) run = 0;
                else if (k == SDLK_n) smooth = 0;
                else if (k == SDLK_s) smooth = 1;
                else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) big += 12;
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) big -= 12;
                else if (k == SDLK_LEFT)  panx += 60;
                else if (k == SDLK_RIGHT) panx -= 60;
                else if (k == SDLK_UP)    pany += 60;
                else if (k == SDLK_DOWN)  pany -= 60;
                else if (k == SDLK_r)     { panx = pany = 0; }   /* reset view */
                else if (k >= SDLK_1 && k <= SDLK_9) big = 24 * (k - SDLK_1 + 1);
                else if (k == SDLK_0) big = 320;
            }
            if (big < 16) big = 16;
            if (big > 480) big = 480;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, smooth ? "best" : "0");
        SDL_SetRenderDrawColor(R, 8, 11, 16, 255);
        SDL_RenderClear(R);

        char hdr[128];
        snprintf(hdr, sizeof hdr, "filter:%s zoom:%dpx  [scroll/+/-]zoom [drag or arrows]pan [R]eset [1-9/0]presets [N/S]filter [Esc]",
                 smooth ? "SMOOTH" : "NEAREST", big);
        label(20, 12, hdr);

        int y = 44, x;
        label(20, y, "PLANETS  (native | upscaled)"); y += 22; x = 20;
        for (int i = 0; planets[i].sub; i++) {
            snprintf(p, sizeof p, "%s/%s", dir, planets[i].sub);
            SDL_Texture *t = load(p);
            show(t, planets[i].name, &x, y, big);
            if (t) SDL_DestroyTexture(t);
            if (x > 1120) { x = 20; y += big + 40; }
        }
        y += big + 60;
        label(20, y, "COLOR PLANETS (netrekxp, frame 0 = Fed tint)"); y += 22; x = 20;
        for (int i = 0; cplanets[i].sub; i++) {
            snprintf(p, sizeof p, "%s/%s", dir, cplanets[i].sub);
            SDL_Texture *t = load(p);
            show(t, cplanets[i].name, &x, y, big);
            if (t) SDL_DestroyTexture(t);
            if (x > 1120) { x = 20; y += big + 40; }
        }
        y += big + 60;
        label(20, y, "SHIPS  (frame 0; 20px std vs hi-res)"); y += 22; x = 20;
        for (int i = 0; ships[i].sub; i++) {
            snprintf(p, sizeof p, "%s/%s", dir, ships[i].sub);
            SDL_Texture *t = load(p);
            show(t, ships[i].name, &x, y, big);
            if (t) SDL_DestroyTexture(t);
            snprintf(p, sizeof p, "%s/%s", hr, ships[i].sub);
            SDL_Texture *th = load(p);
            if (th) { show(th, "(hi-res)", &x, y, big); SDL_DestroyTexture(th); }
        }
        y += big + 60;
        label(20, y, "WEAPONS"); y += 22; x = 20;
        for (int i = 0; weps[i].sub; i++) {
            snprintf(p, sizeof p, "%s/%s", dir, weps[i].sub);
            SDL_Texture *t = load(p);
            show(t, weps[i].name, &x, y, big);
            if (t) SDL_DestroyTexture(t);
        }
        SDL_RenderPresent(R);
        SDL_Delay(16);
    }
    return 0;
}
