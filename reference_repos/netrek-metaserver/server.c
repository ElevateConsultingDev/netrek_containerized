/*
 * server.c - interact with a Netrek server
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 *
 * This is sorta messed up.  Things would be much simpler if we could just
 * sit and block on the Netrek socket, but that could take several seconds
 * to complete.  We need something which works the same way, but deals with
 * partial packets, etc, etc, without blowing away the CPU like we are now.
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>		/* or strings.h */
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
/*
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
*/
#include <netinet/in.h>
#include <errno.h>
#include "meta.h"
#include "packets.h"

int new_conn;		/* scan.c sets to TRUE when server conn is opened */
int mnew_conn;		/* whoops, need one for read_minus too */


/*
 * Private stuff
 */
#define NOBODY_PLAYING	"No one is playing"
#define SERVER_IN_USE	"Server is in use"

#define BUFSIZE	85000	/* was 65000 */
static char rdbuf[BUFSIZE+1];

static int ssock;

/* from struct.h */
char *classes[NUM_TYPES] = {
  "SC", "DD", "CA", "BB", "AS", "SB", "GA", "??"
};
char *pclasses[PNUM_TYPES] = {
  "SC", "DD", "CA", "BB", "AS", "SB", "AT",
  "JS", "FR", "WB", "CL", "CV", "UT", "PT"
};
char teamlet[] = { 'I', 'F', 'R', 'X', 'K', 'X', 'X', 'X', 'O' };
char shipnos[] = {
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'
};

#define NUMRANKS 9
#define PNUMRANKS 18
static struct rankstrings {
  char *name;
  int len;
} rank[NUMRANKS] = {
  { "Ensign", 6 },
  { "Lieutenant", 9 },	/* one short in case it got cut off */
  { "Lt. Cmdr.", 9 },
  { "Commander", 9 },
  { "Captain", 7 },
  { "Flt. Capt.", 9 },	/* (ditto) */
  { "Commodore", 9 },
  { "Rear Adm.", 9 },
  { "Admiral", 7 },
  /* "(unknown)" */
}, prank[PNUMRANKS] = {
  { "Recruit",     7 },
  { "Specialist",  9 },
  { "Cadet",       5 },
  { "Midshipman",  9 },
  { "Ensn., J.G.", 9 },
  { "Ensign",      6 },
  { "Lt., J.G.",   9 },
  { "Lieutenant",  9 },
  { "Lt. Cmdr.",   9 },
  { "Commander",   9 },
  { "Captain",     7 },
  { "Fleet Capt.", 9 },
  { "Commodore",   9 },
  { "Moff",        4 },
  { "Grand Moff",  9 },
  { "Rear Adml.",  9 },
  { "Admiral",     7 },
  { "Grand Adml.", 9 }
};



/*
 * ---------------------------------------------------------------------------
 *	Stuff adapted from netrek/socket.c
 * ---------------------------------------------------------------------------
 */
