/* config.h - macOS/SDL2 configuration for NetrekXP port
 *
 * Replaces win32/config.h for building NetrekXP with SDL2 on POSIX.
 */

#ifndef __CONFIG_H
#define __CONFIG_H

/*
################################################################################
        Compilation specific defines
################################################################################
*/

#define CBUGS "Send bugs/suggestions to: billbalcerski@gmail.com"

/*
################################################################################
        Feature defines (from win32/config.h)
################################################################################
*/

/* THREADED - disabled for SDL2/macOS port.
 * SDL2 on macOS requires all rendering + event pumping on the main thread.
 * Without THREADED, input() does select+events+rendering in one loop. */
#undef THREADED

#define CORRUPTED_PACKETS
#define META
#define METAPING
#define PACKET_LOG
#define MULTILINE_MACROS
#undef XTREKRC_HELP
#undef TOOLS
#define SOUND
#define HOCKEY_LINES
#define BEEPLITE
#undef  WARP_DEAD
#define PARADISE
#define RCM

#define PLIST1
#define PLIST
#define DOC_WIN
#define ROTATERACE

/* client options */
#define MOUSE_AS_SHIFT
#define SHIFTED_MOUSE
#define MOTION_MOUSE
#define XTRA_MESSAGE_UI
#define PHASER_STATS

#define RECORDGAME
#define REVERSE_PLAYBACK

/*
##############################################################################
       POSIX system defines (replacing Windows-specific ones)
##############################################################################
*/

#define NEED_EXIT
#define STDC_HEADERS 1
#define RETSIGTYPE void
#undef pid_t
#undef uid_t
#undef gid_t
#undef size_t
#undef vfork

/* Don't redefine inline on modern compilers */
/* #define inline */

#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define nint(x) ((int)(x))

#define R_OK 4

/* Signal handling */
#define SIGNAL(x,y) signal(x,y)

/* Memory/string macros */
#define MCMP(x,y,z)     memcmp(x,y,z)
#define MCOPY(b1,b2,l)  memcpy(b2,b1,l)
#define MZERO(b1,l)     memset(b1,0,l)
#define INDEX(s,c)       strchr(s,c)
#define RINDEX(s,c)      strrchr(s,c)

#ifdef SPEEDUP
#include <string.h>
#define STRNCPY(x,y,z)  memccpy(x,y,'\0',z)
#else
#define STRNCPY(x,y,z)  strncpy(x,y,z)
#endif

/* POSIX random */
#define RANDOM()    random()
#define SRANDOM(x)  srandom(x)

/* Types */
#define LONG int
#define U_LONG unsigned int

/* BSD types used throughout NetrekXP */
#include <sys/types.h>
#ifndef __u_char_defined
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
#define __u_char_defined
#endif

#define VOID void
typedef char CHAR;
typedef short SHORT;

/* WIN32 compat - we are NOT on Win32 but some code checks this */
#undef WIN32

/* Windows NetrekXP calls getpwuid() with no args.
 * Proto.h declares 'struct passwd *getpwuid()' which conflicts with POSIX.
 * We rename it to xp_getpwuid via macro so the proto.h declaration doesn't conflict. */
#include <unistd.h>
#include <pwd.h>
#define getpwuid xp_getpwuid

/* POSIX replacements for Windows string functions */
#include <strings.h>
#define strncasecmp strncasecmp  /* already available on POSIX */
#define stricmp strcasecmp
#define strcmpi strcasecmp

/* POSIX functions that Windows renames */
#define _strdup strdup
#define _strcmpi strcasecmp
#define _memccpy memccpy
#define _stricmp strcasecmp
#define _getpid getpid
#define _read read
#define _close close
#define _access access
#define _unlink unlink

/* POSIX sleep (Windows has Sleep in milliseconds) */
#define Sleep(ms) usleep((ms) * 1000)

/* Proto.h declares void sleep(int) which conflicts with POSIX unsigned sleep(unsigned).
 * We define sleep as a macro so proto.h's declaration becomes a macro expansion
 * (not a conflicting function declaration). The actual sleep() calls still work
 * because the macro just expands to xp_sleep which we define. */
#include <unistd.h>
static inline void xp_sleep(int s) { usleep((unsigned)s * 1000000); }
#define sleep xp_sleep

/* Semaphore stubs for THREADED mode */
#ifdef THREADED
typedef unsigned long int SEMAPHORE;
/* These use pipe-based signaling in our port */
#define CREATE_SEMAPHORE(initstate) 0
#define SET_SEMAPHORE(sem) do { (void)(sem); } while(0)
#define RESET_SEMAPHORE(sem) do { (void)(sem); } while(0)
#endif

/* select() wrapper - pump SDL events and cap timeout for responsiveness.
 * SDL2/Cocoa requires event pumping on the main thread only.
 * NetrekXP's THREADED mode calls select() from a worker thread too. */
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h>
extern void sdl2_pump_events(void);
extern pthread_t sdl2_main_thread;
static inline int xp_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
                              struct timeval *tv) {
    if (pthread_equal(pthread_self(), sdl2_main_thread))
        sdl2_pump_events();
    struct timeval cap = {0, 16000}; /* 16ms ~ 60Hz */
    if (!tv || tv->tv_sec > 0 || tv->tv_usec > cap.tv_usec)
        tv = &cap;
    return select(nfds, r, w, e, tv);
}
/* Override select in NetrekXP source */
#define select xp_select

/* Forward-declare struct motd_line so proto.h's showValues() declaration
 * doesn't create a conflicting incomplete type (the struct is defined in newwin.c) */
struct motd_line;

#endif /* __CONFIG_H */
