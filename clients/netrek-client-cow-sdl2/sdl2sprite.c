/* sdl2sprite.c - Sprite loading via SDL2_image
 *
 * Replaces x11sprite.c (Imlib2-based) for the SDL2 backend.
 * Loads PNG ship/planet/weapon sprites and background images.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "defaults.h"
#include "sdl2window.h"

extern SDL_Renderer *sdl_renderer;

#define NUM_BG_IMGS 5
#define NUM_PL_IMGS 11

/* Canonical on-screen ship size (px, logical). COW ship bitmaps are 20x20; the
 * hi-res strips are downscaled to this so they render crisp, not oversized. */
#define SHIP_DISP 20

#define PL_PIX_UKN 0
#define PL_PIX_ROCK 1
#define PL_PIX_AGRI 2
#define PL_PIX_ARMY 3
#define PL_PIX_REPAIR 4
#define PL_PIX_FUEL 5
#define PL_PIX_IND 6
#define PL_PIX_FED 7
#define PL_PIX_ROM 8
#define PL_PIX_KLI 9
#define PL_PIX_ORI 10

const int reremap[5] = {NO_IND_PIX, NO_FED_PIX, NO_ROM_PIX,
                        NO_KLI_PIX, NO_ORI_PIX};

static struct S_Object mplanetImg[NUM_PL_IMGS];
static struct S_Object shipImg[NUMTEAM + 1][NUM_TYPES];
static struct S_Object torpImg[NUMTEAM + 1][2];
static struct S_Object plasmaImg[NUMTEAM + 1][2];
static struct S_Object cloakImg;
static struct S_Object explosionImg[2];

/* Target windows for sprites */
static struct window *tactical_win = NULL;
static struct window *galactic_win = NULL;

static SDL_Texture *backTex[NUM_BG_IMGS];

const char teamnames[NUMTEAM + 1][4] = {"Ind", "Fed", "Rom", "Kli", "Ori"};

const char mplanetfiles[NUM_PL_IMGS][12] = {
    "UNKN.png", "ROCK.png", "AGRI.png", "army.png", "repair.png",
    "fuel.png", "Ind.png", "Fed.png", "Rom.png", "Kli.png", "Ori.png"
};
const char shipfiles[NUM_TYPES][8] = {
    "SC.png", "DD.png", "CA.png", "BB.png",
    "AS.png", "SB.png", "GA.png", "AT.png"
};
const char torpfiles[2][14] = {"torp.png", "torp_det.png"};
const char plasmafiles[2][16] = {"plasma.png", "plasma_det.png"};
const char cloakfile[10] = "cloak.png";
const char explosionfiles[2][18] = {"explosion.png", "sbexplosion.png"};
const char bgfiles[NUM_BG_IMGS][16] = {
    "map_back.png", "local_back.png", "ghostbust.png",
    "genocide.png", "hockey.png"
};

static int ReadFileToSprite(const char *filename, struct S_Object *sprite,
                           struct window *target)
{
    SDL_Surface *surface = IMG_Load(filename);
    if (!surface) {
        fprintf(stderr, "sprite: cannot load %s: %s\n", filename, IMG_GetError());
        sprite->texture = NULL;
        sprite->nviews = 1;
        sprite->target_win = target;
        return 1;
    }

    int width = surface->w;
    int height = surface->h;
    int nviews = height / width;
    if (nviews * width != height) nviews = 1;

    sprite->texture = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);

    if (!sprite->texture) {
        sprite->nviews = 1;
        sprite->target_win = target;
        return 1;
    }

    SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
    sprite->view = 0;
    sprite->nviews = nviews;
    sprite->width = width;
    sprite->height = height / nviews;
    sprite->disp_w = sprite->width;   /* default: draw 1:1 with the source */
    sprite->disp_h = sprite->height;
    sprite->cloak = 0;
    sprite->target_win = target;

    return 0;
}

static SDL_Texture *ReadFileToTile(const char *filename)
{
    SDL_Surface *surface = IMG_Load(filename);
    if (!surface) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    return tex;
}

static char pixmapDir[1024] = {'\0'};

