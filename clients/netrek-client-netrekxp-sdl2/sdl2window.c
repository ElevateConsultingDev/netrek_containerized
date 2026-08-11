/* sdl2window.c - SDL2 implementation of Wlib for NetrekXP
 *
 * Replaces mswindow.c for macOS/SDL2 builds.
 * Each Wlib "window" is a virtual window backed by an SDL_Texture.
 * Adapted from the COW SDL2 backend.
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
#include <pthread.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "proto.h"

/* Main thread ID for SDL event pump safety */
pthread_t sdl2_main_thread;

/* Window types */
#define WIN_GRAPH   1
#define WIN_TEXT    2
#define WIN_MENU    3
#define WIN_SCROLL  4

/* Layout constants */
#define WIN_EDGE    1
#define MENU_PAD    4
#define MENU_BAR    4
#define MAX_TEXT_WIDTH 100
#define SCROLL_THUMB_WIDTH 5
#define TILESIDE    16

/* Color indices */
#define WHITE   0
#define BLACK   1
#define RED     2
#define GREEN   3
#define YELLOW  4
#define CYAN    5
#define GREY    6
#define C_ROM   7
#define C_KLI   8
#define C_FED   9
#define C_ORI   10
#define C_IND   11
#define C_GOD   12
#define NCOLORS 13

#define RaceDefaultOffset (C_ROM - RED)

/* Line cache */
#define MAXCACHE 128

/* Maximum windows */
#define MAX_WINDOWS 256

/* Event queue */
#define EVQ_SIZE 64

/* ================================================================
 * Internal structures
 * ================================================================ */

struct stringList {
    char    string[MAX_TEXT_WIDTH];
    int     color;
    struct stringList *next;
};

struct scrollingWindow {
    int     lines;
    int     updated;
    int     topline;
    struct stringList *head;
    struct stringList *tail;
};

struct menuItem {
    char   *string;
    int     color;
    short   len;
};

struct xp_window {
    int     id;
    SDL_Texture *texture;
    int     type;
    void   *data;           /* scrollingWindow* or menuItem* */
    int     mapped;
    int     width, height;
    int     x, y;
    int     parent_id;
    char   *name;
    W_Callback handle_keydown;
    W_Callback handle_keyup;
    W_Callback handle_button;
    W_Callback handle_expose;
    int     insensitive;
    int     border_color;
    SDL_Texture *background;
    int     num_items;      /* for menus */
};

struct xp_icon {
    SDL_Texture *texture;
    int     width, height;
    int     x, y;           /* source position within texture */
    unsigned char *raw_bits;
};

struct colors {
    char *name;
    SDL_Color sdl_color;
};

/* ================================================================
 * Global state
 * ================================================================ */

static int font_indices[] = {0, 1, 2, 3};

W_Font W_BigFont = (W_Font)&font_indices[0];
W_Font W_SmallFont = (W_Font)&font_indices[1];
W_Font W_RegularFont = (W_Font)&font_indices[1];
W_Font W_HighlightFont = (W_Font)&font_indices[2];
W_Font W_UnderlineFont = (W_Font)&font_indices[3];
W_Font W_IndyFont = (W_Font)&font_indices[1];
W_Font W_MyPlanetFont = (W_Font)&font_indices[2];
W_Font W_FriendlyPlanetFont = (W_Font)&font_indices[2];
W_Font W_EnemyPlanetFont = (W_Font)&font_indices[1];

W_Color W_White = WHITE, W_Black = BLACK, W_Red = RED, W_Green = GREEN;
W_Color W_Yellow = YELLOW, W_Cyan = CYAN, W_Grey = GREY;
W_Color W_God = C_GOD, W_Ind = C_IND, W_Fed = C_FED;
W_Color W_Rom = C_ROM, W_Kli = C_KLI, W_Ori = C_ORI;

int W_Textwidth = 6, W_Textheight = 10;
int W_FastClear = 1;
int forceMono = 0;

/* SDL2 state */
static SDL_Window   *sdl_window = NULL;
static SDL_Renderer *sdl_renderer = NULL;

static struct xp_window windows[MAX_WINDOWS];
static int num_windows = 0;

static int socket_pipe[2] = {-1, -1};

static TTF_Font *sdl_fonts[4] = {NULL, NULL, NULL, NULL};

static struct colors colortable[NCOLORS] = {
    {"white",     {255, 255, 255, 255}},
    {"black",     {0,   0,   0,   255}},
    {"red",       {255, 0,   0,   255}},
    {"green",     {0,   255, 0,   255}},
    {"yellow",    {255, 255, 0,   255}},
    {"cyan",      {0,   255, 255, 255}},
    {"dark grey", {128, 128, 128, 255}},
    {"Rom",       {255, 0,   0,   255}},
    {"Kli",       {0,   128, 0,   255}},
    {"Fed",       {255, 255, 0,   255}},
    {"Ori",       {0,   255, 255, 255}},
    {"Ind",       {128, 128, 128, 255}},
    {"God",       {255, 255, 255, 255}},
};

/* Line cache */
static SDL_Point _lcache[NCOLORS][MAXCACHE * 2];
static int _lcache_index[NCOLORS];

/* Clear area cache */
static SDL_Rect _rcache[MAXCACHE];
static int _rcache_index = 0;
static struct xp_window *_rcache_win = NULL;

/* Event queue */
static W_Event evq_buf[EVQ_SIZE];
static int evq_head = 0, evq_tail = 0;

