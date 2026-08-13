/* sdl2window.c - SDL2 implementation of Wlib
 *
 * Replaces x11window.c for macOS/SDL2 builds.
 * All COW code calls Wlib functions; this implements them via SDL2.
 *
 * Each Wlib "window" is a virtual window backed by an SDL_Texture.
 * W_Flush() composites all mapped textures onto a single SDL_Window.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <signal.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "smessage.h"
#include "defaults.h"
#include "sdl2window.h"

extern void terminate(int error);

/* ========================================================================
 * Global Wlib variables (required by all COW code)
 * ======================================================================== */

static int zero = 0, one = 1, two = 2, three = 3;

W_Font W_BigFont = (W_Font)&zero, W_RegularFont = (W_Font)&one;
W_Font W_HighlightFont = (W_Font)&two, W_UnderlineFont = (W_Font)&three;
W_Font W_IndyFont = (W_Font)&one;
W_Font W_MyPlanetFont = (W_Font)&two;
W_Font W_FriendlyPlanetFont = (W_Font)&two;
W_Font W_EnemyPlanetFont = (W_Font)&one;

W_Color W_White = WHITE, W_Black = BLACK, W_Red = RED, W_Green = GREEN;
W_Color W_Yellow = YELLOW, W_Cyan = CYAN, W_Grey = GREY;

#ifdef RACE_COLORS
W_Color W_Ind = C_IND, W_Fed = C_FED, W_Rom = C_ROM, W_Kli = C_KLI, W_Ori = C_ORI;
#endif

int W_BigTextwidth = 24, W_BigTextheight = 40;
int W_Textwidth = 6, W_Textheight = 10;
int W_FastClear = 1;  /* SDL2: full clear each frame is cheap with HW accel */
int W_in_message = 0;
int forceMono = 0;
int full_screen_default = 0, full_screen_enabled = 0;

/* ========================================================================
 * SDL2 backend state
 * ======================================================================== */

SDL_Window   *sdl_window = NULL;
SDL_Renderer *sdl_renderer = NULL;

struct window windows[MAX_WINDOWS];
int num_windows = 0;

/* ------------------------------------------------------------------------
 * Retina supersampling.
 *
 * Every sub-window is backed by a texture rendered at RENDER_SCALE x its
 * logical size, and fonts are rasterized at RENDER_SCALE x point size, so
 * text and vector graphics are crisp on a HiDPI display instead of a 1x
 * buffer being upscaled by the OS. All game/layout coordinates stay in the
 * logical space (W_Textwidth=6 etc.); the scale is applied only in the
 * renderer via winTarget()/the compositor. Mouse input is mapped back to
 * logical space by hand (pt_to_logical) so it stays correct regardless of
 * DPI or window resize -- this is deliberately NOT SDL_RenderSetLogicalSize,
 * whose automatic mouse translation is wrong under HiDPI.
 * ---------------------------------------------------------------------- */
/* RENDER_SCALE lives in sdl2window.h so sdl2sprite.c uses the same value. */
static int LOGICAL_W = 1024;   /* logical canvas size (baseWin) */
static int LOGICAL_H = 768;

/* Pipe for W_Socket() integration with select() */
static int socket_pipe[2] = {-1, -1};

/* Target a window's (supersampled) texture: draw calls use logical coords and
 * are scaled up by RENDER_SCALE into the RS-sized backing texture. */
void winTarget(struct window *win)
{
    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_RenderSetScale(sdl_renderer, (float)RENDER_SCALE, (float)RENDER_SCALE);
}

