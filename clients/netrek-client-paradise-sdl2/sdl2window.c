/* sdl2window.c - SDL2 implementation of Wlib for Paradise
 *
 * Replaces x11window.c for macOS/SDL2 builds.
 * All Paradise code calls Wlib functions; this implements them via SDL2.
 *
 * Each Wlib "window" is a virtual window backed by an SDL_Texture.
 * W_Flush() composites all mapped textures onto a single SDL_Window.
 */

#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "proto.h"
#include "images.h"
#include "sdl2window.h"

/* ========================================================================
 * Global Wlib variables
 * ======================================================================== */

static int zero = 0, one = 1, two = 2, three = 3;

W_Font W_BigFont = (W_Font)&zero, W_RegularFont = (W_Font)&one;
W_Font W_HighlightFont = (W_Font)&two, W_UnderlineFont = (W_Font)&three;
W_Color W_White = WHITE, W_Black = BLACK, W_Red = RED, W_Green = GREEN;
W_Color W_Yellow = YELLOW, W_Cyan = CYAN, W_Grey = LIGHT_GREY;
int W_Textwidth, W_Textheight;
int W_FastClear = 1;
int W_in_message = 0;
extern int forceMono;

extern W_Window baseWin;

/* ========================================================================
 * SDL2 backend state
 * ======================================================================== */

SDL_Window   *sdl_window = NULL;
SDL_Renderer *sdl_renderer = NULL;

struct window windows[MAX_WINDOWS];
int num_windows = 0;

/* Pipe for W_Socket() integration with select() */
static int socket_pipe[2] = {-1, -1};

/* Font handles: 0=big, 1=regular, 2=bold, 3=italic/underline */
TTF_Font *sdl_fonts[4] = {NULL, NULL, NULL, NULL};

/* Color table */
struct colors colortable[NCOLORS] = {
    {"white",      {255, 255, 255, 255}},
    {"black",      {0,   0,   0,   255}},
    {"red",        {255, 0,   0,   255}},
    {"green",      {0,   255, 0,   255}},
    {"yellow",     {255, 255, 0,   255}},
    {"cyan",       {0,   255, 255, 255}},
    {"#767676",    {118, 118, 118, 255}},  /* dark grey */
    {"#d8d8d8",    {216, 216, 216, 255}},  /* light grey */
};

/* Line cache */
static SDL_Point _lcache[NCOLORS][MAXCACHE * 2];
static int _lcache_index[NCOLORS];

/* Clear area cache */
static SDL_Rect _rcache[MAXCACHE];
static int _rcache_index = 0;
static struct window *_rcache_win = NULL;

/* Point cache */
static SDL_Point _pcache[NCOLORS][MAXCACHE];
static int _pcache_index[NCOLORS];

/* Event queue - ring buffer */
#define EVQ_SIZE 64
static W_Event evq_buf[EVQ_SIZE];
static int evq_head = 0;
static int evq_tail = 0;

static int evq_empty(void) { return evq_head == evq_tail; }
static int evq_full(void)  { return ((evq_head + 1) % EVQ_SIZE) == evq_tail; }

static void evq_push(W_Event *ev)
{
    if (evq_full()) return;
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

/* Continuous mouse support */
int controlkey = 0;
int altkey = 0;
int buttonDown = 0;

/* Debug screenshot on SIGUSR1 */
static volatile sig_atomic_t screenshot_requested = 0;
static int screenshot_count = 0;
static void sigusr1_handler(int sig) { (void)sig; screenshot_requested = 1; }

/* Forward declarations */
static void redrawMenu(struct window *win);
static void redrawMenuItem(struct window *win, int n);
static void changeMenuItem(struct window *win, int x, int n, W_Color color,
                           char *str, int len, W_Font font);
static void AddToScrolling(struct window *win, W_Color color, char *str, int len);
static void redrawScrolling(struct window *win);
static void renderTextOnWindow(struct window *win, int px, int py,
                               W_Color color, char *str, int len, W_Font font);
static void poll_sdl_into_queue(void);

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

int fontNum(W_Font font)
{
    if (!font) return 1;
    return *((int *)font);
}

SDL_Color getColor(W_Color color)
{
    if (color >= 0 && color < NCOLORS)
        return colortable[color].sdl_color;
    return colortable[WHITE].sdl_color;
}

void setRenderColor(W_Color color)
{
    SDL_Color c = getColor(color);
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
}

struct window *newWindow(Window window, int type)
{
    (void)window; /* X11 Window ID not used in SDL2 backend */
    if (num_windows >= MAX_WINDOWS) {
        fprintf(stderr, "sdl2window: too many windows\n");
        return NULL;
    }
    struct window *win = &windows[num_windows];
    memset(win, 0, sizeof(struct window));
    win->id = num_windows;
    win->type = type;
    win->parent_id = -1;
    win->border_color = -1;
    win->scrollbar_parent_id = -1;
    num_windows++;
    return win;
}

struct window *findWindowAt(int sx, int sy)
{
    for (int i = num_windows - 1; i >= 0; i--) {
        struct window *win = &windows[i];
        if (!win->mapped) continue;
        if (win->type == WIN_BORDER || win->type == WIN_SCROLLBAR) continue;
        if (sx >= win->x && sx < win->x + (int)win->width &&
            sy >= win->y && sy < win->y + (int)win->height) {
            return win;
        }
    }
    return NULL;
}

static SDL_Texture *createTexture(int w, int h)
{
    SDL_Texture *tex = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(sdl_renderer, tex);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_SetRenderTarget(sdl_renderer, NULL);
    }
    return tex;
}

/* ========================================================================
 * W_Initialize
 * ======================================================================== */

