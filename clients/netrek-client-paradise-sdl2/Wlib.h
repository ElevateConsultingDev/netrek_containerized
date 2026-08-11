/* Wlib.h - SDL2 replacement for Paradise's X11-based Wlib.h
 *
 * Same API contract as the original, but without X11 dependencies.
 * The W_Image struct uses void* instead of Pixmap for backend storage.
 */

#ifndef WLIB_H
#define WLIB_H

#include "config.h"

/* X11 compatibility: proto.h references these X11 types in newWindow() prototype.
 * We provide dummy typedefs so the code compiles without X11 headers. */
typedef unsigned long Window;
typedef unsigned long Pixmap;
typedef unsigned long Cursor;
typedef void *Display;
typedef void *GC;

/* Image struct - replaces X11 Pixmap with opaque pointers.
 * Only x11window.c (now sdl2window.c) touches the private fields. */
typedef struct {
  /* public */
  unsigned int width, height, frames;
  int xpm;
  char *filename;
  int loaded;
  int alternate;
  int bad;
  int compiled_in;
  unsigned char *xbmdata;
  char **xpmdata;
  /* SDL2 private data (replaces X11 Pixmap) */
  void *pixmap;        /* SDL_Texture* for the image */
  void *clipmask;      /* SDL_Texture* for the clip mask (unused in SDL2) */
} W_Image;

typedef char *W_Icon;
typedef char *W_Font;
typedef int W_Color;

typedef int (*W_Callback) ();
typedef char *W_Window;

extern W_Font W_BigFont, W_RegularFont, W_UnderlineFont, W_HighlightFont;
extern W_Color W_White, W_Black, W_Red, W_Green, W_Yellow, W_Cyan, W_Grey;
extern int W_Textwidth, W_Textheight;
extern int W_FastClear;

#define W_EV_EXPOSE	1
#define W_EV_KEY	2
#define W_EV_BUTTON	3
#define W_EV_KILL_WINDOW 4

#define W_LBUTTON	1
#define W_MBUTTON	2
#define W_RBUTTON	3

typedef struct event {
    int     type;
    W_Window Window;
    int     key;
    int     x, y;
} W_Event;

#define W_BoldFont W_HighlightFont

#define W_StringWidth(st,font) (strlen(st)*W_Textwidth)

#endif /* WLIB_H */
