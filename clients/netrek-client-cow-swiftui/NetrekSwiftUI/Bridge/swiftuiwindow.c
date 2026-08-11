/* swiftuiwindow.c - CoreGraphics/SwiftUI implementation of Wlib
 *
 * Replaces sdl2window.c (and x11window.c) for the SwiftUI backend.
 * All COW code calls Wlib functions; this implements them via CoreGraphics.
 *
 * Each Wlib "window" is a virtual window backed by a CGBitmapContext.
 * W_Flush() snapshots all mapped windows as CGImages for SwiftUI to display.
 *
 * Threading model:
 *   - COW engine runs on a background thread (all Wlib calls happen here)
 *   - SwiftUI runs on the main thread
 *   - W_Flush() copies pixel buffers under a mutex and publishes CGImages
 *   - Events flow from SwiftUI → event queue → pipe wakeup → C engine
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

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "smessage.h"
#include "defaults.h"
#include "swiftuiwindow.h"

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

int W_BigTextwidth = 18, W_BigTextheight = 30;
int W_Textwidth = 6, W_Textheight = 10;
int W_FastClear = 1;
int W_in_message = 0;
int forceMono = 0;
int full_screen_default = 0, full_screen_enabled = 0;

/* ========================================================================
 * CoreGraphics backend state
 * ======================================================================== */

/* ALL our backend globals go in __DATA,__swui to isolate from COW BSS overflow.
 * COW has a buffer overflow that writes ASCII sequences into __DATA,__bss,
 * corrupting any globals in that section. Our custom section is immune. */
#define SWUI_SECTION __attribute__((section("__DATA,__swui")))

SWUI_SECTION int _swui_canary1 = 0xCAFEBABE;
SWUI_SECTION struct window swui_windows[MAX_WINDOWS] = {{0}};
SWUI_SECTION int swui_num_windows = 0;
SWUI_SECTION int _swui_canary2 = 0xDEADBEEF;

/* Color table */
struct colors colortable[NCOLORS] = {
    {"white",    255, 255, 255, 255},
    {"black",    0,   0,   0,   255},
    {"red",      255, 0,   0,   255},
    {"green",    0,   255, 0,   255},
    {"yellow",   255, 255, 0,   255},
    {"cyan",     0,   255, 255, 255},
    {"dark grey",128, 128, 128, 255},
#ifdef RACE_COLORS
    {"Rom",      255, 0,   0,   255},
    {"Kli",      0,   128, 0,   255},
    {"Fed",      255, 255, 0,   255},
    {"Ori",      0,   255, 255, 255},
    {"Ind",      128, 128, 128, 255},
#endif
};

/* Pipe for W_Socket() integration with select() */
static int socket_pipe[2] = {-1, -1};

/* Glyph atlas */
SWUI_SECTION uint8_t *glyph_atlas[4] = {NULL, NULL, NULL, NULL};
SWUI_SECTION int glyph_w[4] = {0, 0, 0, 0};
SWUI_SECTION int glyph_h[4] = {0, 0, 0, 0};

/* Snapshot system */
SWUI_SECTION CGImageRef snapshots[MAX_WINDOWS];
SWUI_SECTION int snapshot_positions[MAX_WINDOWS][4];
SWUI_SECTION int snapshot_mapped[MAX_WINDOWS];
SWUI_SECTION int snapshot_count = 0;
SWUI_SECTION uint64_t snapshot_version = 0;
SWUI_SECTION pthread_mutex_t snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Line cache */
typedef struct { int x0, y0, x1, y1; } LineSeg;
static LineSeg _lcache[NCOLORS][MAXCACHE];
static int _lcache_index[NCOLORS];

/* Clear area cache */
static CGRect _rcache[MAXCACHE];
static int _rcache_index = 0;
static struct window *_rcache_win = NULL;

/* Scrolling window settings */
static int scrollbar = 1;
static int scroll_thumb_width = SCROLL_THUMB_WIDTH;
static int scroll_lines = 100;

/* Event queue - ring buffer (also protected in __DATA,__swui) */
#define EVQ_SIZE 64
SWUI_SECTION static W_Event evq_buf[EVQ_SIZE] = {{0}};
SWUI_SECTION static int evq_head = 0;
SWUI_SECTION static int evq_tail = 0;
SWUI_SECTION static pthread_mutex_t evq_mutex = PTHREAD_MUTEX_INITIALIZER;

static int evq_empty(void) { return evq_head == evq_tail; }
static int evq_full(void)  { return ((evq_head + 1) % EVQ_SIZE) == evq_tail; }

static void evq_push(W_Event *ev)
{
    pthread_mutex_lock(&evq_mutex);
    if (!evq_full()) {
        evq_buf[evq_head] = *ev;
        evq_head = (evq_head + 1) % EVQ_SIZE;
    }
    pthread_mutex_unlock(&evq_mutex);
}

static int evq_pop(W_Event *ev)
{
    pthread_mutex_lock(&evq_mutex);
    if (evq_empty()) {
        pthread_mutex_unlock(&evq_mutex);
        return 0;
    }
    *ev = evq_buf[evq_tail];
    evq_tail = (evq_tail + 1) % EVQ_SIZE;
    pthread_mutex_unlock(&evq_mutex);
    return 1;
}

static int evq_has_events(void)
{
    pthread_mutex_lock(&evq_mutex);
    int has = !evq_empty();
    pthread_mutex_unlock(&evq_mutex);
    return has;
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
    if (!font) return 1;
    return *((int *)font);
}

static void getColorRGB(W_Color color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (color >= 0 && color < NCOLORS) {
        *r = colortable[color].r;
        *g = colortable[color].g;
        *b = colortable[color].b;
    } else {
        *r = 255; *g = 255; *b = 255;
    }
}

static void setCGColor(CGContextRef ctx, W_Color color)
{
    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);
    CGContextSetRGBStrokeColor(ctx, r/255.0, g/255.0, b/255.0, 1.0);
    CGContextSetRGBFillColor(ctx, r/255.0, g/255.0, b/255.0, 1.0);
}