void
W_Initialize(char *str)
{
    int i;
    char *s;

    fprintf(stderr, "sdl2window: W_Initialize(\"%s\")\n", str ? str : "");

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

    int win_w = 1024;
    int win_h = 768;

    sdl_window = SDL_CreateWindow("Netrek Paradise",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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

    /* Create pipe for W_Socket() integration with select() */
    if (pipe(socket_pipe) < 0) {
        perror("pipe");
        exit(1);
    }
    fcntl(socket_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(socket_pipe[1], F_SETFL, O_NONBLOCK);

    /* Load fonts */
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
        for (int sz = 10; sz >= 6; sz--) {
            TTF_Font *test = TTF_OpenFont(*fp, sz);
            if (!test) break;
            int w, h;
            TTF_SizeText(test, "M", &w, &h);
            if (h <= 10) {
                sdl_fonts[1] = test;
                fprintf(stderr, "sdl2window: selected font %s size %d (%dx%d)\n",
                        *fp, sz, w, h);
                sdl_fonts[0] = TTF_OpenFont(*fp, sz * 3); /* big */
                sdl_fonts[2] = TTF_OpenFont(*fp, sz);      /* bold */
                sdl_fonts[3] = TTF_OpenFont(*fp, sz);      /* italic/underline */
                if (sdl_fonts[2]) TTF_SetFontStyle(sdl_fonts[2], TTF_STYLE_BOLD);
                if (sdl_fonts[3]) TTF_SetFontStyle(sdl_fonts[3], TTF_STYLE_UNDERLINE);
                break;
            }
            TTF_CloseFont(test);
        }
        if (sdl_fonts[1]) break;
    }

    /* Force cell dimensions - Paradise uses 6x10 */
    W_Textwidth = 6;
    W_Textheight = 10;

    /* Set up image directory */
    s = strdup(stringDefault("shipBitmapPath", SHIP_BITMAP_PATH));
    s = expandFilename(s);
    imagedir = (char*)malloc(strlen(s) + 100);
    strcpy(imagedir, s);
    free(s);
    imagedirend = imagedir + strlen(imagedir);

    /* Initialize root window */
    memset(&myroot, 0, sizeof(myroot));
    myroot.id = -1;
    myroot.type = WIN_GRAPH;
    myroot.mapped = 1;
    SDL_GetWindowSize(sdl_window, (int*)&myroot.width, (int*)&myroot.height);

    /* Clear screen */
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderPresent(sdl_renderer);

    /* Register SIGUSR1 for screenshots */
    signal(SIGUSR1, sigusr1_handler);

    fprintf(stderr, "sdl2window: initialization complete (PID %d)\n", getpid());
}

/* ========================================================================
 * Window creation
 * ======================================================================== */

static W_Window
w_MakeWindow(char *name, int x, int y, int width, int height,
             W_Window parent, char *cursname, int border, W_Color color,
             int wsort)
{
    unsigned int pwidth, pheight;
    struct window *neww;
    char *s;

    if (wsort == WIN_TEXT || wsort == WIN_SCROLL || wsort == WIN_MENU) {
        pwidth = width * W_Textwidth + WIN_EDGE * 2;
        if (wsort == WIN_MENU)
            pheight = height * (W_Textheight + MENU_PAD * 2 + MENU_BAR) - MENU_BAR;
        else
            pheight = height * W_Textheight + MENU_PAD * 2;
    } else {
        pwidth = width;
        pheight = height;
    }

    neww = newWindow(0, wsort);
    if (!neww) return NULL;

    neww->width = (wsort == WIN_TEXT || wsort == WIN_SCROLL || wsort == WIN_MENU)
                  ? width : pwidth;
    neww->height = (wsort == WIN_TEXT || wsort == WIN_SCROLL || wsort == WIN_MENU)
                   ? height : pheight;
    neww->border = border;
    neww->border_color = -1;
    neww->x = x;
    neww->y = y;
    if (name) neww->name = strdup(name);

    if (parent) {
        struct window *pw = W_Void2Window(parent);
        neww->parent_id = pw->id;
        neww->x += pw->x;
        neww->y += pw->y;
    }

    if (neww->x < 0) neww->x = 0;
    if (neww->y < 0) neww->y = 0;

    /* Adjust width/height to pixel dimensions for storage */
    neww->width = pwidth;
    neww->height = pheight;

    /* Create render target texture */
    neww->texture = createTexture(pwidth, pheight);

    /* Set up window title */
    if (name && strcmp(name, "netrek") == 0) {
        char buf[80];
        if (!title) {
            sprintf(buf, "Netrek Paradise @ %s", serverName);
            SDL_SetWindowTitle(sdl_window, buf);
        } else {
            SDL_SetWindowTitle(sdl_window, title);
        }
    }

    /* Set dynamic window sizes */
    if (name) {
        if (strcmp(name, "local") == 0) {
            winside = pwidth;
        } else if (strcmp(name, "map") == 0) {
            mapside = pwidth;
        }
    }

    /* Type-specific initialization */
    switch (wsort) {
    case WIN_MENU: {
        struct menuItem *items = (struct menuItem *)malloc(
            height * sizeof(struct menuItem));
        for (int i = 0; i < height; i++) {
            items[i].string = NULL;
            items[i].color = W_White;
            items[i].font = W_RegularFont;
            items[i].x = 0;
        }
        neww->data = (char *)items;
        /* Store logical height for menu item count */
        neww->height = pheight;
        break;
    }
    case WIN_SCROLL: {
        int maxlines = checkScrollSaveLines(name);
        if (maxlines < (int)height) maxlines = height;
        neww->scrolldata = (struct scrolldata *)
            malloc(sizeof(struct scrolldata) * maxlines);
        if (!neww->scrolldata) {
            perror("Error allocating scrollback storage");
            exit(errno);
        }
        memset(neww->scrolldata, 0, sizeof(struct scrolldata) * maxlines);
        neww->maxlines = maxlines;
        neww->top = neww->bottom = neww->currenttop = 0;
        break;
    }
    default:
        neww->data = 0;
        break;
    }

    /* Check if window should be auto-mapped */
    if (parent && wsort != WIN_BORDER && wsort != WIN_SCROLLBAR) {
        if (checkMapped(name))
            W_MapWindow(W_Window2Void(neww));
    }

    /* Check if buffering should be enabled */
    if (wsort != WIN_BORDER && wsort != WIN_SCROLLBAR) {
        if (checkBuffered(name)) {
            W_Buffer(W_Window2Void(neww), 1);
        }
    }

    fprintf(stderr, "sdl2window: w_MakeWindow(\"%s\", %d,%d, %ux%u) type=%d\n",
            name ? name : "", neww->x, neww->y, pwidth, pheight, wsort);

    return W_Window2Void(neww);
}

W_Window
W_MakeWindow(char *name, int x, int y, int width, int height,
             W_Window parent, char *cursname, int border, W_Color color)
{
    return w_MakeWindow(name, x, y, width, height, parent,
                        cursname, border, color, WIN_GRAPH);
}

W_Window
W_MakeTextWindow(char *name, int x, int y, int width, int height,
                 W_Window parent, char *cursname, int border)
{
    return w_MakeWindow(name, x, y, width, height,
                        parent, cursname, border, W_White, WIN_TEXT);
}

W_Window
W_MakeScrollingWindow(char *name, int x, int y, int width, int height,
                      W_Window parent, char *cursname, int border)
{
    return w_MakeWindow(name, x, y, width, height, parent, cursname,
                        border, W_White, WIN_SCROLL);
}

W_Window
W_MakeMenu(char *name, int x, int y, int width, int height,
           W_Window parent, int border)
{
    return w_MakeWindow(name, x, y, width, height, parent,
                        "left_ptr", border, W_White, WIN_MENU);
}

/* ========================================================================
 * Double buffering
 * ======================================================================== */

void
W_ClearBuffer(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win->isbuffered || !win->buffer) return;

    SDL_SetRenderTarget(sdl_renderer, win->buffer);
    setRenderColor(backColor);
    SDL_RenderClear(sdl_renderer);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

int
W_IsBuffered(W_Window window)
{
    return W_Void2Window(window)->isbuffered;
}

void
W_Buffer(W_Window window, int on)
{
    struct window *win = W_Void2Window(window);

    if (on) {
        win->isbuffered = 1;
        if (!win->buffer) {
            win->buffer = createTexture(win->width, win->height);
        }
        W_ClearBuffer(window);
    } else {
        win->isbuffered = 0;
    }
}

void
W_DisplayBuffer(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win->isbuffered || !win->buffer || !win->texture) return;

    /* Copy buffer to main texture */
    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_RenderCopy(sdl_renderer, win->buffer, NULL, NULL);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ========================================================================
 * Window mapping
 * ======================================================================== */

void
W_MapWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || win->mapped) return;
    win->mapped = 1;

    /* Generate expose event */
    W_Event ev;
    ev.type = W_EV_EXPOSE;
    ev.Window = window;
    ev.key = 0;
    ev.x = 0;
    ev.y = 0;
    evq_push(&ev);
}

void
W_UnmapWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    win->mapped = 0;
}

int
W_IsMapped(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return 0;
    return win->mapped;
}

/* ========================================================================
 * Window properties
 * ======================================================================== */

void
W_RenameWindow(W_Window window, char *str)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    if (win->name) free(win->name);
    win->name = str ? strdup(str) : NULL;
}

int
W_WindowWidth(W_Window window)
{
    return W_Void2Window(window)->width;
}

int
W_WindowHeight(W_Window window)
{
    return W_Void2Window(window)->height;
}

void
W_ChangeBorder(W_Window window, int color)
{
    struct window *win = W_Void2Window(window);
    if (win) win->border_color = color;
}

void
W_SetSensitive(W_Window window, int v)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    win->insensitive = !v;
    if (win->type == WIN_SCROLL)
        redrawScrolling(win);
}

/* ========================================================================
 * Clearing
 * ======================================================================== */

void
W_ClearWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    if (win->background) {
        SDL_RenderCopy(sdl_renderer, win->background, NULL, NULL);
    } else {
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_ClearArea(W_Window window, int x, int y,
            unsigned int width, unsigned int height)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    int px, py;
    unsigned int pw, ph;
    if (win->type == WIN_GRAPH) {
        px = x; py = y; pw = width; ph = height;
    } else {
        px = WIN_EDGE + x * W_Textwidth;
        py = MENU_PAD + y * W_Textheight;
        pw = width * W_Textwidth;
        ph = W_Textheight * height;
    }

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(backColor);
    SDL_Rect r = {px, py, (int)pw, (int)ph};
    SDL_RenderFillRect(sdl_renderer, &r);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_CacheClearArea(W_Window window, int x, int y, int width, int height)
{
    struct window *win = W_Void2Window(window);
    if (_rcache_win != win) {
        if (_rcache_index > 0) W_FlushClearAreaCache((W_Window)_rcache_win);
        _rcache_win = win;
    }
    if (_rcache_index >= MAXCACHE) {
        W_FlushClearAreaCache((W_Window)win);
        _rcache_win = win;
    }
    SDL_Rect *r = &_rcache[_rcache_index++];
    r->x = x;
    r->y = y;
    r->w = width;
    r->h = height;
}

void
W_FlushClearAreaCache(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || _rcache_index == 0) {
        _rcache_index = 0;
        return;
    }
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) { _rcache_index = 0; return; }

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(backColor);
    SDL_RenderFillRects(sdl_renderer, _rcache, _rcache_index);
    SDL_SetRenderTarget(sdl_renderer, NULL);
    _rcache_index = 0;
}