/* Return to the screen target at 1:1 scale (the compositor sets its own). */
void screenTarget(void)
{
    SDL_RenderSetScale(sdl_renderer, 1.0f, 1.0f);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* Map an SDL mouse-event point (window-point space) to logical canvas coords.
 * SDL events are in window POINTS; the logical canvas maps to points by the
 * uniform scale s = points/LOGICAL, so logical = point / s. This is the ONLY
 * mouse transform (we do NOT use SDL_RenderSetLogicalSize, whose event watcher
 * would translate a second time). MUST use the same uniform scale the
 * compositor uses (there in drawable space = s * dpi, but the ratio is what
 * keeps drawn angle == fired angle). */
static void pt_to_logical(int px, int py, int *lx, int *ly)
{
    int ww = LOGICAL_W, wh = LOGICAL_H;
    if (sdl_window) SDL_GetWindowSize(sdl_window, &ww, &wh);  /* points */
    if (ww <= 0) ww = LOGICAL_W;
    if (wh <= 0) wh = LOGICAL_H;
    float sx = (float)ww / LOGICAL_W, sy = (float)wh / LOGICAL_H;
    float s = sx < sy ? sx : sy;   /* uniform (letterbox), matches W_Flush */
    if (s <= 0) s = 1.0f;
    *lx = (int)(px / s);
    *ly = (int)(py / s);
}

/* Font handles */
TTF_Font *sdl_fonts[4] = {NULL, NULL, NULL, NULL};

/* TTS font */
static TTF_Font *tts_font = NULL;

/* Color table */
struct colors colortable[NCOLORS] = {
    {"white",    {255, 255, 255, 255}},
    {"black",    {0,   0,   0,   255}},
    {"red",      {255, 0,   0,   255}},
    {"green",    {0,   255, 0,   255}},
    {"yellow",   {255, 255, 0,   255}},
    {"cyan",     {0,   255, 255, 255}},
    {"dark grey",{128, 128, 128, 255}},
#ifdef RACE_COLORS
    {"Rom",      {255, 0,   0,   255}},
    {"Kli",      {0,   128, 0,   255}},
    {"Fed",      {255, 255, 0,   255}},
    {"Ori",      {0,   255, 255, 255}},
    {"Ind",      {128, 128, 128, 255}},
#endif
};

/* Line cache */
static SDL_Point _lcache[NCOLORS][MAXCACHE * 2]; /* pairs of points */
static int _lcache_index[NCOLORS];

/* Clear area cache */
static SDL_Rect _rcache[MAXCACHE];
static int _rcache_index = 0;
static struct window *_rcache_win = NULL;

/* Scrolling window settings */
static int scrollbar = 1;
static int scroll_thumb_width = SCROLL_THUMB_WIDTH;
static int scroll_lines = 100;

/* Event queue - ring buffer to avoid dropping events */
#define EVQ_SIZE 64
static W_Event evq_buf[EVQ_SIZE];
static int evq_head = 0;  /* next slot to write */
static int evq_tail = 0;  /* next slot to read */

static int evq_empty(void) { return evq_head == evq_tail; }
static int evq_full(void)  { return ((evq_head + 1) % EVQ_SIZE) == evq_tail; }

static void evq_push(W_Event *ev)
{
    if (evq_full()) return;  /* drop oldest if full (shouldn't happen) */
    evq_buf[evq_head] = *ev;
    evq_head = (evq_head + 1) % EVQ_SIZE;
}

static int evq_pop(W_Event *ev)
{
    if (evq_empty()) return 0;
    *ev = evq_buf[evq_tail];
    evq_tail = (evq_tail + 1) % EVQ_SIZE;
    return 1;
}

/* Root window placeholder */
static struct window myroot;

/* Forward declarations */
static void redrawMenu(struct window *win);
static void renderTextOnWindow(struct window *win, int px, int py,
                               W_Color color, char *str, int len, W_Font font);

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

struct window *W_Void2Window(W_Window win)
{
    if (win == NULL) return &myroot;
    return (struct window *)win;
}

W_Window W_Window2Void(struct window *win)
{
    return (W_Window)win;
}

struct icon *W_Void2Icon(W_Icon bit)
{
    return (struct icon *)bit;
}

W_Icon W_Icon2Void(struct icon *ic)
{
    return (W_Icon)ic;
}

int fontNum(W_Font font)
{
    if (!font) return 1;  /* default to W_RegularFont */
    return *((int *)font);
}

SDL_Color getColor(W_Color color)
{
    if (color >= 0 && color < NCOLORS)
        return colortable[color].sdl_color;
    return colortable[WHITE].sdl_color;
}

static void setRenderColor(W_Color color)
{
    SDL_Color c = getColor(color);
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
}

/* Allocate a new virtual window */
struct window *newWindow(int type)
{
    if (num_windows >= MAX_WINDOWS) {
        fprintf(stderr, "sdl2window: too many windows\n");
        return NULL;
    }
    struct window *win = &windows[num_windows];
    memset(win, 0, sizeof(struct window));
    win->id = num_windows;
    win->type = type;
    win->parent_id = -1;
    num_windows++;
    return win;
}

/* Find which virtual window contains the given screen coordinate */
struct window *findWindowAt(int sx, int sy)
{
    /* Search in reverse order (top-most windows last in creation) */
    for (int i = num_windows - 1; i >= 0; i--) {
        struct window *win = &windows[i];
        if (!win->mapped) continue;
        if (sx >= win->x && sx < win->x + win->width &&
            sy >= win->y && sy < win->y + win->height) {
            return win;
        }
    }
    return NULL;
}

/* ========================================================================
 * W_Initialize - Core initialization
 * ======================================================================== */

void W_Initialize(char *str)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        exit(1);
    }

    if (IMG_Init(IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        exit(1);
    }

    /* Create the main SDL window.
     * COW layout: tactical(500) + galactic(500) + border(3) wide
     *             tactical(500) + dashboard + messages tall
     * We start with a reasonable default. */
    int win_w = 1024;
    int win_h = 768;

    /* Smooth interpolation when a texture is scaled, instead of blocky
     * nearest-neighbor. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    /* Let SDL_RaiseWindow actually take focus (no-op without this on macOS),
     * so a terminal-launched window is focused without a click. */
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");

    /* Logical canvas = initial window size; all layout/mouse math uses this. */
    LOGICAL_W = win_w;
    LOGICAL_H = win_h;

    /* HIGHDPI so the retina drawable is the full 2x pixels; combined with the
     * RENDER_SCALE-supersampled window textures this yields crisp output.
     * SDL_RenderSetLogicalSize (set once the "netrek" baseWin is created)
     * owns ALL scaling: it maps the logical canvas onto the drawable AND
     * translates mouse event coords back into logical space, so render and
     * input can never drift out of sync. */
    sdl_window = SDL_CreateWindow("Netrek",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!sdl_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!sdl_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(1);
    }

    /* macOS: a bare binary launched from the terminal (no .app bundle) does
     * not reliably become the focused foreground app, so the window opens but
     * keyboard/mouse focus stays with Terminal until you click away and back.
     * Explicitly raise the window to key-and-front, and pump the event queue
     * once so the activation takes effect before the game loop starts. */
    SDL_RaiseWindow(sdl_window);
    SDL_PumpEvents();

    /* Create pipe for W_Socket() integration with select() */
    if (pipe(socket_pipe) < 0) {
        perror("pipe");
        exit(1);
    }
    fcntl(socket_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(socket_pipe[1], F_SETFL, O_NONBLOCK);

    /* Load fonts - try to find a monospace font that gives 6x10 cells.
     * COW layout depends on W_Textwidth=6, W_Textheight=10.
     * Strategy: try each font at decreasing point sizes until we get
     * a cell height <= 10, then accept whatever width we get. */
    const char *font_paths[] = {
        "fonts/DejaVuSansMono.ttf",
        "/opt/homebrew/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.dfont",
        "/System/Library/Fonts/Courier.dfont",
        NULL
    };

    for (const char **fp = font_paths; *fp; fp++) {
        /* Try sizes from 10 down to 6, looking for cell height <= 10 */
        for (int sz = 10; sz >= 6; sz--) {
            TTF_Font *test = TTF_OpenFont(*fp, sz);
            if (!test) break; /* font file not loadable */
            int w, h;
            TTF_SizeText(test, "M", &w, &h);
            if (h <= 10) {
                /* Measured at logical size sz; open the real handles at
                 * RENDER_SCALE x so glyphs rasterize sharp, then draw them at
                 * logical size in renderTextOnWindow. */
                TTF_CloseFont(test);
                sdl_fonts[1] = TTF_OpenFont(*fp, sz * RENDER_SCALE);
                sdl_fonts[0] = TTF_OpenFont(*fp, sz * 3 * RENDER_SCALE); /* big */
                sdl_fonts[2] = TTF_OpenFont(*fp, sz * RENDER_SCALE);     /* bold */
                sdl_fonts[3] = TTF_OpenFont(*fp, sz * RENDER_SCALE);     /* underline */
                if (sdl_fonts[2]) TTF_SetFontStyle(sdl_fonts[2], TTF_STYLE_BOLD);
                if (sdl_fonts[3]) TTF_SetFontStyle(sdl_fonts[3], TTF_STYLE_UNDERLINE);
                break;
            }
            TTF_CloseFont(test);
        }
        if (sdl_fonts[1]) break;
    }

    /* Force W_Textwidth=6, W_Textheight=10 regardless of actual font metrics.
     * COW layout depends on these exact values. The font may render slightly
     * narrower or wider per glyph, but the cell grid stays correct. */
    W_Textwidth = 6;
    W_Textheight = 10;

    if (sdl_fonts[1]) {
        int w, h;
        TTF_SizeText(sdl_fonts[1], "M", &w, &h);
        if (sdl_fonts[0]) {
            TTF_SizeText(sdl_fonts[0], "M", &w, &h);
            /* Fonts are RENDER_SCALE x; report logical metrics for layout. */
            W_BigTextwidth = w / RENDER_SCALE;
            W_BigTextheight = h / RENDER_SCALE;
        }
    } else {
        fprintf(stderr, "sdl2window: WARNING: no font loaded\n");
    }

    /* Initialize root window entry */
    memset(&myroot, 0, sizeof(myroot));
    myroot.id = -1;
    myroot.mapped = 1;
    SDL_GetWindowSize(sdl_window, &myroot.width, &myroot.height);

    /* Clear screen to black */
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderPresent(sdl_renderer);

}

/* ========================================================================
 * Window creation
 * ======================================================================== */

W_Window W_MakeWindow(char *name, int x, int y, int width, int height,
                      W_Window parent, int border, W_Color color)
{
    struct window *win = newWindow(WIN_GRAPH);
    if (!win) return NULL;

    win->width = width;
    win->height = height;
    win->x = x;
    win->y = y;
    win->border_color = color;
    if (name) win->name = strdup(name);

    if (parent) {
        struct window *pw = W_Void2Window(parent);
        win->parent_id = pw->id;
        win->x += pw->x;
        win->y += pw->y;
    }

    /* Clamp negative coordinates to 0 (X11 lets WM decide, we just fix it) */
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;

    /* Scaling: the COW baseWin ("netrek") defines the full logical canvas.
     * We do NOT use SDL_RenderSetLogicalSize: its render-side scaling is
     * silently disabled by our per-frame SDL_SetRenderTarget switches, while
     * its mouse-event watcher keeps translating -- so render and mouse drift
     * apart. Instead W_Flush scales the composite by hand and pt_to_logical
     * scales the mouse by hand, each exactly once. */
    if (name && strcmp(name, "netrek") == 0) {
        LOGICAL_W = win->x + win->width;
        LOGICAL_H = win->y + win->height;
    }

    /* Create render target texture */
    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        width * RENDER_SCALE, height * RENDER_SCALE);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        /* Clear to black */
        winTarget(win);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        screenTarget();
    }

    return W_Window2Void(win);
}

W_Window W_MakeTextWindow(char *name, int x, int y, int width, int height,
                          W_Window parent, int border)
{
    int pix_width = width * W_Textwidth + WIN_EDGE * 2;
    int pix_height = height * W_Textheight + MENU_PAD * 2;

    struct window *win = newWindow(WIN_TEXT);
    if (!win) return NULL;

    win->width = pix_width;
    win->height = pix_height;
    win->x = x;
    win->y = y;
    if (name) win->name = strdup(name);

    if (parent) {
        struct window *pw = W_Void2Window(parent);
        win->parent_id = pw->id;
        win->x += pw->x;
        win->y += pw->y;
    }

    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        pix_width * RENDER_SCALE, pix_height * RENDER_SCALE);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        winTarget(win);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        screenTarget();
    }

    return W_Window2Void(win);
}

