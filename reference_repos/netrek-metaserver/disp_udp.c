/*
 * disp_udp.c - output to client via returned buffer
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "meta.h"

char *
display_udp(int *length)
{
    SERVER *sp;
    int srv, *sorted, count;
    char *buffer, *bp;
    time_t now = time(NULL);
    int extendedstatus;

    /* sort and count the servers that will be returned */
    sorted = sort_servers();
    count = 0;
    for (srv = 0; srv < server_count; srv++) {
	sp = &servers[sorted[srv]];
	if (sp->status == SS_WORKING) sp = &prev_info;
	if (sp->status == SS_INIT) continue;
	if (sp->status == SS_WORKING) continue;
	count++;
    }

    /* estimate memory required and allocate it, match with sprintf()'s */
    buffer = malloc((MAX_HOSTNAME+1 +6+1 +3+1 +6+1 +3+1 +3+1 +1+1 +1)*count+6);
    /*			host        port stat age  play que  type \n	*/

    /* out of memory? */
    if ( buffer == NULL ) return "";
    bp = buffer;

    /* include the count at the top to ease the client's coding */
    sprintf(bp,"r,%d\n",count);
    bp += strlen(bp);

    for (srv = 0; srv < server_count; srv++) {
	sp = &servers[sorted[srv]];
	if (sp->status == SS_WORKING) sp = &prev_info;
	if (sp->status == SS_INIT) continue;
	if (sp->status == SS_WORKING) continue;

        /* this if structure is to satisfy COW's server filtering based on */
        /* the TCP output of the metaserver. TCP was text, this is only #s */
        if (sp->status == SS_NOCONN )
          extendedstatus = (sp->why_dead == WD_TIMEOUT) ? 6 : 4;
        else
          extendedstatus = sp->status;

        sprintf(bp,"%s,%d,%d,%d,%d,%d,%c\n",
	    sp->hostname,		/* host name of server		*/
	    sp->port,			/* port number of server	*/
	    extendedstatus,		/* metaserver status code	*/
	    now - sp->last_update,	/* age of data in seconds	*/
	    sp->player_count,		/* count of players on server	*/
	    sp->queue_size,		/* length of wait queue		*/
	    sp->type[0] );		/* type code, B, P, etc		*/
        bp += strlen(bp);
    }

    *length = strlen(buffer);		/* for future optimisation	*/

    return buffer;			/* caller must free buffer!	*/
}