void
W_FillArea(W_Window window, int x, int y,
           unsigned int width, unsigned int height, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    int px, py;
    unsigned int pw, ph;
    if (win->type == WIN_GRAPH) {
        px = x; py = y; pw = width; ph = height;
    } else {
        px = WIN_EDGE + x * W_Textwidth;
        py = MENU_PAD + y * W_Textheight;
        pw = width * W_Textwidth;
        ph = height * W_Textheight;
    }

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);
    SDL_Rect r = {px, py, (int)pw, (int)ph};
    SDL_RenderFillRect(sdl_renderer, &r);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ========================================================================
 * Text rendering
 * ======================================================================== */

static void
renderTextOnWindow(struct window *win, int px, int py,
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

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) { SDL_DestroyTexture(tex); return; }

    SDL_SetRenderTarget(sdl_renderer, target);
    SDL_Rect dst = {px, py, tw, th};
    SDL_RenderCopy(sdl_renderer, tex, NULL, &dst);
    SDL_SetRenderTarget(sdl_renderer, NULL);
    SDL_DestroyTexture(tex);
}

void
W_WriteText(W_Window window, int x, int y, W_Color color, char *str,
            int len, W_Font font)
{
    struct window *win = W_Void2Window(window);
    if (!win || !str || len <= 0) return;

    if (!font) font = W_RegularFont;

    switch (win->type) {
    case WIN_GRAPH: {
        int addr = 0; /* baseline offset approximated by font metrics */
        renderTextOnWindow(win, x, y, color, str, len, font);
        break;
    }
    case WIN_SCROLL:
        if (win->currenttop == win->top) {
            /* Redraw: clear bottom line and draw text */
            int vis_h = (win->height - MENU_PAD * 2) / W_Textheight;
            int py;
            if (y < 0) {
                /* Scroll up mode */
                py = MENU_PAD;
            } else {
                py = MENU_PAD + W_Textheight * (vis_h - 1);
            }
            renderTextOnWindow(win, WIN_EDGE, py, color, str, len, W_RegularFont);
        }
        AddToScrolling(win, color, str, len);
        break;
    case WIN_MENU:
        changeMenuItem(win, x, y, color, str, len, font);
        break;
    default: /* WIN_TEXT */
        renderTextOnWindow(win, x * W_Textwidth + WIN_EDGE,
                          MENU_PAD + y * W_Textheight,
                          color, str, len, font);
        break;
    }
}

void
W_MaskText(W_Window window, int x, int y, W_Color color, char *str,
           int len, W_Font font)
{
    struct window *win = W_Void2Window(window);
    if (!win || !str || len <= 0) return;

    int fi = fontNum(font);
    if (fi < 0 || fi > 3) fi = 1;
    TTF_Font *f = sdl_fonts[fi];
    if (!f) return;

    char buf[MAX_TEXT_WIDTH + 1];
    if (len > MAX_TEXT_WIDTH) len = MAX_TEXT_WIDTH;
    memcpy(buf, str, len);
    buf[len] = '\0';

    /* Render without background (transparent) */
    SDL_Color fg = getColor(color);
    SDL_Surface *surface = TTF_RenderText_Blended(f, buf, fg);
    if (!surface) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) { SDL_DestroyTexture(tex); return; }

    SDL_SetRenderTarget(sdl_renderer, target);
    SDL_Rect dst = {x, y, tw, th};
    SDL_RenderCopy(sdl_renderer, tex, NULL, &dst);
    SDL_SetRenderTarget(sdl_renderer, NULL);
    SDL_DestroyTexture(tex);
}

void
W_DirectMaskText(W_Window window, int x, int y, W_Color color, char *str,
                 int len, W_Font font)
{
    /* In SDL2, always draw to texture - no separate window vs drawable */
    struct window *win = W_Void2Window(window);
    if (!win || !str || len <= 0) return;

    int fi = fontNum(font);
    if (fi < 0 || fi > 3) fi = 1;
    TTF_Font *f = sdl_fonts[fi];
    if (!f) return;

    char buf[MAX_TEXT_WIDTH + 1];
    if (len > MAX_TEXT_WIDTH) len = MAX_TEXT_WIDTH;
    memcpy(buf, str, len);
    buf[len] = '\0';

    SDL_Color fg = getColor(color);
    SDL_Surface *surface = TTF_RenderText_Blended(f, buf, fg);
    if (!surface) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    if (!tex) return;

    int tw, th;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);

    /* Always draw to the main texture (bypasses buffer) */
    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_Rect dst = {x, y, tw, th};
    SDL_RenderCopy(sdl_renderer, tex, NULL, &dst);
    SDL_SetRenderTarget(sdl_renderer, NULL);
    SDL_DestroyTexture(tex);
}

/* ========================================================================
 * Line drawing
 * ======================================================================== */

void
W_MakeLine(W_Window window, int X0, int Y0, int X1, int Y1, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);
    SDL_RenderDrawLine(sdl_renderer, X0, Y0, X1, Y1);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_CacheLine(W_Window window, int X0, int Y0, int X1, int Y1, int color)
{
    if (color < 0 || color >= NCOLORS) return;
    int idx = _lcache_index[color];
    if (idx >= MAXCACHE) {
        struct window *win = W_Void2Window(window);
        if (win) {
            SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
            if (target) {
                SDL_SetRenderTarget(sdl_renderer, target);
                setRenderColor(color);
                for (int i = 0; i < idx; i++) {
                    SDL_RenderDrawLine(sdl_renderer,
                        _lcache[color][i*2].x, _lcache[color][i*2].y,
                        _lcache[color][i*2+1].x, _lcache[color][i*2+1].y);
                }
                SDL_SetRenderTarget(sdl_renderer, NULL);
            }
        }
        _lcache_index[color] = 0;
        idx = 0;
    }
    _lcache[color][idx*2].x = X0;
    _lcache[color][idx*2].y = Y0;
    _lcache[color][idx*2+1].x = X1;
    _lcache[color][idx*2+1].y = Y1;
    _lcache_index[color] = idx + 1;
}

