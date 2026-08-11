/*
 * disp_new.c - output in fancy new format
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "meta.h"

#define SEMI_VERBOSE		/* not quite fully verbose */

/* "when" should be the length of time in seconds */
char *
nice_time(when)
time_t when;
{
    static char buf[64];

    if (when < 120)
	sprintf(buf, "%d seconds", when);
    else if (when/60 < 120)
	sprintf(buf, "%d minutes", when/60);
    else if (when/3600 < 48)
	sprintf(buf, "%d hours", when/3600);
    else
	sprintf(buf, "%d days", when/(3600*24));
    return (buf);
}


int
display_new(idx, port_idx)
int idx, port_idx;
{
    register int i;
    USER *up;
    SERVER *sp;
    time_t now;
    int srv, ago, *sorted;
    char flagbuf[8], buf[128];
    char *cp;

    up = &users[idx];
    up->data_size = up->buf_size = up->pos = 0;

    now = time(0);

    /* print header */
    Uprintf(idx, "\n*** Connected to MetaServerII v%s on port %d ***\n",
	VERSION, ports[port_idx].port);
    cp = ctime(&now);
    cp[strlen(cp)-1] = '\0';	/* playing with fire? */
    Uprintf(idx, "Date in %s is %s (data spans %s)\n", location, cp,
	nice_time(now-checktime));
    Uprintf(idx, "E-mail administrative requests to %s.\n\n", admin);

    Uprintf(idx, "Retry periods (in minutes):  ");
    Uprintf(idx, "down:%d empty:%d open:%d queue:%d\n\n",
	wait_noconn, wait_empty, wait_open, wait_queue);
    Uprintf(idx, "Other interesting MetaServerII ports:  ");
    for (i = 0; i < port_count; i++) {
	if (ports[i].kind != DI_NEW)
	    Uprintf(idx, "%d ", ports[i].port);
    }
    Uprintf(idx, "\n\n");

    Uprintf(idx, "%-39s %-8s %-3s  %s\n", "", "", "Mins", "");
    Uprintf(idx, "%-39s %-8s %-3s  %-17s %s\n", "Server Host","Port","Ago",
	"Status", "Flags");
    Uprintf(idx, "--------------------------------------- ");
    Uprintf(idx, "-------- ---- ----------------- -------\n");

    /* print player info */
    sorted = sort_servers();
    for (srv = 0; srv < server_count; srv++) {
	/* (need to sort these?) */
	sp = &servers[sorted[srv]];
	if (sp->status == SS_WORKING) sp = &prev_info;
	Uprintf(idx, "-h %-36.36s -p %-5d ", sp->hostname, sp->port);

	strcpy(flagbuf, "       ");
	for (i = 0; i < 6; i++)
	    if (sp->display_flags & flags[i].bit)
		flagbuf[i] = flags[i].chr;
	flagbuf[6] = sp->type[0];		/* hack in the type field */

	ago = (now - sp->last_update) / 60;
	switch (sp->status) {
	case SS_INIT:
	    Uprintf(idx, "(no data yet)\n");
	    break;
	case SS_WORKING:
	    Uprintf(idx, "(scanning server now)\n");
	    break;
	case SS_NOCONN:
	    switch (sp->why_dead) {
		case WD_CONN:
		    Uprintf(idx, "%3d  %-17s %s\n", ago, "* No connection", 
			flagbuf);
		    break;
		case WD_READ:
		    Uprintf(idx, "%3d  %-17s %s\n", ago, "* Bad read", 
                        flagbuf);
                    break;
		case WD_GARBAGE:
		    Uprintf(idx, "%3d  %-17s %s\n", ago, "* Garbaged read", 
                        flagbuf);
                    break;
		case WD_TIMEOUT:
		    Uprintf(idx, "%3d  %-17s %s\n", ago, "* Timed out", 
                        flagbuf);
                    break;
		case WD_FLOODING:
		    Uprintf(idx, "%3d  %-17s %s\n", ago, "* Flooding",
			flagbuf);
		    break;
		default:
	    	    Uprintf(idx, "%3d  %-17s %s\n", ago, "* Not responding", 
			flagbuf);
		    break;
	    }
	    break;
	case SS_EMPTY:
	    Uprintf(idx, "%3d  %-17s %s\n", ago, "Nobody playing", flagbuf);
	    break;
	case SS_OPEN:
	    sprintf(buf, "OPEN: %d player%s", sp->player_count,
		(sp->player_count != 1) ? "s" : "");
	    Uprintf(idx, "%3d  %-17s %s\n", ago, buf, flagbuf);
	    break;
	case SS_QUEUE:
	    sprintf(buf, "Wait queue: %d", sp->queue_size);
	    Uprintf(idx, "%3d  %-17s %s\n", ago, buf, flagbuf);
	    break;

	default:
	    /* huh? */
	    fprintf(stderr, "Internal ERROR: strange state in disp_new\n");
	    break;
	}
    }

    /* print footer */
    Uprintf(idx, "\nThat's it!\n");

    return(0);
}