static int evq_empty(void) { return evq_head == evq_tail; }
static int evq_full(void)  { return ((evq_head + 1) % EVQ_SIZE) == evq_tail; }

static void evq_push(W_Event *ev) {
    if (evq_full()) return;
    evq_buf[evq_head] = *ev;
    evq_head = (evq_head + 1) % EVQ_SIZE;
}

static int evq_pop(W_Event *ev) {
    if (evq_empty()) return 0;
    *ev = evq_buf[evq_tail];
    evq_tail = (evq_tail + 1) % EVQ_SIZE;
    return 1;
}

static struct xp_window myroot;

/* Thread sync for W_TerminateWait */
static volatile int terminate_wait_flag = 0;

/* Scrolling window settings */
static int scroll_lines = 100;

/* Screenshot */
static volatile sig_atomic_t screenshot_requested = 0;
static void sigusr1_handler(int sig) { (void)sig; screenshot_requested = 1; }

/* Forward declarations */
static void redrawMenu(struct xp_window *win);
static void renderText(struct xp_window *win, int px, int py,
                       W_Color color, char *str, int len, W_Font font);
static void compositeWindows(void);

/* ================================================================
 * Helpers
 * ================================================================ */

static struct xp_window *void2win(W_Window w) {
    if (!w) return &myroot;
    return (struct xp_window *)w;
}

static W_Window win2void(struct xp_window *w) {
    return (W_Window)w;
}

static int fontNum(W_Font font) {
    if (!font) return 1;
    return *((int *)font);
}

static SDL_Color getColor(W_Color color) {
    if (color >= 0 && color < NCOLORS)
        return colortable[color].sdl_color;
    return colortable[WHITE].sdl_color;
}

static void setRenderColor(W_Color color) {
    SDL_Color c = getColor(color);
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
}

static struct xp_window *allocWindow(int type) {
    if (num_windows >= MAX_WINDOWS) {
        fprintf(stderr, "sdl2window: too many windows\n");
        return NULL;
    }
    struct xp_window *win = &windows[num_windows];
    memset(win, 0, sizeof(struct xp_window));
    win->id = num_windows;
    win->type = type;
    win->parent_id = -1;
    num_windows++;
    return win;
}

static struct xp_window *findWindowAt(int sx, int sy) {
    for (int i = num_windows - 1; i >= 0; i--) {
        struct xp_window *win = &windows[i];
        if (!win->mapped) continue;
        if (sx >= win->x && sx < win->x + win->width &&
            sy >= win->y && sy < win->y + win->height)
            return win;
    }
    return NULL;
}

/* ================================================================
 * SDL2 event pump (called from select wrapper in config.h)
 * ================================================================ */

void sdl2_pump_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            exit(0);
        }

        if (ev.type == SDL_KEYDOWN) {
            int key = ev.key.keysym.sym;
            int mod = ev.key.keysym.mod;
            char ch = 0;

            /* Convert SDL keycode to ASCII */
            if (key >= SDLK_a && key <= SDLK_z) {
                ch = (mod & KMOD_CTRL) ? (key - SDLK_a + 1) :
                     (mod & KMOD_SHIFT) ? (key - SDLK_a + 'A') :
                     (key - SDLK_a + 'a');
            } else if (key >= SDLK_0 && key <= SDLK_9) {
                if (mod & KMOD_SHIFT) {
                    const char *shifted = ")!@#$%^&*(";
                    ch = shifted[key - SDLK_0];
                } else {
                    ch = key - SDLK_0 + '0';
                }
            } else if (key == SDLK_SPACE) ch = ' ';
            else if (key == SDLK_RETURN) ch = '\r';
            else if (key == SDLK_ESCAPE) ch = 27;
            else if (key == SDLK_BACKSPACE) ch = 8;
            else if (key == SDLK_TAB) ch = '\t';
            else if (key == SDLK_DELETE) ch = 127;
            else if (key == SDLK_MINUS) ch = (mod & KMOD_SHIFT) ? '_' : '-';
            else if (key == SDLK_EQUALS) ch = (mod & KMOD_SHIFT) ? '+' : '=';
            else if (key == SDLK_LEFTBRACKET) ch = (mod & KMOD_SHIFT) ? '{' : '[';
            else if (key == SDLK_RIGHTBRACKET) ch = (mod & KMOD_SHIFT) ? '}' : ']';
            else if (key == SDLK_BACKSLASH) ch = (mod & KMOD_SHIFT) ? '|' : '\\';
            else if (key == SDLK_SEMICOLON) ch = (mod & KMOD_SHIFT) ? ':' : ';';
            else if (key == SDLK_QUOTE) ch = (mod & KMOD_SHIFT) ? '"' : '\'';
            else if (key == SDLK_COMMA) ch = (mod & KMOD_SHIFT) ? '<' : ',';
            else if (key == SDLK_PERIOD) ch = (mod & KMOD_SHIFT) ? '>' : '.';
            else if (key == SDLK_SLASH) ch = (mod & KMOD_SHIFT) ? '?' : '/';
            else if (key == SDLK_BACKQUOTE) ch = (mod & KMOD_SHIFT) ? '~' : '`';

            if (ch) {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                struct xp_window *hit = findWindowAt(mx, my);
                if (hit) {
                    W_Event wev;
                    wev.type = W_EV_KEY;
                    wev.Window = win2void(hit);
                    wev.key = ch;
                    wev.x = mx - hit->x;
                    wev.y = my - hit->y;
#ifdef MOUSE_AS_SHIFT
                    wev.modifier = 0;
#endif
                    evq_push(&wev);
                    /* Wake select via pipe */
                    if (socket_pipe[1] >= 0) {
                        char c = 1;
                        write(socket_pipe[1], &c, 1);
                    }
                }
            }
        }

        if (ev.type == SDL_MOUSEBUTTONDOWN) {
            int mx = ev.button.x;
            int my = ev.button.y;
            int button = 0;
            int mod;

            switch (ev.button.button) {
                case SDL_BUTTON_LEFT:   button = W_LBUTTON; break;
                case SDL_BUTTON_MIDDLE: button = W_MBUTTON; break;
                case SDL_BUTTON_RIGHT:  button = W_RBUTTON; break;
                case SDL_BUTTON_X1:     button = W_XBUTTON1; break;
                case SDL_BUTTON_X2:     button = W_XBUTTON2; break;
            }

#ifdef SHIFTED_MOUSE
            mod = SDL_GetModState();
            if (mod & KMOD_SHIFT) button += 7;
            else if (mod & KMOD_CTRL) button += 12;
            else if (mod & KMOD_ALT) button += 17;
#endif

            struct xp_window *hit = findWindowAt(mx, my);
            if (hit) {
                W_Event wev;
                wev.type = W_EV_BUTTON;
                wev.Window = win2void(hit);
                wev.key = button;
                wev.x = mx - hit->x;
                wev.y = my - hit->y;
#ifdef MOUSE_AS_SHIFT
                wev.modifier = 0;
#endif
                evq_push(&wev);
                if (socket_pipe[1] >= 0) {
                    char c = 1;
                    write(socket_pipe[1], &c, 1);
                }
            }
        }

        if (ev.type == SDL_MOUSEWHEEL) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            struct xp_window *hit = findWindowAt(mx, my);
            if (hit) {
                W_Event wev;
                wev.type = W_EV_BUTTON;
                wev.Window = win2void(hit);
                wev.key = (ev.wheel.y > 0) ? W_WHEELUP : W_WHEELDOWN;
                wev.x = mx - hit->x;
                wev.y = my - hit->y;
#ifdef MOUSE_AS_SHIFT
                wev.modifier = 0;
#endif
                evq_push(&wev);
            }
        }