void
W_FlushLineCaches(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    for (int c = 0; c < NCOLORS; c++) {
        int idx = _lcache_index[c];
        if (idx > 0) {
            setRenderColor(c);
            for (int i = 0; i < idx; i++) {
                SDL_RenderDrawLine(sdl_renderer,
                    _lcache[c][i*2].x, _lcache[c][i*2].y,
                    _lcache[c][i*2+1].x, _lcache[c][i*2+1].y);
            }
            _lcache_index[c] = 0;
        }
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_MakeTractLine(W_Window window, int X0, int Y0, int X1, int Y1, W_Color color)
{
    /* Dashed line */
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);

    double dx = X1 - X0, dy = Y1 - Y0;
    double len = sqrt(dx*dx + dy*dy);
    if (len < 1.0) { SDL_SetRenderTarget(sdl_renderer, NULL); return; }

    double ux = dx/len, uy = dy/len;
    int dash_on = 1, dash_len = 4;
    double t = 0;
    while (t < len) {
        double end = t + dash_len;
        if (end > len) end = len;
        if (dash_on) {
            SDL_RenderDrawLine(sdl_renderer,
                (int)(X0 + ux*t), (int)(Y0 + uy*t),
                (int)(X0 + ux*end), (int)(Y0 + uy*end));
        }
        t = end;
        dash_on = !dash_on;
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ========================================================================
 * Point drawing
 * ======================================================================== */

void
W_DrawPoint(W_Window window, int x, int y, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);
    SDL_RenderDrawPoint(sdl_renderer, x, y);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_CachePoint(W_Window window, int x, int y, int color)
{
    if (color < 0 || color >= NCOLORS) return;
    if (_pcache_index[color] >= MAXCACHE) {
        W_FlushPointCaches(window);
    }
    int idx = _pcache_index[color]++;
    _pcache[color][idx].x = x;
    _pcache[color][idx].y = y;
}

void
W_FlushPointCaches(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    for (int c = 0; c < NCOLORS; c++) {
        if (_pcache_index[c] > 0) {
            setRenderColor(c);
            SDL_RenderDrawPoints(sdl_renderer, _pcache[c], _pcache_index[c]);
            _pcache_index[c] = 0;
        }
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ========================================================================
 * Shape drawing
 * ======================================================================== */

void
W_WriteTriangle(W_Window window, int x, int y, int s, int t, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);

    /* Filled triangle using horizontal line scan */
    int x1, y1, x2, y2, x3, y3;
    if (t == 0) {
        x1 = x; y1 = y;
        x2 = x + s; y2 = y - s;
        x3 = x - s; y3 = y - s;
    } else {
        x1 = x; y1 = y;
        x2 = x + s; y2 = y + s;
        x3 = x - s; y3 = y + s;
    }

    /* Simple fill: draw outline + fill with horizontal lines */
    SDL_Point pts[4] = {{x1,y1},{x2,y2},{x3,y3},{x1,y1}};
    SDL_RenderDrawLines(sdl_renderer, pts, 4);

    /* Fill interior */
    int miny = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
    int maxy = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);
    for (int row = miny; row <= maxy; row++) {
        int left = x1, right = x1;
        /* Find intersection with edges */
        int pts_x[3] = {x1, x2, x3};
        int pts_y[3] = {y1, y2, y3};
        int first = 1;
        for (int e = 0; e < 3; e++) {
            int ea = e, eb = (e+1)%3;
            int ya = pts_y[ea], yb = pts_y[eb];
            int xa = pts_x[ea], xb = pts_x[eb];
            if ((ya <= row && yb >= row) || (yb <= row && ya >= row)) {
                int ix;
                if (ya == yb) {
                    if (xa < xb) { ix = xa; } else { ix = xb; }
                    if (first || ix < left) left = ix;
                    ix = xa > xb ? xa : xb;
                    if (first || ix > right) right = ix;
                    first = 0;
                } else {
                    ix = xa + (row - ya) * (xb - xa) / (yb - ya);
                    if (first) { left = right = ix; first = 0; }
                    else { if (ix < left) left = ix; if (ix > right) right = ix; }
                }
            }
        }
        if (!first) SDL_RenderDrawLine(sdl_renderer, left, row, right, row);
    }

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_WriteAnyTriangle(W_Window window, int X1, int Y1, int X2, int Y2,
                   int X3, int Y3, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);

    /* Fill triangle with horizontal scan lines */
    int pts_x[3] = {X1, X2, X3};
    int pts_y[3] = {Y1, Y2, Y3};
    int miny = pts_y[0], maxy = pts_y[0];
    for (int i = 1; i < 3; i++) {
        if (pts_y[i] < miny) miny = pts_y[i];
        if (pts_y[i] > maxy) maxy = pts_y[i];
    }

    for (int row = miny; row <= maxy; row++) {
        int left = 10000, right = -10000;
        for (int e = 0; e < 3; e++) {
            int ea = e, eb = (e+1)%3;
            int ya = pts_y[ea], yb = pts_y[eb];
            int xa = pts_x[ea], xb = pts_x[eb];
            if ((ya <= row && yb >= row) || (yb <= row && ya >= row)) {
                int ix;
                if (ya == yb) {
                    if (xa < left) left = xa;
                    if (xb < left) left = xb;
                    if (xa > right) right = xa;
                    if (xb > right) right = xb;
                } else {
                    ix = xa + (row - ya) * (xb - xa) / (yb - ya);
                    if (ix < left) left = ix;
                    if (ix > right) right = ix;
                }
            }
        }
        if (left <= right) SDL_RenderDrawLine(sdl_renderer, left, row, right, row);
    }

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_DrawSectorHighlight(W_Window window, int x, int y, int width, int h,
                      W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);
    SDL_Rect r = {x + 2, y + 2, width - 4, h - 4};
    /* Draw dashed rectangle - approximate with dotted */
    SDL_RenderDrawRect(sdl_renderer, &r);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* Midpoint circle algorithm - used for shields and arcs */
static void
drawCircle(int cx, int cy, int radius)
{
    int x = 0, y = radius;
    int d = 1 - radius;
    while (x <= y) {
        SDL_RenderDrawPoint(sdl_renderer, cx + x, cy + y);
        SDL_RenderDrawPoint(sdl_renderer, cx - x, cy + y);
        SDL_RenderDrawPoint(sdl_renderer, cx + x, cy - y);
        SDL_RenderDrawPoint(sdl_renderer, cx - x, cy - y);
        SDL_RenderDrawPoint(sdl_renderer, cx + y, cy + x);
        SDL_RenderDrawPoint(sdl_renderer, cx - y, cy + x);
        SDL_RenderDrawPoint(sdl_renderer, cx + y, cy - x);
        SDL_RenderDrawPoint(sdl_renderer, cx - cy, cy - x);
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

void
W_DrawShield(W_Window window, int centerx, int centery,
             unsigned int diameter, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    int radius = (int)(diameter - 1) / 2;
    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);

    /* Draw circle */
    int cx = centerx, cy = centery;
    int x = 0, y = radius;
    int d = 1 - radius;
    while (x <= y) {
        SDL_RenderDrawPoint(sdl_renderer, cx + x, cy + y);
        SDL_RenderDrawPoint(sdl_renderer, cx - x, cy + y);
        SDL_RenderDrawPoint(sdl_renderer, cx + x, cy - y);
        SDL_RenderDrawPoint(sdl_renderer, cx - x, cy - y);
        SDL_RenderDrawPoint(sdl_renderer, cx + y, cy + x);
        SDL_RenderDrawPoint(sdl_renderer, cx - y, cy + x);
        SDL_RenderDrawPoint(sdl_renderer, cx + y, cy - x);
        SDL_RenderDrawPoint(sdl_renderer, cx - y, cy - x);
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_WriteArc(int filled, W_Window window, int x, int y, int width, int height,
           int angle1, int angle2, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    setRenderColor(color);

    int cx = x, cy = y;
    int rx = width / 2, ry = height / 2;

    /* Convert angles from degrees to radians */
    double a1 = angle1 * M_PI / 180.0;
    double a2 = (angle1 + angle2) * M_PI / 180.0;
    double step = M_PI / 180.0;

    if (filled) {
        /* Fill arc with lines from center to perimeter */
        for (double a = a1; a < a2; a += step) {
            int px = cx + (int)(rx * cos(a));
            int py = cy - (int)(ry * sin(a));
            SDL_RenderDrawLine(sdl_renderer, cx, cy, px, py);
        }
    } else {
        /* Draw arc outline */
        int prev_px = cx + (int)(rx * cos(a1));
        int prev_py = cy - (int)(ry * sin(a1));
        for (double a = a1 + step; a <= a2; a += step) {
            int px = cx + (int)(rx * cos(a));
            int py = cy - (int)(ry * sin(a));
            SDL_RenderDrawLine(sdl_renderer, prev_px, prev_py, px, py);
            prev_px = px;
            prev_py = py;
        }
    }

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ========================================================================
 * Image system (W_Image) - replaces XPM/XBM loading
 * ======================================================================== */

/* Convert 1-bit XBM data to an RGBA SDL_Texture */
static SDL_Texture *
xbm_to_texture(unsigned char *bits, unsigned int width, unsigned int height)
{
    if (!bits || width == 0 || height == 0) return NULL;

    int stride = (width + 7) / 8;
    Uint32 *pixels = calloc(width * height, sizeof(Uint32));
    if (!pixels) return NULL;

    for (unsigned int row = 0; row < height; row++) {
        for (unsigned int col = 0; col < width; col++) {
            int byte_idx = row * stride + col / 8;
            int bit = (bits[byte_idx] >> (col % 8)) & 1;
            if (bit) {
                pixels[row * width + col] = 0xFFFFFFFF; /* white, full alpha */
            }
        }
    }

    SDL_Texture *tex = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
        width, height);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, pixels, width * sizeof(Uint32));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    free(pixels);
    return tex;
}

int
checkloaded(W_Image *image)
{
    if (!image->loaded) {
        if (W_LoadImage(image)) {
            strcpy(imagedirend, image->filename);
            fprintf(stderr, "Can't load image for %s\n", imagedir);
            return 1;
        }
    }
    if (image->bad) return 1;
    return 0;
}

W_Image *
W_BitmapToImage(unsigned int width, unsigned int height, char *bits)
{
    W_Image *image = (W_Image *)malloc(sizeof(W_Image));
    memset(image, 0, sizeof(W_Image));

    image->width = width;
    image->height = height;
    image->frames = 1;
    if (height % width == 0) {
        image->frames = height / width;
    }
    if (image->frames > 1) {
        image->height = height / image->frames;
    }

    image->pixmap = xbm_to_texture((unsigned char *)bits, width, height);
    image->clipmask = image->pixmap;
    image->xpm = 0;
    image->loaded = 1;
    image->bad = 0;
    image->filename = "";
    return image;
}

void
W_FreeImage(W_Image *image)
{
    if (image->loaded && image->pixmap) {
        SDL_DestroyTexture((SDL_Texture *)image->pixmap);
        if (image->clipmask != image->pixmap && image->clipmask) {
            SDL_DestroyTexture((SDL_Texture *)image->clipmask);
        }
        image->pixmap = NULL;
        image->clipmask = NULL;
    }
    if (image > getImage(I_LAST) || image < getImage(I_FIRST)) {
        free(image);
    }
    image->loaded = 0;
    image->xpm = 0;
    image->frames = 0;
}

int
W_LoadExternalImage(W_Image *image)
{
    char buf[256], *t, *pi;
    SDL_Surface *surface = NULL;

    /* Try XPM first, then XBM, then PNG */
    pi = imagedir;
    do {
        t = strchr(pi, ':');
        int plen = t ? (int)(t - pi) : (int)strlen(pi);
        strncpy(buf, pi, plen);

        /* Try .xpm via SDL2_image */
        sprintf(buf + plen, "%s.xpm", image->filename);
        surface = IMG_Load(buf);
        if (surface) break;

        /* Try .png */
        sprintf(buf + plen, "%s.png", image->filename);
        surface = IMG_Load(buf);
        if (surface) break;

        pi = t ? t + 1 : NULL;
    } while (pi);

    if (surface) {
        /* Successfully loaded colored image */
        fprintf(stderr, "W_LoadExternalImage: loaded %s (%dx%d)\n",
                image->filename, surface->w, surface->h);
        SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surface);
        image->xpm = 1;
        image->width = surface->w;
        if (image->frames == 0) {
            if (surface->h % surface->w == 0)
                image->frames = surface->h / surface->w;
            else
                image->frames = 1;
        }
        image->height = surface->h / image->frames;
        SDL_FreeSurface(surface);

        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            image->pixmap = tex;
            image->clipmask = NULL;
            image->loaded = 1;
            return 0;
        }
    }

    /* Try XBM file */
    pi = imagedir;
    do {
        t = strchr(pi, ':');
        int plen = t ? (int)(t - pi) : (int)strlen(pi);
        strncpy(buf, pi, plen);
        sprintf(buf + plen, "%s.xbm", image->filename);

        /* Manual XBM loading */
        FILE *fp = fopen(buf, "r");
        if (fp) {
            /* Parse XBM header to get width/height/data */
            /* XBM files are C source - complex to parse here */
            /* For now, skip and try compiled-in data */
            fclose(fp);
        }
        pi = t ? t + 1 : NULL;
    } while (pi);

    /* Try compiled-in data */
    if (image->compiled_in)
        return W_LoadInternalImage(image);

    /* Try alternate */
    if (image->alternate == -1) return 1;

    image->bad = 1;
    W_Image *alt = getImage(image->alternate);
    memcpy(image, alt, sizeof(W_Image));
    return 0;
}

int
W_LoadInternalImage(W_Image *image)
{
    W_Image *imagetmp;

    if (image->xbmdata) {
        /* Convert compiled-in XBM data to texture */
        unsigned int total_h = image->height;
        if (image->frames > 1) total_h = image->height * image->frames;

        image->pixmap = xbm_to_texture(image->xbmdata, image->width, total_h);
        image->clipmask = image->pixmap;
        image->xpm = 0;
        image->loaded = 1;

        if (image->frames == 0) {
            if (total_h % image->width == 0)
                image->frames = total_h / image->width;
            else
                image->frames = 1;
        }
        if (image->frames > 1)
            image->height = total_h / image->frames;

        return 0;
    }

    if (image->xpmdata) {
        /* Try to load XPM data via SDL2_image */
        /* XPM data is an array of strings - SDL2_image can read it via RWops */
        /* For now, fall through to alternate */
    }

    /* No image data - try alternate */
    if (image->alternate == -1) {
        fprintf(stderr, "Error: bad image data for %s.\n", image->filename);
        return 1;
    }
    image->bad = 1;
    imagetmp = getImage(image->alternate);
    memcpy(image, imagetmp, sizeof(W_Image));
    return 0;
}

int
W_LoadImage(W_Image *image)
{
    if (image->loaded) return 0;
    if (image->bad) return 1;

    if (useExternalImages && getImageNum(image) != I_DEFAULT) {
        int ret = W_LoadExternalImage(image);
        if (ret) ret = W_LoadInternalImage(image);
        return ret;
    } else if (image->compiled_in) {
        return W_LoadInternalImage(image);
    } else {
        return W_LoadExternalImage(image);
    }
}

/* Draw an image (frame-based sprite system) */
void
W_DrawImage(W_Window window, int x, int y, int frame, W_Image *image,
            W_Color color)
{
    int height, width;
    struct window *win = W_Void2Window(window);
    if (!win) return;

    if (checkloaded(image)) return;

    SDL_Texture *tex = (SDL_Texture *)image->pixmap;
    if (!tex) return;

    if (frame < 0) {
        height = image->height * image->frames;
        frame = 0;
    } else {
        height = image->height;
        frame = frame % image->frames;
    }
    width = image->width;

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);

    SDL_Rect src = {0, (int)(height * frame), width, height};
    /* Scale up tiny images (torps etc.) so they're visible */
    int dw = width, dh = height;
    int dx = x, dy = y;
    if (width <= 10 && height <= 10) {
        int scale = (width <= 5) ? 3 : 2;
        dw = width * scale;
        dh = height * scale;
        dx = x - (dw - width) / 2;
        dy = y - (dh - height) / 2;
    }
    SDL_Rect dst = {dx, dy, dw, dh};

    /* Debug: trace torp-related image draws (only when torps exist) */
    static int torp_draw_logged = 0;
    if (me && me->p_ntorp > 0 && torp_draw_logged < 20) {
        fprintf(stderr, "W_DrawImage(torps_active): %dx%d xpm=%d "
                "at(%d,%d) win=%d '%s'\n",
                width, height, image->xpm, x, y, win->id,
                image->filename ? image->filename : "?");
        torp_draw_logged++;
    }

    if (image->xpm) {
        /* Color pixmap - draw directly */
        SDL_RenderCopy(sdl_renderer, tex, &src, &dst);
    } else {
        /* Monochrome bitmap - colorize with color mod */
        SDL_Color c = getColor(color);
        SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
        SDL_RenderCopy(sdl_renderer, tex, &src, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_DrawImageNoClip(W_Window window, int x, int y, int frame, W_Image *image,
                  W_Color color)
{
    /* In SDL2, same as W_DrawImage - no clip mask handling needed */
    W_DrawImage(window, x, y, frame, image, color);
}

void
W_DrawImageOr(W_Window window, int x, int y, int frame, W_Image *image,
              W_Color color)
{
    /* OR mode - use additive blending */
    struct window *win = W_Void2Window(window);
    if (!win || checkloaded(image)) return;

    SDL_Texture *tex = (SDL_Texture *)image->pixmap;
    if (!tex) return;

    frame = frame % image->frames;

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    SDL_SetRenderTarget(sdl_renderer, target);

    SDL_Rect src = {0, (int)(image->height * frame), (int)image->width, (int)image->height};
    SDL_Rect dst = {x, y, (int)image->width, (int)image->height};

    if (!image->xpm) {
        SDL_Color c = getColor(color);
        SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    }
    SDL_RenderCopy(sdl_renderer, tex, &src, &dst);

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    if (!image->xpm)
        SDL_SetTextureColorMod(tex, 255, 255, 255);

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_DrawImageBar(W_Window win, int x, int y, int len, W_Image *image)
{
    struct window *w = W_Void2Window(win);
    if (!w || len == 0 || checkloaded(image)) return;

    SDL_Texture *tex = (SDL_Texture *)image->pixmap;
    if (!tex) return;

    SDL_Texture *target = w->isbuffered ? w->buffer : w->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    SDL_Rect src = {0, 0, len + 1, (int)image->height};
    SDL_Rect dst = {x, y, len + 1, (int)image->height};
    SDL_RenderCopy(sdl_renderer, tex, &src, &dst);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void
W_OverlayImage(W_Window window, int x, int y, int frame, W_Image *image,
               int overframe, W_Image *overimage, W_Color color)
{
    if (checkloaded(image) || checkloaded(overimage)) return;

    int width = image->width > overimage->width ? image->width : overimage->width;
    int height = image->height > overimage->height ? image->height : overimage->height;

    /* Create temporary work area texture */
    SDL_Texture *workarea = createTexture(width, height);
    if (!workarea) return;

    /* Draw first image */
    struct window fakewin;
    memset(&fakewin, 0, sizeof(fakewin));
    fakewin.texture = workarea;

    frame = frame % image->frames;
    overframe = overframe % overimage->frames;

    W_DrawImageNoClip(W_Window2Void(&fakewin),
                      (width - (int)image->width) / 2,
                      (height - (int)image->height) / 2,
                      frame, image, color);

    W_DrawImage(W_Window2Void(&fakewin),
                (width - (int)overimage->width) / 2,
                (height - (int)overimage->height) / 2,
                overframe, overimage, color);

    /* Copy work area to destination */
    struct window *win = W_Void2Window(window);
    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (target) {
        SDL_SetRenderTarget(sdl_renderer, target);
        SDL_Rect src_r = {0, 0, width, height};
        SDL_Rect dst_r = {x - (width - (int)image->width) / 2,
                          y - (height - (int)image->height) / 2,
                          width, height};
        SDL_RenderCopy(sdl_renderer, workarea, &src_r, &dst_r);
        SDL_SetRenderTarget(sdl_renderer, NULL);
    }

    SDL_DestroyTexture(workarea);
}

W_Image *
W_CreateCombinedImage(W_Image **imagelist, W_Color color)
{
    int width = 1, height = 1, frames = 1;
    W_Image **list;

    for (list = imagelist; *list; list++) {
        if (!checkloaded(*list)) {
            if ((int)(*list)->width > width) width = (*list)->width;
            if ((int)(*list)->height > height) height = (*list)->height;
            if ((int)(*list)->frames > frames) frames = (*list)->frames;
        }
    }

    int centerx = width / 2;
    int centery = height / 2;

    W_Image *image = (W_Image *)malloc(sizeof(W_Image));
    memset(image, 0, sizeof(W_Image));
    image->loaded = 1;
    image->xpm = 1;
    image->bad = 0;
    image->width = width;
    image->height = height;
    image->frames = frames;
    image->filename = "combined image";

    /* Create texture for combined image */
    SDL_Texture *combined = createTexture(width, height * frames);
    image->pixmap = combined;
    image->clipmask = NULL;

    if (!combined) return image;

    struct window fakewin;
    memset(&fakewin, 0, sizeof(fakewin));
    fakewin.texture = combined;

    for (list = imagelist; *list; list++) {
        for (int i = 0; i < frames; i++) {
            W_DrawImage(W_Window2Void(&fakewin),
                       centerx - (int)(*list)->width / 2,
                       i * height + centery - (int)(*list)->height / 2,
                       i, *list, color);
        }
    }

    return image;
}

/* ========================================================================
 * Window tiling
 * ======================================================================== */

void
W_TileWindow(W_Window window, W_Image *image)
{
    struct window *win = W_Void2Window(window);
    if (!win || checkloaded(image)) return;

    SDL_Texture *tex = (SDL_Texture *)image->pixmap;
    if (!tex) return;

    if (win->background) SDL_DestroyTexture(win->background);
    win->background = createTexture(win->width, win->height);
    if (!win->background) return;

    SDL_SetRenderTarget(sdl_renderer, win->background);
    for (int ty = 0; ty < (int)win->height; ty += (int)image->height) {
        for (int tx = 0; tx < (int)win->width; tx += (int)image->width) {
            SDL_Rect dst = {tx, ty, (int)image->width, (int)image->height};
            if (!image->xpm) {
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            }
            SDL_Rect src = {0, 0, (int)image->width, (int)image->height};
            SDL_RenderCopy(sdl_renderer, tex, &src, &dst);
        }
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);

    W_ClearWindow(window);
}

void
W_UnTileWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    if (win->background) {
        SDL_DestroyTexture(win->background);
        win->background = NULL;
    }
    W_ClearWindow(window);
}

/* ========================================================================
 * Scrolling window support
 * ======================================================================== */

static void
AddToScrolling(struct window *win, W_Color color, char *str, int len)
{
    int cur = win->top % win->maxlines;

    if (win->top - win->bottom >= win->maxlines) {
        free(win->scrolldata[cur].string);
        win->bottom++;
    }

    char *newstring = (char *)malloc((unsigned)(len + 1));
    strncpy(newstring, str, (unsigned)len);
    newstring[len] = 0;
    win->scrolldata[cur].string = newstring;
    win->scrolldata[cur].color = color;
    win->scrolldata[cur].len = len;

    if (win->currenttop == win->top) win->currenttop++;
    win->top++;

    W_UpdateScrollBar(W_Window2Void(win));
}

void
W_UpdateScrollBar(W_Window window)
{
    /* Scrollbar is visual-only; simplified for SDL2 */
    /* Could draw a scrollbar indicator but not critical */
}

static void
redrawScrolling(struct window *win)
{
    if (win->top == win->bottom) return;

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    SDL_SetRenderTarget(sdl_renderer, target);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_SetRenderTarget(sdl_renderer, NULL);

    int vis_h = (win->height - MENU_PAD * 2) / W_Textheight;
    for (int i = win->currenttop - vis_h, y = 0; i < win->currenttop; i++, y++) {
        if (i >= win->bottom) {
            int loc = i % win->maxlines;
            renderTextOnWindow(win, WIN_EDGE,
                             MENU_PAD + y * W_Textheight,
                             win->scrolldata[loc].color,
                             win->scrolldata[loc].string,
                             win->scrolldata[loc].len,
                             W_RegularFont);
        }
    }
}

/* ========================================================================
 * Menu support
 * ======================================================================== */

static void
redrawMenuItem(struct window *win, int n)
{
    struct menuItem *items = (struct menuItem *)win->data;
    if (!items) return;

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;

    /* Clear the item area */
    SDL_SetRenderTarget(sdl_renderer, target);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_Rect clr = {WIN_EDGE, n * item_h + MENU_PAD,
                    (int)win->width - WIN_EDGE * 2, W_Textheight};
    SDL_RenderFillRect(sdl_renderer, &clr);
    SDL_SetRenderTarget(sdl_renderer, NULL);

    if (items[n].string) {
        renderTextOnWindow(win, WIN_EDGE + items[n].x * W_Textwidth,
                          n * item_h + MENU_PAD,
                          items[n].color, items[n].string,
                          (int)strlen(items[n].string), items[n].font);
    }
}

static void
redrawMenu(struct window *win)
{
    struct menuItem *items = (struct menuItem *)win->data;
    if (!items) return;

    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
    int num_items = (win->height + MENU_BAR) / item_h;

    SDL_Texture *target = win->isbuffered ? win->buffer : win->texture;
    if (!target) return;

    /* Draw separator bars */
    SDL_SetRenderTarget(sdl_renderer, target);
    SDL_Color grey = getColor(LIGHT_GREY);
    SDL_SetRenderDrawColor(sdl_renderer, grey.r, grey.g, grey.b, 255);

    for (int count = 1; count < num_items; count++) {
        int sep_y = count * item_h - MENU_BAR;
        SDL_Rect bar = {0, sep_y, (int)win->width, MENU_BAR};
        SDL_RenderFillRect(sdl_renderer, &bar);
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);

    for (int count = 0; count < num_items; count++) {
        redrawMenuItem(win, count);
    }
}

static void
changeMenuItem(struct window *win, int x, int n, W_Color color, char *str,
               int len, W_Font font)
{
    struct menuItem *items = (struct menuItem *)win->data;
    if (!items) return;

    if (items[n].string) free(items[n].string);

    char *news = (char *)malloc((unsigned)(len + 1));
    strncpy(news, str, (unsigned)len);
    news[len] = 0;
    items[n].string = news;
    items[n].color = color;
    items[n].font = font;
    items[n].x = x;

    redrawMenuItem(win, n);
}

/* ========================================================================
 * Event handling
 * ======================================================================== */

static int
translate_sdl_event(SDL_Event *sdl_ev, W_Event *wev)
{
    memset(wev, 0, sizeof(W_Event));

    switch (sdl_ev->type) {
    case SDL_KEYDOWN: {
        SDL_Keycode sym = sdl_ev->key.keysym.sym;
        SDL_Keymod mod = sdl_ev->key.keysym.mod;
        char ch = 0;

        controlkey = (mod & KMOD_CTRL) ? 1 : 0;
        altkey = (mod & (KMOD_ALT | KMOD_GUI)) ? 1 : 0;

        /* Convert key to character */
        if (sym >= SDLK_a && sym <= SDLK_z) {
            ch = sym - SDLK_a + 'a';
            int shifted = (mod & KMOD_SHIFT) != 0;
            int caps = (mod & KMOD_CAPS) != 0;
            if (shifted ^ caps) ch -= 32;
        } else if (sym >= SDLK_0 && sym <= SDLK_9) {
            if (mod & KMOD_SHIFT) {
                const char shifted[] = ")!@#$%^&*(";
                ch = shifted[sym - SDLK_0];
            } else {
                ch = sym;
            }
        } else {
            switch (sym) {
            case SDLK_SPACE:     ch = ' '; break;
            case SDLK_RETURN:    ch = '\r'; break;
            case SDLK_ESCAPE:    ch = 27; break;
            case SDLK_BACKSPACE: ch = 8; break;
            case SDLK_TAB:       ch = '\t'; break;
            case SDLK_DELETE:    ch = 127; break;
            case SDLK_SEMICOLON: ch = (mod & KMOD_SHIFT) ? ':' : ';'; break;
            case SDLK_QUOTE:     ch = (mod & KMOD_SHIFT) ? '"' : '\''; break;
            case SDLK_COMMA:     ch = (mod & KMOD_SHIFT) ? '<' : ','; break;
            case SDLK_PERIOD:    ch = (mod & KMOD_SHIFT) ? '>' : '.'; break;
            case SDLK_SLASH:     ch = (mod & KMOD_SHIFT) ? '?' : '/'; break;
            case SDLK_BACKSLASH: ch = (mod & KMOD_SHIFT) ? '|' : '\\'; break;
            case SDLK_MINUS:     ch = (mod & KMOD_SHIFT) ? '_' : '-'; break;
            case SDLK_EQUALS:    ch = (mod & KMOD_SHIFT) ? '+' : '='; break;
            case SDLK_LEFTBRACKET:  ch = (mod & KMOD_SHIFT) ? '{' : '['; break;
            case SDLK_RIGHTBRACKET: ch = (mod & KMOD_SHIFT) ? '}' : ']'; break;
            case SDLK_BACKQUOTE: ch = (mod & KMOD_SHIFT) ? '~' : '`'; break;
            default: return 0;
            }
        }
        if (ch == 0) return 0;

        wev->type = W_EV_KEY;
        if (controlkey)
            wev->key = (int)ch + 128;
        else if (altkey)
            wev->key = (int)ch + 256;
        else
            wev->key = ch;

        int mx, my;
        SDL_GetMouseState(&mx, &my);
        struct window *win = findWindowAt(mx, my);
        wev->Window = win ? W_Window2Void(win) : NULL;
        wev->x = mx - (win ? win->x : 0);
        wev->y = my - (win ? win->y : 0);
        return 1;
    }

    case SDL_MOUSEBUTTONDOWN: {
        int mx = sdl_ev->button.x;
        int my = sdl_ev->button.y;
        struct window *win = findWindowAt(mx, my);
        if (!win) return 0;

        /* Handle scrolling window clicks */
        if (win->type == WIN_SCROLL) {
            switch (sdl_ev->button.button) {
            case SDL_BUTTON_LEFT:
                if (win->currenttop > win->bottom + (int)win->height) {
                    win->currenttop -= win->height;
                    int vis_h = (win->height - MENU_PAD * 2) / W_Textheight;
                    if (win->currenttop - win->bottom < vis_h)
                        win->currenttop = win->bottom + vis_h;
                }
                redrawScrolling(win);
                return 0;
            case SDL_BUTTON_MIDDLE:
                win->currenttop = win->top;
                redrawScrolling(win);
                return 0;
            case SDL_BUTTON_RIGHT:
                win->currenttop += win->height;
                if (win->currenttop > win->top)
                    win->currenttop = win->top;
                redrawScrolling(win);
                return 0;
            }
        }

        wev->type = W_EV_BUTTON;
        wev->Window = W_Window2Void(win);
        wev->x = mx - win->x;
        wev->y = my - win->y;

        /* Menu: translate Y to item index */
        if (win->type == WIN_MENU) {
            int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
            if (wev->y % item_h >= W_Textheight + MENU_PAD * 2)
                return 0; /* clicked on separator */
            wev->y = wev->y / item_h;
        }

        switch (sdl_ev->button.button) {
        case SDL_BUTTON_LEFT:   wev->key = W_LBUTTON; break;
        case SDL_BUTTON_MIDDLE: wev->key = W_MBUTTON; break;
        case SDL_BUTTON_RIGHT:  wev->key = W_RBUTTON; break;
        default: return 0;
        }

        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_CTRL) wev->key += 6;
        if (mod & KMOD_SHIFT) wev->key += 3;
        if (mod & (KMOD_ALT | KMOD_GUI)) wev->key += 12;

        if (continuousMouse && allowContinuousMouse) {
            buttonDown = 1;
            exitInputLoop = 1;
        }
        return 1;
    }

    case SDL_MOUSEBUTTONUP:
        wev->type = -1;
        buttonDown = 0;
        return 1;

    case SDL_MOUSEMOTION:
        if (!(continuousMouse && allowContinuousMouse) || !buttonDown)
            return 0;
        wev->type = W_EV_BUTTON;
        {
            int mx = sdl_ev->motion.x;
            int my = sdl_ev->motion.y;
            struct window *win = findWindowAt(mx, my);
            if (!win) return 0;
            wev->Window = W_Window2Void(win);
            wev->x = mx - win->x;
            wev->y = my - win->y;
        }
        return 1;

    case SDL_WINDOWEVENT:
        if (sdl_ev->window.event == SDL_WINDOWEVENT_EXPOSED) {
            for (int i = 0; i < num_windows; i++) {
                if (windows[i].mapped) {
                    wev->type = W_EV_EXPOSE;
                    wev->Window = W_Window2Void(&windows[i]);
                    return 1;
                }
            }
        }
        if (sdl_ev->window.event == SDL_WINDOWEVENT_CLOSE) {
            wev->type = W_EV_KILL_WINDOW;
            return 1;
        }
        return 0;

    case SDL_QUIT:
        fprintf(stderr, "sdl2window: quit event, exiting\n");
        SDL_Quit();
        exit(0);
        return 0;

    default:
        return 0;
    }
}

static void
poll_sdl_into_queue(void)
{
    SDL_Event sdl_ev;
    while (SDL_PollEvent(&sdl_ev)) {
        if (sdl_ev.type == SDL_QUIT) {
            fprintf(stderr, "sdl2window: window closed, exiting\n");
            SDL_Quit();
            exit(0);
        }
        W_Event tmp;
        if (translate_sdl_event(&sdl_ev, &tmp)) {
            evq_push(&tmp);
        }
    }
    if (!evq_empty()) {
        char dummy = 'e';
        write(socket_pipe[1], &dummy, 1);
    }
}

void
W_GetEvent(W_Event *wevent)
{
    while (evq_empty()) {
        SDL_Event sdl_ev;
        if (SDL_WaitEvent(&sdl_ev)) {
            W_Event tmp;
            if (translate_sdl_event(&sdl_ev, &tmp)) {
                evq_push(&tmp);
            }
        }
    }
    evq_pop(wevent);
    char dummy;
    while (read(socket_pipe[0], &dummy, 1) > 0) {}
}

int
W_EventsPending(void)
{
    /* Auto-flush for SDL2 */
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

void
W_NextEvent(W_Event *wevent)
{
    W_GetEvent(wevent);
}

/* ========================================================================
 * W_Socket and W_Flush
 * ======================================================================== */

void sdl2_pump_events(void)
{
    poll_sdl_into_queue();
}

int
W_Socket(void)
{
    sdl2_pump_events();
    return socket_pipe[0];
}

void
W_Flush(void)
{
    if (!sdl_renderer) return;

    SDL_SetRenderTarget(sdl_renderer, NULL);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    /* Composite all mapped windows */
    for (int i = 0; i < num_windows; i++) {
        struct window *win = &windows[i];
        if (!win->mapped || !win->texture) continue;
        if (win->type == WIN_SCROLLBAR) continue;

        SDL_Rect dst = {win->x, win->y, (int)win->width, (int)win->height};
        SDL_RenderCopy(sdl_renderer, win->texture, NULL, &dst);

        /* Draw border */
        if (win->border_color >= 0 && win->border_color < NCOLORS) {
            setRenderColor(win->border_color);
            SDL_RenderDrawRect(sdl_renderer, &dst);
        } else if (win->border > 0) {
            SDL_Color grey = getColor(DARK_GREY);
            SDL_SetRenderDrawColor(sdl_renderer, grey.r, grey.g, grey.b, 255);
            SDL_RenderDrawRect(sdl_renderer, &dst);
        }
    }

    /* Debug screenshot on SIGUSR1 */
    if (screenshot_requested) {
        screenshot_requested = 0;
        int w, h;
        SDL_GetRendererOutputSize(sdl_renderer, &w, &h);
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
            SDL_PIXELFORMAT_RGBA8888);
        if (surf) {
            SDL_RenderReadPixels(sdl_renderer, NULL, SDL_PIXELFORMAT_RGBA8888,
                                 surf->pixels, surf->pitch);
            char fname[64];
            snprintf(fname, sizeof(fname), "screenshot_%d.bmp", screenshot_count++);
            SDL_SaveBMP(surf, fname);
            SDL_FreeSurface(surf);
            fprintf(stderr, "sdl2window: saved %s\n", fname);
        }
    }

    /* Debug: periodic torp state dump */
    {
        static int flush_count = 0;
        if (me && ++flush_count % 60 == 0) { /* ~every second at 60fps */
            int total_torps = 0;
            if (torps && nplayers > 0 && ntorps > 0) {
                for (int i = 0; i < nplayers * ntorps; i++) {
                    if (torps[i].t_status) total_torps++;
                }
            }
            fprintf(stderr, "TORP_STATE: me->p_ntorp=%d me->p_status=%d "
                    "total_active=%d view=%d scale=%d winside=%d\n",
                    me->p_ntorp, me->p_status, total_torps,
                    view, scale, winside);
        }
    }

    SDL_RenderPresent(sdl_renderer);
    sdl2_pump_events();
}

void
W_Sync(void)
{
    W_Flush();
}

/* ========================================================================
 * Window destruction and misc
 * ======================================================================== */

void
W_DestroyWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || win == &myroot) return;

    if (win->texture) { SDL_DestroyTexture(win->texture); win->texture = NULL; }
    if (win->buffer) { SDL_DestroyTexture(win->buffer); win->buffer = NULL; }
    if (win->background) { SDL_DestroyTexture(win->background); win->background = NULL; }
    if (win->name) { free(win->name); win->name = NULL; }

    if (win->type == WIN_SCROLL && win->scrolldata) {
        for (int i = 0; i < win->maxlines; i++) {
            if (win->scrolldata[i].string) free(win->scrolldata[i].string);
        }
        free(win->scrolldata);
        win->scrolldata = NULL;
    }
    if (win->type == WIN_MENU && win->data) {
        struct menuItem *items = (struct menuItem *)win->data;
        /* Count items from height */
        int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
        int n = (win->height + MENU_BAR) / item_h;
        for (int i = 0; i < n; i++) {
            if (items[i].string) free(items[i].string);
        }
        free(win->data);
        win->data = NULL;
    }
    win->mapped = 0;
}

void
W_SetIconWindow(W_Window win, W_Window icon)
{
    /* No-op for SDL2 */
}

void
W_Beep(void)
{
    fprintf(stderr, "\a");
}

int
W_Mono(void)
{
    return forceMono;
}

/* ========================================================================
 * Cursor functions (stubs)
 * ======================================================================== */

void W_DefineTCrossCursor(W_Window window) {}
void W_DefineTextCursor(W_Window window) {}
void W_RevertCursor(W_Window window) {}
void W_DefineCursor(W_Window window, int width, int height, char *bits,
                    char *mask, int xhot, int yhot) {}

/* ========================================================================
 * Pointer and coordinate functions
 * ======================================================================== */

void
W_WarpPointer(W_Window window, int x, int y)
{
    static int warped_from_x = 0, warped_from_y = 0;

    if (window == NULL) {
        if (W_in_message) {
            SDL_WarpMouseGlobal(warped_from_x, warped_from_y);
            W_in_message = 0;
        }
    } else {
        int gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        warped_from_x = gx;
        warped_from_y = gy;
        struct window *win = W_Void2Window(window);
        SDL_WarpMouseInWindow(sdl_window, win->x, win->y);
        W_in_message = 1;
    }
}

int
findMouseInWin(int *x, int *y, W_Window window)
{
    struct window *win = W_Void2Window(window);
    int mx, my;
    SDL_GetMouseState(&mx, &my);

    int wx = mx - win->x;
    int wy = my - win->y;

    if (wx >= 0 && wx <= (int)win->width && wy >= 0 && wy <= (int)win->height) {
        *x = wx;
        *y = wy;
        return 1;
    }
    *x = 0;
    *y = 0;
    return 0;
}

void
W_TranslatePoints(W_Window window, int *x, int *y)
{
    struct window *win = W_Void2Window(window);
    if (win->type == WIN_TEXT) {
        *y = (*y - MENU_PAD) / W_Textheight;
        *x = (*x - MENU_PAD) / W_Textwidth;
    }
}

/* ========================================================================
 * Resize functions
 * ======================================================================== */

void
W_ResizeWindow(W_Window window, int neww, int newh)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;

    win->width = neww;
    win->height = newh;

    if (win->texture) SDL_DestroyTexture(win->texture);
    win->texture = createTexture(neww, newh);

    if (win->isbuffered) {
        if (win->buffer) SDL_DestroyTexture(win->buffer);
        win->buffer = createTexture(neww, newh);
    }
}

void
W_ResizeMenu(W_Window window, int neww, int newh)
{
    W_ResizeWindow(window,
                   neww * W_Textwidth + WIN_EDGE * 2,
                   newh * (W_Textheight + MENU_PAD * 2) + (newh - 1) * MENU_BAR);
}

void
W_ResizeText(W_Window window, int neww, int newh)
{
    W_ResizeWindow(window,
                   neww * W_Textwidth + WIN_EDGE * 2,
                   newh * W_Textheight + MENU_PAD * 2);
}

void
W_Deiconify(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (win) win->mapped = 1;
}

/* ========================================================================
 * Config checking functions (called by w_MakeWindow)
 * ======================================================================== */

int
checkMapped(char *name)
{
    char buf[100];
    sprintf(buf, "%s.mapped", name);
    return booleanDefault(buf, 0);
}

int
checkScrollSaveLines(char *name)
{
    char buf[100];
    sprintf(buf, "%s.scrollSaveLines", name);
    return intDefault(buf, scrollSaveLines);
}

int
checkBuffered(char *name)
{
    char buf[100];
    sprintf(buf, "%s.buffered", name);
    if (!useOR && strcmp(name, "local") == 0) {
        printf("Double Buffering automatically enabled.\n");
        return booleanDefault(buf, 1);
    }
    return booleanDefault(buf, 0);
}
