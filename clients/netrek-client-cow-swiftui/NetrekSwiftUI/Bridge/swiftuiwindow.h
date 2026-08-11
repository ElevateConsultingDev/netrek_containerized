/* swiftuiwindow.h - Internal structures for CoreGraphics/SwiftUI Wlib backend */

#ifndef _h_swiftuiwindow
#define _h_swiftuiwindow

#include <CoreGraphics/CoreGraphics.h>
#include <pthread.h>

/* Window types (same as x11window.c / sdl2window.c) */
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

/* Glyph atlas dimensions */
#define GLYPH_FIRST 32
#define GLYPH_LAST  126
#define GLYPH_COUNT (GLYPH_LAST - GLYPH_FIRST + 1)

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

struct colors {
    char   *name;
    uint8_t r, g, b, a;
};

struct window {
    int     id;
    CGContextRef context;       /* bitmap drawing context */
    uint8_t *pixels;            /* RGBA pixel buffer (owned by context) */
    int     type;               /* WIN_GRAPH, WIN_TEXT, WIN_MENU, WIN_SCROLL */
    char   *data;               /* scrollingWindow* or menuItem[] */
    int     mapped;
    int     width, height;      /* pixel dimensions */
    int     stride;             /* bytes per row */
    int     x, y;               /* position within parent */
    int     parent_id;          /* index of parent window, -1 for root */
    char   *name;
    W_Callback handle_keydown;
    W_Callback handle_keyup;
    W_Callback handle_button;
    W_Callback handle_expose;
    int     insensitive;
    int     border_color;
    uint8_t *bg_pixels;         /* tiled background pixel data (or NULL) */
    int     dirty;              /* set when content changed since last snapshot */
};

struct icon {
    uint8_t *pixels;            /* RGBA pixel data (width * height * 4) */
    int     width, height;
    unsigned char *raw_bits;    /* original XBM data for re-colorization */
    struct window *parent_win;
};

/* Sprite object for PNG-loaded images */
struct S_Object {
    uint8_t *pixels;            /* RGBA sprite sheet pixel data */
    int     view, nviews;
    int     width, height;      /* per-frame dimensions */
    int     sheet_height;       /* total pixel height of sprite sheet */
    int     cloak;
    struct window *target_win;  /* which window to draw on */
};

/* Maximum number of virtual windows */
#define MAX_WINDOWS 256

/* Global state (defined in swiftuiwindow.c) */
extern struct window swui_windows[MAX_WINDOWS];
extern int swui_num_windows;           /* in __DATA,__swui section (isolated from COW BSS) */
extern struct colors colortable[NCOLORS];
extern int W_in_message;

/* Snapshot system - thread-safe frame publishing */
extern CGImageRef snapshots[MAX_WINDOWS];
extern int snapshot_positions[MAX_WINDOWS][4]; /* x, y, w, h for each window */
extern int snapshot_mapped[MAX_WINDOWS];
extern int snapshot_count;       /* number of valid snapshots */
extern uint64_t snapshot_version;
extern pthread_mutex_t snapshot_mutex;

/* Glyph atlas */
extern uint8_t *glyph_atlas[4]; /* one per font style (big, regular, bold, underline) */
extern int glyph_w[4], glyph_h[4]; /* per-font glyph dimensions */

/* Internal functions */
struct window *W_Void2Window(W_Window win);
W_Window W_Window2Void(struct window *win);
struct icon *W_Void2Icon(W_Icon bit);
W_Icon W_Icon2Void(struct icon *ic);
int fontNum(W_Font font);
struct window *newWindow(int type);
struct window *findWindowAt(int x, int y);

/* Swift → C event injection (called from Swift) */
void swiftui_inject_key(int window_id, unsigned char key, int x, int y,
                        int type, int modifier);
void swiftui_inject_button(int window_id, int button, int x, int y,
                           int modifier);
void swiftui_inject_expose(int window_id);

/* C → Swift notification (implemented in Swift, called from C) */
extern void swiftui_notify_flush(void);

#endif /* _h_swiftuiwindow */