W_Window W_MakeScrollingWindow(char *name, int x, int y, int width, int height,
                               W_Window parent, int border)
{
    int extra = scrollbar ? scroll_thumb_width : 0;
    int pix_width = width * W_Textwidth + WIN_EDGE * 2 + extra;
    int pix_height = height * W_Textheight + MENU_PAD * 2;

    struct window *win = newWindow(WIN_SCROLL);
    if (!win) return NULL;

    win->width = pix_width;
    win->height = pix_height;
    win->x = x;
    win->y = y;
    if (name) win->name = strdup(name);

    if (parent) {
        struct window *pw = W_Void2Window(parent);
        win->parent_id = pw->id;
        win->x += pw->x;
        win->y += pw->y;
    }

    /* Allocate scrolling window data */
    struct scrollingWindow *sw = calloc(1, sizeof(struct scrollingWindow));
    win->data = (char *)sw;

    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        pix_width * RENDER_SCALE, pix_height * RENDER_SCALE);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        winTarget(win);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        screenTarget();
    }

    return W_Window2Void(win);
}

W_Window W_MakeMenu(char *name, int x, int y, int width, int height,
                    W_Window parent, int border)
{
    int pix_width = width * W_Textwidth + WIN_EDGE * 2;
    int pix_height = height * (W_Textheight + MENU_PAD * 2 + MENU_BAR);

    struct window *win = newWindow(WIN_MENU);
    if (!win) return NULL;

    win->width = pix_width;
    win->height = pix_height;
    win->x = x;
    win->y = y;
    if (name) win->name = strdup(name);

    if (parent) {
        struct window *pw = W_Void2Window(parent);
        win->parent_id = pw->id;
        win->x += pw->x;
        win->y += pw->y;
    }

    /* Allocate menu items */
    struct menuItem *items = calloc(height, sizeof(struct menuItem));
    for (int i = 0; i < height; i++) {
        items[i].string = calloc(MAX_TEXT_WIDTH, 1);
        items[i].color = W_White;
    }
    win->data = (char *)items;

    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        pix_width * RENDER_SCALE, pix_height * RENDER_SCALE);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        winTarget(win);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        screenTarget();
    }

    return W_Window2Void(win);
}

/* ========================================================================
 * Window mapping / visibility
 * ======================================================================== */

void W_MapWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    win->mapped = 1;

    /* Redraw menus when mapped (X11 generates expose events for this) */
    if (win->type == WIN_MENU) {
        redrawMenu(win);
    }

    /* Generate an expose event like X11 does on map, so the application
     * knows to draw content into the newly-visible window. */
    W_Event ev;
    ev.type = W_EV_EXPOSE;
    ev.Window = window;
    ev.key = 0;
    ev.x = 0;
    ev.y = 0;
    evq_push(&ev);
}

void W_UnmapWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (win) win->mapped = 0;
}

int W_IsMapped(W_Window window)
{
    struct window *win = W_Void2Window(window);
    return win ? win->mapped : 0;
}

/* ========================================================================
 * Window properties
 * ======================================================================== */

int W_WindowWidth(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return 0;
    if (win->type == WIN_GRAPH)
        return win->width;
    return (win->width - WIN_EDGE * 2) / W_Textwidth;
}

int W_WindowHeight(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return 0;
    if (win->type == WIN_GRAPH)
        return win->height;
    if (win->type == WIN_MENU)
        return win->height / (W_Textheight + MENU_PAD * 2 + MENU_BAR);
    return (win->height - MENU_PAD * 2) / W_Textheight;
}

void W_RenameWindow(W_Window window, char *str)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    free(win->name);
    win->name = str ? strdup(str) : NULL;
    /* If this is a top-level window, set SDL window title */
    if (win->parent_id < 0 && sdl_window) {
        SDL_SetWindowTitle(sdl_window, str ? str : "Netrek");
    }
}

void W_SetWindowName(W_Window w, char *name)
{
    W_RenameWindow(w, name);
}

void W_FlushWindow(W_Window window)
{
    /* No-op for SDL2 - we composite in W_Flush */
}

void W_SetSensitive(W_Window w, int v)
{
    struct window *win = W_Void2Window(w);
    if (win) win->insensitive = !v;
}

/* ========================================================================
 * Clearing
 * ======================================================================== */

void W_ClearWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    winTarget(win);

    if (win->background) {
        /* Restore background */
        SDL_RenderCopy(sdl_renderer, win->background, NULL, NULL);
    } else {
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
    }

    screenTarget();

    /* Redraw menu separators after clear */
    if (win->type == WIN_MENU) {
        redrawMenu(win);
    }
}

void W_ClearArea(W_Window window, int x, int y, int width, int height)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    int px, py, pw, ph;
    if (win->type == WIN_GRAPH) {
        px = x; py = y; pw = width; ph = height;
    } else {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
        pw = width * W_Textwidth;
        ph = height * W_Textheight;
    }

    winTarget(win);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_Rect r = {px, py, pw, ph};
    SDL_RenderFillRect(sdl_renderer, &r);
    screenTarget();
}

void W_CacheClearArea(W_Window window, int x, int y, int width, int height)
{
    struct window *win = W_Void2Window(window);
    if (_rcache_win != win) {
        if (_rcache_index > 0) W_FlushClearAreaCache((W_Window)_rcache_win);
        _rcache_win = win;
    }
    if (_rcache_index < MAXCACHE) {
        _rcache[_rcache_index].x = x;
        _rcache[_rcache_index].y = y;
        _rcache[_rcache_index].w = width;
        _rcache[_rcache_index].h = height;
        _rcache_index++;
    } else {
        W_FlushClearAreaCache((W_Window)win);
        _rcache[0].x = x;
        _rcache[0].y = y;
        _rcache[0].w = width;
        _rcache[0].h = height;
        _rcache_index = 1;
        _rcache_win = win;
    }
}

void W_FlushClearAreaCache(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture || _rcache_index == 0) {
        _rcache_index = 0;
        return;
    }

    winTarget(win);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderFillRects(sdl_renderer, _rcache, _rcache_index);
    screenTarget();
    _rcache_index = 0;
}

void W_FillArea(W_Window window, int x, int y, int width, int height, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    int px, py, pw, ph;
    if (win->type == WIN_GRAPH) {
        px = x; py = y; pw = width; ph = height;
    } else {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
        pw = width * W_Textwidth;
        ph = height * W_Textheight;
    }

    winTarget(win);
    setRenderColor(color);
    SDL_Rect r = {px, py, pw, ph};
    SDL_RenderFillRect(sdl_renderer, &r);
    screenTarget();
}

/* ========================================================================
 * Text rendering
 * ======================================================================== */

/* Internal: add a line to scrolling window's linked list */
static void AddToScrolling(struct window *win, W_Color color, W_Font font,
                           char *str, int len)
{
    struct scrollingWindow *sw = (struct scrollingWindow *)win->data;
    if (!sw) return;

    struct stringList *new;

    if (sw->lines > 0 && sw->lines > scroll_lines) {
        /* Reuse tail node */
        new = sw->tail;
        sw->tail = new->prev;
        if (new->prev) new->prev->next = NULL;
        new->prev = NULL;
        new->next = sw->head;
        if (sw->head) sw->head->prev = new;
        sw->head = new;
    } else {
        new = calloc(1, sizeof(struct stringList));
        if (!new) return;
        new->next = sw->head;
        if (sw->head) sw->head->prev = new;
        sw->head = new;
        if (!sw->tail) sw->tail = new;
        sw->lines++;
    }

    sw->index = sw->head;
    sw->topline = 0;
    sw->updated++;

    if (len >= MAX_TEXT_WIDTH) len = MAX_TEXT_WIDTH - 1;
    memcpy(new->string, str, len);
    /* Pad with spaces like X11 backend */
    memset(&new->string[len], ' ', MAX_TEXT_WIDTH - len - 1);
    new->string[MAX_TEXT_WIDTH - 1] = '\0';
    new->color = color;
    new->font = font;
}

/* Internal: render a text string onto a window's texture */
static void renderTextOnWindow(struct window *win, int px, int py,
                               W_Color color, char *str, int len, W_Font font)
{
    int fi = fontNum(font);
    if (fi < 0 || fi > 3) fi = 1;
    TTF_Font *f = sdl_fonts[fi];
    if (!f) return;

    char buf[MAX_TEXT_WIDTH + 1];
    if (len > MAX_TEXT_WIDTH) len = MAX_TEXT_WIDTH;
    memcpy(buf, str, len);
    buf[len] = '\0';
    if (buf[0] == '\0') return;

    SDL_Color fg = getColor(color);
    SDL_Color bg = {0, 0, 0, 255};
    SDL_Surface *surface = TTF_RenderText_Shaded(f, buf, fg, bg);
    if (!surface) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);

    winTarget(win);
    /* Glyph texture is RENDER_SCALE x; draw it at logical size so winTarget's
     * scale renders it back at native resolution (crisp). */
    SDL_Rect dst = {px, py, tw / RENDER_SCALE, th / RENDER_SCALE};
    SDL_RenderCopy(sdl_renderer, tex, NULL, &dst);
    screenTarget();
    SDL_DestroyTexture(tex);
}