static void GetPixmapDir(void)
{
    char *pd;
    struct stat buf;

    if (pixmapDir[0] != '\0') return;

    pd = getdefault("pixmapDir");
    if (pd != NULL)
        strncpy(pixmapDir, pd, sizeof(pixmapDir) - 1);
    else
        strcpy(pixmapDir, "/usr/share/pixmaps/netrek-client-cow");

    if ((strcmpi(pixmapDir, "None") == 0) || (pixMissing & NO_PIXMAPS)) {
        pixMissing = NO_IND_PIX | NO_FED_PIX | NO_ROM_PIX |
            NO_KLI_PIX | NO_ORI_PIX | NO_WEP_PIX | NO_EXP_PIX |
            NO_CLK_PIX | NO_MAP_PIX | NO_BG_PIX | NO_PIXMAPS;
        fprintf(stderr, "pixmaps turned off\n");
    } else {
        if ((stat(pixmapDir, &buf)) || (!(S_ISDIR(buf.st_mode)))) {
            if ((stat("pixmaps", &buf)) || (!(S_ISDIR(buf.st_mode)))) {
                pixMissing = NO_IND_PIX | NO_FED_PIX | NO_ROM_PIX |
                    NO_KLI_PIX | NO_ORI_PIX | NO_WEP_PIX | NO_EXP_PIX |
                    NO_CLK_PIX | NO_MAP_PIX | NO_BG_PIX | NO_PIXMAPS;
                fprintf(stderr, "pixmaps not found at %s or pixmaps/\n", pixmapDir);
            } else {
                strcpy(pixmapDir, "pixmaps");
            }
        }
    }
}

