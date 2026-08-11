/* sdl2window.h - Internal structures for Paradise SDL2 Wlib backend */

#ifndef _h_sdl2window
#define _h_sdl2window

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Window types (same as x11window.c) */
#define WIN_GRAPH     1
#define WIN_TEXT      2
#define WIN_MENU      3
#define WIN_SCROLL    4
#define WIN_BORDER    5
#define WIN_SCROLLBAR 6

/* Color indices */
#define WHITE      0
#define BLACK      1
#define RED        2
#define GREEN      3
#define YELLOW     4
#define CYAN       5
#define DARK_GREY  6
#define LIGHT_GREY 7

#define NCOLORS    8

/* Cache sizes */
#define MAXCACHE 128
#define MAX_TEXT_WIDTH 100

/* Maximum number of virtual windows */
#define MAX_WINDOWS 256

struct menuItem {
    char   *string;
    W_Color color;
    W_Font  font;
    int     x;         /* character offset from left */
};

struct scrolldata {
    W_Color color;
    char   *string;
    int     len;
};

struct window {
    int     id;
    SDL_Texture *texture;       /* render target for this virtual window */
    SDL_Texture *buffer;        /* double-buffer texture (if buffered) */
    int     isbuffered;
    int     type;
    char   *data;               /* menuItem[] or scrolldata[] */
    int     mapped;
    unsigned int width, height; /* pixel dimensions for GRAPH, char dims for text */
    int     x, y;               /* position within SDL window */
    char   *name;
    int     insensitive;
    int     border;             /* border width */
    W_Color border_color;
    int     parent_id;
    /* scrolling window support */
    int     isscrollbar;
    int     scrollbar_parent_id;
    int     top, bottom, currenttop;
    struct scrolldata *scrolldata;
    int     maxlines;
    SDL_Texture *background;    /* tiled background texture */
};

struct colors {
    char *name;
    SDL_Color sdl_color;
};

/* Global SDL2 state */
extern SDL_Window   *sdl_window;
extern SDL_Renderer *sdl_renderer;
extern struct window windows[MAX_WINDOWS];
extern int num_windows;
extern struct colors colortable[NCOLORS];
extern TTF_Font *sdl_fonts[4];

/* Internal functions */
struct window *W_Void2Window(W_Window win);
W_Window W_Window2Void(struct window *win);
int fontNum(W_Font font);
struct window *newWindow(Window window, int type);
struct window *findWindowAt(int x, int y);
SDL_Color getColor(W_Color color);
void setRenderColor(W_Color color);

#endif /* _h_sdl2window */