/* Internal: redraw a single menu item (clear its row + draw text) */
static void redrawMenuItem(struct window *win, int n)
{
    if (!win->data) return;
    struct menuItem *items = (struct menuItem *)win->data;
    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
    int py = n * item_h + MENU_PAD;
    int content_w = win->width - WIN_EDGE * 2;

    /* Clear the item's text area */
    winTarget(win);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_Rect clr = {WIN_EDGE, py, content_w, W_Textheight};
    SDL_RenderFillRect(sdl_renderer, &clr);
    screenTarget();

    /* Draw the text */
    int slen = strlen(items[n].string);
    if (slen > 0) {
        int px = WIN_EDGE + W_Textwidth * items[n].column;
        renderTextOnWindow(win, px, py, items[n].color, items[n].string,
                          slen, W_RegularFont);
    }
}

/* Internal: redraw entire menu (separators + all items) */
static void redrawMenu(struct window *win)
{
    if (!win->data || !win->texture) return;
    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
    int num_items = win->height / item_h;
    if (num_items <= 0) num_items = 1;

    winTarget(win);

    /* Clear entire menu */
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    /* Draw separator bars (grey lines between items) */
    SDL_Color grey = getColor(GREY);
    SDL_SetRenderDrawColor(sdl_renderer, grey.r, grey.g, grey.b, 255);
    for (int i = 1; i < num_items; i++) {
        int sep_y = i * item_h - MENU_BAR;
        SDL_RenderDrawLine(sdl_renderer, 0, sep_y, win->width - 1, sep_y);
    }

    screenTarget();

    /* Redraw all menu items */
    for (int i = 0; i < num_items; i++) {
        redrawMenuItem(win, i);
    }
}

/* Internal: update a menu item's stored text/color and redraw it */
static void changeMenuItem(struct window *win, int col, int row,
                           char *str, int len, W_Color color)
{
    if (!win->data) return;
    struct menuItem *items = (struct menuItem *)win->data;
    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
    int num_items = win->height / item_h;
    if (row < 0 || row >= num_items) return;

    items[row].column = col;
    items[row].color = color;
    if (len >= MAX_TEXT_WIDTH) len = MAX_TEXT_WIDTH - 1;
    memcpy(items[row].string, str, len);
    items[row].string[len] = '\0';

    redrawMenuItem(win, row);
}

void W_WriteText(W_Window window, int x, int y, W_Color color, char *str,
                 int len, W_Font font)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture || !str || len <= 0) return;

    /* Debug: log all writes to the tactical window ("local") */
    /* Calculate pixel position */
    int px, py;
    switch (win->type) {
    case WIN_GRAPH:
        px = x;
        py = y;
        break;
    case WIN_SCROLL: {
        /* For scrolling windows, draw at bottom line and add to list */
        int vis_height = (win->height - MENU_PAD * 2) / W_Textheight;
        px = WIN_EDGE;
        py = MENU_PAD + W_Textheight * (vis_height - 1);
        renderTextOnWindow(win, px, py, color, str, len, font);
        AddToScrolling(win, color, font, str, len);
        return;
    }
    case WIN_MENU:
        changeMenuItem(win, x, y, str, len, color);
        return;
    default: /* WIN_TEXT */
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
        break;
    }

    renderTextOnWindow(win, px, py, color, str, len, font);
}

void W_MaskText(W_Window window, int x, int y, W_Color color, char *str,
                int len, W_Font font)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture || !str || len <= 0) return;

    int fi = fontNum(font);
    if (fi < 0 || fi > 3) fi = 1;
    TTF_Font *f = sdl_fonts[fi];
    if (!f) return;

    char buf[MAX_TEXT_WIDTH + 1];
    if (len > MAX_TEXT_WIDTH) len = MAX_TEXT_WIDTH;
    memcpy(buf, str, len);
    buf[len] = '\0';
    if (buf[0] == '\0') return;

    int px, py;
    if (win->type == WIN_GRAPH) {
        px = x; py = y;
    } else {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
    }

    /* Render text without background (transparent) */
    SDL_Color fg = getColor(color);
    SDL_Surface *surface = TTF_RenderText_Blended(f, buf, fg);
    if (!surface) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);

    winTarget(win);
    /* Glyph texture is RENDER_SCALE x; draw it at logical size so winTarget's
     * scale renders it back at native resolution (crisp). */
    SDL_Rect dst = {px, py, tw / RENDER_SCALE, th / RENDER_SCALE};
    SDL_RenderCopy(sdl_renderer, tex, NULL, &dst);
    screenTarget();
    SDL_DestroyTexture(tex);
}

/* ========================================================================
 * Bitmap storage and rendering
 * ======================================================================== */

W_Icon W_StoreBitmap(int width, int height, char *data, W_Window window)
{
    struct icon *ic = calloc(1, sizeof(struct icon));
    if (!ic) return NULL;

    ic->width = width;
    ic->height = height;

    /* Store a copy of the raw XBM bits for later re-colorization */
    int bytes = ((width + 7) / 8) * height;
    ic->raw_bits = malloc(bytes);
    if (data && ic->raw_bits) {
        memcpy(ic->raw_bits, data, bytes);
    }

    /* Convert 1-bit XBM data to RGBA texture (white + alpha) */
    Uint32 *pixels = calloc(width * height, sizeof(Uint32));
    if (pixels && data) {
        int stride = (width + 7) / 8;
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                int byte_idx = row * stride + col / 8;
                int bit = (((unsigned char)data[byte_idx]) >> (col % 8)) & 1;
                if (bit) {
                    /* White pixel, full alpha (will be color-modulated) */
                    pixels[row * width + col] = 0xFFFFFFFF;
                } else {
                    /* Transparent */
                    pixels[row * width + col] = 0x00000000;
                }
            }
        }
    }

    ic->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
        width, height);
    if (ic->texture) {
        SDL_UpdateTexture(ic->texture, NULL, pixels, width * sizeof(Uint32));
        SDL_SetTextureBlendMode(ic->texture, SDL_BLENDMODE_BLEND);
    }
    free(pixels);

    ic->parent_win = W_Void2Window(window);
    return W_Icon2Void(ic);
}

void W_WriteBitmap(int x, int y, W_Icon bit, W_Color color)
{
    struct icon *ic = W_Void2Icon(bit);
    if (!ic || !ic->texture || !ic->parent_win) return;
    struct window *win = ic->parent_win;
    if (!win->texture) return;

    SDL_Color c = getColor(color);
    SDL_SetTextureColorMod(ic->texture, c.r, c.g, c.b);

    winTarget(win);
    /* (x,y) is the top-left corner - callers already pre-center */
    SDL_Rect dst = {x, y, ic->width, ic->height};
    SDL_RenderCopy(sdl_renderer, ic->texture, NULL, &dst);
    screenTarget();
}

void W_OverlayBitmap(int x, int y, W_Icon bit, W_Color color)
{
    struct icon *ic = W_Void2Icon(bit);
    if (!ic || !ic->texture || !ic->parent_win) return;
    struct window *win = ic->parent_win;
    if (!win->texture) return;

    SDL_Color c = getColor(color);
    SDL_SetTextureColorMod(ic->texture, c.r, c.g, c.b);
    SDL_SetTextureBlendMode(ic->texture, SDL_BLENDMODE_ADD);

    winTarget(win);
    /* (x,y) is the top-left corner - callers already pre-center */
    SDL_Rect dst = {x, y, ic->width, ic->height};
    SDL_RenderCopy(sdl_renderer, ic->texture, NULL, &dst);
    screenTarget();

    SDL_SetTextureBlendMode(ic->texture, SDL_BLENDMODE_BLEND);
}

/* ========================================================================
 * Line drawing
 * ======================================================================== */

void W_MakeLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    winTarget(win);
    setRenderColor(color);
    SDL_RenderDrawLine(sdl_renderer, x0, y0, x1, y1);
    screenTarget();
}