/* Convert screen Y (0=top) to pixel buffer row index.
 * The CGBitmapContext has a flip CTM so CGContext drawing functions
 * use Y=0 at top (matching COW). But direct pixel buffer access
 * bypasses the CTM, so we must flip Y manually.
 * In the flipped NSView, CGImages appear inverted, so we apply
 * a compensating flip in the draw method — this helper ensures
 * direct pixel writes match the CGContext flip CTM. */
static inline int buf_y(int win_height, int screen_y) {
    return win_height - 1 - screen_y;
}

/* Create a CGBitmapContext with RGBA pixel buffer */
static CGContextRef createBitmapContext(int width, int height, uint8_t **out_pixels)
{
    int stride = width * 4;
    uint8_t *pixels = calloc(height * stride, 1);
    if (!pixels) return NULL;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(pixels, width, height, 8, stride,
        cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);

    if (!ctx) {
        free(pixels);
        *out_pixels = NULL;
        return NULL;
    }

    /* CoreGraphics has origin at bottom-left; flip to top-left */
    CGContextTranslateCTM(ctx, 0, height);
    CGContextScaleCTM(ctx, 1, -1);

    /* Disable anti-aliasing for pixel-perfect rendering */
    CGContextSetShouldAntialias(ctx, false);
    CGContextSetAllowsAntialiasing(ctx, false);

    *out_pixels = pixels;
    return ctx;
}

/* Allocate a new virtual window */
struct window *newWindow(int type)
{
    if (swui_num_windows >= MAX_WINDOWS) {
        fprintf(stderr, "swiftuiwindow: too many windows\n");
        return NULL;
    }
    struct window *win = &swui_windows[swui_num_windows];
    memset(win, 0, sizeof(struct window));
    win->id = swui_num_windows;
    win->type = type;
    win->parent_id = -1;
    swui_num_windows++;
    return win;
}

/* Create bitmap context for a window */
static void createWindowContext(struct window *win)
{
    if (win->width <= 0 || win->height <= 0) return;

    win->context = createBitmapContext(win->width, win->height, &win->pixels);
    win->stride = win->width * 4;
    win->dirty = 1;

    /* Clear to black */
    if (win->context) {
        CGContextSetRGBFillColor(win->context, 0, 0, 0, 1);
        CGContextFillRect(win->context, CGRectMake(0, 0, win->width, win->height));
    }
}

/* Find which virtual window contains the given screen coordinate */
struct window *findWindowAt(int sx, int sy)
{
    for (int i = swui_num_windows - 1; i >= 0; i--) {
        struct window *win = &swui_windows[i];
        if (!win->mapped) continue;
        if (sx >= win->x && sx < win->x + win->width &&
            sy >= win->y && sy < win->y + win->height) {
            return win;
        }
    }
    return NULL;
}

/* ========================================================================
 * Glyph atlas - pre-rendered bitmap font
 * ======================================================================== */

