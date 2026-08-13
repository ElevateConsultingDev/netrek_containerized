/* sdl2_config.h - Hand-crafted config.h for macOS/arm64 SDL2 build
 *
 * Replaces the autoconf-generated config.h for building COW with SDL2
 * instead of X11.
 */

#ifndef __CONFIG_H
#define __CONFIG_H

/*
 * Feature defines (same as upstream COW)
 */
/* SDL2 backend uses the deferred AddToScrolling + W_FlushScrollingWindow model
 * (like the WIN32 backend), so shared code must flush scrolling windows each
 * frame. Gated in redraw.c alongside WIN32. */
#define SDL2BACKEND
#define RCD
#define PING
#define SHORT_PACKETS
#define SPEEDUP
#define FEATURE_PACKETS
#define UDP_PORTSWAP
#undef  DEBUG
#define NBT
#define MAXPLAYER 36
#define CORRUPTED_PACKETS
#define ARMY_SLIDER
#define META
#define PACKET_LOG
#define NEWMACRO
#define SMARTMACRO
#define MULTILINE_MACROS
#define XTREKRC_HELP
#define TOOLS
#define SOUND
#define HOCKEY_LINES
#define BEEPLITE
#define VSHIELD_BITMAPS
#define VARY_HULL
#undef  WARP_DEAD
#define RCM
#define RACE_COLORS
#define XTRA_MESSAGE_UI
#define SYSTEM_DEFAULTFILE "/etc/netrek-client-cow/netrekrc"
#define PLIST1
#define PLIST
#define CONTROL_KEY
#define DOC_WIN
#define ROTATERACE
#define FUNCTION_KEYS
#define IGNORE_SIGNALS_SEGV_BUS
#define MOUSE_AS_SHIFT
#define SHIFTED_MOUSE
#define TNG_FED_BITMAPS
#define MOTION_MOUSE
#define DIST_KEY_NAME
#define PHASER_STATS
#define RECORDGAME
#define CAMERA
#define FULLSCREEN

/*
 * System defines for macOS/arm64
 */
#define NEED_EXIT
#define HAVE_MATH_H 1
#define HAVE_STDLIB_H 1
#define POSIX_SIGNALS 1
#define STDC_HEADERS 1
#define HAVE_WAIT3 1
#undef  NEED_SYS_SELECT_H
#undef  NO_FD_SET
#define HAVE_UNISTD_H 1
#undef  HAVE_SYS_TIMEB_H
#undef  HAVE_FTIME
#define TIME_WITH_SYS_TIME 1
#undef  NEED_SYS_TIME_H
#undef  HAVE_SYS_PTYIO_H
#define HAVE_FCNTL_H 1
#undef  HAVE_SYS_FCNTL_H
#define HAVE_CTYPE_H 1
#define HAVE_MACHINE_ENDIAN_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#undef  HAVE_SYS_FILIO_H
#undef  HAVE_GMP2_H
#undef  NO_U_INT
#define SIZEOF_LONG 8
#define HAVE_USLEEP 1
#define HAVE_SETSTATE 1
#define HAVE_RANDOM 1
#define HAVE_STRFTIME 1
#undef  HAVE_STRCMPI
#undef  HAVE_STRNCMPI
#undef  HAVE_NINT
#undef  NEED_RINT_DEC
#undef  NO_PATH_MAX

/* SDL2 backend - not X11 or Win32 */
#undef  HAVE_X11
#undef  HAVE_WIN32
#undef  HAVE_XPM
#undef  HAVE_X11_XPM_H
#define HAVE_SDL 1

/*
 * System-dependent macros (from config.h.in template)
 */

/* POSIX signal handling */
#define HANDLE_SIG(s,h) signal(s,h)
/* Note: COW's POSIX_SIGNALS redefines setjmp/longjmp to sigsetjmp/siglongjmp.
 * On modern macOS, setjmp already saves the signal mask, so we leave
 * setjmp/longjmp as-is to avoid conflicts with <setjmp.h>. */

#define SIGNAL(x,y) signal(x,y)
#define PAUSE(x) pause()
#define SIGSETMASK(x) sigsetmask(x)

#define INC_STRINGS <string.h>
#define MCMP(x,y,z) memcmp(x,y,z)
#define MCOPY(b1,b2,l) memcpy(b2,b1,l)
#define MZERO(b1,l) memset(b1,0,l)
#define INDEX(s,c) strchr(s,c)
#define RINDEX(s,c) strrchr(s,c)

#ifdef SPEEDUP
#include <string.h>
#define STRNCPY(x,y,z) memccpy(x,y,'\0',z)
#else
#define STRNCPY(x,y,z) strncpy(x,y,z)
#endif

#define WAIT3(x,y,z) wait3(x, y, z)
#define RANDOM() random()
#define SRANDOM(x) srandom(x)

/* 64-bit long on arm64 */
#define _64BIT
#define LONG int
#define U_LONG u_int

/* System include macros */
#define NULLFILE "null"

#undef NEED_SYS_SELECT_H
#define INC_SYS_SELECT NULLFILE
/* SDL2 wrapper: cap select() timeouts and pump events before blocking.
 * On X11, user input arrives on the socket FD and wakes select immediately.
 * With SDL2, events are queued internally and only visible after
 * SDL_PollEvent(), so we must pump before every select and keep timeouts
 * short to stay responsive.
 *
 * Also auto-flush: in X11, draw commands go to the display server and
 * become visible immediately.  In SDL2, nothing appears until
 * SDL_RenderPresent (W_Flush).  Some COW loops (entrywindow, findslot)
 * never call W_Flush, so we do it here at ~60 Hz. */
#include <sys/select.h>
#include <sys/time.h>
extern void sdl2_pump_events(void);  /* defined in sdl2window.c */
static inline int sdl2_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
                               struct timeval *tv) {
    sdl2_pump_events();
    struct timeval cap = {0, 16000}; /* 16ms ~ 60Hz */
    if (!tv || tv->tv_sec > 0 || tv->tv_usec > cap.tv_usec)
        tv = &cap;
    return select(nfds, r, w, e, tv);
}
#define SELECT sdl2_select

#define INC_UNISTD <unistd.h>
#define INC_SYS_SIGNAL NULLFILE
#define INC_SYS_FCNTL NULLFILE
#define INC_FCNTL <fcntl.h>
#define INC_SYS_TIME <sys/time.h>
#define INC_SYS_PTYIO NULLFILE
#define INC_CTYPE <ctype.h>
#define INC_MACHINE_ENDIAN <machine/endian.h>
#define INC_SYS_RESOURCE <sys/resource.h>
#define INC_SYS_WAIT <sys/wait.h>
#define INC_NETINET_IN <netinet/in.h>
#define INC_NETINET_TCP <netinet/tcp.h>
#define INC_SYS_FILIO NULLFILE
#define INC_LIMITS <limits.h>
#define INC_XPM NULLFILE
#define INC_STDLIB <stdlib.h>
#define INC_IO NULLFILE

/* ftime replaced by gettimeofday */
#define NOTIMEB
#define INC_SYS_TIMEB <sys/time.h>

/* Math */
#define INC_MATH <math.h>
#define nint(x) ((int) rint(x))

/* macOS uses 0 for SHMFLAG */
#define SHMFLAG 0

/* strcmpi/strncmpi: COW provides its own in defaults.c.
 * Do NOT define macros here - it would conflict with the
 * function declarations in defaults.h. */

#endif /* __CONFIG_H */