static foo() {}
struct packet_handler handlers[] = {
  { 0, NULL },	/* record 0 */
  { sizeof(struct mesg_spacket), foo }, 	/* SP_MESSAGE */
  { sizeof(struct plyr_info_spacket), foo },	/* SP_PLAYER_INFO */
  { sizeof(struct kills_spacket), foo },	/* SP_KILLS */
  { sizeof(struct player_spacket), foo },	/* SP_PLAYER */
  { sizeof(struct torp_info_spacket), foo },	/* SP_TORP_INFO */
  { sizeof(struct torp_spacket), foo }, 	/* SP_TORP */
  { sizeof(struct phaser_spacket), foo },	/* SP_PHASER */
  { sizeof(struct plasma_info_spacket), foo},	/* SP_PLASMA_INFO */
  { sizeof(struct plasma_spacket), foo},	/* SP_PLASMA */
  { sizeof(struct warning_spacket), foo },	/* SP_WARNING */
  { sizeof(struct motd_spacket), foo },	/* SP_MOTD */
  { sizeof(struct you_spacket), foo },	/* SP_YOU */
  { sizeof(struct queue_spacket), foo },	/* SP_QUEUE */
  { sizeof(struct status_spacket), foo },	/* SP_STATUS */
  { sizeof(struct planet_spacket), foo }, 	/* SP_PLANET */
  { sizeof(struct pickok_spacket), foo },	/* SP_PICKOK */
  { sizeof(struct login_spacket), foo }, 	/* SP_LOGIN */
  { sizeof(struct flags_spacket), foo },	/* SP_FLAGS */
  { sizeof(struct mask_spacket), foo },	/* SP_MASK */
  { sizeof(struct pstatus_spacket), foo },	/* SP_PSTATUS */
  { sizeof(struct badversion_spacket), foo },	/* SP_BADVERSION */
  { sizeof(struct hostile_spacket), foo },	/* SP_HOSTILE */
  { sizeof(struct stats_spacket), foo },	/* SP_STATS */
  { sizeof(struct plyr_login_spacket), foo },	/* SP_PL_LOGIN */
  { sizeof(struct reserved_spacket), foo },	/* SP_RESERVED */
  { sizeof(struct planet_loc_spacket), foo },	/* SP_PLANET_LOC */
  /* paradise packets */
  { sizeof(struct scan_spacket), foo },	/* SP_SCAN (ATM) */
  { sizeof(struct udp_reply_spacket), foo },	/* SP_UDP_STAT */
  { sizeof(struct sequence_spacket), foo },   /* SP_SEQUENCE */
  { sizeof(struct sc_sequence_spacket), foo },/*SP_SC_SEQUENCE*/
  { sizeof(struct rsa_key_spacket), foo },	/* SP_RSA_KEY */
  { sizeof(struct motd_pic_spacket), foo },   /* SP_MOTD_PIC */
  { sizeof(struct stats_spacket2), foo },	/* SP_STATS2 */
  { sizeof(struct status_spacket2), foo },	/* SP_STATUS2 */
  { sizeof(struct planet_spacket2), foo },	/* SP_PLANET2 */
  { sizeof(struct obvious_packet), foo },     /* SP_TEMP_5 */
  { sizeof(struct thingy_spacket), foo },	/* SP_THINGY */
  { sizeof(struct thingy_info_spacket), foo },/* SP_THINGY_INFO*/
  { sizeof(struct ship_cap_spacket), foo }    /* SP_SHIP_CAP */
};

int sizes[] = {
  0,	/* record 0 */
  sizeof(struct mesg_cpacket), 		/* CP_MESSAGE */
  sizeof(struct speed_cpacket),		/* CP_SPEED */
  sizeof(struct dir_cpacket),			/* CP_DIRECTION */
  sizeof(struct phaser_cpacket),		/* CP_PHASER */
  sizeof(struct plasma_cpacket),		/* CP_PLASMA */
  sizeof(struct torp_cpacket),		/* CP_TORP */
  sizeof(struct quit_cpacket), 		/* CP_QUIT */
  sizeof(struct login_cpacket),		/* CP_LOGIN */
  sizeof(struct outfit_cpacket),		/* CP_OUTFIT */
  sizeof(struct war_cpacket),			/* CP_WAR */
  sizeof(struct practr_cpacket),		/* CP_PRACTR */
  sizeof(struct shield_cpacket),		/* CP_SHIELD */
  sizeof(struct repair_cpacket),		/* CP_REPAIR */
  sizeof(struct orbit_cpacket),		/* CP_ORBIT */
  sizeof(struct planlock_cpacket),		/* CP_PLANLOCK */
  sizeof(struct playlock_cpacket),		/* CP_PLAYLOCK */
  sizeof(struct bomb_cpacket),		/* CP_BOMB */
  sizeof(struct beam_cpacket),		/* CP_BEAM */
  sizeof(struct cloak_cpacket),		/* CP_CLOAK */
  sizeof(struct det_torps_cpacket),		/* CP_DET_TORPS */
  sizeof(struct det_mytorp_cpacket),		/* CP_DET_MYTORP */
  sizeof(struct copilot_cpacket),		/* CP_COPILOT */
  sizeof(struct refit_cpacket),		/* CP_REFIT */
  sizeof(struct tractor_cpacket),		/* CP_TRACTOR */
  sizeof(struct repress_cpacket),		/* CP_REPRESS */
  sizeof(struct coup_cpacket),		/* CP_COUP */
  sizeof(struct socket_cpacket),		/* CP_SOCKET */
  sizeof(struct options_cpacket),		/* CP_OPTIONS */
  sizeof(struct bye_cpacket),			/* CP_BYE */
  sizeof(struct dockperm_cpacket),		/* CP_DOCKPERM */
  sizeof(struct updates_cpacket),		/* CP_UPDATES */
  sizeof(struct resetstats_cpacket),		/* CP_RESETSTATS */
  sizeof(struct reserved_cpacket)		/* CP_RESERVED */
};

