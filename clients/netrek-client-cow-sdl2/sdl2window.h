/* sdl2window.h - Internal structures for SDL2 Wlib backend */

#ifndef _h_sdl2window
#define _h_sdl2window

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Window types (same as x11window.c) */
#define WIN_GRAPH   1
#define WIN_TEXT    2
#define WIN_MENU    3
#define WIN_SCROLL  4

/* Layout constants (matching X11 backend) */
#define WIN_EDGE    5
#define MENU_PAD    6
#define MENU_BAR    1
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

#ifdef RACE_COLORS
#define C_ROM   7
#define C_KLI   8
#define C_FED   9
#define C_ORI   10
#define C_IND   11
#define NCOLORS 12
#else
#define NCOLORS 7
#endif

#define RaceDefaultOffset (C_ROM - RED)

/* Line cache for W_CacheLine */
#define MAXCACHE 128

struct stringList {
    char    string[MAX_TEXT_WIDTH];
    W_Color color;
    W_Font  font;
    struct stringList *next, *prev;
};

struct scrollingWindow {
    int     lines;
    int     updated;
    int     topline;
    struct stringList *head;
    struct stringList *tail;
    struct stringList *index;
};

struct menuItem {
    int     column;
    char   *string;
    W_Color color;
};

struct window {
    int     id;
    SDL_Texture *texture;       /* render target for this virtual window */
    int     type;               /* WIN_GRAPH, WIN_TEXT, WIN_MENU, WIN_SCROLL */
    char   *data;               /* scrollingWindow* or menuItem[] */
    int     mapped;
    int     width, height;      /* pixel dimensions */
    int     x, y;               /* position within parent */
    int     parent_id;          /* index of parent window, -1 for root */
    char   *name;
    W_Callback handle_keydown;
    W_Callback handle_keyup;
    W_Callback handle_button;
    W_Callback handle_expose;
    int     insensitive;
    int     border_color;
    SDL_Texture *background;    /* tiled/background texture */
    int     text_cursor;        /* for scrolling windows: show text cursor? */
};

struct icon {
    SDL_Texture *texture;       /* RGBA (white + alpha from 1-bit data) */
    int     width, height;
    unsigned char *raw_bits;    /* original XBM data for re-colorization */
    struct window *parent_win;
};

struct colors {
    char *name;
    SDL_Color sdl_color;        /* RGBA color */
};

/* Sprite object for PNG-loaded images */
struct S_Object {
    SDL_Texture *texture;       /* RGBA with transparency */
    int     view, nviews;
    int     width, height;      /* source frame size in the texture */
    int     disp_w, disp_h;     /* on-screen (logical) size; = source unless a
                                   hi-res sheet is downscaled to canonical size */
    int     cloak;
    struct window *target_win;  /* which window to draw on */
};

/* Maximum number of virtual windows */
#define MAX_WINDOWS 256

/* Retina supersample factor (see sdl2window.c). Shared so sprite rendering in
 * sdl2sprite.c scales into the same RS-sized window textures. */
#define RENDER_SCALE 2

/* Target a window's supersampled texture (applies the RS draw scale) / return
 * to the screen at 1:1. Defined in sdl2window.c. */
void winTarget(struct window *win);
void screenTarget(void);

/* Global SDL2 state (defined in sdl2window.c) */
extern SDL_Window   *sdl_window;
extern SDL_Renderer *sdl_renderer;
extern struct window windows[MAX_WINDOWS];
extern int num_windows;
extern struct colors colortable[NCOLORS];
extern TTF_Font *sdl_fonts[4];     /* regular, bold, italic, big */
extern int W_in_message;

/* Internal functions */
struct window *W_Void2Window(W_Window win);
W_Window W_Window2Void(struct window *win);
struct icon *W_Void2Icon(W_Icon bit);
W_Icon W_Icon2Void(struct icon *ic);
int fontNum(W_Font font);
struct window *newWindow(int type);
struct window *findWindowAt(int x, int y);
void compositeWindows(void);
SDL_Color getColor(W_Color color);

#endif /* _h_sdl2window */