static void buildGlyphAtlas(int font_idx, const char *font_name,
                            float font_size, int target_w, int target_h,
                            int bold)
{
    int gw = target_w;
    int gh = target_h;
    int atlas_w = gw * GLYPH_COUNT;
    int atlas_h = gh;

    uint8_t *atlas = calloc(atlas_w * atlas_h, 1); /* alpha only */
    if (!atlas) return;

    /* Create a NON-FLIPPED context for glyph rendering.
     * CoreText works in native CG coordinates (Y=0 at bottom).
     * Using createBitmapContext() would apply a Y-flip CTM that
     * interferes with CoreText glyph rendering, causing mirrored text. */
    int tmp_stride = gw * 4;
    uint8_t *tmp_pixels = calloc(gh * tmp_stride, 1);
    if (!tmp_pixels) { free(atlas); return; }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef tmp_ctx = CGBitmapContextCreate(tmp_pixels, gw, gh, 8,
        tmp_stride, cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (!tmp_ctx) { free(tmp_pixels); free(atlas); return; }

    CTFontRef font = CTFontCreateWithName(
        CFStringCreateWithCString(NULL, font_name, kCFStringEncodingUTF8),
        font_size, NULL);
    if (!font) {
        /* Fallback to Menlo */
        font = CTFontCreateWithName(CFSTR("Menlo"), font_size, NULL);
    }
    if (!font) {
        CGContextRelease(tmp_ctx);
        free(tmp_pixels);
        free(atlas);
        return;
    }

    /* Set up attributes */
    CFStringRef keys[2];
    CFTypeRef values[2];
    int num_attrs = 1;
    keys[0] = kCTFontAttributeName;
    values[0] = font;

    CFDictionaryRef attrs = CFDictionaryCreate(NULL,
        (const void **)keys, (const void **)values, num_attrs,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    for (int ch = GLYPH_FIRST; ch <= GLYPH_LAST; ch++) {
        /* Clear tmp context */
        memset(tmp_pixels, 0, gw * gh * 4);

        /* Render character */
        UniChar uc = (UniChar)ch;
        CFStringRef str = CFStringCreateWithCharacters(NULL, &uc, 1);
        CFAttributedStringRef astr = CFAttributedStringCreate(NULL, str, attrs);
        CTLineRef line = CTLineCreateWithAttributedString(astr);

        /* Draw at baseline */
        CGFloat ascent, descent, leading;
        CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        CGContextSetTextPosition(tmp_ctx, 0, descent);
        CGContextSetRGBFillColor(tmp_ctx, 1, 1, 1, 1);
        CTLineDraw(line, tmp_ctx);

        CFRelease(line);
        CFRelease(astr);
        CFRelease(str);

        /* Extract alpha channel into atlas */
        int atlas_x = (ch - GLYPH_FIRST) * gw;
        for (int y = 0; y < gh; y++) {
            for (int x = 0; x < gw; x++) {
                /* Context Y=0 is at bottom; atlas Y=0 is at top — flip */
                int src_y = gh - 1 - y;
                int offset = (src_y * gw + x) * 4;
                uint8_t a = tmp_pixels[offset + 3]; /* alpha */
                /* Also check RGB for non-premultiplied */
                uint8_t r = tmp_pixels[offset + 0];
                if (a > 0 || r > 0) {
                    atlas[y * atlas_w + atlas_x + x] = (a > r) ? a : r;
                }
            }
        }
    }

    CFRelease(attrs);
    CFRelease(font);
    CGContextRelease(tmp_ctx);
    free(tmp_pixels);

    glyph_atlas[font_idx] = atlas;
    glyph_w[font_idx] = gw;
    glyph_h[font_idx] = gh;

    int nonzero = 0;
    for (int i = 0; i < atlas_w * atlas_h; i++)
        if (atlas[i] > 0) nonzero++;
    fprintf(stderr, "[swiftui] glyph atlas[%d] '%s' %.0fpt: %d/%d non-zero pixels\n",
            font_idx, font_name, font_size, nonzero, atlas_w * atlas_h);
}

static void initGlyphAtlases(void)
{
    /* Regular font: 6x10 to match COW layout */
    buildGlyphAtlas(1, "Menlo", 9.0, 6, 10, 0);  /* regular */
    buildGlyphAtlas(2, "Menlo-Bold", 9.0, 6, 10, 1);  /* bold/highlight */
    buildGlyphAtlas(3, "Menlo", 9.0, 6, 10, 0);  /* underline (same as regular) */
    buildGlyphAtlas(0, "Menlo", 24.0, 18, 30, 0); /* big font */
}

/* ========================================================================
 * W_Initialize - Core initialization
 * ======================================================================== */

void W_Initialize(char *str)
{
    fprintf(stderr, "[swiftui] W_Initialize called\n");

    /* Create pipe for W_Socket() integration with select() */
    if (pipe(socket_pipe) < 0) {
        perror("pipe");
        exit(1);
    }
    fcntl(socket_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(socket_pipe[1], F_SETFL, O_NONBLOCK);

    /* Build glyph atlases for text rendering */
    initGlyphAtlases();
    fprintf(stderr, "[swiftui] Glyph atlases built\n");

    /* Force COW layout dimensions */
    W_Textwidth = 6;
    W_Textheight = 10;
    W_BigTextwidth = 18;
    W_BigTextheight = 30;

    /* Initialize root window */
    memset(&myroot, 0, sizeof(myroot));
    myroot.id = -1;
    myroot.mapped = 1;
    myroot.width = 1024;
    myroot.height = 768;

    /* Initialize snapshot array */
    memset(snapshots, 0, sizeof(snapshots));
    memset(snapshot_positions, 0, sizeof(snapshot_positions));
    memset(snapshot_mapped, 0, sizeof(snapshot_mapped));
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

    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;

    createWindowContext(win);
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

    createWindowContext(win);
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

    struct scrollingWindow *sw = calloc(1, sizeof(struct scrollingWindow));
    win->data = (char *)sw;

    createWindowContext(win);
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

    struct menuItem *items = calloc(height, sizeof(struct menuItem));
    for (int i = 0; i < height; i++) {
        items[i].string = calloc(MAX_TEXT_WIDTH, 1);
        items[i].color = W_White;
    }
    win->data = (char *)items;

    createWindowContext(win);
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
    win->dirty = 1;

    if (win->type == WIN_MENU) {
        redrawMenu(win);
    }

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
    if (win) { win->mapped = 0; win->dirty = 1; }
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
}

void W_SetWindowName(W_Window w, char *name)
{
    W_RenameWindow(w, name);
}

void W_FlushWindow(W_Window window)
{
    /* No-op - we composite in W_Flush */
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
    if (!win || !win->context) return;

    if (win->bg_pixels) {
        memcpy(win->pixels, win->bg_pixels, win->height * win->stride);
    } else {
        memset(win->pixels, 0, win->height * win->stride);
        /* Set alpha to 255 for opaque black */
        for (int i = 3; i < win->height * win->stride; i += 4)
            win->pixels[i] = 255;
    }
    win->dirty = 1;

    if (win->type == WIN_MENU) {
        redrawMenu(win);
    }
}

void W_ClearArea(W_Window window, int x, int y, int width, int height)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    int px, py, pw, ph;
    if (win->type == WIN_GRAPH) {
        px = x; py = y; pw = width; ph = height;
    } else {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
        pw = width * W_Textwidth;
        ph = height * W_Textheight;
    }

    /* Clamp to window bounds */
    if (px < 0) { pw += px; px = 0; }
    if (py < 0) { ph += py; py = 0; }
    if (px + pw > win->width) pw = win->width - px;
    if (py + ph > win->height) ph = win->height - py;
    if (pw <= 0 || ph <= 0) return;

    /* Fill with opaque black */
    for (int row = py; row < py + ph; row++) {
        int by = buf_y(win->height, row);
        if (by < 0 || by >= win->height) continue;
        uint8_t *p = win->pixels + by * win->stride + px * 4;
        for (int col = 0; col < pw; col++) {
            p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
            p += 4;
        }
    }
    win->dirty = 1;
}

void W_CacheClearArea(W_Window window, int x, int y, int width, int height)
{
    struct window *win = W_Void2Window(window);
    if (_rcache_win != win) {
        if (_rcache_index > 0) W_FlushClearAreaCache((W_Window)_rcache_win);
        _rcache_win = win;
    }
    if (_rcache_index < MAXCACHE) {
        _rcache[_rcache_index] = CGRectMake(x, y, width, height);
        _rcache_index++;
    } else {
        W_FlushClearAreaCache((W_Window)win);
        _rcache[0] = CGRectMake(x, y, width, height);
        _rcache_index = 1;
        _rcache_win = win;
    }
}

void W_FlushClearAreaCache(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels || _rcache_index == 0) {
        _rcache_index = 0;
        return;
    }

    for (int i = 0; i < _rcache_index; i++) {
        int px = (int)_rcache[i].origin.x;
        int py = (int)_rcache[i].origin.y;
        int pw = (int)_rcache[i].size.width;
        int ph = (int)_rcache[i].size.height;

        if (px < 0) { pw += px; px = 0; }
        if (py < 0) { ph += py; py = 0; }
        if (px + pw > win->width) pw = win->width - px;
        if (py + ph > win->height) ph = win->height - py;
        if (pw <= 0 || ph <= 0) continue;

        for (int row = py; row < py + ph; row++) {
            int by = buf_y(win->height, row);
            if (by < 0 || by >= win->height) continue;
            uint8_t *p = win->pixels + by * win->stride + px * 4;
            for (int col = 0; col < pw; col++) {
                p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
                p += 4;
            }
        }
    }
    win->dirty = 1;
    _rcache_index = 0;
}

void W_FillArea(W_Window window, int x, int y, int width, int height, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    int px, py, pw, ph;
    if (win->type == WIN_GRAPH) {
        px = x; py = y; pw = width; ph = height;
    } else {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
        pw = width * W_Textwidth;
        ph = height * W_Textheight;
    }

    if (px < 0) { pw += px; px = 0; }
    if (py < 0) { ph += py; py = 0; }
    if (px + pw > win->width) pw = win->width - px;
    if (py + ph > win->height) ph = win->height - py;
    if (pw <= 0 || ph <= 0) return;

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    for (int row = py; row < py + ph; row++) {
        int by = buf_y(win->height, row);
        if (by < 0 || by >= win->height) continue;
        uint8_t *p = win->pixels + by * win->stride + px * 4;
        for (int col = 0; col < pw; col++) {
            p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            p += 4;
        }
    }
    win->dirty = 1;
}

/* ========================================================================
 * Text rendering (glyph atlas blit)
 * ======================================================================== */

static void blitGlyph(struct window *win, int px, int py,
                      uint8_t r, uint8_t g, uint8_t b,
                      int font_idx, int ch, int shaded)
{
    if (ch < GLYPH_FIRST || ch > GLYPH_LAST) return;
    if (!glyph_atlas[font_idx]) return;

    int gw = glyph_w[font_idx];
    int gh = glyph_h[font_idx];
    int atlas_w = gw * GLYPH_COUNT;
    int atlas_x = (ch - GLYPH_FIRST) * gw;

    uint8_t *atlas = glyph_atlas[font_idx];

    for (int y = 0; y < gh; y++) {
        int dy = py + y;
        int by = buf_y(win->height, dy);
        if (by < 0 || by >= win->height) continue;
        for (int x = 0; x < gw; x++) {
            int dx = px + x;
            if (dx < 0 || dx >= win->width) continue;

            uint8_t alpha = atlas[y * atlas_w + atlas_x + x];
            uint8_t *dst = win->pixels + by * win->stride + dx * 4;

            if (alpha > 128) {
                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
            } else if (shaded) {
                dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 255;
            }
        }
    }
}

static void AddToScrolling(struct window *win, W_Color color, W_Font font,
                           char *str, int len)
{
    struct scrollingWindow *sw = (struct scrollingWindow *)win->data;
    if (!sw) return;

    struct stringList *new;

    if (sw->lines > 0 && sw->lines > scroll_lines) {
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
    memset(&new->string[len], ' ', MAX_TEXT_WIDTH - len - 1);
    new->string[MAX_TEXT_WIDTH - 1] = '\0';
    new->color = color;
    new->font = font;
}

static void renderTextOnWindow(struct window *win, int px, int py,
                               W_Color color, char *str, int len, W_Font font)
{
    int fi = fontNum(font);
    if (fi < 0 || fi > 3) fi = 1;
    if (!glyph_atlas[fi]) fi = 1;
    if (!glyph_atlas[fi]) return;

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    int gw = glyph_w[fi];

    for (int i = 0; i < len && str[i]; i++) {
        blitGlyph(win, px + i * gw, py, r, g, b, fi, (unsigned char)str[i], 1);
    }
    win->dirty = 1;
}

static void redrawMenuItem(struct window *win, int n)
{
    if (!win->data) return;
    struct menuItem *items = (struct menuItem *)win->data;
    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
    int py = n * item_h + MENU_PAD;
    int content_w = win->width - WIN_EDGE * 2;

    /* Clear the item's text area */
    for (int row = py; row < py + W_Textheight && row < win->height; row++) {
        int by = buf_y(win->height, row);
        if (by < 0 || by >= win->height) continue;
        uint8_t *p = win->pixels + by * win->stride + WIN_EDGE * 4;
        for (int col = 0; col < content_w && (WIN_EDGE + col) < win->width; col++) {
            p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
            p += 4;
        }
    }

    int slen = strlen(items[n].string);
    if (slen > 0) {
        int px = WIN_EDGE + W_Textwidth * items[n].column;
        renderTextOnWindow(win, px, py, items[n].color, items[n].string,
                          slen, W_RegularFont);
    }
}

static void redrawMenu(struct window *win)
{
    if (!win->data || !win->pixels) return;
    int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
    int num_items = win->height / item_h;
    if (num_items <= 0) num_items = 1;

    /* Clear entire menu to black */
    for (int i = 0; i < win->height * win->stride; i += 4) {
        win->pixels[i] = 0;
        win->pixels[i+1] = 0;
        win->pixels[i+2] = 0;
        win->pixels[i+3] = 255;
    }

    /* Draw separator bars (grey) */
    uint8_t gr = colortable[GREY].r;
    uint8_t gg = colortable[GREY].g;
    uint8_t gb = colortable[GREY].b;
    for (int i = 1; i < num_items; i++) {
        int sep_y = i * item_h - MENU_BAR;
        int by = buf_y(win->height, sep_y);
        if (by >= 0 && by < win->height) {
            uint8_t *p = win->pixels + by * win->stride;
            for (int x = 0; x < win->width; x++) {
                p[0] = gr; p[1] = gg; p[2] = gb; p[3] = 255;
                p += 4;
            }
        }
    }

    for (int i = 0; i < num_items; i++) {
        redrawMenuItem(win, i);
    }
    win->dirty = 1;
}

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
    if (!win || !win->pixels || !str || len <= 0) return;

    int px, py;
    switch (win->type) {
    case WIN_GRAPH:
        px = x;
        py = y;
        break;
    case WIN_SCROLL: {
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
    if (!win || !win->pixels || !str || len <= 0) return;

    int fi = fontNum(font);
    if (fi < 0 || fi > 3) fi = 1;
    if (!glyph_atlas[fi]) fi = 1;
    if (!glyph_atlas[fi]) return;

    int px, py;
    if (win->type == WIN_GRAPH) {
        px = x; py = y;
    } else {
        px = x * W_Textwidth + WIN_EDGE;
        py = y * W_Textheight + MENU_PAD;
    }

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    int gw = glyph_w[fi];
    for (int i = 0; i < len && str[i]; i++) {
        /* Non-shaded: only draw foreground pixels, leave background untouched */
        blitGlyph(win, px + i * gw, py, r, g, b, fi, (unsigned char)str[i], 0);
    }
    win->dirty = 1;
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

    /* Store raw XBM bits */
    int bytes = ((width + 7) / 8) * height;
    ic->raw_bits = malloc(bytes);
    if (data && ic->raw_bits) {
        memcpy(ic->raw_bits, data, bytes);
    }

    /* Convert 1-bit XBM data to RGBA (white + alpha) */
    ic->pixels = calloc(width * height * 4, 1);
    if (ic->pixels && data) {
        int stride = (width + 7) / 8;
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                int byte_idx = row * stride + col / 8;
                int bit = (((unsigned char)data[byte_idx]) >> (col % 8)) & 1;
                int offset = (row * width + col) * 4;
                if (bit) {
                    ic->pixels[offset + 0] = 255;
                    ic->pixels[offset + 1] = 255;
                    ic->pixels[offset + 2] = 255;
                    ic->pixels[offset + 3] = 255;
                }
                /* else: stays 0,0,0,0 (transparent) */
            }
        }
    }

    ic->parent_win = W_Void2Window(window);
    return W_Icon2Void(ic);
}

void W_WriteBitmap(int x, int y, W_Icon bit, W_Color color)
{
    struct icon *ic = W_Void2Icon(bit);
    if (!ic || !ic->pixels || !ic->parent_win) return;
    struct window *win = ic->parent_win;
    if (!win->pixels) return;

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    for (int row = 0; row < ic->height; row++) {
        int dy = y + row;
        int by = buf_y(win->height, dy);
        if (by < 0 || by >= win->height) continue;
        for (int col = 0; col < ic->width; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= win->width) continue;

            int src_off = (row * ic->width + col) * 4;
            if (ic->pixels[src_off + 3] > 0) {
                int dst_off = by * win->stride + dx * 4;
                win->pixels[dst_off + 0] = r;
                win->pixels[dst_off + 1] = g;
                win->pixels[dst_off + 2] = b;
                win->pixels[dst_off + 3] = 255;
            }
        }
    }
    win->dirty = 1;
}