#ifdef MOTION_MOUSE
        if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK)) {
            int mx = ev.motion.x;
            int my = ev.motion.y;
            struct xp_window *hit = findWindowAt(mx, my);
            if (hit) {
                W_Event wev;
                wev.type = W_EV_CM_BUTTON;
                wev.Window = win2void(hit);
                wev.key = W_LBUTTON;
                wev.x = mx - hit->x;
                wev.y = my - hit->y;
#ifdef MOUSE_AS_SHIFT
                wev.modifier = 0;
#endif
                evq_push(&wev);
            }
        }
#endif
    }

    /* Composite and present */
    compositeWindows();
}

/* ================================================================
 * Compositor
 * ================================================================ */

static void compositeWindows(void) {
    SDL_SetRenderTarget(sdl_renderer, NULL);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    for (int i = 0; i < num_windows; i++) {
        struct xp_window *win = &windows[i];
        if (!win->mapped || !win->texture) continue;

        SDL_Rect dst = { win->x, win->y, win->width, win->height };
        SDL_RenderCopy(sdl_renderer, win->texture, NULL, &dst);

        /* Draw border */
        if (win->border_color >= 0) {
            setRenderColor(win->border_color);
            SDL_RenderDrawRect(sdl_renderer, &dst);
        }
    }

    SDL_RenderPresent(sdl_renderer);
}

/* ================================================================
 * W_Initialize
 * ================================================================ */

void W_Initialize(char *str)
{
    (void)str;
    sdl2_main_thread = pthread_self();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        exit(1);
    }

    if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
    }

    int win_w = 1024, win_h = 768;

    sdl_window = SDL_CreateWindow("NetrekXP",
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

    /* Pipe for select() integration */
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
                sdl_fonts[0] = TTF_OpenFont(*fp, sz * 3);
                sdl_fonts[2] = TTF_OpenFont(*fp, sz);
                sdl_fonts[3] = TTF_OpenFont(*fp, sz);
                if (sdl_fonts[2]) TTF_SetFontStyle(sdl_fonts[2], TTF_STYLE_BOLD);
                if (sdl_fonts[3]) TTF_SetFontStyle(sdl_fonts[3], TTF_STYLE_UNDERLINE);
                break;
            }
            TTF_CloseFont(test);
        }
        if (sdl_fonts[1]) break;
    }

    W_Textwidth = 6;
    W_Textheight = 10;

    memset(&myroot, 0, sizeof(myroot));
    myroot.id = -1;
    myroot.mapped = 1;
    SDL_GetWindowSize(sdl_window, &myroot.width, &myroot.height);

    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderPresent(sdl_renderer);

    signal(SIGUSR1, sigusr1_handler);
}

void GetColors(void) { /* colors are initialized in colortable */ }

/* ================================================================
 * Window creation
 * ================================================================ */

W_Window W_MakeWindow(char *name, int x, int y, int width, int height,
                      W_Window parent, int border, W_Color color)
{
    struct xp_window *win = allocWindow(WIN_GRAPH);
    if (!win) return NULL;

    win->width = width;
    win->height = height;
    win->x = x;
    win->y = y;
    win->border_color = color;
    if (name) win->name = strdup(name);

    if (parent) {
        struct xp_window *pw = void2win(parent);
        win->parent_id = pw->id;
        win->x += pw->x;
        win->y += pw->y;
    }

    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;

    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        width, height);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(sdl_renderer, win->texture);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_SetRenderTarget(sdl_renderer, NULL);
    }

    return win2void(win);
}