/*
 * Do the "verbose" output format
 */
int
display_verbose(idx, port_idx)
int idx;
{
    USER *up;
    SERVER *sp;
    time_t now;
    int srv, ago, *sorted;
    char buf[80];
    int i;

    up = &users[idx];
    up->data_size = up->buf_size = up->pos = 0;

    now = time(0);

    /* print header */
    Uprintf(idx, "\n*** Connected to MetaServerII v%s on port %d ***\n",
	VERSION, ports[port_idx].port);
    Uprintf(idx, "Date in %s is %s", location, ctime(&now));
    Uprintf(idx, "E-mail administrative requests to %s.\n", admin);
    Uprintf(idx, "Up continuously for %s", nice_time(now-uptime));
    if (uptime != checktime)
	Uprintf(idx, " (data spans %s).\n", nice_time(now-checktime));
    else
	Uprintf(idx, ".\n");
    Uprintf(idx, "Retry periods (in minutes):  ");
    Uprintf(idx, "down:%d empty:%d open:%d queue:%d\n\n",
	wait_noconn, wait_empty, wait_open, wait_queue);
    Uprintf(idx, "Other interesting MetaServerII ports:\n");
    for (i = 0; i < port_count; i++)
	if (ports[i].kind != DI_VERBOSE)
	    Uprintf(idx, "\t%d - %s\n", ports[i].port, ports[i].desc);
    Uprintf(idx, "\n");


    /* print player info */
    sorted = sort_servers();
    for (srv = 0; srv < server_count; srv++) {
	/* (need to sort these?) */
	sp = &servers[sorted[srv]];
	if (sp->status == SS_WORKING) sp = &prev_info;
#ifdef SEMI_VERBOSE
	if (sp->status == SS_NOCONN || sp->status == SS_EMPTY) continue;
#endif
	Uprintf(idx, "Server: %s [%c], port %d", sp->hostname,
		sp->type[0], sp->port);
	/* print comment if we've got one */
	if (strlen(sp->comment) > 0)
	    Uprintf(idx, " (%s)\n", sp->comment);
	else
	    Uprintf(idx, "\n");
	ago = (now - sp->last_update) / 60;
	strncpy(buf, ctime(&sp->last_update)+11, 8);
	*(buf+8) = '\0';
	if (sp->status != SS_INIT && sp->status != SS_WORKING)
	    Uprintf(idx, "Last checked at %s (%d minute%s ago).\n", buf, ago,
		(ago == 1) ? "" : "s");
	Uprintf(idx, "\n");

	switch (sp->status) {
	case SS_INIT:
	    Uprintf(idx, "This server has not been examined yet.\n");
	    break;
	case SS_WORKING:
	    Uprintf(idx,
		"Information for this server is being retrieved right now.\n");
	    break;
	case SS_NOCONN:
	    Uprintf(idx, "Index %d\n", sp->why_dead);
	    switch (sp->why_dead) {
	    case WD_UNKNOWN:
		Uprintf(idx, "The server is not responding.\n");
		break;
	    case WD_CONN:
		Uprintf(idx, "Unable to connect to server.\n");
		break;
	    case WD_READ:
		Uprintf(idx, "Got errors reading from server.\n");
		break;
	    case WD_GARBAGE:
		Uprintf(idx, "Server sent garbled data.\n");
		break;
	    case WD_TIMEOUT:
		Uprintf(idx, "Timed out waiting for response.\n");
		break;
	    case WD_FLOODING:
		Uprintf(idx, "Flooding metaserver, temporarily delisted.\n");
		break;
	    default:
		log_msg("got weird sp->why_dead (%d) in display_verbose()",
			sp->why_dead);
		Uprintf(idx, "The server is not responding.\n");
	    }
	    break;
	case SS_EMPTY:
	    Uprintf(idx, "Nobody is playing.\n");
	    break;
	case SS_OPEN:
	    if (sp->player_count == 1)
		Uprintf(idx, "There is 1 person playing here.\n");
	    else
		Uprintf(idx, "There are %d people playing here.\n",
		    sp->player_count);
	    print_players(idx, sorted[srv]);
	    break;
	case SS_QUEUE:
	    if (sp->queue_size == 1)
		Uprintf(idx, "There is a wait queue with 1 person on it.\n");
	    else
		Uprintf(idx, "There is a wait queue with %d people on it.\n",
		    sp->queue_size);
	    print_players(idx, sorted[srv]);
	    break;

	default:
	    /* huh? */
	    fprintf(stderr, "Internal ERROR: strange state in disp_verbose\n");
	    break;
	}

	Uprintf(idx, "\n\n");
    }

    /* print footer */
    Uprintf(idx, "\nThat's it!\n\n");
    return (0);
}