void W_OverlayBitmap(int x, int y, W_Icon bit, W_Color color)
{
    /* Same as WriteBitmap but with additive blending - for simplicity,
     * just use normal blit (good enough for COW's use cases) */
    W_WriteBitmap(x, y, bit, color);
}

/* ========================================================================
 * Line drawing
 * ======================================================================== */

/* Bresenham line - draws directly into pixel buffer */
static void drawLine(struct window *win, int x0, int y0, int x1, int y1,
                     uint8_t r, uint8_t g, uint8_t b)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < win->width && y0 >= 0 && y0 < win->height) {
            int by0 = buf_y(win->height, y0);
            int off = by0 * win->stride + x0 * 4;
            win->pixels[off + 0] = r;
            win->pixels[off + 1] = g;
            win->pixels[off + 2] = b;
            win->pixels[off + 3] = 255;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void W_MakeLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);
    drawLine(win, x0, y0, x1, y1, r, g, b);
    win->dirty = 1;
}

void W_CacheLine(W_Window window, int x0, int y0, int x1, int y1, int color)
{
    if (color < 0 || color >= NCOLORS) return;
    int idx = _lcache_index[color];
    if (idx >= MAXCACHE) {
        /* Flush this color's cache */
        struct window *win = W_Void2Window(window);
        if (win && win->pixels) {
            uint8_t r, g, b;
            getColorRGB(color, &r, &g, &b);
            for (int i = 0; i < idx; i++) {
                drawLine(win, _lcache[color][i].x0, _lcache[color][i].y0,
                         _lcache[color][i].x1, _lcache[color][i].y1, r, g, b);
            }
            win->dirty = 1;
        }
        _lcache_index[color] = 0;
        idx = 0;
    }
    _lcache[color][idx].x0 = x0;
    _lcache[color][idx].y0 = y0;
    _lcache[color][idx].x1 = x1;
    _lcache[color][idx].y1 = y1;
    _lcache_index[color] = idx + 1;
}