W_Window W_RenameWindow(W_Window window, char *str)
{
    /* Rename an existing window */
    if (!window) return NULL;
    struct xp_window *win = void2win(window);
    if (win->name) free(win->name);
    if (str) win->name = strdup(str);
    return window;
}

/* newWindow - proto.h declares: Window *newWindow(name,x,y,w,h,parent,border,color,type) */
Window *newWindow(char *name, int x, int y, int width, int height,
                  W_Window parent, int border, W_Color color, int type)
{
    (void)type; /* we treat all types the same internally */
    W_Window w = W_MakeWindow(name, x, y, width, height, parent, border, color);
    return (Window *)w; /* Window* and W_Window are both pointer types */
}

W_Window W_MakeTextWindow(char *name, int x, int y, int width, int height,
                          W_Window parent, int border)
{
    int pix_width = width * W_Textwidth + WIN_EDGE * 2;
    int pix_height = height * W_Textheight + MENU_PAD * 2;

    return W_MakeWindow(name, x, y, pix_width, pix_height, parent, border, W_White);
}

W_Window W_MakeScrollingWindow(char *name, int x, int y, int width, int height,
                               W_Window parent, int border)
{
    int pix_width = width * W_Textwidth + WIN_EDGE * 2;
    int pix_height = height * W_Textheight + MENU_PAD * 2;

    W_Window ww = W_MakeWindow(name, x, y, pix_width, pix_height, parent, border, W_White);
    if (ww) {
        struct xp_window *win = void2win(ww);
        win->type = WIN_SCROLL;
        struct scrollingWindow *sw = calloc(1, sizeof(struct scrollingWindow));
        sw->lines = 0;
        sw->topline = 0;
        win->data = sw;
    }
    return ww;
}

W_Window W_MakeScrollingRichTextWindow(char *name, int x, int y, int width, int height,
                                       W_Window parent, int border)
{
    /* Treat rich text as regular scrolling window */
    return W_MakeScrollingWindow(name, x, y, width, height, parent, border);
}

W_Window W_MakeMenu(char *name, int x, int y, int width, int height,
                    W_Window parent, int border)
{
    int pix_width = width * W_Textwidth + WIN_EDGE * 2;
    int pix_height = height * (W_Textheight + MENU_PAD * 2 + MENU_BAR);

    W_Window ww = W_MakeWindow(name, x, y, pix_width, pix_height, parent, border, W_White);
    if (ww) {
        struct xp_window *win = void2win(ww);
        win->type = WIN_MENU;
        win->num_items = height;
        struct menuItem *items = calloc(height, sizeof(struct menuItem));
        win->data = items;
    }
    return ww;
}

void W_ResizeMenuToNumItems(W_Window window, int numitems)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    int new_height = numitems * (W_Textheight + MENU_PAD * 2 + MENU_BAR);
    win->height = new_height;
    win->num_items = numitems;
    /* Recreate texture */
    if (win->texture) SDL_DestroyTexture(win->texture);
    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        win->width, new_height);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(sdl_renderer, win->texture);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_SetRenderTarget(sdl_renderer, NULL);
    }
}

/* ================================================================
 * Window management
 * ================================================================ */

void W_MapWindow(W_Window window)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    win->mapped = 1;
    if (win->handle_expose)
        win->handle_expose(window, 0, 0);
}

void W_UnmapWindow(W_Window window)
{
    if (!window) return;
    void2win(window)->mapped = 0;
}

int W_IsMapped(W_Window window)
{
    if (!window) return 0;
    return void2win(window)->mapped;
}

void W_DestroyWindow(W_Window window)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    win->mapped = 0;
    if (win->texture) {
        SDL_DestroyTexture(win->texture);
        win->texture = NULL;
    }
}

int W_WindowWidth(W_Window window)
{
    if (!window) return 0;
    return void2win(window)->width;
}

int W_WindowHeight(W_Window window)
{
    if (!window) return 0;
    return void2win(window)->height;
}

void W_SetWindowName(W_Window window, char *name)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (win->name) free(win->name);
    win->name = name ? strdup(name) : NULL;
}

void W_ChangeBorder(W_Window window, int color)
{
    if (!window) return;
    void2win(window)->border_color = color;
}

void W_SetSensitive(Window *window, int v)
{
    /* Stub - the Window* here is the struct.h type, not our internal type */
    (void)window; (void)v;
}

void W_ResizeWindow(W_Window window, int width, int height)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    win->width = width;
    win->height = height;
    if (win->texture) SDL_DestroyTexture(win->texture);
    win->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        width, height);
    if (win->texture) {
        SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(sdl_renderer, win->texture);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_SetRenderTarget(sdl_renderer, NULL);
    }
}

void W_ResizeTextWindow(W_Window window, int width, int height)
{
    W_ResizeWindow(window, width * W_Textwidth + WIN_EDGE * 2,
                   height * W_Textheight + MENU_PAD * 2);
}

void W_ResizeMenu(W_Window window, int width, int height)
{
    W_ResizeWindow(window, width * W_Textwidth + WIN_EDGE * 2,
                   height * (W_Textheight + MENU_PAD * 2 + MENU_BAR));
}

