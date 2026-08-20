/* Exercises the real extrap_apply()/extrap_restore() from redraw.c.
 * Links against the client's own objects, so this tests the shipped code
 * rather than a copy of it. Build instructions in tests/README.md. */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "redraw.h"
#include "interface.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    printf("  FAIL: %s\n", msg); failures++; } } while (0)

static void setup(void)
{
    players = calloc(MAXPLAYER, sizeof(struct player));
    torps   = calloc(MAXPLAYER * MAXTORP, sizeof(struct torp));
    for (int i = 0; i < MAXPLAYER; i++) {
        players[i].p_no = i;
        players[i].p_status = PFREE;
        players[i].p_ship.s_torpspeed = 12;
    }
    for (int i = 0; i < MAXPLAYER * MAXTORP; i++) {
        torps[i].t_status = TFREE;
        torps[i].t_owner = i / MAXTORP;
    }
    me = &players[0];
    server_ups = 25;            /* 40ms per update */
    extrapolate = 1;
}

int main(void)
{
    setup();

    /* Half an update elapsed: 25 ups -> 40ms per update, so 20ms = 0.5 */
    last_update_ms = msetime() - 20;

    /* dir 64 is +x (Cos[64]~1, Sin[64]~0); warp 10 => 10*20*0.5 = 100 units */
    players[1].p_status = PALIVE;
    players[1].p_speed = 10;
    players[1].p_dir = 64;
    players[1].p_x = 50000;
    players[1].p_y = 50000;

    /* dir 0 is -y (up): Cos[0]=0, Sin[0]=-1 */
    players[2].p_status = PALIVE;
    players[2].p_speed = 10;
    players[2].p_dir = 0;
    players[2].p_x = 50000;
    players[2].p_y = 50000;

    /* a dead player must not move */
    players[3].p_status = PFREE;
    players[3].p_speed = 10;
    players[3].p_dir = 64;
    players[3].p_x = 50000;
    players[3].p_y = 50000;

    /* Torps must NOT be reckoned: the short-packet handler never sets
     * t_dir, so the stored direction is stale or belongs to whatever last
     * used the slot, and moving along it sends torps off at wrong angles. */
    torps[8].t_status = TMOVE;      /* player 1's first torp slot */
    torps[8].t_owner = 1;
    torps[8].t_dir = 64;
    torps[8].t_x = 50000;
    torps[8].t_y = 50000;

    extrap_apply();

    printf("after apply: p1=(%d,%d) p2=(%d,%d) p3=(%d,%d) torp=(%d,%d)\n",
           players[1].p_x, players[1].p_y, players[2].p_x, players[2].p_y,
           players[3].p_x, players[3].p_y, torps[8].t_x, torps[8].t_y);

    /* ~100 units east, allow a unit of rounding/timing slop */
    CHECK(players[1].p_x > 50080 && players[1].p_x < 50120, "p1 moved east ~100");
    CHECK(abs(players[1].p_y - 50000) <= 2,                 "p1 y unchanged");
    /* dir 0 is up: y decreases */
    CHECK(players[2].p_y < 49920 && players[2].p_y > 49880, "p2 moved north ~100");
    CHECK(abs(players[2].p_x - 50000) <= 2,                 "p2 x unchanged");
    /* not alive => untouched */
    CHECK(players[3].p_x == 50000 && players[3].p_y == 50000, "dead player still");
    CHECK(torps[8].t_x == 50000 && torps[8].t_y == 50000,   "torp NOT reckoned");

    /* restore must be exact for every slot */
    extrap_restore();
    CHECK(players[1].p_x == 50000 && players[1].p_y == 50000, "p1 restored");
    CHECK(players[2].p_x == 50000 && players[2].p_y == 50000, "p2 restored");
    CHECK(torps[8].t_x == 50000 && torps[8].t_y == 50000,     "torp untouched");

    /* repeated apply/restore cycles must not accumulate drift */
    for (int i = 0; i < 500; i++) { extrap_apply(); extrap_restore(); }
    CHECK(players[1].p_x == 50000 && players[1].p_y == 50000, "no drift over 500 frames");
    CHECK(torps[8].t_x == 50000 && torps[8].t_y == 50000,     "torp still untouched");

    /* a stale timestamp clamps to one update, it does not fling anything away */
    last_update_ms = msetime() - 5000;      /* 125 updates late */
    extrap_apply();
    int dx = players[1].p_x - 50000;
    printf("stale-clamp offset = %d (one update at warp 10 = 200)\n", dx);
    CHECK(dx <= 200 && dx > 150, "late packet clamped to one update");
    extrap_restore();

    /* the option switches it off completely */
    extrapolate = 0;
    last_update_ms = msetime() - 20;
    extrap_apply();
    CHECK(players[1].p_x == 50000, "extrapolate:0 disables");
    extrap_restore();

    printf(failures ? "EXTRAP TEST FAILED\n" : "EXTRAP TEST PASSED\n");
    return failures != 0;
}