#define NUM_PACKETS (sizeof(handlers) / sizeof(handlers[0]) - 1)
#define NUM_SIZES (sizeof(sizes) / sizeof(sizes[0]) - 1)


int
sendServerPacket(packet)
     struct player_spacket *packet;
{
  int size;

#ifdef DEBUG
  printf("- Sending %d\n", packet->type);
#endif
	
  if (packet->type<1 || packet->type>NUM_SIZES || sizes[packet->type]==0) {
    fprintf(stderr, "Attempt to send strange packet %d!\n", packet->type);
    return (-1);
  }
  size=sizes[packet->type];
  if (gwrite(ssock, packet, size) != size) {
    return (-1);
  }

  return (0);
}


/*static*/ int
gwrite(fd, buf, bytes)
     int fd;
     char *buf;
     register int bytes;
{
  long orig = bytes;
  register long n;

  while (bytes) {
    n = write(fd, buf, bytes);
    if (n < 0)
      return(-1);
    bytes -= n;
    buf += n;
  }
  return(orig);
}


/*
 * ---------------------------------------------------------------------------
 *	New stuff
 * ---------------------------------------------------------------------------
 */

/*static*/ char *
scan_buf(buf, str, count)
     register char *buf, *str;
     int count;
{
  register int i;
  int len, max;

  len = strlen(str);
  max = count - len;
  if (max <= 0) return (NULL);

  for (i = 0; i < max; i++, buf++) {
    if (*buf == *str && !strncmp(buf, str, len))
      return (buf);
  }

  return (NULL);
}


/*
 * Parse a "players" output (port 2591 stuff).  Returns the #of players found.
 *
 * Note that the data here will be overwritten if we get better info from
 * the normal port (this is useful because it will get player info even
 * if there's a wait queue).
 *
 * Note that we want to do this BEFORE trying the other port, so that we
 * don't wake up the server if nobody is playing.
 *
 * New strategy: look for "standard keywords" in a header line, which is
 * expected to immediately follow after "<>==".  Gather a collection of
 * line offsets, and use those when scanning the "info" lines.
 *
 * Info line strategy: find a line which starts with F/K/R/O/I and a number
 * or small letter, and grab info until we hit something which doesn't match
 * (like "<>==").  Then we're done.
 *
 * We do have something of a dilemma here: we want to allow the display
 * to be off by a char or two, but it's possible that one or more of the
 * fields could be blank (for example, if the user is just logging in).
 * Until somebody defines the exact format to use (I did ask!), this will
 * be satisfactory.
 */
#define PL_SEP	"<>=="
#ifdef FUBAR
#define POS_MAPCHARS	2
#define POS_PSEUDO	POS_MAPCHARS+4
#define POS_LOGIN	POS_PSEUDO+17
#define POS_DISPLAY	POS_LOGIN+12
#define POS_SHIP	POS_DISPLAY+18
#endif
#define POSx_MAPCHARS	0
#define POSx_RANK	1
#define POSx_PSEUDO	2
#define POSx_LOGIN	3
#define POSx_DISPLAY	4
#define POSx_TYPE	5
#define MAX_POSx	6
#define TEAMCHARS	"IFRKO"
#define PLAYERCHARS	"0123456789abcdefghiABCDEFGHI"