/* ================================================================
 * Callback handlers
 * ================================================================ */

W_Callback W_GetWindowKeyDownHandler(W_Window w) {
    if (!w) return NULL;
    return void2win(w)->handle_keydown;
}

W_Callback W_GetWindowKeyUpHandler(W_Window w) {
    if (!w) return NULL;
    return void2win(w)->handle_keyup;
}

W_Callback W_GetWindowButtonHandler(W_Window w) {
    if (!w) return NULL;
    return void2win(w)->handle_button;
}

W_Callback W_GetWindowExposeHandler(W_Window w) {
    if (!w) return NULL;
    return void2win(w)->handle_expose;
}

W_Callback W_SetWindowKeyDownHandler(W_Window w, W_Callback c) {
    if (!w) return NULL;
    W_Callback old = void2win(w)->handle_keydown;
    void2win(w)->handle_keydown = c;
    return old;
}

W_Callback W_SetWindowKeyUpHandler(W_Window w, W_Callback c) {
    if (!w) return NULL;
    W_Callback old = void2win(w)->handle_keyup;
    void2win(w)->handle_keyup = c;
    return old;
}

W_Callback W_SetWindowButtonHandler(W_Window w, W_Callback c) {
    if (!w) return NULL;
    W_Callback old = void2win(w)->handle_button;
    void2win(w)->handle_button = c;
    return old;
}

W_Callback W_SetWindowExposeHandler(W_Window w, W_Callback c) {
    if (!w) return NULL;
    W_Callback old = void2win(w)->handle_expose;
    void2win(w)->handle_expose = c;
    return old;
}

/* ================================================================
 * Event handling
 * ================================================================ */

int W_EventsPending(void)
{
    /* Drain the pipe */
    if (socket_pipe[0] >= 0) {
        char buf[64];
        while (read(socket_pipe[0], buf, sizeof(buf)) > 0)
            ;
    }
    return !evq_empty();
}

int W_EventsQueued(void)
{
    return !evq_empty();
}

void W_NextEvent(W_Event *event)
{
    while (evq_empty()) {
        sdl2_pump_events();
        if (terminate_wait_flag) {
            memset(event, 0, sizeof(*event));
            return;
        }
        SDL_Delay(10);
    }
    evq_pop(event);
}

int W_SpNextEvent(W_Event *event)
{
    if (!evq_empty()) {
        evq_pop(event);
        return 1;
    }
    return 0;
}

int W_WaitForEvent(void)
{
    terminate_wait_flag = 0;
    while (evq_empty()) {
        sdl2_pump_events();  /* pump events + composite on main thread */
        if (terminate_wait_flag) return 0;
        SDL_Delay(16);  /* ~60fps */
    }
    return 1;
}


void W_TerminateWait(void)
{
    terminate_wait_flag = 1;
}

int W_Socket(void)
{
    return socket_pipe[0];
}

void W_Beep(void) { /* TODO: beep sound */ }

/* ================================================================
 * Drawing
 * ================================================================ */

