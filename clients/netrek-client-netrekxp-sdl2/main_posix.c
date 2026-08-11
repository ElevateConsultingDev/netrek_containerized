/* main_posix.c - POSIX entry point for NetrekXP SDL2 port
 *
 * Replaces winmain.c: no Winsock init needed, no WinMain,
 * uses POSIX getpwuid() and standard C main().
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <math.h>

#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "proto.h"

/* Globals that winmain.c provides */
char ExeDir[100];
char HomeDir[100];

char *
GetExeDir(void)
{
    return ExeDir;
}

/* xp_getpwuid: replacement for Windows getpwuid() which took no args.
 * The macro in config.h renames all getpwuid references to xp_getpwuid.
 * We call the real POSIX getpwuid(getuid()). */
#undef getpwuid  /* temporarily restore real name */
struct passwd *
xp_getpwuid(void)
{
    return getpwuid(getuid());
}
#define getpwuid xp_getpwuid  /* restore macro */

/* sleep - on POSIX this is already available */

/* rint - on POSIX this is already in math.h */

/* perror - use standard C version */

/* Our POSIX main entry point */
int
main(int argc, char *argv[])
{
    char *home;

    /* Get HOME directory */
    home = getenv("HOME");
    if (home) {
        snprintf(HomeDir, sizeof(HomeDir), "%s", home);
    }

    /* Get executable directory from argv[0] or cwd */
    if (argv[0] && argv[0][0] == '/') {
        strncpy(ExeDir, argv[0], sizeof(ExeDir) - 1);
        /* Remove filename to get directory */
        char *last_slash = strrchr(ExeDir, '/');
        if (last_slash) *last_slash = '\0';
    } else {
        getcwd(ExeDir, sizeof(ExeDir));
    }

    /* Call the NetrekXP main2 entry point */
    main2(argc, argv);

    return 0;
}

/* Cleanup - called via atexit or on exit */
/* No Winsock cleanup needed on POSIX */