struct {
  int offset;
  char *title;
  int len;
} offsets[MAX_POSx] = {
  { 0, "Pl:", 3 },
  { 0, "Rank", 4 },
  { 0, "Name", 4 },
  { 0, "Login", 5 },
  { 0, "Display", 7 },
  { 0, "Type", 4 },
};

/*static*/ int
parse_playerlist(buf, size)
     register char *buf;
     int size;
{
  SERVER *sp = &servers[busy];
  PLAYER *pp;
  int pl_count = 0;
  register char *end = buf+size;
  char ship_type[4], *cp, *brkp;
  char c;
  int i, j, pl, len, tmpo;

#ifdef DEBUG
  printf("parsing playerlist, buf = 0x%.8lx, end = 0x%.8lx\n",
	 buf, end);	/* DEBUG */
#endif
  for (i = 0; i < MAX_POSx; i++)
    offsets[i].offset = -1;

  /* find start of interesting stuff */
  while (buf < end) {
    if (!strncmp(buf, PL_SEP, strlen(PL_SEP))) break;

    while (*buf != '\n' && buf < end)
      buf++;
    buf++;
  }
  /* title line is expected next */
  while (*buf != '\n' && buf < end)
    buf++;
  buf++;
  if (buf >= end) return (-1);

  for (cp = buf; *cp != '\n' && cp < end; cp++)
    ;
  if (cp >= end) return (-1);
  brkp = cp;
  *brkp = '\0';

  /* title line is at *buf; figure out where each of the fields begins */
  if ((len = strlen(buf)) < 4) return (-1);

  for (i = 0, cp = buf; i < len; i++, cp++) {
    for (j = 0; j < MAX_POSx; j++) {
      if (*cp == *(offsets[j].title)) {
	if (!strncmp(cp, offsets[j].title, offsets[j].len)) {
	  /*printf("offset %d (%s) = %d\n", j, offsets[j].title, i);*/
	  offsets[j].offset = i;
	  i += offsets[j].len;
	  cp += offsets[j].len;
	  break;	/* out of j-loop */
	}
      }
    }
  }

  /* resume past the '\0' */
  buf = brkp+1;

  while (buf < end) {
    tmpo = offsets[POSx_MAPCHARS].offset;
    if (strchr(TEAMCHARS,*(buf+tmpo)) && strchr(PLAYERCHARS,*(buf+tmpo+1))){
      /* found the start */

      while (buf < end) {
	tmpo = offsets[POSx_MAPCHARS].offset;
	c = *(buf+tmpo+1);
	if (c >= '0' && c <= '9') pl = c - '0';
	else {
	  if (islower(c)) c = toupper(c);
	  pl = (c - 'A') + 10;
	}
	if (pl < 0 || pl >= sp->max_players) {
	  /*printf("illegal player #%d\n", pl);*/
	  return (-1);
	}
	/*printf("Player %d\n", pl);*/
	pp = &(sp->players[pl]);
	pp->p_mapchars[0] = c = *(buf+tmpo);
	pp->p_mapchars[1] = *(buf+tmpo+1);

	switch (c) {
	case 'I': pp->p_team = 0; break;
	case 'F': pp->p_team = 1; break;
	case 'R': pp->p_team = 2; break;
	case 'K': pp->p_team = 4; break;
	case 'O': pp->p_team = 8; break;
	default:
	  /*printf("Bad Team %s\n", buf);*/
	  return (-1);
	}

	if ((tmpo = offsets[POSx_RANK].offset) == -1 ||
	    *(buf+tmpo) == ' ') {
	  pp->p_rank = NUMRANKS;
	} else {
	  struct rankstrings	*ranklist;
	  int	nranks;
	  if (sp->type[0]=='P') {
	    nranks = PNUMRANKS;
	    ranklist = prank;
	  } else {
	    nranks = NUMRANKS;
	    ranklist = rank;
	  }
	  pp->p_rank = nranks;
	  for (i = 0; i < nranks; i++) {
	    if (!strncmp(buf+tmpo, ranklist[i].name, ranklist[i].len)) {
	      pp->p_rank = i;
	      break;
	    }
	  }
	}

	if ((tmpo = offsets[POSx_PSEUDO].offset) == -1 ||
	    *(buf+tmpo) == ' ') {
	  strcpy(pp->p_name, "(none)");
	} else {
	  strncpy(pp->p_name, buf+tmpo, 15);
	  cp = pp->p_name + strlen(pp->p_name) -1;
	  if (cp > pp->p_name) {
	    while (*cp == ' ') cp--;
	    *(cp+1) = '\0';
	  }
	  /*printf("name = '%s' ", pp->p_name);*/
	}

	if ((tmpo = offsets[POSx_LOGIN].offset) == -1 ||
	    *(buf+tmpo) == ' ') {
	  strcpy(pp->p_login, "(none)");
	} else {
	  if (strtok(buf+tmpo, " ") == NULL) {
	    /*printf("Bad login %s\n", buf);*/
	    return (-1);
	  }
	  strncpy(pp->p_login, buf+tmpo, 15);
	  *(pp->p_login+15) = '\0';
	  /*printf("login = '%s' ", pp->p_login);*/
	}

	if ((tmpo = offsets[POSx_DISPLAY].offset) == -1 ||
	    *(buf+tmpo) == ' ') {
	  strcpy(pp->p_name, "(none)");
	} else {
	  if (strtok(buf+tmpo, " ") == NULL) {
	    /*printf("Bad display %s\n", buf);*/
	    return (-1);
	  }
	  strncpy(pp->p_monitor, buf+tmpo, 15);
	  *(pp->p_name+15) = '\0';
	  /*printf("disp = '%s'\n", pp->p_monitor);*/
	}

	if ((tmpo = offsets[POSx_TYPE].offset) == -1) {
	  strcpy(ship_type, "??");
	  pp->ship_type = NUM_TYPES-1;
	  goto skip_type;
	}
	if (*(buf+tmpo) == ' ') tmpo++;		/* allow a blank here */
	if (*(buf+tmpo) == ' ' || *(buf+tmpo+1) == ' ') {
	  /*printf("Bad ship type %s\n", buf);*/
	  return (-1);
	}
	ship_type[0] = *(buf+tmpo);
	ship_type[1] = *(buf+tmpo+1);
	ship_type[2] = '\0';
	pp->ship_type = NUM_TYPES-1;	/* default is "??" */
	for (i = 0; i < NUM_TYPES; i++) {
	  if (!strcmp(ship_type, classes[i])) {
	    pp->ship_type = i;
	    break;
	  }
	}
      skip_type:
	/*printf("ship type = %d (%s)\n", pp->ship_type,
	  classes[pp->ship_type]);*/

	pp->p_status = PALIVE;
	pl_count++;


	/* move to start of next line */
	while (*buf != '\n' && buf < end)
	  buf++;
	buf++;

	/* done yet? */
	if (*buf == *PL_SEP) goto done;

      }
    }

    while (*buf != '\n' && buf < end)
      buf++;
    buf++;
  }
done:
  sp->player_count = pl_count;
  return (pl_count);
}