void W_FlushLineCaches(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    for (int c = 0; c < NCOLORS; c++) {
        int idx = _lcache_index[c];
        if (idx > 0) {
            uint8_t r, g, b;
            getColorRGB(c, &r, &g, &b);
            for (int i = 0; i < idx; i++) {
                drawLine(win, _lcache[c][i].x0, _lcache[c][i].y0,
                         _lcache[c][i].x1, _lcache[c][i].y1, r, g, b);
            }
            _lcache_index[c] = 0;
        }
    }
    win->dirty = 1;
}

void W_MakeTractLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1.0) return;

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
            drawLine(win, sx, sy, ex, ey, r, g, b);
        }
        t = end;
        dash_on = !dash_on;
    }
    win->dirty = 1;
}

void W_MakePhaserLine(W_Window window, int x0, int y0, int x1, int y1, W_Color color)
{
    W_MakeLine(window, x0, y0, x1, y1, color);
}

/* ========================================================================
 * Shape drawing
 * ======================================================================== */

void W_WriteCircle(W_Window window, int x, int y, int r, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    uint8_t cr, cg, cb;
    getColorRGB(color, &cr, &cg, &cb);

    /* Midpoint circle algorithm */
    int cx = 0, cy = r;
    int d = 1 - r;

    while (cx <= cy) {
        int pts[][2] = {
            {x + cx, y + cy}, {x - cx, y + cy},
            {x + cx, y - cy}, {x - cx, y - cy},
            {x + cy, y + cx}, {x - cy, y + cx},
            {x + cy, y - cx}, {x - cy, y - cx}
        };
        for (int i = 0; i < 8; i++) {
            int px = pts[i][0], py = pts[i][1];
            if (px >= 0 && px < win->width && py >= 0 && py < win->height) {
                int by = buf_y(win->height, py);
                int off = by * win->stride + px * 4;
                win->pixels[off + 0] = cr;
                win->pixels[off + 1] = cg;
                win->pixels[off + 2] = cb;
                win->pixels[off + 3] = 255;
            }
        }
        if (d < 0) {
            d += 2 * cx + 3;
        } else {
            d += 2 * (cx - cy) + 5;
            cy--;
        }
        cx++;
    }
    win->dirty = 1;
}