void W_CacheLine(W_Window window, int x0, int y0, int x1, int y1, int color)
{
    if (color < 0 || color >= NCOLORS) return;
    int idx = _lcache_index[color];
    if (idx >= MAXCACHE) {
        /* Flush this color's cache - draw individual segments (NOT polyline) */
        struct window *win = W_Void2Window(window);
        if (win && win->texture) {
            winTarget(win);
            setRenderColor(color);
            for (int i = 0; i < idx; i++) {
                SDL_RenderDrawLine(sdl_renderer,
                    _lcache[color][i * 2].x, _lcache[color][i * 2].y,
                    _lcache[color][i * 2 + 1].x, _lcache[color][i * 2 + 1].y);
            }
            screenTarget();
        }
        _lcache_index[color] = 0;
        idx = 0;
    }
    _lcache[color][idx * 2].x = x0;
    _lcache[color][idx * 2].y = y0;
    _lcache[color][idx * 2 + 1].x = x1;
    _lcache[color][idx * 2 + 1].y = y1;
    _lcache_index[color] = idx + 1;
}

void W_FlushLineCaches(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    winTarget(win);
    for (int c = 0; c < NCOLORS; c++) {
        int idx = _lcache_index[c];
        if (idx > 0) {
            setRenderColor(c);
            /* Draw individual line segments */
            for (int i = 0; i < idx; i++) {
                SDL_RenderDrawLine(sdl_renderer,
                    _lcache[c][i * 2].x, _lcache[c][i * 2].y,
                    _lcache[c][i * 2 + 1].x, _lcache[c][i * 2 + 1].y);
            }
            _lcache_index[c] = 0;
        }
    }
    screenTarget();
}

void W_MakeTractLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    /* Dashed line: SDL2 doesn't support dash patterns, so draw segments */
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    winTarget(win);
    setRenderColor(color);

    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1.0) {
        screenTarget();
        return;
    }

    double ux = dx / len, uy = dy / len;
    int dash_on = 1, dash_len = 8;
    double t = 0;

    while (t < len) {
        double end = t + dash_len;
        if (end > len) end = len;
        if (dash_on) {
            int sx = (int)(x0 + ux * t);
            int sy = (int)(y0 + uy * t);
            int ex = (int)(x0 + ux * end);
            int ey = (int)(y0 + uy * end);
            SDL_RenderDrawLine(sdl_renderer, sx, sy, ex, ey);
        }
        t = end;
        dash_on = !dash_on;
    }

    screenTarget();
}

void W_MakePhaserLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    /* Phaser line - just a regular line, could be thicker */
    W_MakeLine(window, x0, y0, x1, y1, color);
}

/* ========================================================================
 * Shape drawing
 * ======================================================================== */

void W_WriteCircle(W_Window window, int x, int y, int r, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    winTarget(win);
    setRenderColor(color);

    /* Midpoint circle algorithm */
    int cx = 0, cy = r;
    int d = 1 - r;

    while (cx <= cy) {
        SDL_RenderDrawPoint(sdl_renderer, x + cx, y + cy);
        SDL_RenderDrawPoint(sdl_renderer, x - cx, y + cy);
        SDL_RenderDrawPoint(sdl_renderer, x + cx, y - cy);
        SDL_RenderDrawPoint(sdl_renderer, x - cx, y - cy);
        SDL_RenderDrawPoint(sdl_renderer, x + cy, y + cx);
        SDL_RenderDrawPoint(sdl_renderer, x - cy, y + cx);
        SDL_RenderDrawPoint(sdl_renderer, x + cy, y - cx);
        SDL_RenderDrawPoint(sdl_renderer, x - cy, y - cx);
        if (d < 0) {
            d += 2 * cx + 3;
        } else {
            d += 2 * (cx - cy) + 5;
            cy--;
        }
        cx++;
    }

    screenTarget();
}

void W_WriteTriangle(W_Window window, int x, int y, int s, int t, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->texture) return;

    winTarget(win);
    setRenderColor(color);

    /* Match the X11 original exactly: the tip is anchored AT (x,y); t selects
     * which way it points. t==0 -> base above the tip, so it points DOWN (used
     * for planet lock, placed just above the planet to point at it). t==1 ->
     * base below the tip, points UP (used for player lock). The prior SDL2
     * version was both inverted and center-anchored, so lock markers pointed
     * away from and floated off their target. */
    SDL_Point pts[4];
    if (t == 0) {
        pts[0].x = x;         pts[0].y = y;
        pts[1].x = x + s;     pts[1].y = y - s;
        pts[2].x = x - s;     pts[2].y = y - s;
        pts[3] = pts[0]; /* close the triangle */
    } else {
        pts[0].x = x;         pts[0].y = y;
        pts[1].x = x + s;     pts[1].y = y + s;
        pts[2].x = x - s;     pts[2].y = y + s;
        pts[3] = pts[0];
    }
    SDL_RenderDrawLines(sdl_renderer, pts, 4);

    screenTarget();
}

/* ========================================================================
 * Event handling
 * ======================================================================== */

static unsigned char sdl_key_to_wlib(SDL_Keycode sym, SDL_Keymod mod)
{
    /* Convert SDL keysym to Wlib key code */
    if (sym == SDLK_UP) return W_Key_Up;
    if (sym == SDLK_DOWN) return W_Key_Down;

    /* For printable ASCII, handle shift and caps lock */
    if (sym >= SDLK_a && sym <= SDLK_z) {
        char base = sym - SDLK_a + 'a';
        int shifted = (mod & KMOD_SHIFT) != 0;
        int caps = (mod & KMOD_CAPS) != 0;
        if (shifted ^ caps) base -= 32; /* uppercase if shift XOR caps */
        if (mod & KMOD_CTRL) base = (base & 0x1f); /* ctrl-a = 1, etc */
        return base;
    }

    if (sym >= SDLK_0 && sym <= SDLK_9) {
        if (mod & KMOD_SHIFT) {
            /* Shifted number keys - US keyboard layout */
            const char shifted[] = ")!@#$%^&*(";
            return shifted[sym - SDLK_0];
        }
        return sym;
    }

    /* Common symbols */
    switch (sym) {
    case SDLK_SPACE:     return ' ';
    case SDLK_RETURN:    return '\r';
    case SDLK_ESCAPE:    return 27;
    case SDLK_BACKSPACE: return 8;
    case SDLK_TAB:       return '\t';
    case SDLK_DELETE:     return 127;
    case SDLK_SEMICOLON: return (mod & KMOD_SHIFT) ? ':' : ';';
    case SDLK_QUOTE:     return (mod & KMOD_SHIFT) ? '"' : '\'';
    case SDLK_COMMA:     return (mod & KMOD_SHIFT) ? '<' : ',';
    case SDLK_PERIOD:    return (mod & KMOD_SHIFT) ? '>' : '.';
    case SDLK_SLASH:     return (mod & KMOD_SHIFT) ? '?' : '/';
    case SDLK_BACKSLASH: return (mod & KMOD_SHIFT) ? '|' : '\\';
    case SDLK_MINUS:     return (mod & KMOD_SHIFT) ? '_' : '-';
    case SDLK_EQUALS:    return (mod & KMOD_SHIFT) ? '+' : '=';
    case SDLK_LEFTBRACKET:  return (mod & KMOD_SHIFT) ? '{' : '[';
    case SDLK_RIGHTBRACKET: return (mod & KMOD_SHIFT) ? '}' : ']';
    case SDLK_BACKQUOTE: return (mod & KMOD_SHIFT) ? '~' : '`';
    default: break;
    }

    return 0; /* Unknown key */
}

/* Raw window-point mouse position, cached from motion/button events. NOT read
 * from SDL_GetMouseState: once SDL_RenderSetScale is active, GetMouseState
 * returns scale-polluted coords, which pt_to_logical would then scale a second
 * time (aim drifts as the window grows). Events carry raw points, so cache. */
static int last_mouse_px = 0, last_mouse_py = 0;

/* Current mouse position in logical canvas coordinates, for keyboard/wheel
 * actions that fire toward the cursor. */
static void get_logical_mouse(int *mx, int *my)
{
    pt_to_logical(last_mouse_px, last_mouse_py, mx, my);
}