void GetPixmaps_SDL2(W_Window t, W_Window g)
{
    int i, j;
    char path[2048];
    int missing;

    tactical_win = W_Void2Window(t);
    galactic_win = W_Void2Window(g);

    GetPixmapDir();

    /* Ship sprites → draw on tactical. Prefer the hi-res set in "<dir>-hr"
     * (80px/frame strips from tools/gen_hires_ships.py); those are drawn at the
     * canonical SHIP_DISP px so they're just crisper, not bigger. Fall back to
     * the standard 20px pixmaps when the hi-res set is absent. */
    for (i = 0; i < NUMTEAM + 1; i++) {
        missing = 0;
        for (j = 0; j < NUM_TYPES; j++) {
            struct stat sb;
            snprintf(path, sizeof(path), "%s-hr/%s/%s", pixmapDir, teamnames[i], shipfiles[j]);
            if (stat(path, &sb) == 0 &&
                ReadFileToSprite(path, &shipImg[i][j], tactical_win) == 0) {
                shipImg[i][j].disp_w = shipImg[i][j].disp_h = SHIP_DISP;
            } else {
                snprintf(path, sizeof(path), "%s/%s/%s", pixmapDir, teamnames[i], shipfiles[j]);
                missing += ReadFileToSprite(path, &shipImg[i][j], tactical_win);
            }
        }
        if (missing == NUM_TYPES) {
            pixMissing |= reremap[i];
            if (!(pixMissing & NO_PIXMAPS))
                fprintf(stderr, "%s ship pixmaps not available\n", teamnames[i]);
        }
    }

    /* Torp and plasma sprites → draw on tactical */
    missing = 0;
    for (i = 0; i < NUMTEAM + 1; i++) {
        for (j = 0; j < 2; j++) {
            snprintf(path, sizeof(path), "%s/%s/%s", pixmapDir, teamnames[i], torpfiles[j]);
            missing += ReadFileToSprite(path, &torpImg[i][j], tactical_win);
            snprintf(path, sizeof(path), "%s/%s/%s", pixmapDir, teamnames[i], plasmafiles[j]);
            missing += ReadFileToSprite(path, &plasmaImg[i][j], tactical_win);
        }
    }
    if (missing == (NUMTEAM + 1) * 4) {
        pixMissing |= NO_WEP_PIX;
        if (!(pixMissing & NO_PIXMAPS))
            fprintf(stderr, "weapon pixmaps not available\n");
    }

    /* Explosion sprites → draw on tactical */
    missing = 0;
    for (i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/Misc/%s", pixmapDir, explosionfiles[i]);
        missing += ReadFileToSprite(path, &explosionImg[i], tactical_win);
    }
    if (missing == 2) {
        pixMissing |= NO_EXP_PIX;
        if (!(pixMissing & NO_PIXMAPS))
            fprintf(stderr, "explosion pixmaps not available\n");
    }

    /* Planet sprites → draw on galactic */
    missing = 0;
    for (i = 0; i < NUM_PL_IMGS && !missing; i++) {
        snprintf(path, sizeof(path), "%s/Planets/Map/%s", pixmapDir, mplanetfiles[i]);
        if (i == PL_PIX_AGRI && missing == 0) {
            if (ReadFileToSprite(path, &mplanetImg[i], galactic_win) != 0) {
                snprintf(path, sizeof(path), "%s/Planets/%s", pixmapDir, mplanetfiles[i - 1]);
                ReadFileToSprite(path, &mplanetImg[i], galactic_win);
            }
        } else {
            missing += ReadFileToSprite(path, &mplanetImg[i], galactic_win);
        }
    }
    if (missing) {
        pixMissing |= NO_MAP_PIX;
        if (!(pixMissing & NO_PIXMAPS))
            fprintf(stderr, "map pixmaps not available\n");
    }

    /* Cloak sprite → draw on tactical */
    snprintf(path, sizeof(path), "%s/Misc/%s", pixmapDir, cloakfile);
    if (ReadFileToSprite(path, &cloakImg, tactical_win)) {
        pixMissing |= NO_CLK_PIX;
        if (!(pixMissing & NO_PIXMAPS))
            fprintf(stderr, "cloak pixmaps not available\n");
    }

    /* Background tiles */
    missing = 0;
    for (i = 0; i < NUM_BG_IMGS; i++) {
        snprintf(path, sizeof(path), "%s/Misc/%s", pixmapDir, bgfiles[i]);
        backTex[i] = ReadFileToTile(path);
        if (!backTex[i]) missing++;
    }
    if (missing == NUM_BG_IMGS) {
        pixMissing |= NO_BG_PIX;
        if (!(pixMissing & NO_PIXMAPS))
            fprintf(stderr, "background pixmaps not available\n");
    }

    pixMissing &= ~NO_HALOS;

    /* Warn if sprites failed to load */
    if (!shipImg[1][2].texture)
        fprintf(stderr, "sprite: Fed/CA FAILED to load\n");
}

/*
 * Sprite accessor functions called by COW rendering code
 * (local.c, map.c, redraw.c, etc.)
 */

int W_DrawSprite(void *in, int x, int y, int winside)
{
    const int view = SCALE * winside / 2;
    struct S_Object *sprite = (struct S_Object *)in;
    if (!sprite || !sprite->texture) return 0;
    if (sprite->view < 0 || sprite->view >= sprite->nviews) return 0;
    if (x > view || x < -view || y > view || y < -view) return 0;

    struct window *win = sprite->target_win;
    if (!win || !win->texture) return 0;

    int dx = x - sprite->disp_w / 2;
    int dy = y - sprite->disp_h / 2;

    /* Source rect: select the correct frame from the sprite sheet. dst uses the
     * display size so a hi-res source is downscaled to the canonical on-screen
     * size (crisper) rather than drawn oversized. */
    SDL_Rect src = {0, sprite->view * sprite->height,
                    sprite->width, sprite->height};
    SDL_Rect dst = {dx, dy, sprite->disp_w, sprite->disp_h};

    /* Handle cloaking: apply alpha from cloak sprite */
    if (sprite->cloak > 0 && !(pixFlags & NO_CLK_PIX) && cloakImg.texture) {
        /* Reduce alpha based on cloak phase */
        Uint8 alpha = 255 - (sprite->cloak * 255 / (CLOAK_PHASES - 1));
        SDL_SetTextureAlphaMod(sprite->texture, alpha);
    }

    winTarget(win);
    SDL_RenderCopy(sdl_renderer, sprite->texture, &src, &dst);
    screenTarget();

    /* Reset alpha */
    if (sprite->cloak > 0) {
        SDL_SetTextureAlphaMod(sprite->texture, 255);
    }

    return sprite->width;
}

