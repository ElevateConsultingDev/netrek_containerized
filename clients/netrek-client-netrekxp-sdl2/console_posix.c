/* console_posix.c - POSIX console output for NetrekXP SDL2 port
 *
 * Replaces console.c: uses standard printf instead of
 * Windows console APIs (GetConsoleTitle, AllocConsole, WriteConsole).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"
#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "proto.h"

/* cons_buffer, consHead, consTail already declared in struct.h/data.h */

int
IsConsoleVisible(void)
{
    return 1;  /* Always visible on POSIX (we have stdout) */
}

void
ShowConsole(void)
{
    /* Nothing to do on POSIX */
}

void
LineToConsole(char *format, ...)
{
    struct cons_buffer *tmp;
    char str[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(str, sizeof(str), format, args);
    va_end(args);

    /* Store in buffer */
    tmp = (struct cons_buffer *)malloc(sizeof(struct cons_buffer));
    if (tmp) {
        tmp->string = strdup(str);
        tmp->next = NULL;
        if (!consHead) {
            consHead = consTail = tmp;
        } else {
            consTail->next = tmp;
            consTail = tmp;
        }
    }

    /* Output to stdout */
    fputs(str, stdout);
    fflush(stdout);
}

void
BufferToConsole(void)
{
    struct cons_buffer *tmp = consHead;
    while (tmp) {
        fputs(tmp->string, stdout);
        tmp = tmp->next;
    }
    fflush(stdout);
}