void W_WriteTriangle(W_Window window, int x, int y, int s, int t, W_Color color)
{
    struct window *win = W_Void2Window(window);
    if (!win || !win->pixels) return;

    uint8_t r, g, b;
    getColorRGB(color, &r, &g, &b);

    int x0, y0, x1, y1, x2, y2;
    if (t == 0) {
        /* Pointing up */
        x0 = x;     y0 = y - s;
        x1 = x - s; y1 = y + s;
        x2 = x + s; y2 = y + s;
    } else {
        /* Pointing down */
        x0 = x;     y0 = y + s;
        x1 = x - s; y1 = y - s;
        x2 = x + s; y2 = y - s;
    }
    drawLine(win, x0, y0, x1, y1, r, g, b);
    drawLine(win, x1, y1, x2, y2, r, g, b);
    drawLine(win, x2, y2, x0, y0, r, g, b);
    win->dirty = 1;
}

/* ========================================================================
 * Event handling - events injected from Swift
 * ======================================================================== */

void swiftui_inject_key(int window_id, unsigned char key, int x, int y,
                        int type, int modifier)
{
    W_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key = key;
    ev.x = x;
    ev.y = y;
#ifdef MOUSE_AS_SHIFT
    ev.modifier = modifier;
#endif

    if (window_id >= 0 && window_id < swui_num_windows) {
        ev.Window = W_Window2Void(&swui_windows[window_id]);
    } else {
        /* Find window at coordinates */
        struct window *win = findWindowAt(x, y);
        if (win) {
            ev.Window = W_Window2Void(win);
            ev.x = x - win->x;
            ev.y = y - win->y;
        }
    }

    evq_push(&ev);

    /* Wake select() */
    char dummy = 'e';
    write(socket_pipe[1], &dummy, 1);
}

void swiftui_inject_button(int window_id, int button, int x, int y,
                           int modifier)
{
    W_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = W_EV_BUTTON;
    ev.key = button;

    struct window *win = NULL;
    if (window_id >= 0 && window_id < swui_num_windows) {
        win = &swui_windows[window_id];
        ev.Window = W_Window2Void(win);
        ev.x = x;  /* already local (view-relative) from Swift */
        ev.y = y;
    } else {
        win = findWindowAt(x, y);
        if (!win) return;
        ev.Window = W_Window2Void(win);
        ev.x = x - win->x;
        ev.y = y - win->y;
    }

    if (!win) return;

    /* For menu windows, translate Y to item index */
    if (win->type == WIN_MENU) {
        int item_h = W_Textheight + MENU_PAD * 2 + MENU_BAR;
        ev.y = ev.y / item_h;
    }

#ifdef SHIFTED_MOUSE
    ev.key |= (modifier & (W_SHIFT_BUTTON | W_CTRL_BUTTON));
#endif

    evq_push(&ev);
    char dummy = 'e';
    write(socket_pipe[1], &dummy, 1);
}

void swiftui_inject_expose(int window_id)
{
    if (window_id < 0 || window_id >= swui_num_windows) return;

    W_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = W_EV_EXPOSE;
    ev.Window = W_Window2Void(&swui_windows[window_id]);
    evq_push(&ev);

    char dummy = 'e';
    write(socket_pipe[1], &dummy, 1);
}

int W_EventsPending(void)
{
    /* Auto-flush for loops that never call W_Flush */
    static int in_flush = 0;
    if (!in_flush) {
        in_flush = 1;
        W_Flush();
        in_flush = 0;
    }

    return evq_has_events();
}

void W_NextEvent(W_Event *wevent)
{
    while (!evq_pop(wevent)) {
        /* Wait for events - sleep briefly then check again */
        usleep(1000); /* 1ms */
    }

    /* Drain the pipe */
    char dummy;
    while (read(socket_pipe[0], &dummy, 1) > 0) {}
}

int W_SpNextEvent(W_Event *wevent)
{
    if (evq_pop(wevent)) {
        char dummy;
        while (read(socket_pipe[0], &dummy, 1) > 0) {}
        return 1;
    }
    return 0;
}

int W_EventsQueued(void)
{
    return evq_has_events();
}

int W_EventsQueuedCk(void)
{
    return W_EventsPending();
}

/* Pump events - called from swiftui_select() wrapper */
void swiftui_pump_events(void)
{
    /* Events are pushed by Swift via swiftui_inject_*.
     * Nothing to pump here (unlike SDL2 which needs SDL_PollEvent).
     * Just wake select() if events are queued. */
    if (evq_has_events()) {
        char dummy = 'e';
        write(socket_pipe[1], &dummy, 1);
    }
}

int W_Socket(void)
{
    swiftui_pump_events();
    return socket_pipe[0];
}