/*
 * Read & interpret data from port-1 (sends a player listing).  Uses blocking
 * reads; the amount of data should be small enough that this isn't a problem.
 *
 * Returns -1 on error, -2 if not done yet, status otherwise.
 *
 * NOTE: if the output of port 2591 was garbage, we would get "0 players
 * playing" from parse_playerlist().  However, if nobody was playing, then
 * we should've seen "No one is playing" earlier.  So, if we get 0 back from
 * parse_playerlist(), assume something went wrong and return (-1).
 */
SERVER_STATUS
read_minus(sock)
     int sock;
{
  static int count = 0;
  int cc, pc;

#ifdef DEBUG
  printf("Reading fd %d\n", sock);
#endif
  ssock = sock;

  if (mnew_conn) {
    count = 0;
    mnew_conn = FALSE;

#ifdef CONNECT 
    /* see if we're really connected */
    if ((cc = write(sock, " ", 1)) < 0) {
      if (errno != EPIPE) {
	if (verbose) perror("strange read_minus error");
	log_msg("strange read_minus write error %d with %s", errno,
		servers[busy].hostname);
      }
      /* no change to why_dead for read_minus */
      return (-1);
    }
#endif
    return (-2);
  }

  /* if we get an error, assume it was the server hanging up on us */
  if ((cc = read(sock, rdbuf+count, BUFSIZE-count)) < 0) {
    if (errno != ECONNREFUSED) {
      if (verbose) perror("read minus");
      log_msg("read error %d in read_minus with %s", errno,
	      servers[busy].hostname);
    }
    return (-1);
  }
  count += cc;
  if (cc) return (-2);	/* more to go (ok if buf fills) */
  if (!count) return (-1);	/* no info! */
  rdbuf[count] = '\0';

  /* see if nobody is playing */
  if (scan_buf(rdbuf, NOBODY_PLAYING, count) != NULL) {
#ifdef DEBUG
    printf("2591 no one is playing!\n");
#endif
    return (SS_EMPTY);
  }

  /* see if server is up, but no verbose info */
  if (scan_buf(rdbuf, SERVER_IN_USE, count) != NULL)
    return (SS_OPEN);

  /* okay, we got valid stuff, now parse it and stuff it into SERVER struct */
  if (!(pc = parse_playerlist(rdbuf, count)))
    return (-1);		/* was SS_EMPTY */
  else if (pc < 0)
    return (-1);
  else
    return (SS_OPEN);
}


