/* swiftuisprite.c - Sprite loading via ImageIO/CoreGraphics
 *
 * Replaces sdl2sprite.c for the SwiftUI backend.
 * Loads PNG ship/planet/weapon sprites into RGBA pixel buffers.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "defaults.h"
#include "swiftuiwindow.h"

#define NUM_BG_IMGS 5
#define NUM_PL_IMGS 11

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

static struct window *tactical_win = NULL;
static struct window *galactic_win = NULL;

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

/* Load a PNG file into an S_Object's pixel buffer */
static int ReadFileToSprite(const char *filename, struct S_Object *sprite,
                           struct window *target)
{
    sprite->target_win = target;
    sprite->pixels = NULL;
    sprite->nviews = 1;

    CFStringRef path = CFStringCreateWithCString(NULL, filename, kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, path, kCFURLPOSIXPathStyle, false);
    CFRelease(path);

    CGImageSourceRef source = CGImageSourceCreateWithURL(url, NULL);
    CFRelease(url);
    if (!source) {
        fprintf(stderr, "sprite: cannot load %s\n", filename);
        return 1;
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
    CFRelease(source);
    if (!image) {
        fprintf(stderr, "sprite: cannot decode %s\n", filename);
        return 1;
    }

    int width = (int)CGImageGetWidth(image);
    int height = (int)CGImageGetHeight(image);
    int nviews = height / width;
    if (nviews < 1 || nviews * width != height) nviews = 1;

    /* Render into RGBA pixel buffer */
    int stride = width * 4;
    uint8_t *pixels = calloc(height * stride, 1);
    if (!pixels) {
        CGImageRelease(image);
        return 1;
    }

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(pixels, width, height, 8, stride,
        cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);

    if (!ctx) {
        free(pixels);
        CGImageRelease(image);
        return 1;
    }

    /* Draw image (CG origin is bottom-left, flip to top-left) */
    CGContextTranslateCTM(ctx, 0, height);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);

    CGContextRelease(ctx);
    CGImageRelease(image);

    sprite->pixels = pixels;
    sprite->width = width;
    sprite->height = height / nviews;
    sprite->sheet_height = height;
    sprite->nviews = nviews;
    sprite->view = 0;
    sprite->cloak = 0;

    return 0;
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

void GetPixmaps_SwiftUI(W_Window t, W_Window g)
{
    int i, j;
    char path[2048];
    int missing;

    tactical_win = W_Void2Window(t);
    galactic_win = W_Void2Window(g);

    GetPixmapDir();

    /* Ship sprites → draw on tactical */
    for (i = 0; i < NUMTEAM + 1; i++) {
        missing = 0;
        for (j = 0; j < NUM_TYPES; j++) {
            snprintf(path, sizeof(path), "%s/%s/%s", pixmapDir, teamnames[i], shipfiles[j]);
            missing += ReadFileToSprite(path, &shipImg[i][j], tactical_win);
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

    pixMissing &= ~NO_HALOS;

    if (!shipImg[1][2].pixels)
        fprintf(stderr, "sprite: Fed/CA FAILED to load\n");
}

/* ========================================================================
 * Sprite drawing - blit from pixel buffer to window pixel buffer
 * ======================================================================== */

/* Flip Y for direct pixel writes (matches buf_y in swiftuiwindow.c) */
static inline int buf_y(int win_height, int screen_y) {
    return win_height - 1 - screen_y;
}

/* Blit a sprite frame with alpha blending */
static void blitSprite(struct S_Object *sprite, struct window *win,
                       int dst_x, int dst_y, uint8_t alpha_mod)
{
    if (!sprite->pixels || !win->pixels) return;

    int src_y_offset = sprite->view * sprite->height;
    int sw = sprite->width;
    int sh = sprite->height;

    for (int row = 0; row < sh; row++) {
        int dy = dst_y + row;
        if (dy < 0 || dy >= win->height) continue;

        int src_row = src_y_offset + row;
        uint8_t *src = sprite->pixels + src_row * sw * 4;
        int by = buf_y(win->height, dy);
        uint8_t *dst = win->pixels + by * win->stride;

        for (int col = 0; col < sw; col++) {
            int dx = dst_x + col;
            if (dx < 0 || dx >= win->width) { src += 4; continue; }

            uint8_t sa = src[3];
            if (alpha_mod < 255) sa = (sa * alpha_mod) / 255;

            if (sa > 0) {
                uint8_t *d = dst + dx * 4;
                if (sa >= 255) {
                    d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = 255;
                } else {
                    /* Alpha blend */
                    int inv = 255 - sa;
                    d[0] = (src[0] * sa + d[0] * inv) / 255;
                    d[1] = (src[1] * sa + d[1] * inv) / 255;
                    d[2] = (src[2] * sa + d[2] * inv) / 255;
                    d[3] = 255;
                }
            }
            src += 4;
        }
    }
    win->dirty = 1;
}

int W_DrawSprite(void *in, int x, int y, int winside)
{
    const int view = SCALE * winside / 2;
    struct S_Object *sprite = (struct S_Object *)in;
    if (!sprite || !sprite->pixels) return 0;
    if (sprite->view < 0 || sprite->view >= sprite->nviews) return 0;
    if (x > view || x < -view || y > view || y < -view) return 0;

    struct window *win = sprite->target_win;
    if (!win || !win->pixels) return 0;

    int dx = x - sprite->width / 2;
    int dy = y - sprite->height / 2;

    uint8_t alpha = 255;
    if (sprite->cloak > 0 && !(pixFlags & NO_CLK_PIX) && cloakImg.pixels) {
        alpha = 255 - (sprite->cloak * 255 / (CLOAK_PHASES - 1));
    }

    blitSprite(sprite, win, dx, dy, alpha);
    return sprite->width;
}

void W_DrawSpriteAbsolute(void *in, int x, int y)
{
    struct S_Object *sprite = (struct S_Object *)in;
    if (!sprite || !sprite->pixels) return;

    struct window *win = sprite->target_win;
    if (!win || !win->pixels) return;

    blitSprite(sprite, win, x, y, 255);
}

void W_ClearSpriteAbsolute(void *in, int x, int y)
{
    struct S_Object *sprite = (struct S_Object *)in;
    if (!sprite) return;

    struct window *win = sprite->target_win;
    if (!win || !win->pixels) return;

    /* Clear sprite area to black */
    for (int row = 0; row < sprite->height; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= win->height) continue;
        int by = buf_y(win->height, dy);
        for (int col = 0; col < sprite->width; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= win->width) continue;
            int off = by * win->stride + dx * 4;
            win->pixels[off + 0] = 0;
            win->pixels[off + 1] = 0;
            win->pixels[off + 2] = 0;
            win->pixels[off + 3] = 255;
        }
    }
    win->dirty = 1;
}

/* ========================================================================
 * Sprite accessor functions (same as sdl2sprite.c)
 * ======================================================================== */

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

    if (!sprite->pixels || (pixFlags & reremap[remap[this->p_team]]))
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

    if (!sprite->pixels || (pixFlags & NO_WEP_PIX))
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

    if (!sprite->pixels || (pixFlags & NO_WEP_PIX))
        return NULL;
    return (void *)sprite;
}