/* ========================================================================
 * Compositor - W_Flush
 * ======================================================================== */

void W_Flush(void)
{
    static int flush_count = 0;
    flush_count++;

    int nw = swui_num_windows;
    if (nw < 0 || nw > MAX_WINDOWS) {
        fprintf(stderr, "[swiftui] W_Flush: BAD swui_num_windows=%d (0x%x) at %p\n"
                "  canary1=%08x (expect CAFEBABE), canary2=%08x (expect DEADBEEF)\n",
                nw, nw, (void *)&swui_num_windows,
                _swui_canary1, _swui_canary2);
        nw = 0;
    }

    if (flush_count == 1 || flush_count % 600 == 0) {
        int mapped_count = 0, dirty_count = 0;
        for (int i = 0; i < nw; i++) {
            if (swui_windows[i].mapped) mapped_count++;
            if (swui_windows[i].dirty) dirty_count++;
        }
        fprintf(stderr, "[swiftui] W_Flush #%d: %d windows, %d mapped, %d dirty, version=%llu\n",
                flush_count, nw, mapped_count, dirty_count, (unsigned long long)snapshot_version);
    }

    pthread_mutex_lock(&snapshot_mutex);

    for (int i = 0; i < nw; i++) {
        struct window *win = &swui_windows[i];

        snapshot_mapped[i] = win->mapped;
        snapshot_positions[i][0] = win->x;
        snapshot_positions[i][1] = win->y;
        snapshot_positions[i][2] = win->width;
        snapshot_positions[i][3] = win->height;

        if (win->mapped && win->dirty && win->context) {
            /* Release old snapshot */
            if (snapshots[i]) {
                CGImageRelease(snapshots[i]);
                snapshots[i] = NULL;
            }

            /* Create new CGImage from bitmap context */
            snapshots[i] = CGBitmapContextCreateImage(win->context);
            win->dirty = 0;
        } else if (!win->mapped && snapshots[i]) {
            CGImageRelease(snapshots[i]);
            snapshots[i] = NULL;
        }
    }
    snapshot_count = nw;
    snapshot_version++;

    pthread_mutex_unlock(&snapshot_mutex);

    /* Notify SwiftUI to refresh display */
    swiftui_notify_flush();
}

void W_ProbeLatency(W_Window w)
{
    /* No-op */
}

/* ========================================================================
 * Window tiling / background
 * ======================================================================== */

void W_TileWindow(W_Window window, W_Icon bit)
{
    struct window *win = W_Void2Window(window);
    struct icon *ic = W_Void2Icon(bit);
    if (!win || !ic || !ic->pixels) return;

    /* Create background by tiling the icon */
    if (win->bg_pixels) free(win->bg_pixels);
    win->bg_pixels = calloc(win->height * win->stride, 1);
    if (!win->bg_pixels) return;

    /* Set all alpha to 255 first */
    for (int i = 3; i < win->height * win->stride; i += 4)
        win->bg_pixels[i] = 255;

    /* Tile the icon pattern */
    for (int ty = 0; ty < win->height; ty += ic->height) {
        for (int tx = 0; tx < win->width; tx += ic->width) {
            for (int row = 0; row < ic->height && (ty + row) < win->height; row++) {
                for (int col = 0; col < ic->width && (tx + col) < win->width; col++) {
                    int src_off = (row * ic->width + col) * 4;
                    if (ic->pixels[src_off + 3] > 0) {
                        int by = buf_y(win->height, ty + row);
                        int dst_off = by * win->stride + (tx + col) * 4;
                        win->bg_pixels[dst_off + 0] = ic->pixels[src_off + 0];
                        win->bg_pixels[dst_off + 1] = ic->pixels[src_off + 1];
                        win->bg_pixels[dst_off + 2] = ic->pixels[src_off + 2];
                        win->bg_pixels[dst_off + 3] = 255;
                    }
                }
            }
        }
    }
}

void W_UnTileWindow(W_Window window)
{
    struct window *win = W_Void2Window(window);
    if (!win) return;
    if (win->bg_pixels) {
        free(win->bg_pixels);
        win->bg_pixels = NULL;
    }
}

void W_SetBackgroundPixmap(W_Window window, unsigned long background) {}
void W_UnsetBackgroundPixmap(W_Window window) { W_UnTileWindow(window); }

/* ========================================================================
 * Cursor definitions (stubs - SwiftUI handles cursors)
 * ======================================================================== */

void W_DefineCursor(W_Window window, int width, int height, char *bits,
                    char *mask, int xhot, int yhot) {}
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

    if (win->context) { CGContextRelease(win->context); win->context = NULL; }
    if (win->pixels) { free(win->pixels); win->pixels = NULL; }
    if (win->bg_pixels) { free(win->bg_pixels); win->bg_pixels = NULL; }
    if (win->name) { free(win->name); win->name = NULL; }

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
            free(win->data);
        }
        win->data = NULL;
    }
    win->mapped = 0;
}

void W_SetIconWindow(W_Window main, W_Window icon) {}