static int translate_sdl_event(SDL_Event *sdl_ev, W_Event *wev)
{
    if (sdl_ev->type == SDL_MOUSEMOTION) {
        last_mouse_px = sdl_ev->motion.x; last_mouse_py = sdl_ev->motion.y;
    } else if (sdl_ev->type == SDL_MOUSEBUTTONDOWN ||
               sdl_ev->type == SDL_MOUSEBUTTONUP) {
        last_mouse_px = sdl_ev->button.x; last_mouse_py = sdl_ev->button.y;
    }
    memset(wev, 0, sizeof(W_Event));

    switch (sdl_ev->type) {
    case SDL_KEYDOWN: {
        unsigned char key = sdl_key_to_wlib(sdl_ev->key.keysym.sym,
                                            sdl_ev->key.keysym.mod);
        if (key == 0) return 0;

        wev->type = W_EV_KEY;
        wev->key = key;
#ifdef MOUSE_AS_SHIFT
        wev->modifier = 0;
        if (sdl_ev->key.keysym.mod & KMOD_SHIFT) wev->modifier |= W_SHIFT_BUTTON;
        if (sdl_ev->key.keysym.mod & KMOD_CTRL) wev->modifier |= W_CTRL_BUTTON;
#endif

        /* Determine which window has focus - use mouse position */
        int mx, my;
        get_logical_mouse(&mx, &my);
        struct window *win = findWindowAt(mx, my);
        wev->Window = win ? W_Window2Void(win) : NULL;
        wev->x = mx - (win ? win->x : 0);
        wev->y = my - (win ? win->y : 0);
        return 1;
    }

#ifdef AUTOKEY
    case SDL_KEYUP: {
        unsigned char key = sdl_key_to_wlib(sdl_ev->key.keysym.sym,
                                            sdl_ev->key.keysym.mod);
        if (key == 0) return 0;
        wev->type = W_EV_KEY_OFF;
        wev->key = key;
        int mx, my;
        get_logical_mouse(&mx, &my);
        struct window *win = findWindowAt(mx, my);
        wev->Window = win ? W_Window2Void(win) : NULL;
        return 1;
    }
#endif

    case SDL_MOUSEBUTTONDOWN: {
        int mx, my;
        pt_to_logical(sdl_ev->button.x, sdl_ev->button.y, &mx, &my);
        struct window *win = findWindowAt(mx, my);
        if (!win) return 0;

        wev->type = W_EV_BUTTON;
        wev->Window = W_Window2Void(win);
        wev->x = mx - win->x;
        wev->y = my - win->y;

        /* For menu windows, translate Y to item index (COW expects this) */
        if (win->type == WIN_MENU) {
            int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
            wev->y = wev->y / item_h;
        }

        switch (sdl_ev->button.button) {
        case SDL_BUTTON_LEFT:   wev->key = W_LBUTTON; break;
        case SDL_BUTTON_MIDDLE: wev->key = W_MBUTTON; break;
        case SDL_BUTTON_RIGHT:  wev->key = W_RBUTTON; break;
        default: return 0;
        }

#ifdef SHIFTED_MOUSE
        {
            SDL_Keymod mod = SDL_GetModState();
            if (mod & KMOD_SHIFT) wev->key |= W_SHIFT_BUTTON;
            if (mod & KMOD_CTRL)  wev->key |= W_CTRL_BUTTON;
        }
#endif
        return 1;
    }

    case SDL_MOUSEWHEEL: {
        int mx, my;
        get_logical_mouse(&mx, &my);
        struct window *win = findWindowAt(mx, my);
        if (!win) return 0;

        wev->type = W_EV_BUTTON;
        wev->Window = W_Window2Void(win);
        wev->x = mx - win->x;
        wev->y = my - win->y;
        wev->key = (sdl_ev->wheel.y > 0) ? W_WUBUTTON : W_WDBUTTON;
        return 1;
    }

#ifdef MOTION_MOUSE
    case SDL_MOUSEMOTION: {
        /* Only report if buttons held */
        if (sdl_ev->motion.state == 0) return 0;
        int mx, my;
        pt_to_logical(sdl_ev->motion.x, sdl_ev->motion.y, &mx, &my);
        struct window *win = findWindowAt(mx, my);
        if (!win) return 0;

        wev->type = W_EV_CM_BUTTON;
        wev->Window = W_Window2Void(win);
        wev->x = mx - win->x;
        wev->y = my - win->y;

        if (sdl_ev->motion.state & SDL_BUTTON_LMASK) wev->key = W_LBUTTON;
        else if (sdl_ev->motion.state & SDL_BUTTON_MMASK) wev->key = W_MBUTTON;
        else if (sdl_ev->motion.state & SDL_BUTTON_RMASK) wev->key = W_RBUTTON;
        return 1;
    }
#endif

    case SDL_WINDOWEVENT:
        if (sdl_ev->window.event == SDL_WINDOWEVENT_EXPOSED) {
            /* Send expose event for all mapped windows */
            /* Just pick the first mapped window for now */
            for (int i = 0; i < num_windows; i++) {
                if (windows[i].mapped) {
                    wev->type = W_EV_EXPOSE;
                    wev->Window = W_Window2Void(&windows[i]);
                    return 1;
                }
            }
            return 0;
        }
        if (sdl_ev->window.event == SDL_WINDOWEVENT_CLOSE) {
            wev->type = W_EV_CLOSED;
            return 1;
        }
        return 0;

    case SDL_QUIT:
        wev->type = W_EV_CLOSED;
        return 1;

    default:
        return 0;
    }
}

/* Poll SDL and enqueue any translated events */
static void poll_sdl_into_queue(void)
{
    SDL_Event sdl_ev;
    while (SDL_PollEvent(&sdl_ev)) {
        if (sdl_ev.type == SDL_QUIT ||
            (sdl_ev.type == SDL_WINDOWEVENT &&
             sdl_ev.window.event == SDL_WINDOWEVENT_CLOSE)) {
            fprintf(stderr, "sdl2window: window closed, exiting\n");
            SDL_Quit();
            exit(0);
        }
        W_Event tmp;
        if (translate_sdl_event(&sdl_ev, &tmp)) {
            evq_push(&tmp);
        }
    }
    /* Wake select() if events are queued */
    if (!evq_empty()) {
        char dummy = 'e';
        write(socket_pipe[1], &dummy, 1);
    }
}

int W_EventsPending(void)
{
    /* Auto-flush: in X11, draw commands go to the display server immediately.
     * In SDL2, nothing appears until SDL_RenderPresent (W_Flush).
     * Some COW loops (entrywindow, findslot) never call W_Flush,
     * so we do it here when the app is polling for events. */
    static int in_flush = 0;
    if (!in_flush && sdl_renderer) {
        in_flush = 1;
        W_Flush();
        in_flush = 0;
    }

    if (!evq_empty()) return 1;
    poll_sdl_into_queue();
    return !evq_empty();
}

void W_NextEvent(W_Event *wevent)
{
    while (evq_empty()) {
        SDL_Event sdl_ev;
        if (SDL_WaitEvent(&sdl_ev)) {
            if (sdl_ev.type == SDL_QUIT ||
                (sdl_ev.type == SDL_WINDOWEVENT &&
                 sdl_ev.window.event == SDL_WINDOWEVENT_CLOSE)) {
                fprintf(stderr, "sdl2window: window closed, exiting\n");
                SDL_Quit();
                exit(0);
            }
            W_Event tmp;
            if (translate_sdl_event(&sdl_ev, &tmp)) {
                evq_push(&tmp);
            }
        }
    }
    evq_pop(wevent);

    /* Drain the pipe */
    char dummy;
    while (read(socket_pipe[0], &dummy, 1) > 0) {}
}

int W_SpNextEvent(W_Event *wevent)
{
    if (!evq_empty()) {
        evq_pop(wevent);
        char dummy;
        while (read(socket_pipe[0], &dummy, 1) > 0) {}
        return 1;
    }
    poll_sdl_into_queue();
    if (!evq_empty()) {
        evq_pop(wevent);
        return 1;
    }
    return 0;
}

int W_EventsQueued(void)
{
    return !evq_empty();
}

int W_EventsQueuedCk(void)
{
    return W_EventsPending();
}

/* ========================================================================
 * W_Socket - pipe FD for select() integration
 * ======================================================================== */