/*
 * See if we have the info for MAX_PLAYERS players in rdbuf.  Returns (-1)
 * if we don't have all the info yet.  Returns 0 otherwise.  Exits with 0
 * if it encounters an SP_QUEUE packet.  Returns (-2) if the data is corrupted.
 *
 * We count SP_PLAYER because it's the last packet in the group sent for
 * each player.  If the server sends them in a different order than we could
 * have bogus data for the last player.  Since this is what ck_players does,
 * we break iff everything else breaks.
 *
 * To alleviate the CPU load, this keeps track of the byte offset of the
 * first interesting item in the buffer.  That way we don't have to seek
 * past the motd every time.  Whenever the data in the buffer changes (other
 * than appending new data), call this with buflen=0 to reset the stuff.
 *
 * Here's what the profiler showed after half a day of activity:
 *
 *	%time  cumsecs  #call  ms/call  name
 *	 24.9     8.58  17371     0.49  _read
 *	 23.3    16.62  19318     0.42  _select
 *	  5.5    18.51                  mcount
 *	  4.8    20.18  24272     0.07  __doprnt
 *	  4.8    21.84   1484     1.12  _connect
 *	  3.6    23.08  17371     0.07  _scan_packets	[]
 *	  3.3    24.21      1  1130.00  _main_loop	[]
 *	  3.0    25.23   3042     0.34  _close
 *	  2.9    26.23   1472     0.68  _write
 *	  2.0    26.91 176515     0.00  .rem
 *	  1.8    27.54  13274     0.05  _strpbrk
 *	  1.7    28.14    472     1.27  _parse_server	[]
 *
 * So this routine is still the most CPU-intensive in the program, but it's
 * relatively insignificant.
 */