void W_Beep(void)
{
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

    if (win->context) CGContextRelease(win->context);
    if (win->pixels) free(win->pixels);
    win->context = NULL;
    win->pixels = NULL;

    win->width = neww;
    win->height = newh;
    createWindowContext(win);
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

    if (win->data) free(win->data);

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

    /* Clear to black */
    for (int i = 0; i < win->height * win->stride; i += 4) {
        win->pixels[i] = 0;
        win->pixels[i+1] = 0;
        win->pixels[i+2] = 0;
        win->pixels[i+3] = 255;
    }

    struct stringList *item = sw->index;
    int y = vis_height - 1;

    for (int i = 0; i < sw->topline && item; i++) {
        item = item->next;
    }

    for (; item && y >= 0; item = item->next, y--) {
        int py = MENU_PAD + y * W_Textheight;
        int slen = strlen(item->string);
        if (slen > 0) {
            renderTextOnWindow(win, WIN_EDGE, py, item->color,
                             item->string, slen, item->font);
        }
    }

    sw->updated = 0;
    win->dirty = 1;
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
 * TTS functions
 * ======================================================================== */

int W_TTSTextHeight(void) { return 20; }

int W_TTSTextWidth(char *s, int l)
{
    return l * W_Textwidth;
}

void W_EraseTTSText(W_Window window, int max_width, int y, int width)
{
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
    W_WriteCircle(mapw, x, y, 8, color);
}

/* ========================================================================
 * Fullscreen support
 * ======================================================================== */

void W_FullScreenOn(W_Window window) { full_screen_enabled = 1; }
void W_FullScreenOff(W_Window window) { full_screen_enabled = 0; }
void W_FullScreenInitialise(void) { full_screen_default = 0; }
int W_FullScreenToggle(W_Window window)
{
    full_screen_enabled = !full_screen_enabled;
    return FULLSCREEN_OK;
}
void W_FullScreenBegin(W_Window window)
{
    if (full_screen_default) W_FullScreenOn(window);
}
void W_FullScreen(W_Window window) {}

/* ========================================================================
 * Camera / screenshot
 * ======================================================================== */

void W_CameraSnap(W_Window window)
{
    /* TODO: save screenshot via CGImage */
}

/* ========================================================================
 * Pixmaps / sprites - delegates to swiftuisprite.c
 * ======================================================================== */

extern void GetPixmaps_SwiftUI(W_Window t, W_Window g);

void W_GetPixmaps(W_Window t, W_Window g)
{
    GetPixmaps_SwiftUI(t, g);
}

/* Background image stubs */
void W_GalacticBgd(int which) {}
void W_LocalBgd(int which) {}
void W_SetBackground(W_Window w, int which) {}
void *W_SetBackgroundImage(W_Window w, char *name) { return NULL; }
void W_NextScreenShot(W_Window w, int x, int y) {}
void W_DrawScreenShot(W_Window w, int x, int y) {}
void *W_ReadImage(W_Window w, char *name) { return NULL; }
void W_DrawImage(int x, int y, void *sprite_v)
{
    if (!sprite_v) return;
    W_DrawSpriteAbsolute(sprite_v, x, y);
}

void W_DropImage(void *sprite_v)
{
    if (sprite_v) {
        struct S_Object *sprite = (struct S_Object *)sprite_v;
        free(sprite->pixels);
        free(sprite);
    }
}

/* ========================================================================
 * Utility functions referenced by COW code
 * ======================================================================== */

void checkParent(char *name, W_Window *parent) {}
int checkMapped(char *name) { return 0; }
int checkMappedPref(char *name, int preferred) { return preferred; }

void findMouse(int *x, int *y)
{
    /* SwiftUI will track mouse position - return center for now */
    *x = 500;
    *y = 500;
}

int findMouseInWin(int *x, int *y, W_Window w)
{
    /* TODO: get actual mouse position from SwiftUI */
    *x = 250;
    *y = 250;
    return 1;
}

void pastebuffer(void) {}

void W_Deinitialize(void)
{
    /* Skip cleanup — COW BSS overflow corrupts global pointers (glyph_atlas,
     * window names, etc.) making free() crash.  The OS reclaims all memory
     * on process exit anyway. */
    if (socket_pipe[0] >= 0) close(socket_pipe[0]);
    if (socket_pipe[1] >= 0) close(socket_pipe[1]);
}

/* ========================================================================
 * Query functions for Swift
 * ======================================================================== */

int swiftui_get_swui_num_windows(void)
{
    return swui_num_windows;
}

void swiftui_get_window_info(int window_id, int *x, int *y,
                             int *width, int *height, int *mapped)
{
    if (window_id < 0 || window_id >= swui_num_windows) {
        *x = *y = *width = *height = *mapped = 0;
        return;
    }
    *x = swui_windows[window_id].x;
    *y = swui_windows[window_id].y;
    *width = swui_windows[window_id].width;
    *height = swui_windows[window_id].height;
    *mapped = swui_windows[window_id].mapped;
}

int swiftui_find_window_at(int x, int y)
{
    struct window *win = findWindowAt(x, y);
    return win ? win->id : -1;
}

/* ========================================================================
 * Engine thread entry point
 * ======================================================================== */

/* cowmain() signature: server hostname (or NULL), port, program name */
extern int cowmain(char *server, int port, char *name);

/* External variables set by main.c's argv parsing - we set them directly */
extern char *display_host;
extern char *deffile;

struct engine_args {
    char *server;
    int port;
    char *name;
};

static void *engine_thread_func(void *arg)
{
    struct engine_args *ea = (struct engine_args *)arg;
    char *server = ea->server;
    int port = ea->port;
    char *name = ea->name;
    free(ea);

    /* Set working directory to the source tree so pixmaps symlink is found.
     * SRCROOT_PATH is set by the Xcode scheme; fall back to HOME. */
    char *srcroot = getenv("SRCROOT_PATH");
    if (srcroot && srcroot[0]) {
        chdir(srcroot);
        fprintf(stderr, "[swiftui] CWD set to %s (SRCROOT)\n", srcroot);
    } else {
        char *home = getenv("HOME");
        if (home) {
            chdir(home);
            fprintf(stderr, "[swiftui] CWD set to %s (HOME fallback)\n", home);
        }
    }

    cowmain(server, port, name);
    return NULL;
}

void engine_start(int argc, char **argv)
{
    /* Parse argv the same way main.c does, then call cowmain(server, port, name) */
    char *server = NULL;
    int port = -1;
    char *name = "netrek-swiftui";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            server = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            deffile = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            display_host = argv[++i];
        }
    }

    fprintf(stderr, "[swiftui] engine_start: server=%s port=%d\n",
            server ? server : "(null)", port);

    struct engine_args *ea = malloc(sizeof(struct engine_args));
    ea->server = server;
    ea->port = port;
    ea->name = name;

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&thread, &attr, engine_thread_func, ea);
    pthread_attr_destroy(&attr);
}