/* Pump SDL events - called from sdl2_select() wrapper before every select().
 * Translates any pending SDL events and queues them, writing to the pipe
 * to wake select() if an event was found. */
void sdl2_pump_events(void)
{
    poll_sdl_into_queue();
}

int W_Socket(void)
{
    sdl2_pump_events();
    return socket_pipe[0];
}

/* ========================================================================
 * Compositor - W_Flush
 * ======================================================================== */

void W_Flush(void)
{
    if (!sdl_renderer) return;

    screenTarget();
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    /* Composite scale: the default target IS the drawable (HiDPI pixels), so
     * scale the logical canvas up to fill it. cs = drawable / LOGICAL, uniform
     * (letterbox). This is drawable-space; pt_to_logical works in point-space,
     * so cs = pt_to_logical's s * dpi -- both derive from the same window and
     * cannot drift. Window textures are RENDER_SCALE x logical, so on the 2x
     * retina drawable each copy lands ~1:1 and text/vectors stay crisp. */
    int out_w = LOGICAL_W, out_h = LOGICAL_H;
    SDL_GetRendererOutputSize(sdl_renderer, &out_w, &out_h);
    float csx = (float)out_w / (float)LOGICAL_W;
    float csy = (float)out_h / (float)LOGICAL_H;
    float cs = csx < csy ? csx : csy;

    for (int i = 0; i < num_windows; i++) {
        struct window *win = &windows[i];
        if (!win->mapped || !win->texture) continue;

        SDL_Rect dst = {(int)(win->x * cs), (int)(win->y * cs),
                        (int)(win->width * cs), (int)(win->height * cs)};
        SDL_RenderCopy(sdl_renderer, win->texture, NULL, &dst);

        /* Draw border if configured */
        if (win->border_color >= 0) {
            setRenderColor(win->border_color);
            SDL_RenderDrawRect(sdl_renderer, &dst);
        }
    }

    SDL_RenderPresent(sdl_renderer);

    /* Pump events (also handles SDL_QUIT) */
    sdl2_pump_events();
}

void W_ProbeLatency(W_Window w)
{
    /* No-op for SDL2 - no server round-trip */
}

/* ========================================================================
 * Window tiling / background
 * ======================================================================== */

void W_TileWindow(W_Window window, W_Icon bit)
{
    struct window *win = W_Void2Window(window);
    struct icon *ic = W_Void2Icon(bit);
    if (!win || !ic || !ic->texture) return;

    /* Create a background texture by tiling the icon pattern */
    if (win->background) SDL_DestroyTexture(win->background);

    win->background = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        win->width * RENDER_SCALE, win->height * RENDER_SCALE);
    if (!win->background) return;

    SDL_SetRenderTarget(sdl_renderer, win->background);
    SDL_RenderSetScale(sdl_renderer, (float)RENDER_SCALE, (float)RENDER_SCALE);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    /* Tile the icon across the background (logical coords, scaled to RS tex) */
    SDL_Color white = {255, 255, 255, 255};
    SDL_SetTextureColorMod(ic->texture, white.r, white.g, white.b);
    for (int ty = 0; ty < win->height; ty += ic->height) {
        for (int tx = 0; tx < win->width; tx += ic->width) {
            SDL_Rect dst = {tx, ty, ic->width, ic->height};
            SDL_RenderCopy(sdl_renderer, ic->texture, NULL, &dst);
        }
    }
    screenTarget();
}

void W_UnTileWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    if (win->background) {
        SDL_DestroyTexture(win->background);
        win->background = NULL;
    }
}

void W_SetBackgroundPixmap(W_Window window, unsigned long background)
{
    /* STUB: background pixmap from unsigned long (X11 legacy) */
}

void W_UnsetBackgroundPixmap(W_Window window)
{
    W_UnTileWindow(window);
}

/* ========================================================================
 * Cursor definitions (stubs - SDL2 uses system cursor or custom)
 * ======================================================================== */

void W_DefineCursor(W_Window window, int width, int height, char *bits,
                    char *mask, int xhot, int yhot)
{
}

void W_DefineMapcursor(W_Window window) {}
void W_DefineLocalcursor(W_Window window) {}
void W_DefineFedCursor(W_Window window) {}
void W_DefineRomCursor(W_Window window) {}
void W_DefineKliCursor(W_Window window) {}
void W_DefineOriCursor(W_Window window) {}
void W_DefineTrekCursor(W_Window window) {}
void W_DefineWarningCursor(W_Window window) {}
void W_DefineArrowCursor(W_Window window) {}
void W_DefineTextCursor(W_Window window) {}

/* ========================================================================
 * Window destruction and misc
 * ======================================================================== */

void W_DestroyWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || win == &myroot) return;

    if (win->texture) { SDL_DestroyTexture(win->texture); win->texture = NULL; }
    if (win->background) { SDL_DestroyTexture(win->background); win->background = NULL; }
    if (win->name) { free(win->name); win->name = NULL; }
    /* Free type-specific data */
    if (win->data) {
        if (win->type == WIN_SCROLL) {
            struct scrollingWindow *sw = (struct scrollingWindow *)win->data;
            struct stringList *s = sw->head;
            while (s) {
                struct stringList *next = s->next;
                free(s);
                s = next;
            }
            free(sw);
        } else if (win->type == WIN_MENU) {
            /* Menu items freed separately */
            free(win->data);
        }
        win->data = NULL;
    }
    win->mapped = 0;
}

void W_SetIconWindow(W_Window main, W_Window icon)
{
    /* No-op for SDL2 */
}

void W_Beep(void)
{
    /* Simple beep - could use SDL_mixer */
    fprintf(stderr, "\a");
}

int W_Mono(void)
{
    return forceMono;
}

void W_ChangeBorder(W_Window window, int color)
{
    struct window *win = W_Void2Window(window);
    if (win) win->border_color = color;
}

void W_ResizeWindow(W_Window window, int neww, int newh)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;

    win->width = neww;
    win->height = newh;

    if (win->texture) SDL_DestroyTexture(win->texture);
    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        neww * RENDER_SCALE, newh * RENDER_SCALE);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        winTarget(win);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        screenTarget();
    }
}

void W_ResizeMenu(W_Window window, int neww, int newh)
{
    int pix_width = neww * W_Textwidth + WIN_EDGE * 2;
    int pix_height = newh * (W_Textheight + MENU_PAD * 2 + MENU_BAR);
    W_ResizeWindow(window, pix_width, pix_height);
}

void W_ReinitMenu(W_Window window, int neww, int newh)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;

    /* Free old menu items */
    if (win->data) {
        free(win->data);
    }

    struct menuItem *items = calloc(newh, sizeof(struct menuItem));
    for (int i = 0; i < newh; i++) {
        items[i].string = calloc(MAX_TEXT_WIDTH, 1);
        items[i].color = W_White;
    }
    win->data = (char *)items;

    W_ResizeMenu(window, neww, newh);
}

void W_DestroyMenu(W_Window window)
{
    W_DestroyWindow(window);
}

void W_ResizeTextWindow(W_Window window, int neww, int newh)
{
    int pix_width = neww * W_Textwidth + WIN_EDGE * 2;
    int pix_height = newh * W_Textheight + MENU_PAD * 2;
    W_ResizeWindow(window, pix_width, pix_height);
}

/* ========================================================================
 * Scrolling window support
 * ======================================================================== */

void W_FlushScrollingWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || win->type != WIN_SCROLL || !win->data) return;
    if (!win->mapped) return;

    struct scrollingWindow *sw = (struct scrollingWindow *)win->data;
    if (!sw->updated) return;

    int vis_height = (win->height - MENU_PAD * 2) / W_Textheight;

    /* Clear the window and redraw all visible lines */
    winTarget(win);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    screenTarget();

    /* Walk the list from index (top of visible area) */
    struct stringList *item = sw->index;
    int y = vis_height - 1;

    /* Walk to find the correct starting position */
    for (int i = 0; i < sw->topline && item; i++) {
        item = item->next;
    }

    /* Draw from bottom up (newest at bottom) */
    for (; item && y >= 0; item = item->next, y--) {
        int py = MENU_PAD + y * W_Textheight;
        int slen = strlen(item->string);
        if (slen > 0) {
            renderTextOnWindow(win, WIN_EDGE, py, item->color,
                             item->string, slen, item->font);
        }
    }

    sw->updated = 0;
}