int
scan_packets(buflen)
     int buflen;
{
  register unsigned char *buf;
  register int pos, type, size;
  int plcount = 0;
  static int first_off = 0;		/* opt */
  int neat;				/* opt */

  if (!buflen) {
    /* reset */
    first_off = 0;
    return;
  }
  pos = first_off;
  buf = (unsigned char *) (rdbuf + pos);

  if (pos > buflen) {
    log_msg("something whacked in scan_packets");
#ifdef DEBUG
    printf("scan_packets has initial pos = %d, buflen = %d\n", pos,buflen);
#endif
    return (-1);
  }

  neat = 0;
  while (pos < buflen) {
    type = (int) *buf;
    if (type < 1 || type > NUM_PACKETS) {
      log_msg("rcvd bad packet %d (0x%x) from %s (%d)", type, type,
	      servers[busy].hostname, servers[busy].port);
      return (-2);
    }
    size = handlers[type].size;
    if (size <= 0) {
      /* got bogus size, headed for infinite spin */
      log_msg("zero size in scan_packets from %s",
	      servers[busy].hostname);
      return (-2);
    }

    if (size+pos > buflen)
      return (-1);

    if (type != SP_MOTD)
      neat++;			/* don't advance first_off anymore */
    if (type == SP_PLAYER) {
      SERVER *serv = &servers[busy];
      {
	struct player_spacket *sp;
	sp = (struct player_spacket *) buf;
      }
      plcount++;
      if (verbose) printf("Got player # %d\n",plcount);
      if (plcount >= serv->max_players)
	return (0);
    }
    if (type == SP_QUEUE)
      return (0);

    buf += size;
    pos += size;
    if (!neat) first_off = pos;	/* start here next time */
  }

  return (-1);
}


/*
 * Parse the info we got from the server.  Returns status.
 */
SERVER_STATUS
parse_server(buflen)
     int buflen;
{
  SERVER *sp = &servers[busy];
  PLAYER *pp;
  struct queue_spacket *qp;
  struct plyr_login_spacket *plp;
  struct plyr_info_spacket *pip;
  struct pstatus_spacket *psp;
  register char *buf;
  register int pos;
  int type, size;
  int pl_seen = 0;

  buf = rdbuf, pos = 0;

  sp->player_count = 0;
  pp = sp->players;
  while (pos < buflen && pl_seen < sp->max_players) {
    type = (int) *buf;
    size = handlers[type].size;

    if (size+pos > buflen) {
#ifdef DEBUG
      printf("type = %d, size = %d, pos = %d, buflen = %d\n",
	     type, size, pos, buflen);
#endif
      return (-1);
    }

    switch (type) {
    case SP_QUEUE:
      qp = (struct queue_spacket *) buf;
      sp->queue_size = ntohs(qp->pos);
      return (SS_QUEUE);
    case SP_PL_LOGIN:
      plp = (struct plyr_login_spacket *) buf;
      if (plp->pnum < 0 || plp->pnum >= sp->max_players) break;
      strncpy(pp[plp->pnum].p_name, plp->name, 16);
      strncpy(pp[plp->pnum].p_monitor, plp->monitor, 16);
      strncpy(pp[plp->pnum].p_login, plp->login, 16);
      pp[plp->pnum].p_rank = plp->rank;
      break;
    case SP_PLAYER_INFO:
      pip = (struct plyr_info_spacket *) buf;
      if (pip->pnum < 0 || pip->pnum >= sp->max_players) break;
      pp[pip->pnum].ship_type = pip->shiptype;
      pp[pip->pnum].p_team = pip->team;
      pp[pip->pnum].p_mapchars[0] = teamlet[pip->team];
      break;
    case SP_PSTATUS:
      psp = (struct pstatus_spacket *) buf;
      if (psp->pnum < 0 || psp->pnum >= sp->max_players) break;
      pp[psp->pnum].p_mapchars[1] = shipnos[psp->pnum];
      if (psp->status >=0 && psp->status < MAX_STATUS) {
	pp[psp->pnum].p_status = psp->status;
      } else if (psp->status == PTQUEUE) {
	/* Paradise server tournament queue */
	pp[psp->pnum].p_status = PALIVE;
      } else {
	log_msg("rcvd bad status (%d) from %s", psp->status,
		servers[busy].hostname);
	pp[psp->pnum].p_status = PFREE;
      }
      /* this is kinda weak, but *we* show up as POUTFIT... */
      if (psp->status != PFREE && psp->status != POUTFIT)
	sp->player_count++;
      pl_seen++;
      break;
#ifdef GET_MASK		/* can't be done w/o full login */
    case SP_MASK:
      mp = (struct mask_spacket *) buf;
      sp->mask = mp->mask;
      mask_seen++;
      printf("mask = 0x%x\n", sp->mask);
      break;
#endif

    case SP_STATS:
    case SP_PLAYER:
    case SP_FLAGS:
    case SP_KILLS:
    case SP_HOSTILE:
    default:
      /* do nothing */
      size = size;	/* silly compiler */
    }

    buf += size;
    pos += size;
  }

  /* all done */
  if (!sp->player_count)
    return (SS_EMPTY);
  else
    return (SS_OPEN);
}


