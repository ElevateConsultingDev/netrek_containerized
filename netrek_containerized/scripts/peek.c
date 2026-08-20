/* peek.c - dump live server state from shared memory, for testing bot sight.
 *
 * Build and run inside the container:
 *   docker cp netrek_containerized/scripts/peek.c vanilla-netrek-server:/tmp/peek.c
 *   docker exec vanilla-netrek-server bash -lc \
 *     'cd /usr/local/src/netrek/netrek-server && \
 *      gcc -I include -I . -o /tmp/peek /tmp/peek.c ntserv/libnetrek.a \
 *          -lgdbm -lresolv -lm'
 *   docker exec vanilla-netrek-server /tmp/peek
 *
 * For a human player it prints distance to the nearest bot and whether that
 * bot is inside detection range, which is what decides whether the bots can
 * see you while cloaked.
 */
#include "config.h"
#include <stdio.h>
#include <math.h>
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "proto.h"

#define CLOAK_RANGE (GWIDTH/7)		/* 14285, matches udplayersight() */
#define OPEN_RANGE  (GWIDTH/3)		/* 33333 */

int main(void)
{
    int i, k;

    openmem(0);
    printf("tourn=%d\n", status->tourn);

    for (i = 0; i < MAXPLAYER; i++) {
	struct player *p = &players[i];
	int bot = (p->p_flags & (PFROBOT | PFBPROBOT)) != 0;
	int nearest = -1;
	double ndist = 0;

	if (p->p_status == PFREE) continue;

	printf("%2d %-12s %-4s team=%d flags=0x%08x%s%s x=%6d y=%6d arm=%d seen=%d",
	       i, p->p_name, bot ? "bot" : "HUMAN", p->p_team, p->p_flags,
	       (p->p_flags & PFCLOAK) ? " CLOAK" : "",
	       (p->p_flags & PFORBIT) ? " ORBIT" : "",
	       p->p_x, p->p_y, p->p_armies,
	       (p->p_flags & PFSEEN) ? 1 : 0);

	if (bot || p->p_status != PALIVE) { printf("\n"); continue; }

	/* human: how close is the nearest enemy bot, and can it see him? */
	for (k = 0; k < MAXPLAYER; k++) {
	    struct player *q = &players[k];
	    double d;

	    if (q->p_status != PALIVE || q == p) continue;
	    if (!(q->p_flags & (PFROBOT | PFBPROBOT))) continue;
	    if (q->p_team == p->p_team) continue;
	    d = hypot((double)(q->p_x - p->p_x), (double)(q->p_y - p->p_y));
	    if (nearest < 0 || d < ndist) { nearest = k; ndist = d; }
	}

	if (nearest < 0) {
	    printf("  no enemy bots\n");
	} else {
	    int range = (p->p_flags & PFCLOAK) ? CLOAK_RANGE : OPEN_RANGE;
	    printf("  nearest bot %s at %.0f (range %d) -> %s\n",
		   players[nearest].p_name, ndist, range,
		   ndist <= range ? "VISIBLE" : "hidden");
	}
    }
    return 0;
}