static void renderText(struct xp_window *win, int px, int py,
                       W_Color color, char *str, int len, W_Font font)
{
    if (!win || !win->texture || !str || len <= 0) return;

    int fi = fontNum(font);
    TTF_Font *f = sdl_fonts[fi];
    if (!f) f = sdl_fonts[1];
    if (!f) return;

    char buf[256];
    if (len > 255) len = 255;
    memcpy(buf, str, len);
    buf[len] = '\0';

    SDL_Color c = getColor(color);
    SDL_Surface *surf = TTF_RenderText_Blended(f, buf, c);
    if (!surf) return;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(sdl_renderer, surf);
    if (tex) {
        SDL_SetRenderTarget(sdl_renderer, win->texture);
        SDL_Rect dst = { px, py, surf->w, surf->h };
        SDL_RenderCopy(sdl_renderer, tex, NULL, &dst);
        SDL_SetRenderTarget(sdl_renderer, NULL);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void W_WriteText(W_Window window, int x, int y, W_Color color,
                 char *str, int len, W_Font font)
{
    if (!window) return;
    struct xp_window *win = void2win(window);

    if (win->type == WIN_SCROLL) {
        /* Add to scrolling buffer */
        struct scrollingWindow *sw = (struct scrollingWindow *)win->data;
        if (!sw) return;
        struct stringList *sl = calloc(1, sizeof(struct stringList));
        if (len > MAX_TEXT_WIDTH - 1) len = MAX_TEXT_WIDTH - 1;
        memcpy(sl->string, str, len);
        sl->string[len] = '\0';
        sl->color = color;
        sl->next = NULL;
        if (sw->tail) {
            sw->tail->next = sl;
        } else {
            sw->head = sl;
        }
        sw->tail = sl;
        sw->lines++;
        sw->updated = 1;
        return;
    }

    if (win->type == WIN_MENU) {
        struct menuItem *items = (struct menuItem *)win->data;
        if (items && y >= 0 && y < win->num_items) {
            if (items[y].string) free(items[y].string);
            items[y].string = strndup(str, len);
            items[y].color = color;
            items[y].len = len;
        }
        if (win->mapped) redrawMenu(win);
        return;
    }

    /* Graph or text window */
    int px = x, py = y;
    if (win->type == WIN_TEXT) {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
    }

    renderText(win, px, py, color, str, len, font);
}

void W_MaskText(W_Window window, int x, int y, W_Color color,
                char *str, int len, W_Font font)
{
    W_WriteText(window, x, y, color, str, len, font);
}

void W_WriteTTSText(W_Window window, int max_width, int tts_ypos, char *str, int len)
{
    if (!str) return;
    (void)max_width;
    W_WriteText(window, 5, tts_ypos, W_White, str, len, W_RegularFont);
}

void W_ClearArea(W_Window window, int x, int y, int width, int height)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_Rect r = { x, y, width, height };
    SDL_RenderFillRect(sdl_renderer, &r);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_ClearWindow(W_Window window)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_FillArea(W_Window window, int x, int y, int width, int height, W_Color color)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    setRenderColor(color);
    SDL_Rect r = { x, y, width, height };
    SDL_RenderFillRect(sdl_renderer, &r);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_CacheClearArea(W_Window window, int x, int y, int width, int height)
{
    W_ClearArea(window, x, y, width, height);
}

void W_ClearAreas(W_Window window, int *xs, int *ys, int *widths, int *heights, int num)
{
    for (int i = 0; i < num; i++) {
        W_ClearArea(window, xs[i], ys[i], widths[i], heights[i]);
    }
}

void W_MakeLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    setRenderColor(color);
    SDL_RenderDrawLine(sdl_renderer, x0, y0, x1, y1);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_MakeDashedLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    /* Just draw a solid line for now */
    W_MakeLine(window, x0, y0, x1, y1, color);
}

void W_CacheLine(W_Window window, int x0, int y0, int x1, int y1, int color)
{
    if (color < 0 || color >= NCOLORS) return;
    int idx = _lcache_index[color];
    if (idx >= MAXCACHE) {
        W_FlushLineCaches(window);
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
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    for (int c = 0; c < NCOLORS; c++) {
        if (_lcache_index[c] == 0) continue;
        setRenderColor(c);
        /* Draw individual line segments (pairs of points), not a connected polyline */
        for (int i = 0; i < _lcache_index[c]; i++) {
            SDL_RenderDrawLine(sdl_renderer,
                _lcache[c][i*2].x, _lcache[c][i*2].y,
                _lcache[c][i*2+1].x, _lcache[c][i*2+1].y);
        }
        _lcache_index[c] = 0;
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_MakeLines(W_Window window, int *x0, int *y0, int *x1, int *y1,
                 int num, W_Color color)
{
    if (!window) return;
    for (int i = 0; i < num; i++) {
        W_MakeLine(window, x0[i], y0[i], x1[i], y1[i], color);
    }
}

void W_MakePoint(W_Window window, int x, int y, W_Color color)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    setRenderColor(color);
    SDL_RenderDrawPoint(sdl_renderer, x, y);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_CachePoint(W_Window window, int x, int y, int color)
{
    W_MakePoint(window, x, y, color);
}

void W_MakeTractLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    W_MakeLine(window, x0, y0, x1, y1, color);
}

void Old_W_MakeTractLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    W_MakeLine(window, x0, y0, x1, y1, color);
}

void W_MakePhaserLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    W_MakeLine(window, x0, y0, x1, y1, color);
}

void W_WriteCircle(W_Window window, int x, int y, int r, int fat, int border, W_Color color)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    setRenderColor(color);

    /* Midpoint circle algorithm */
    int cx = r, cy = 0, err = 1 - r;
    while (cx >= cy) {
        SDL_RenderDrawPoint(sdl_renderer, x + cx, y + cy);
        SDL_RenderDrawPoint(sdl_renderer, x + cy, y + cx);
        SDL_RenderDrawPoint(sdl_renderer, x - cy, y + cx);
        SDL_RenderDrawPoint(sdl_renderer, x - cx, y + cy);
        SDL_RenderDrawPoint(sdl_renderer, x - cx, y - cy);
        SDL_RenderDrawPoint(sdl_renderer, x - cy, y - cx);
        SDL_RenderDrawPoint(sdl_renderer, x + cy, y - cx);
        SDL_RenderDrawPoint(sdl_renderer, x + cx, y - cy);
        cy++;
        if (err < 0) {
            err += 2 * cy + 1;
        } else {
            cx--;
            err += 2 * (cy - cx) + 1;
        }
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_WriteTriangle(W_Window window, int x, int y, int s, int t, W_Color color)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    setRenderColor(color);

    /* Draw a triangle */
    SDL_RenderDrawLine(sdl_renderer, x, y - s, x - s, y + s);
    SDL_RenderDrawLine(sdl_renderer, x - s, y + s, x + s, y + s);
    SDL_RenderDrawLine(sdl_renderer, x + s, y + s, x, y - s);
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_WriteRectangle(W_Window window, int x, int y, int w, int h, int fat, W_Color color)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (!win->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    setRenderColor(color);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderDrawRect(sdl_renderer, &r);
    if (fat) {
        SDL_Rect r2 = { x+1, y+1, w-2, h-2 };
        SDL_RenderDrawRect(sdl_renderer, &r2);
    }
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ================================================================
 * Bitmap handling
 * ================================================================ */

W_Icon W_StoreBitmap(int width, int height, char *data, W_Window window)
{
    struct xp_icon *icon = calloc(1, sizeof(struct xp_icon));
    if (!icon) return NULL;

    icon->width = width;
    icon->height = height;
    icon->x = 0;
    icon->y = 0;

    /* Convert XBM data to RGBA */
    int bw = (width + 7) / 8;
    int pixels = width * height;
    unsigned char *rgba = calloc(pixels * 4, 1);

    if (data && rgba) {
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                int byte_idx = row * bw + col / 8;
                int bit = (unsigned char)data[byte_idx] & (1 << (col % 8));
                int pidx = (row * width + col) * 4;
                if (bit) {
                    rgba[pidx] = rgba[pidx+1] = rgba[pidx+2] = 255;
                    rgba[pidx+3] = 255;
                } else {
                    rgba[pidx+3] = 0;
                }
            }
        }
    }

    icon->texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
        width, height);
    if (icon->texture && rgba) {
        SDL_UpdateTexture(icon->texture, NULL, rgba, width * 4);
        SDL_SetTextureBlendMode(icon->texture, SDL_BLENDMODE_BLEND);
    }

    /* Save raw bits for re-colorization */
    if (data) {
        int raw_size = bw * height;
        icon->raw_bits = malloc(raw_size);
        if (icon->raw_bits) memcpy(icon->raw_bits, data, raw_size);
    }

    free(rgba);
    return (W_Icon)icon;
}

