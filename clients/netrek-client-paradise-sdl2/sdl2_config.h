/* sdl2_config.h - Hand-crafted config.h for macOS/arm64 SDL2 build
 *
 * Replaces the autoconf-generated config.h for building Paradise with SDL2
 * instead of X11.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Header defines */
#define HAVE_FCNTL_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_PWD_H 1
#undef  HAVE_STRINGS_H
#undef  HAVE_BSTRING_H
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_NETDB_H 1
#undef  HAVE_SYS_SELECT_H
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_ARPA_INET_H 1

/* No XPM - we use SDL2_image instead */
#undef HAVE_XPM_H
#undef HAVE_X11_XPM_H

/* time.h */
#undef TM_IN_SYS_TIME
#define TIME_WITH_SYS_TIME 1
#undef NEED_MEMORY_H

/* zlib */
#define HAVE_ZLIB_H 1

/* char signedness */
#undef __CHAR_UNSIGNED__

/* wait3 */
#define HAVE_WAIT3 1

/* STDC */
#define STDC_HEADERS 1

/* signal return type */
#define RETSIGTYPE void

/* standard functions */
#define HAVE_GETTIMEOFDAY 1
#define HAVE_STRDUP 1
#define HAVE_STRSTR 1
#define HAVE_UNAME 1

/* Prototype macro */
#ifdef STDC_HEADERS
#define P(x) x
#else
#define P(x) ()
#endif

#ifndef HAVE_GETTIMEOFDAY
struct timeval {
  long tv_sec;
  long tv_usec;
};
struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};
#endif

#ifndef HAVE_UNAME
#define SYS_NMLN 256
struct utsname {
  char sysname[SYS_NMLN];
  char release[SYS_NMLN];
};
#endif

/* Sound support - Paradise uses fork/exec of sound server */
#define UNIX_SOUND

/* Image path - override with command line or rc file */
#ifndef SHIP_BITMAP_PATH
#define SHIP_BITMAP_PATH "images"
#endif

/* SYS_NMLN - macOS doesn't define this but Paradise uses it in dmessage.c */
#ifndef SYS_NMLN
#define SYS_NMLN 256
#endif

/* SDL2 backend marker */
#define HAVE_SDL2 1

/* SDL2 select() wrapper - pump events before blocking.
 * Paradise uses select() on W_Socket() FD and network socket.
 * SDL2 events are queued internally, so we must pump before select. */
#include <sys/select.h>
#include <sys/time.h>
extern void sdl2_pump_events(void);
static inline int sdl2_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
                               struct timeval *tv) {
    sdl2_pump_events();
    struct timeval cap = {0, 16000}; /* 16ms ~ 60Hz */
    if (!tv || tv->tv_sec > 0 || tv->tv_usec > cap.tv_usec)
        tv = &cap;
    return select(nfds, r, w, e, tv);
}
#define SELECT sdl2_select

#endif /* CONFIG_H */