/* ========================================================================
 * Callback handler accessors
 * ======================================================================== */

W_Callback W_GetWindowKeyDownHandler(W_Window w)
{
    struct window *win = W_Void2Window(w);
    return win ? win->handle_keydown : NULL;
}

void W_SetWindowKeyDownHandler(W_Window w, W_Callback c)
{
    struct window *win = W_Void2Window(w);
    if (win) win->handle_keydown = c;
}

W_Callback W_GetWindowKeyUpHandler(W_Window w)
{
    struct window *win = W_Void2Window(w);
    return win ? win->handle_keyup : NULL;
}

void W_SetWindowKeyUpHandler(W_Window w, W_Callback c)
{
    struct window *win = W_Void2Window(w);
    if (win) win->handle_keyup = c;
}

W_Callback W_GetWindowButtonHandler(W_Window w)
{
    struct window *win = W_Void2Window(w);
    return win ? win->handle_button : NULL;
}

void W_SetWindowButtonHandler(W_Window w, W_Callback c)
{
    struct window *win = W_Void2Window(w);
    if (win) win->handle_button = c;
}

W_Callback W_GetWindowExposeHandler(W_Window w)
{
    struct window *win = W_Void2Window(w);
    return win ? win->handle_expose : NULL;
}

void W_SetWindowExposeHandler(W_Window w, W_Callback c)
{
    struct window *win = W_Void2Window(w);
    if (win) win->handle_expose = c;
}

/* ========================================================================
 * TTS (Text-To-Speech / Beeplite overlay) functions
 * ======================================================================== */

int W_TTSTextHeight(void)
{
    if (tts_font) {
        return TTF_FontHeight(tts_font);
    }
    return 20;
}

int W_TTSTextWidth(char *s, int l)
{
    if (!tts_font || !s) return l * 10;
    char buf[256];
    int n = (l < 255) ? l : 255;
    memcpy(buf, s, n);
    buf[n] = '\0';
    int w, h;
    TTF_SizeText(tts_font, buf, &w, &h);
    return w;
}

void W_EraseTTSText(W_Window window, int max_width, int y, int width)
{
    /* Clear TTS text area */
    W_ClearArea(window, 0, y, max_width, W_TTSTextHeight());
}

void W_WriteTTSText(W_Window window, int max_width, int y, int width,
                    char *str, int len)
{
    if (str && len > 0) {
        W_MaskText(window, 0, y, W_White, str, len, W_RegularFont);
    }
}

void W_Halo(int x, int y, W_Color color)
{
    extern W_Window mapw;
    struct window *win = W_Void2Window(mapw);
    if (!win || !win->texture) return;
    if (color == W_Ind || color == W_Grey) return;

    /* Draw a circle on the galactic map around a planet (mplanet_width/2) */
    int r = 8;
    winTarget(win);
    setRenderColor(color);

    /* Midpoint circle algorithm */
    int cx = x, cy = y;
    int px = 0, py = r;
    int d = 1 - r;
    while (px <= py) {
        SDL_RenderDrawPoint(sdl_renderer, cx + px, cy + py);
        SDL_RenderDrawPoint(sdl_renderer, cx - px, cy + py);
        SDL_RenderDrawPoint(sdl_renderer, cx + px, cy - py);
        SDL_RenderDrawPoint(sdl_renderer, cx - px, cy - py);
        SDL_RenderDrawPoint(sdl_renderer, cx + py, cy + px);
        SDL_RenderDrawPoint(sdl_renderer, cx - py, cy + px);
        SDL_RenderDrawPoint(sdl_renderer, cx + py, cy - px);
        SDL_RenderDrawPoint(sdl_renderer, cx - py, cy - px);
        px++;
        if (d < 0) {
            d += 2 * px + 1;
        } else {
            py--;
            d += 2 * (px - py) + 1;
        }
    }

    screenTarget();
}

/* ========================================================================
 * Fullscreen support
 * ======================================================================== */

void W_FullScreenOn(W_Window window)
{
    if (sdl_window)
        SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    full_screen_enabled = 1;
}

void W_FullScreenOff(W_Window window)
{
    if (sdl_window)
        SDL_SetWindowFullscreen(sdl_window, 0);
    full_screen_enabled = 0;
}

void W_FullScreenInitialise(void)
{
    /* Read config for initial fullscreen preference */
    full_screen_default = 0;
}

int W_FullScreenToggle(W_Window window)
{
    if (full_screen_enabled)
        W_FullScreenOff(window);
    else
        W_FullScreenOn(window);
    return FULLSCREEN_OK;
}

void W_FullScreenBegin(W_Window window)
{
    if (full_screen_default)
        W_FullScreenOn(window);
}

void W_FullScreen(W_Window window)
{
    /* Called regularly to maintain fullscreen state - no-op for SDL2 */
}

/* ========================================================================
 * Camera / screenshot
 * ======================================================================== */

void W_CameraSnap(W_Window window)
{
    int w, h;
    SDL_GetRendererOutputSize(sdl_renderer, &w, &h);
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
        SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return;

    screenTarget();
    SDL_RenderReadPixels(sdl_renderer, NULL, SDL_PIXELFORMAT_RGBA8888,
                         surf->pixels, surf->pitch);

    char fname[128];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(fname, sizeof(fname), "netrek_%04d%02d%02d_%02d%02d%02d.bmp",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    SDL_SaveBMP(surf, fname);
    SDL_FreeSurface(surf);
    fprintf(stderr, "sdl2window: screenshot saved to %s\n", fname);
}

/* ========================================================================
 * W_GetPixmaps - delegates to sdl2sprite.c
 * ======================================================================== */

extern void GetPixmaps_SDL2(W_Window t, W_Window g);

void W_GetPixmaps(W_Window t, W_Window g)
{
    GetPixmaps_SDL2(t, g);
}

/* ========================================================================
 * Background images (delegates to sdl2sprite.c)
 * ======================================================================== */

void W_GalacticBgd(int which)
{
}

void W_LocalBgd(int which)
{
}

void W_SetBackground(W_Window w, int which)
{
}

void *W_SetBackgroundImage(W_Window w, char *name)
{
    return NULL;
}

void W_NextScreenShot(W_Window w, int x, int y) {}
void W_DrawScreenShot(W_Window w, int x, int y) {}

void *W_ReadImage(W_Window w, char *name)
{
    return NULL;
}

void W_DrawImage(int x, int y, void *sprite_v)
{
}

void W_DropImage(void *sprite_v)
{
    if (sprite_v) {
        struct S_Object *sprite = (struct S_Object *)sprite_v;
        if (sprite->texture) SDL_DestroyTexture(sprite->texture);
        free(sprite);
    }
}

/* ========================================================================
 * Utility functions referenced by COW code
 * ======================================================================== */

void checkParent(char *name, W_Window *parent)
{
}

int checkMapped(char *name)
{
    return 0;
}

int checkMappedPref(char *name, int preferred)
{
    return preferred;
}

void findMouse(int *x, int *y)
{
    get_logical_mouse(x, y);
}

int findMouseInWin(int *x, int *y, W_Window w)
{
    struct window *win = W_Void2Window(w);
    if (!win) return 0;
    int mx, my;
    get_logical_mouse(&mx, &my);
    *x = mx - win->x;
    *y = my - win->y;
    return (*x >= 0 && *x < win->width && *y >= 0 && *y < win->height);
}

/* Paste from system clipboard - X11 had pastebuffer() */
void pastebuffer(void)
{
    char *text = SDL_GetClipboardText();
    if (!text) return;
    for (int i = 0; text[i]; i++) {
        smessage(text[i]);
    }
    SDL_free(text);
}

void W_Deinitialize(void)
{
    for (int i = 0; i < num_windows; i++) {
        if (windows[i].texture) SDL_DestroyTexture(windows[i].texture);
        if (windows[i].background) SDL_DestroyTexture(windows[i].background);
        free(windows[i].name);
    }

    for (int i = 0; i < 4; i++) {
        if (sdl_fonts[i]) TTF_CloseFont(sdl_fonts[i]);
    }
    if (tts_font) TTF_CloseFont(tts_font);

    if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
    if (sdl_window) SDL_DestroyWindow(sdl_window);

    if (socket_pipe[0] >= 0) close(socket_pipe[0]);
    if (socket_pipe[1] >= 0) close(socket_pipe[1]);

    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}