W_Icon W_StoreBitmap2(HINSTANCE hDLLInstance, int width, int height,
                      int data, W_Window window, UINT BmpType)
{
    /* Resource-based bitmap loading - not available on macOS.
     * Return a placeholder icon. */
    (void)hDLLInstance; (void)data; (void)BmpType;
    struct xp_icon *icon = calloc(1, sizeof(struct xp_icon));
    if (!icon) return NULL;
    icon->width = width;
    icon->height = height;
    return (W_Icon)icon;
}

W_Icon W_StoreBitmap3(char *BMPfile, int width, int height,
                      int bits, W_Window window, UINT BmpType)
{
    /* Load BMP file from disk */
    (void)bits; (void)BmpType;
    struct xp_icon *icon = calloc(1, sizeof(struct xp_icon));
    if (!icon) return NULL;

    icon->width = width;
    icon->height = height;

    if (BMPfile) {
        SDL_Surface *surf = IMG_Load(BMPfile);
        if (surf) {
            icon->texture = SDL_CreateTextureFromSurface(sdl_renderer, surf);
            if (icon->texture) {
                SDL_SetTextureBlendMode(icon->texture, SDL_BLENDMODE_BLEND);
            }
            icon->width = surf->w;
            icon->height = surf->h;
            SDL_FreeSurface(surf);
        }
    }

    return (W_Icon)icon;
}

W_Icon W_PointBitmap2(W_Icon bigbitmap, int col, int row, int width, int height)
{
    /* Create a sub-bitmap reference pointing into a larger bitmap */
    struct xp_icon *big = (struct xp_icon *)bigbitmap;
    struct xp_icon *icon = calloc(1, sizeof(struct xp_icon));
    if (!icon) return NULL;

    icon->texture = big ? big->texture : NULL;
    icon->width = width;
    icon->height = height;
    icon->x = col * width;
    icon->y = row * height;

    return (W_Icon)icon;
}