void W_DrawSpriteAbsolute(void *in, int x, int y)
{
    struct S_Object *sprite = (struct S_Object *)in;
    if (!sprite || !sprite->texture) return;

    struct window *win = sprite->target_win;
    if (!win || !win->texture) return;

    SDL_Rect src = {0, sprite->view * sprite->height,
                    sprite->width, sprite->height};
    SDL_Rect dst = {x, y, sprite->disp_w, sprite->disp_h};

    winTarget(win);
    SDL_RenderCopy(sdl_renderer, sprite->texture, &src, &dst);
    screenTarget();
}

void W_ClearSpriteAbsolute(void *in, int x, int y)
{
    struct S_Object *sprite = (struct S_Object *)in;
    if (!sprite || !sprite->texture) return;

    struct window *win = sprite->target_win;
    if (!win || !win->texture) return;

    /* Clear the area where the sprite was drawn */
    winTarget(win);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_Rect r = {x, y, sprite->disp_w, sprite->disp_h};
    SDL_RenderFillRect(sdl_renderer, &r);
    screenTarget();
}

void *S_Ship(int playerno)
{
    struct S_Object *sprite;
    struct player *this;

    if (playerno > MAXPLAYER || playerno < 0) return NULL;
    this = &players[playerno];

    if ((this->p_status != PALIVE) && (this->p_status != PEXPLODE))
        return NULL;
    if ((this->p_flags & PFOBSERV) && !(this->p_flags & PFPLOCK))
        return NULL;
    if ((this->p_flags & PFOBSERV) && !(this->p_flags & PFCLOAK))
        return NULL;

    sprite = &(shipImg[remap[this->p_team]][this->p_ship.s_type]);
    sprite->cloak = 0;

    sprite->view = ((((this->p_dir) + 128 / (sprite->nviews))
                     / (256 / (sprite->nviews))) % (sprite->nviews));

    if (this->p_status == PEXPLODE) {
        int i;
        if (pixFlags & NO_EXP_PIX) return NULL;
        if (this->p_ship.s_type == STARBASE) {
            i = this->p_explode * 5 / server_ups;
            sprite = &explosionImg[1];
        } else {
            i = this->p_explode * 10 / server_ups;
            sprite = &explosionImg[0];
        }
        if (i >= sprite->nviews) return NULL;
        sprite->view = i;
        this->p_explode++;
    } else if ((this->p_flags & PFCLOAK) || (this->p_cloakphase > 0)) {
        if (this->p_cloakphase == (CLOAK_PHASES - 1)) {
            if ((this == me) && !(pixFlags & NO_CLK_PIX)) {
                sprite = &cloakImg;
                sprite->view = cloakImg.nviews - 1;
            } else {
                return NULL;
            }
        } else {
            if ((CLOAK_PHASES - 2) == 0) return NULL;
            sprite->cloak = this->p_cloakphase;
            cloakImg.view = MAX(0, cloakImg.nviews -
                (this->p_cloakphase * (cloakImg.nviews - 1) / (CLOAK_PHASES - 2)) - 2);
        }
    }

    if (!sprite->texture || (pixFlags & reremap[remap[this->p_team]]))
        return NULL;
    return (void *)sprite;
}

void *S_mPlanet(int planetno)
{
    struct S_Object *sprite;
    struct planet *this = &planets[planetno];

    if (pixFlags & NO_MAP_PIX) return NULL;

    if ((this->pl_info & me->p_team)
#ifdef RECORDGAME
        || playback
#endif
       ) {
        if ((this->pl_flags & PLAGRI) && (F_agri_pix))
            sprite = &mplanetImg[PL_PIX_AGRI];
        else
            sprite = &mplanetImg[PL_PIX_ROCK];
    } else {
        sprite = &mplanetImg[PL_PIX_UKN];
    }
    sprite->view = 0;
    return (void *)sprite;
}

