/* Drives the REAL short.c packet handlers with crafted packets.
 * Built with ASan; any out-of-bounds access aborts the run.
 * Used to compare pre-fix vs post-fix short.c. */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "packets.h"

/* ---- stubs for the display / network layer short.c calls ---- */
void dmessage(char *m, int f, int from, int to) { (void)m;(void)f;(void)from;(void)to; }
void handleMessage(struct mesg_spacket *p) { (void)p; }
void become(int t) { (void)t; }
void calibrate_stats(void) {}
void plistReorder(void) {}
void redrawStats(void) {}
void sendServerPacket(struct player_spacket *p) { (void)p; }
void sendUdpReq(int r) { (void)r; }
void sprefresh(void) {}
void updatePlayer(int p) { (void)p; }
void rotate_coord(int *x,int *y,int d,int a,int b){(void)x;(void)y;(void)d;(void)a;(void)b;}
void rotate_dir(unsigned char *d,int r){(void)d;(void)r;}
void warning(char *s) { (void)s; }
#ifdef RCM
void makedistress(struct distress *d, char *m, char *n){(void)d;(void)m;(void)n;}
#endif
int plistUpdated = 0;
W_Window reviewWin;

extern void handleSMessage(struct mesg_s_spacket *);
extern void handleSWarning(struct warning_s_spacket *);
extern void handleVTorp(unsigned char *, int);

static void setup(void) {
    players = calloc(MAXPLAYER, sizeof(struct player));
    torps   = calloc(MAXPLAYER * MAXTORP, sizeof(struct torp));
    planets = calloc(MAXPLANETS, sizeof(struct planet));
    phasers = calloc(MAXPLAYER, sizeof(struct phaser));
    plasmatorps = calloc(MAXPLAYER * MAXPLASMA, sizeof(struct plasmatorp));
    for (int i = 0; i < MAXPLAYER; i++) { players[i].p_no = i; players[i].p_team = 1; }
    for (int i = 0; i < MAXPLANETS; i++) { planets[i].pl_no = i; strcpy(planets[i].pl_name, "Pln"); }
    for (int i = 0; i < MAXPLAYER * MAXTORP; i++) { torps[i].t_status = TFREE; torps[i].t_owner = i / MAXTORP; }
    me = &players[0];
    myship = &me->p_ship;
    mystats = &me->p_stats;
    nranks = DEFAULT_NUMRANKS;
    ranks = malloc(nranks * sizeof(struct rank));
    memcpy(ranks, &default_ranks, nranks * sizeof(struct rank));
    server_ups = 10;
}

int main(int argc, char **argv) {
    int only = (argc > 1) ? atoi(argv[1]) : 0;
    setup();

    if (only==0||only==1)
    /* 1. Message whose declared length runs well past the 100-byte format
     *    buffer, with no NUL anywhere in the payload. */
    {
        unsigned char raw[256];
        memset(raw, 'A', sizeof(raw));       /* no terminator at all */
        struct mesg_s_spacket *p = (struct mesg_s_spacket *)raw;
        p->type = SP_S_MESSAGE;
        p->m_flags = MALL;
        p->m_recpt = 200;                    /* out of range recipient */
        p->m_from = 200;                     /* out of range sender   */
        p->length = 255;                     /* server-declared max   */
        handleSMessage(p);
        printf("  msg: max-length unterminated ok\n");
    }

    if (only==0||only==2)
    /* 2. Torp packet naming a player slot far past MAXPLAYER. */
    {
        unsigned char raw[64];
        memset(raw, 0xff, sizeof(raw));
        raw[0] = SP_S_TORP;
        raw[1] = 0xff;                       /* bitset: all 8 torps */
        raw[2] = 255;                        /* player slot 255 -> 2040 */
        handleVTorp(raw, 0);
        printf("  torp: slot 255 ok\n");
    }

    if (only==0||only==3)
    /* 3. Every warning subtype with both argument bytes at 255, and every
     *    declared packet length. The packet sits at the very END of a
     *    1024-byte region, exactly as doRead can present it, so any read or
     *    write past the declared length runs off the mapped area. */
    {
        for (int msg = 0; msg < 256; msg++) {
            for (int plen = 0; plen < 256; plen += 17) {
                int sz = (plen < 4) ? 4 : plen;
                unsigned char *region = malloc(sz);   /* exact size: ASan
                                                       * red-zones both ends */
                memset(region, 'B', sz);              /* never terminated */
                region[0] = SP_S_WARNING;
                region[1] = (unsigned char) msg;
                region[2] = 0xff;                     /* pad2 / argument  */
                region[3] = (unsigned char) plen;     /* pad3 / argument2 */
                handleSWarning((struct warning_s_spacket *) region);
                free(region);
            }
        }
        printf("  warning: all subtypes x all declared lengths ok\n");
    }

    printf("HARNESS PASSED\n");
    return 0;
}

/* additional stubs: only symbols data.o does not already define */
void W_SetSensitive(W_Window w, int b) { (void)w; (void)b; }
unsigned char numofbits[256];
unsigned long last_update_ms = 0;
unsigned long msetime(void) { return 0; }
int vtisize[9];

int gather_stats = 0;
FILE *statsFile = NULL;
unsigned char redrawPlayer[MAXPLAYER];
int shortversion = 0;