/*
 * Read & interpret data from the standard Netrek port.  This is a whole lot
 * of fun, because we don't want to block on the read() calls.
 *
 * Basic algorithm is:
 *   while (still hungry && room in buffer) {
 *       read more data
 *       if (we have all of the information for each player)
 *	     analyze it and return
 *	 else {
 *	     if we've got more room
 *		 return with -2 so we come back later
 *	     else
 *		 return with -1 (hopefully this won't happen!)
 *	 }
 *   }
 *
 * Two possible adjustments: one is to interpret the data as we go along
 * (but that quickly turns to spaghetti), the other is to filter out the
 * motd lines and scrunch the buffer up (which should work, but will raise
 * the CPU load because of all the bcopy() calls).  The latter approach will
 * be implemented, just in case some bozo has a 60K motd.  (idea: wait until
 * the buffer is >50% full before doing any scrunching.)
 *
 * Returns -1 on error, -2 if not yet done, status otherwise.
 */
SERVER_STATUS
read_server(sock)
     int sock;
{
  struct socket_cpacket sockPack;
  static char *buf;			/* buf & count stay across calls */
  static int count, extend_time;
  int cc, res;


  ssock = sock;
  if (new_conn) {
    new_conn = FALSE;
    buf = rdbuf;
    extend_time = 0;
    count = 0;
    scan_packets(0);		/* reset */

    sockPack.type=CP_SOCKET;
    sockPack.socket=htonl(65535);
    sockPack.version=(char) SOCKVERSION;
    if (sendServerPacket(&sockPack) < 0) {
      /* see if server really exists */
      if (errno != EPIPE) {
	perror("strange error on sendServerPacket");
	log_msg("strange server write 27 error %d with %s", errno,
		servers[busy].hostname);
      }
      servers[busy].why_dead = WD_CONN;	/* connection really failed */
      return (-1);
    }

    /* the server might not have data to send just yet */
    /* (minor improvement: put a select() call here)   */
    return (-2);
  }

  if ((cc = read(sock, buf, BUFSIZE-count)) < 0) {
    log_msg("server read error %d from %s", errno,
	    servers[busy].hostname);
#ifdef DEBUG
    perror("read_server read");
#endif
    servers[busy].why_dead = WD_READ;
    return (-1);
  }
#ifdef DEBUG
  printf("read, cc=%d\n", cc);
#endif
  if (!cc) {
    /* we've hit EOF, but we don't have all the data; assume server dead */
#ifdef DEBUG
    fprintf(stderr, "EOF hit early\n");
#endif
    if (verbose)
      log_msg("+ early EOF reading from %s", servers[busy].hostname);
    servers[busy].why_dead = WD_GARBAGE;
    return (-1);
  }
  buf += cc, count += cc;

  if (!extend_time && count > 1024) {
    busy_time += BUSY_EXTEN;	/* this could be in the future... */
    extend_time++;
  }

  /* FIX: add motd scrunch code here */

  if ((res = scan_packets(count)) < 0) {
    if (res == (-2)) {		/* corruption */
      servers[busy].why_dead = WD_GARBAGE;
      return (-1);
    }
    if (count < BUFSIZE)
      return (-2);		/* get more */
    else {
      if (verbose)fprintf(stderr,"Internal ERROR: read buffer flooded\n");
      log_msg("ERROR: read buffer flooded by server %s\n",
	      servers[busy].hostname);
      return (-1);		/* ran out of room! */
    }
  }

  /* hooray, we have all the info in the buffer! */
  return (parse_server(count));
}