void *S_mArmy(int planetno)
{
    struct planet *this = &planets[planetno];
    if ((pixFlags & NO_MAP_PIX) || (showgalactic != 1)) return NULL;
    if (((this->pl_info & me->p_team)
#ifdef RECORDGAME
         || playback
#endif
        ) && (this->pl_armies > 4)) {
        struct S_Object *sprite = &mplanetImg[PL_PIX_ARMY];
        sprite->view = 0;
        return (void *)sprite;
    }
    return NULL;
}

void *S_mRepair(int planetno)
{
    struct planet *this = &planets[planetno];
    if ((pixFlags & NO_MAP_PIX) || (showgalactic != 1)) return NULL;
    if (((this->pl_info & me->p_team)
#ifdef RECORDGAME
         || playback
#endif
        ) && (this->pl_flags & PLREPAIR)) {
        struct S_Object *sprite = &mplanetImg[PL_PIX_REPAIR];
        sprite->view = 0;
        return (void *)sprite;
    }
    return NULL;
}

void *S_mFuel(int planetno)
{
    struct planet *this = &planets[planetno];
    if ((pixFlags & NO_MAP_PIX) || (showgalactic != 1)) return NULL;
    if (((this->pl_info & me->p_team)
#ifdef RECORDGAME
         || playback
#endif
        ) && (this->pl_flags & PLFUEL)) {
        struct S_Object *sprite = &mplanetImg[PL_PIX_FUEL];
        sprite->view = 0;
        return (void *)sprite;
    }
    return NULL;
}

void *S_mOwner(int planetno)
{
    struct planet *this = &planets[planetno];
    if ((pixFlags & NO_MAP_PIX) || (showgalactic != 0)) return NULL;
    if ((this->pl_info & me->p_team)
#ifdef RECORDGAME
        || playback
#endif
       ) {
        struct S_Object *sprite = &mplanetImg[PL_PIX_IND + remap[this->pl_owner]];
        sprite->view = 0;
        return (void *)sprite;
    }
    return NULL;
}

void *S_Torp(int torpno)
{
    struct S_Object *sprite;
    struct torp *this = &torps[torpno];
    int numdetframes, frame;

    if (this->t_status == TEXPLODE) {
        sprite = &torpImg[remap[players[this->t_owner].p_team]][1];
        this->t_fuse--;
        numdetframes = NUMDETFRAMES * server_ups / 5;
        frame = this->t_fuse * 5 / server_ups;
        if (this->t_fuse <= 0) {
            this->t_status = PTFREE;
            players[this->t_owner].p_ntorp--;
        } else if (this->t_fuse >= numdetframes) {
            this->t_fuse = numdetframes - 1;
        } else {
            sprite->view = frame;
        }
    } else {
        sprite = &torpImg[remap[players[this->t_owner].p_team]][0];
        sprite->view = (sprite->view + 1) % sprite->nviews;
    }

    if (!sprite->texture || (pixFlags & NO_WEP_PIX))
        return NULL;
    return (void *)sprite;
}

void *S_Plasma(int plasmatorpno)
{
    struct S_Object *sprite;
    struct plasmatorp *this = &plasmatorps[plasmatorpno];
    int numdetframes, frame;

    if (this->pt_status == PTEXPLODE) {
        sprite = &plasmaImg[remap[players[this->pt_owner].p_team]][1];
        this->pt_fuse--;
        numdetframes = NUMDETFRAMES * server_ups / 5;
        frame = this->pt_fuse * 5 / server_ups;
        if (this->pt_fuse <= 0) {
            this->pt_status = PTFREE;
            players[this->pt_owner].p_nplasmatorp--;
        } else if (this->pt_fuse >= numdetframes) {
            this->pt_fuse = numdetframes - 1;
        } else {
            sprite->view = frame;
        }
    } else {
        sprite = &plasmaImg[remap[players[this->pt_owner].p_team]][0];
        sprite->view = (sprite->view + 1) % sprite->nviews;
    }

    if (!sprite->texture || (pixFlags & NO_WEP_PIX))
        return NULL;
    return (void *)sprite;
}