#define NOBODY 0x0
#define IND 0x0
#define FED 0x1
#define ROM 0x2
#define KLI 0x4
#define ORI 0x8
#define ALLTEAM (FED|ROM|KLI|ORI)
#define MAXTEAM (ORI)
#define NUMTEAM 4

#define NUMRANKS 9
#define PNUMRANKS 18
struct rank {
    char *name;
};
static struct rank ranks[NUMRANKS+1] = {
    { "Ensign"},
    { "Lieutenant"},
    { "Lt. Cmdr."},
    { "Commander"},
    { "Captain"},
    { "Flt. Capt."},
    { "Commodore"},
    { "Rear Adm."},
    { "Admiral"},
    { "(unknown)"}
};

static struct rank    pranks[PNUMRANKS] =
{
	{ "Recruit"},
	{ "Specialist"},
	{ "Cadet"},
	{ "Midshipman"},
	{ "Ensn., J.G."},
	{ "Ensign"},
	{ "Lt., J.G."},
	{ "Lieutenant"},
	{ "Lt. Cmdr."},
	{ "Commander"},
	{ "Captain"},
	{ "Fleet Capt."},
	{ "Commodore"},
	{ "Moff"},
	{ "Grand Moff"},
	{ "Rear Adml."},
	{ "Admiral"},
	{ "Grand Adml."}
};

/*static*/ int
print_players(idx, srv)
int idx, srv;
{
    SERVER *sp;
    PLAYER *j;
    int i, k, t, ci;
    int plcount, fromm;
    char namebuf[18];

    Uprintf(idx, "\n");
    sp = &servers[srv];

    /* first, scan list */
    plcount = 0;
    for (i = 0; i < sp->max_players; i++)
	if (sp->players[i].p_status!=PFREE && sp->players[i].p_status!=POUTFIT)
		plcount++;

    if (!plcount) {
	Uprintf(idx, "(Player list not available.)\n");
	return (0);
    }
    fromm = (sp->status == SS_QUEUE);
    if (fromm)
	Uprintf(idx, "(player list from port %d; may not be 100%% accurate)\n",
	    sp->port-1);

    /* this was adapted from ck_players */
    Uprintf(idx, "  Type Rank       Name              Login\n");
/*    for (k = IND; k <= ORI; k<<=1) {*/
    for (t = 0; t <= NUMTEAM; t++) {
	if (!t) k = 0;
	else    k = 1 << (t-1);
	for (i = 0, j = &sp->players[i]; i < sp->max_players; i++, j++) {
	    if ((j->p_status != PFREE && j->p_status != POUTFIT)
							&& j->p_team == k) {
		/* ugly way to ferret out trailing spaces */
		strncpy(namebuf+1, j->p_name, 16);
		namebuf[0] = ' ';
		namebuf[16] = namebuf[17] = '\0';
		if (namebuf[strlen(namebuf)-1] == ' ') {
		    namebuf[0] = '"';
		    namebuf[strlen(namebuf)] = '"';
		}
		for (ci = 0; ci < 18; ci++)	/* weed out control chars */
		    if (namebuf[ci] && ((namebuf[ci] & 0x7f) < 0x20))
			namebuf[ci] = '^';
		Uprintf(idx, "%c%c %s  %-9.9s %-18.18s %s@%s\n",
		    teamlet[j->p_team],
		    shipnos[i],
		    /*classes[j->ship_type],*/
		    (sp->type[0] != 'P')
			? ((j->ship_type<NUM_TYPES) ?
			   classes[j->ship_type]:"!!")
			: ((j->ship_type<PNUM_TYPES) ?
			   pclasses[j->ship_type]:"!!"),
		    (sp->type[0] != 'P')
			? ((j->p_rank<NUMRANKS) ?
			   ranks[j->p_rank].name:"(unknown)")
			: ((j->p_rank<PNUMRANKS) ?
			   pranks[j->p_rank].name:"(unknown)"),
		    namebuf,	/*j->p_name,*/
		    j->p_login, j->p_monitor );
	    }
	}
    }
}