void W_WriteBitmap(int x, int y, W_Icon bit, W_Color color, W_Window window)
{
    if (!window || !bit) return;
    struct xp_window *win = void2win(window);
    struct xp_icon *icon = (struct xp_icon *)bit;
    if (!win->texture || !icon->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);

    /* Colorize */
    SDL_Color c = getColor(color);
    SDL_SetTextureColorMod(icon->texture, c.r, c.g, c.b);

    SDL_Rect src = { icon->x, icon->y, icon->width, icon->height };
    SDL_Rect dst = { x, y, icon->width, icon->height };
    SDL_RenderCopy(sdl_renderer, icon->texture, &src, &dst);

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_WriteScaleBitmap(int x, int y, int destwidth, int destheight,
                        int srcwidth, int srcheight, int angle,
                        W_Icon icon, W_Color color, W_Window window)
{
    if (!window || !icon) return;
    struct xp_window *win = void2win(window);
    struct xp_icon *ic = (struct xp_icon *)icon;
    if (!win->texture || !ic->texture) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);

    SDL_Color c = getColor(color);
    SDL_SetTextureColorMod(ic->texture, c.r, c.g, c.b);

    SDL_Rect src = { ic->x, ic->y, srcwidth, srcheight };
    SDL_Rect dst = { x, y, destwidth, destheight };
    SDL_RenderCopyEx(sdl_renderer, ic->texture, &src, &dst,
                     (double)angle, NULL, SDL_FLIP_NONE);

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

void W_WriteBitmapGrey(int x, int y, W_Icon bit, W_Color color, W_Window window)
{
    /* Just render with reduced alpha for "grey" effect */
    if (!bit) return;
    struct xp_icon *icon = (struct xp_icon *)bit;
    if (icon->texture)
        SDL_SetTextureAlphaMod(icon->texture, 128);
    W_WriteBitmap(x, y, bit, color, window);
    if (icon->texture)
        SDL_SetTextureAlphaMod(icon->texture, 255);
}

void W_OverlayBitmap(int x, int y, W_Icon bit, W_Color color, W_Window window)
{
    W_WriteBitmap(x, y, bit, color, window);
}

void W_OverlayScaleBitmap(int x, int y, int destwidth, int destheight,
                          int srcwidth, int srcheight, int angle,
                          W_Icon icon, W_Color color, W_Window window)
{
    W_WriteScaleBitmap(x, y, destwidth, destheight, srcwidth, srcheight,
                       angle, icon, color, window);
}

/* ================================================================
 * Tiling
 * ================================================================ */

void W_TileWindow(W_Window window, W_Icon bit)
{
    (void)window; (void)bit;
    /* Stub - tiling not essential for gameplay */
}

void W_UnTileWindow(W_Window window)
{
    (void)window;
}

/* ================================================================
 * Menu rendering
 * ================================================================ */

static void redrawMenu(struct xp_window *win)
{
    if (!win || !win->texture) return;
    struct menuItem *items = (struct menuItem *)win->data;
    if (!items) return;

    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    int item_height = W_Textheight + MENU_PAD * 2;

    for (int i = 0; i < win->num_items; i++) {
        int ypos = i * (item_height + MENU_BAR);

        /* Separator bar */
        if (i > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 128, 128, 128, 255);
            SDL_Rect bar = { 0, ypos - MENU_BAR, win->width, MENU_BAR };
            SDL_RenderFillRect(sdl_renderer, &bar);
        }

        /* Text */
        if (items[i].string) {
            renderText(win, WIN_EDGE, ypos + MENU_PAD,
                      items[i].color, items[i].string, items[i].len, W_RegularFont);
        }
    }

    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ================================================================
 * Scrolling windows
 * ================================================================ */

void W_FlushScrollingWindow(W_Window window)
{
    if (!window) return;
    struct xp_window *win = void2win(window);
    if (win->type != WIN_SCROLL || !win->data) return;

    struct scrollingWindow *sw = (struct scrollingWindow *)win->data;
    if (!sw->updated) return;

    /* Clear and redraw */
    SDL_SetRenderTarget(sdl_renderer, win->texture);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    int visible_lines = win->height / W_Textheight;
    int line = 0;
    struct stringList *sl = sw->head;

    /* Skip to show last visible_lines */
    int skip = sw->lines - visible_lines;
    if (skip < 0) skip = 0;
    while (sl && skip > 0) {
        sl = sl->next;
        skip--;
    }

    while (sl && line < visible_lines) {
        renderText(win, WIN_EDGE, line * W_Textheight + MENU_PAD,
                  sl->color, sl->string, strlen(sl->string), W_RegularFont);
        sl = sl->next;
        line++;
    }

    sw->updated = 0;
    SDL_SetRenderTarget(sdl_renderer, NULL);
}

/* ================================================================
 * Cursor
 * ================================================================ */

void W_DefineCursor(W_Window window, int width, int height,
                    char *bits, char *mask, int xhot, int yhot)
{
    (void)window; (void)width; (void)height;
    (void)bits; (void)mask; (void)xhot; (void)yhot;
}

void W_DefineMapcursor(W_Window window) { (void)window; }
void W_DefineLocalcursor(W_Window window) { (void)window; }
void W_DefineTrekCursor(W_Window window) { (void)window; }
void W_DefineWarningCursor(W_Window window) { (void)window; }
void W_DefineArrowCursor(W_Window window) { (void)window; }
void W_DefineTextCursor(W_Window window) { (void)window; }
void W_DefineFedCursor(W_Window window) { (void)window; }
void W_DefineRomCursor(W_Window window) { (void)window; }
void W_DefineKliCursor(W_Window window) { (void)window; }
void W_DefineOriCursor(W_Window window) { (void)window; }

void W_WarpPointer(W_Window window) { (void)window; }

/* ================================================================
 * Misc
 * ================================================================ */

int W_Mono(void) { return 0; }

void W_ShowBitmaps(void) { /* stub */ }

void W_AutoRepeatOff(void) { /* stub */ }
void W_AutoRepeatOn(void) { /* stub */ }

char *checkWindowsGeometry(W_Window win) { (void)win; return NULL; }
void updateWindowsGeometry(W_Window win) { (void)win; }

void W_MessageAllowedWindows(int messtype, int x, int y, W_Color color,
                             char *str, int len, W_Font font)
{
    (void)messtype; (void)x; (void)y; (void)color;
    (void)str; (void)len; (void)font;
}

void W_SetWAM(W_Window win) { (void)win; }

/* Double-buffering stubs - our SDL2 backend handles this internally.
 * SDBUFFER is typedef'd in struct.h. */
SDBUFFER *W_InitSDB(W_Window window) { (void)window; return NULL; }
SDBUFFER *SDB_lookup(W_Window window) { (void)window; return NULL; }
void W_UpdateSDB(W_Window window, SDBUFFER *sdb) { (void)window; (void)sdb; }
void W_Win2Mem(SDBUFFER *sdb) { (void)sdb; }
void W_Mem2Win(SDBUFFER *sdb) { (void)sdb; }

unsigned long GetTickCount(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* checkMapped / checkMappedPref - check .netrekrc for window.mapped */
int checkMapped(char *name)
{
    char buf[100];
    sprintf(buf, "%s.mapped", name);
    return booleanDefault(buf, 0);
}

int checkMappedPref(char *name, int preferred)
{
    char buf[100];
    sprintf(buf, "%s.mapped", name);
    return booleanDefault(buf, preferred);
}

/* findMouseInWin - get mouse position relative to a window */
int findMouseInWin(int *x, int *y, W_Window window)
{
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    if (!window) { *x = mx; *y = my; return 1; }
    struct xp_window *win = void2win(window);
    *x = mx - win->x;
    *y = my - win->y;
    return (*x >= 0 && *y >= 0 && *x < win->width && *y < win->height);
}

/* colornames array - used by defaults.c for color customization */
char *colornames[13] = {
    "white", "black", "red", "green", "yellow", "cyan",
    "light grey", "God", "Rom", "Kli", "Fed", "Ori", "Ind"
};
