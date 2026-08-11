/*
 * scan.c - scan the various servers
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 */
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "meta.h"

/*
#ifndef getdtablesize


        int
getdtablesize ()
{
        return(sysconf(_SC_OPEN_MAX));
}

#endif
*/

/*
 * Server connection globals
 */
int busy = -1,
    last_busy = 0,
    busy_sock = -1,
    busy_time = 0;

SERVER prev_info;		/* previous info for busy server */

USER *users;

/*
 * Private variables
 */
static fd_set readfd_set, writefd_set;
static int maxfd;
static /*volatile*/ int coffee;	/* wake up and smell the */


/*
 * ---------------------------------------------------------------------------
 *	initialization/setup
 * ---------------------------------------------------------------------------
 */

/*
 * Find a host given on the server list.
 *
 * Strategy: convert the iphost to numeric form.  If that fails, do a host
 * name lookup on the doman name form.  If THAT fails, we're hosed.  We
 * check for !addr in case the user puts 0.0.0.0 (meaning "use the ascii
 * host").
 *
 * If you don't have both, just pass it twice... DNS names will fail the
 * numeric conversion.
 */
long
lookup(host, iphost)
char *host, *iphost;
{
    struct hostent *hp;
    long addr;

    if (!strncmp(host, "RSA", 3))	/* skip over leading "RSA" kluge */
	host += 3;

    if (((addr = inet_addr(iphost)) == -1) || !addr) {
	if (verbose) printf("--- Looking up %s\n", host);
        if ((hp = gethostbyname(host)) == NULL) {
            fprintf(stderr, "WARNING: unknown host %s in config file\n",
		host);
	    /*log_msg("unknown host %s on server list", host);   (not open!) */
            return(-1);
        } else {
            addr = *(long *) hp->h_addr;
        }
    }

    return (addr);
}


/*
 * Wake up and smell the server (SIGALRM handler).
 */
int
wakeup()
{
    coffee = TRUE;
}


/*
 * Do some silly setup stuff.
 */
void
init_connections()
{
    maxfd = getdtablesize();
    if (maxfd > FD_SETSIZE)
	maxfd = FD_SETSIZE;
    if (verbose) printf("%s: maximum file descriptor is %d\n", prog, maxfd);

    /* one struct for every fd */
    if ((users = (USER *) malloc(maxfd * sizeof(USER))) == NULL) {
	fprintf(stderr, "malloc failure (users)\n");
	log_msg("exit: malloc failure in init_connections\n");
	exit(1);
    }
    bzero(users, maxfd * sizeof(USER));		/* force start_time, buf to 0 */

#ifdef DEBUG2
    int i;

    for (i = 0; i < server_count; i++) {
	printf("%.2d: %d '%s' '%s' 0x%.8lx %d '%s'\n", i,
		servers[i].status,
		servers[i].hostname,
		servers[i].ip_addr,
		servers[i].addr,
		servers[i].port,
		servers[i].comment);
    }
#endif
}


/*
 * ---------------------------------------------------------------------------
 *	low-level connection stuff
 * ---------------------------------------------------------------------------
 */

/* debugging */
printPeerInfo(sock)
int sock;
{
    struct sockaddr_in addr;
    int len;

    len = sizeof(addr);
    if (getsockname(sock, &addr, &len) < 0) {
/*      perror("printUdpInfo: getsockname");*/
        return;
    }
    log_msg("+ LOCAL: addr=0x%x, family=%d, port=%d\n", addr.sin_addr.s_addr,
        addr.sin_family, ntohs(addr.sin_port));

    if (getpeername(sock, &addr, &len) < 0) {
/*      perror("printUdpInfo: getpeername");*/
        return;
    }
    log_msg("+ PEER : addr=0x%x, family=%d, port=%d\n", addr.sin_addr.s_addr,
        addr.sin_family, ntohs(addr.sin_port));
}


/*
 * write a bunch of data
 */
/*static*/ int
write_data(fd, buf, count)
int fd;
register char *buf;
register int count;
{
    register long n;
    int orig = count;

    for (; count ; buf += n, count -= n) {
	if ((n = write(fd, buf, count)) < 0) {
	    perror("write_data");
	    if (errno == EPIPE)
		log_msg("%.2d unable to connect (EPIPE)", fd);
	    else
		log_msg("%.2d write error %d", fd, errno);
	    return (-1);
	}
    }
    return (orig);
}


/*
 * We have a new connection from a user.  Get the requested information and
 * add the user to the list of fds we watch for writeability.  Don't actually
 * transmit any data yet.
 *
 * If it looks like a particular host is flooding us (> 3 calls in 10 seconds),
 * ignore him.  This is a rudimentary scheme which gets reset if somebody else
 * calls, so a flooder could just call from two different hosts at the same
 * time.
 *
 * This does NOT reset the flood_time on every rejected call, so after 10
 * seconds all is (mostly) forgiven.  This avoids the problem of somebody
 * who likes to bang his head against the wall.
 *
 * "sock" is user's socket, "idx" is service index.
 */
void
new_user(idx, sock)
int idx, sock;
{
    EXCLUDE *ep;
    struct sockaddr_in addr, from;
    struct hostent *hp;
    struct linger ling;
    char *host;
    int csock, port;
    int i, length, on=1;
    static time_t flood_start = 0;
    static int flood_count = 0, flood_addr = 0;
    time_t now;

#ifdef DEBUG
    printf("Got client connection!\n");
#endif
    length = sizeof(addr);
    if ((csock = accept(sock, &addr, &length)) < 0) {
	/* this can happen if we get too many connections, or file tab ovfl */
	log_msg("user accept failed, err=%d\n", errno);
	/*perror("WARNING: accept failed");*/
	return;
    }
#ifdef DEBUG
    log_msg("Accept opened (s=%d)\n",csock);
#endif
    /* block gatech twinks */
#define GABASE	0x82cfb164	/* 130.207.177.100+N == richsunN.gatech.edu */
#define GAMAX	20		/* don't think it goes higher than this */
/*
    if (addr.sin_addr.s_addr > GABASE && addr.sin_addr.s_addr < GABASE+GAMAX) {
	close(csock);
	log_msg("EXCLUDE richsun%d.gatech.edu (0x%.8lx) svc %d\n",
	    addr.sin_addr.s_addr - GABASE,
	    addr.sin_addr.s_addr,
	    idx);
	return;
    }
*/
    /* check exclude list */
    if (exclude_count) {
	for (i = 0, ep = excludes; i < exclude_count; i++, ep++) {
	    if (addr.sin_addr.s_addr == ep->addr) {
		close(csock);
		log_msg("EXCLUDE 0x%.8lx (#%d on svc %d)",
		    addr.sin_addr.s_addr, i, idx);
		return;
	    }
	}
    }

    /*
     * TEMPORARY: to facilitate the change from charon.amdahl.com to
     * csuchico, a packet pass-thru on charon may be used for a little
     * while.  Need to ignore floods from charon because it won't be
     * clear where the requests are really coming from.
     *
     * charon.amdahl.com == 129.212.11.1 from outside Amdahl
     */
    if (addr.sin_addr.s_addr == 0x81d40b01)
	goto skip_flood;

    /* check for floods */
    if (addr.sin_addr.s_addr == flood_addr) {
	/* at least two in a row */
	now = time(0);
	if (!flood_start) flood_start = now;

	if (now > flood_start + FLOOD_TIME) {
	    /* he's moving slowly, but keep an eye on him */
	    flood_start = now;
	    flood_count = -1;
	}
	if (++flood_count >= MAX_CALLS) {
	    /* he's flooding, knock him */
	    close(csock);
	    log_msg("FLOOD 0x%.8lx (svc %d)", addr.sin_addr.s_addr, idx);
	    return;
	}
    } else {
	/* flood_start is set to 0 to avoid extra time(0) call */
	/* (slightly silly, but we're really only worried about psychos) */
	flood_addr = addr.sin_addr.s_addr;
	flood_start = flood_count = 0;
    }

skip_flood:


#ifdef DEBUG
    length = sizeof(ling);
    if (getsockopt(csock, SOL_SOCKET, SO_LINGER, &ling, &length) < 0)
	perror("getsockopt");
    printf("(len = %d) linger on=%d, time=%d\n", length,
	    ling.l_onoff, ling.l_linger);
#endif
    ling.l_onoff = 1;	/* turn it on */
    ling.l_linger = /*3*/  10;
    length = sizeof(ling);
    if (setsockopt(csock, SOL_SOCKET, SO_LINGER, &ling, length) < 0)
	perror("setsockopt (SO_LINGER)");
    if (setsockopt(csock, SOL_SOCKET, SO_OOBINLINE, &on, sizeof(int)) < 0)
	perror("setsockopt (SO_OOBINLINE)");

    /* figure out who we're connected to for logging purposes */
    length = sizeof(from);
    if (getpeername(csock, (struct sockaddr *) &from, &length) < 0) {
	host = "unknown";
	port = 0;
    } else {
	/*
	 * IMPORTANT:
	 * The gethostbyaddr() call can take several seconds to complete
	 * when the networks are being flaky.  This is why sometimes a
	 * connection to MS-II will appear to hang for a few seconds.
	 */
	now = time(0);		/* just curious */
	hp = NULL;
	if (log_dns_names)
	    hp = gethostbyaddr(
		(char *)&from.sin_addr.s_addr, sizeof(long), AF_INET);
	if (hp != NULL)
	    host = hp->h_name;
	else
	    host = inet_ntoa(from.sin_addr);
	port = ntohs(from.sin_port);

	/* long DNS lookups could cause a server connection to time out */
	i = time(0) - now;
	if (busy >= 0)
	    busy_time += i;

	/* just for kicks, display it in the log file */
	if ((i = time(0) - now) > 10) {
	    if (busy < 0)
		log_msg("Lookup took %d seconds", i);
	    else
		log_msg("Lookup took %d seconds; extended server conn", i);
	}
    }
    log_msg("Conn to svc %d from %s (%d)  (s=%d)", idx, host, port, csock);

    switch (ports[idx].kind) {
    case DI_OLD:
	display_old(csock, idx);
	break;
    case DI_NEW:
	display_new(csock, idx);
	break;
    case DI_WEB:
	display_web(csock, idx);
	break;
    case DI_VERBOSE:
	display_verbose(csock, idx);
	break;
    case DI_INFO:
	display_info(csock, idx);
	break;
    default:
	log_msg("Internal ERROR: unknown service %d", ports[idx].kind);
	/*fprintf(stderr, "Internal ERROR: got call on strange socket\n");*/
	close(csock);
	return;
    }

    users[csock].start_time = time(0);
    FD_SET(csock, &writefd_set);
}


/*
 * Handle a user connection.
 *
 * Calls close_user() after all data is written.
 */
/*static*/ int
handle_user(fd)
{
    int cc;

    if ((cc = write(fd, users[fd].buf+users[fd].pos, users[fd].data_size)) < 0){
#ifdef DEBUG
	perror("write in handle_user");
#endif
	log_msg("write error in handle_user = %d", errno);
	close_user(fd);
	return (-1);
    }
#ifdef DEBUG
    printf("wrote %d bytes to user (%d)\n", cc, users[fd].data_size);
#endif
    users[fd].pos += cc;
    users[fd].data_size -= cc;

    if (!users[fd].data_size) {
	/* all done! */
	close_user(fd);
	return (1);
    }
    return (0);
}


/*
 * Close a user connection.
 */
/*static*/ int
close_user(fd)
{
    if (users[fd].data_size) {
	log_msg("hosed user on fd %d after %d seconds\n", fd,
	    time(0) - users[fd].start_time);
    }
    close(fd);			/* this can linger */
#ifdef DEBUG
	log_msg("3:closed fd (s=%d)\n",fd);
#endif
    FD_CLR(fd, &writefd_set);
    FD_CLR(fd, &readfd_set);
    free(users[fd].buf);  users[fd].buf = NULL;
    users[fd].start_time = 0;	/* mark as inactive */

    return (0);
}


/*
 * Open a new connection to a server.  Doesn't actually do anything; just
 * opens it non-blocking.
 *
 * Set busy before calling.  Sets busy_sock.
 */
/*static*/ int
open_server()
{
    struct sockaddr_in addr;
    int sock;
    int flags;

#ifdef DEBUG2
    { int i;
    for (i = 0; i < server_count; i++) {
	printf("%d: %s\n", i, servers[i].hostname);
    }}
#endif

    if (servers[busy].cstate != CS_CLOSED)
	fprintf(stderr, "Internal WARNING: cstate != CLOSED\n");
    if (verbose)
	printf("%s: Opening connection to #%d: %s (%d)\n", prog, busy,
	    servers[busy].hostname, servers[busy].port -
	    ((servers[busy].pstate == PS_PORTM) ? 1 : 0));

    /* establish TCP connection to server */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
#ifdef DEBUG
	perror("socket (server connection)");
#endif
	log_msg("socket() failed, err=%d\n", errno);
	return (-1);
    }

    if ((flags = fcntl(sock, F_GETFL, 0)) < 0) {
	/*perror("unable to get flags for NB");*/
	log_msg("fcntl(get) failed, err=%d\n", errno);
	return(-1);
    }
    if (fcntl(sock, F_SETFL, flags|O_NDELAY) < 0) {
	/*perror("unable to set NB mode for connect");*/
	log_msg("fcntl(set) failed, err=%d\n", errno);
	return (-1);
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = servers[busy].addr;
    if (servers[busy].pstate == PS_PORTM)
	addr.sin_port = htons(servers[busy].port-1);	/* try port-1 */
    else
	addr.sin_port = htons(servers[busy].port);	/* do normal port */
    if (connect(sock, &addr, sizeof(addr)) < 0) {
	if (errno != EINPROGRESS) {
#ifdef DEBUG
	    perror("connect (server connection)");
#endif
	    log_msg("connect() failed in open_server, err=%d", errno);
	    close(sock);
	    return (-1);
	} else {
	    /* connect has not taken effect yet */
	    FD_SET(sock, &writefd_set);
	    FD_CLR(sock, &readfd_set);
	    servers[busy].cstate = CS_CONNECTING;
	}
    } else {
	/* connect succeeded immediately */
	FD_SET(sock, &readfd_set);
	servers[busy].cstate = CS_CONNECTED;
    }

    busy_sock = sock;

    new_conn = TRUE;		/*tell server.c that this is a new connection*/
    mnew_conn = TRUE;

    return (0);
}


/*
 * Looks like the server is finally connected.  Add it to the readfd list.
 */
/*static*/ int
server_connected(fd)
int fd;
{
    int flags;

    if (servers[busy].cstate != CS_CONNECTING)
	fprintf(stderr, "Internal WARNING: cstate != CONNECTING\n");
    FD_CLR(fd, &writefd_set);
    FD_SET(fd, &readfd_set);
    servers[busy].cstate = CS_CONNECTED;

    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
	perror("unable to get flags for NB clear (sc)");
	return;
    }
    if (fcntl(fd, F_SETFL, flags & (~O_NDELAY)) < 0) {
	perror("unable to set NB mode for connect (sc)");
	return;
    }

    /* note that the log message is also a "verbose" thang */
    if (verbose) {
	printf("%s: server connection established (%d)\n", prog, fd);
	log_msg("connected to %s (%d)", servers[busy].hostname,
		servers[busy].port -
		((servers[busy].pstate == PS_PORTM) ? 1 : 0));
    }
    return (0);
}


/*
 * Close connection to server.  Clears busy_sock.
 */
void
close_server()
{
    SERVER *sp;

    /* close the connection */
#ifdef DEBUG
    printf("Clearing busy_sock (%d)\n", busy_sock);
#endif
    close(busy_sock);
    FD_CLR(busy_sock, &readfd_set);
    FD_CLR(busy_sock, &writefd_set);
    busy_sock = -1;
    sp = &servers[busy];
    sp->cstate = CS_CLOSED;
}


/*
 * ---------------------------------------------------------------------------
 *	higher-level connection stuff
 * ---------------------------------------------------------------------------
 */

/*
 * Prepare a server connection.  Opens the first connection and sets up
 * the struct.
 */
int
prep_server()
{
    SERVER *sp;
    int i;

    sp = &servers[busy];

    /* hack to avoid the "(scanning server now)" message */
    bcopy(sp, &prev_info, sizeof(SERVER));

    sp->last_update = time(0);		/* in case we get a SS_NOCONN */

    /* clear out all the player info */
    sp->player_count = sp->queue_size = 0;
    for (i = 0; i < sp->max_players ; i++)
	sp->players[i].p_status = PFREE;

    sp->status = SS_WORKING;
    sp->why_dead = WD_UNKNOWN;

    if (try_alt_port)
	sp->pstate = PS_PORTM;
    else
	sp->pstate = PS_PORT;

    busy_time = time(0);
    if (open_server() < 0) {
	log_msg("open_server failed for %s", servers[busy].hostname);
	sp->status = SS_NOCONN;
	sp->why_dead = WD_CONN;
#ifdef DEBUG
	printf("Down_time = %d, new Down_time = %d\n",sp->down_time,busy_time);
#endif
	if (sp->down_time == 0) 
		sp->down_time = busy_time;
	/* time normally done in nuke_server, but we won't get that far */
	sp->next_update = time(0) + wait_noconn*60;
	return (-1);
    }

    return (0);
}


/*
 * Server stuff in progress.
 */
int
handle_server(fd)
{
    int cc;

    if (fd != busy_sock) {
	fprintf(stderr, "Internal ERROR: fd != busy sock in handle_server\n");
	fprintf(stderr, "fd = %d, busy_sock = %d\n", fd, busy_sock);
	log_msg(
	    "exit: Internal ERROR: fd != busy_sock in handle_server (%d, %d)\n",
	    fd, busy_sock);
	exit(1);
    }

    if (servers[busy].pstate == PS_PORTM) {
	if ((cc = read_minus(fd)) < 0) {
	    if (cc == -2) {
		/* not finished reading yet */
		return;
	    }
	    /* port-1 doesn't exist; move on to normal port */
#ifdef DEBUG
	    printf("No port-1 for this server\n");
#endif
	    close_server();
	    servers[busy].pstate = PS_PORT;
	    if (open_server() < 0) {
		log_msg("open_server failed (a)");
		servers[busy].status = SS_NOCONN;
		servers[busy].why_dead = WD_CONN;
#ifdef DEBUG
		printf("Down_time = %d, new Down_time = %d\n",servers[busy].down_time,time(0));
#endif
		if (servers[busy].down_time == 0)
                	servers[busy].down_time = time(0);
		nuke_server();
	    }
	} else if (cc == SS_EMPTY) {
	    /* port exists, but server is empty */
	    servers[busy].status = SS_EMPTY;
	    close_server();
	    nuke_server();
	} else {
	    /* we got info back; go look for wait queue or better info */
	    close_server();
#ifndef USE2592TOO
	    servers[busy].pstate = PS_PORT;
	    if (open_server() < 0) {
		/* weird... 2591 is up, but 2592 isn't */
		log_msg("open_server failed (b)");
		servers[busy].status = SS_NOCONN;
		servers[busy].why_dead = WD_CONN;
#ifdef DEBUG
                printf("Down_time = %d, new Down_time = %d\n",servers[busy].down_time,time(0));
#endif
		if (servers[busy].down_time == 0)
                	servers[busy].down_time = time(0);
		nuke_server();
	    }
#else
	    servers[busy].status = SS_OPEN;
	    nuke_server();
#endif
	}

    } else if (servers[busy].pstate == PS_PORT) {
	if ((cc = read_server(fd)) < 0) {
	    if (cc == -2) {
		/* not finished reading from server; keep going */
		return;
	    }
	    /* server must not exist */
#ifdef DEBUG
	    printf("Server dead\n");
#endif
	    servers[busy].status = SS_NOCONN;
#ifdef DEBUG
                printf("Down_time = %d, new Down_time = %d\n",servers[busy].down_time,time(0));
#endif
	    if (servers[busy].down_time == 0)
               	servers[busy].down_time = time(0);
	    /*servers[busy].why_dead = WD_CONN;*/	/* set by r_s() */
	    close_server();
	    nuke_server();
	} else {
	    /* success! */
#ifdef DEBUG
	    printf("Success: status %d\n", cc);
#endif
	    servers[busy].down_time = 0;
	    servers[busy].status = cc;
	    close_server();
	    nuke_server();
	}

    } else {
	fprintf(stderr, "Internal ERROR: bogus pstate in handle_server\n");
	log_msg("exit: Internal ERROR: bogus pstate in handle_server");
	exit(1);
    }
}


/*
 * Update the structures associated with the server connection.  Assumes
 * that the sockets have been closed.  The "status" field should be set.
 *
 * Clears "busy"; do not use it after calling this.
 *
 * This also sets various flags, so the "status" field should be set before
 * calling here.
 */
int
nuke_server()
{
    SERVER *sp;
    int plustime;

    sp = &servers[busy];

    switch (sp->status) {
    case SS_NOCONN:
	plustime = wait_noconn;
	break;
    case SS_EMPTY:
	plustime = wait_empty;
	break;
    case SS_OPEN:
	plustime = wait_open;
	break;
    case SS_QUEUE:
	plustime = wait_queue;
	break;
    default:
	fprintf(stderr, "Internal ERROR: strange 'why' in nuke_server\n");
	break;
    }

    /* update the struct */
    sp->last_update = time(0);
    sp->next_update = time(0) + plustime*60;

    /* tell it to pick another real soon */
    coffee = TRUE;

    /* while we're here, update the server's display_flags */
    if (sp->status == SS_QUEUE || sp->player_count >= 8)
	sp->display_flags |= DF_HAD_TMODE;
    if (sp->status != SS_NOCONN)
	sp->display_flags &= ~(DF_ALWAYS_DEAD);

    if (verbose) printf("%s: server conn closed (status %d)(%d); next at %s", prog,
	sp->status, sp->down_time, ctime(&sp->next_update));

    busy = -1;
    return (0);
}


/*
 * Check the status of our server-scanning activities.  Also looks for
 * user connections which have been open too long.
 */
int
check_status()
{
    register USER *up;
    time_t now;
    register int i;
    static time_t last_user_check;		/* don't nuke the CPU */

    now = time(0);

    /* time out stale user connections */
    if (now - last_user_check > MAX_USER) {
	for (i = 0, up = users; i < maxfd; i++, up++) {
	    if (up->start_time && verbose) printf("active user %d\n", i);
	    if (up->start_time && (now - up->start_time >= MAX_USER)){
		close_user(i);
		if (verbose) printf("%s: hosed %d\n", prog, i);
	    }
	}

	last_user_check = now;
    }

    /* check up on the server connection */
    now = time(0);
    if (busy >= 0) {
	/* see how long we've been busy (note busy_time can be > now) */
	if (now - busy_time > MAX_BUSY) {
	    /* we must've stalled... hose this one */
	    /* there may be a problem with playerlist's getting stuck. */
	    /* Don't know what to do about this yet so we'll just complain */
	    /* and watch and see.. */
	    if (servers[busy].player_count > 0) 
		log_msg("Server's playerlist is Hosed. Got %d from %s\n",
			servers[busy].player_count,servers[busy].hostname);
#ifdef DEBUG
	    printf("Server not responding\n");
#endif
	    if (verbose) log_msg("Out of time for %s", servers[busy].hostname);
	    servers[busy].status = SS_NOCONN;
	    servers[busy].why_dead = WD_TIMEOUT;
#ifdef DEBUG
                printf("Down_time = %d, new Down_time = %d\n",servers[busy].down_time,time(0));
#endif
	    if (servers[busy].down_time == 0)
                servers[busy].down_time = now;
	    close_server();
	    nuke_server();
	    /* fall through and pick another */
	} else {
	    /* doing just fine */
#ifdef DEBUG
	    printf("Status checked\n");
#endif
	    return;
	}
    }

    if (busy >= 0) {
	fprintf(stderr, "Internal ERROR: still busy??\n");
	log_msg("exit: Internal ERROR: still busy?");
	exit(3);
    }

    /* we're not busy, so find somebody to prod */
    i = (last_busy+1) % server_count;
    do {
	if (servers[i].next_update <= now) {
	    /* got one */
	    busy = i;
	    if (prep_server() >= 0)
		break;
	    busy = -1;
	    /* get here when connection was refused (network problems) */
	}
	i = (i+1) % server_count;
    } while (i != (last_busy+1) % server_count);

    if (busy < 0) return;		/* didn't find one, go sulk */
    last_busy = busy;			/* got one, update last_busy */
}

/* Maximum size of a UDP packet accepted */
/* ??? need to have this documented and agreed */
#define MAXMETABYTES 1024

/* Port on metaserver for incoming UDP */
#define USOCKPORT 3521

void
uread(int usock)
{
    char packet[MAXMETABYTES];
    struct hostent *hp;
    struct sockaddr_in from;
    unsigned int fromlen = sizeof(from);
    int bytes;
    SERVER *sp, srvbuf;
    char *p;
    int i;

    /* read the UDP packet from the server */
    bytes = recvfrom(usock, packet, MAXMETABYTES, 0, (struct sockaddr *)&from,
        &fromlen );
    if (bytes < 0) {
        perror("uread: recvfrom");
	return;
    }

    /* initialise our server structure */
    srvbuf.status = SS_OPEN;
    srvbuf.last_update = time(NULL);
    srvbuf.next_update = srvbuf.last_update+3600;
    srvbuf.addr = from.sin_addr.s_addr;

    /* null terminate it so we can use strtok on it */
    packet[bytes] = 0;
#ifdef UREADTRACE
    log_msg("%s %s", inet_ntoa(srvbuf.addr), packet );
#endif

    /* version */
    p = strtok(packet, "\n");
    if (p == NULL) return;
    if (p[0] != 'a') {
	log_msg("uread: bad version %02.2x from %s\n", p[0],
	    inet_ntoa(srvbuf.addr));
	return;
    }

    /* address (DNS, as stated by server within packet) */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    strcpy(srvbuf.hostname, p);

    /* address (IP, as received on socket) */
    p = inet_ntoa ( from.sin_addr );
    strcpy(srvbuf.ip_addr, p);

    /* type */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    srvbuf.type[0] = p[0];

    /* port */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    srvbuf.port = atoi(p);

    /* observer port (ignored) */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;

    /* number of players */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    srvbuf.player_count = atoi(p);
    if (srvbuf.player_count == 0) srvbuf.status = SS_EMPTY;

    /* number of free slots */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    if (atoi(p) > 0) srvbuf.max_players = srvbuf.player_count + atoi(p);
    srvbuf.queue_size = -atoi(p);
    if (srvbuf.queue_size < 0) srvbuf.queue_size = 0;
    if (srvbuf.queue_size > 0) srvbuf.status = SS_QUEUE;

    /* t-mode */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    srvbuf.display_flags = 0;
    if (p[0] == 'y') {
        srvbuf.display_flags |= DF_HAD_TMODE;
    }

    /* comment */
    p = strtok(NULL, "\n");
    if (p == NULL) goto truncated;
    strncpy(srvbuf.comment, p, 40);

    /* parse player list */
    if (srvbuf.player_count > 0)
    {
	int i; /* player number */

	for (i=0; i<MAX_PLAYER; i++) srvbuf.players[i].p_status = PFREE;

	for(;;)
	{
	    /* slot number */
	    p = strtok(NULL, "\n");
	    if (p == NULL) break;

	    /* determine offset in our player structure, give up if bad */
	    for (i=0; i<MAX_PLAYER; i++) if (shipnos[i] == p[0]) break;
	    if (i == MAX_PLAYER) break;

	    /* save the second map character */
	    srvbuf.players[i].p_mapchars[1] = p[0];

	    /* team letter */
	    p = strtok(NULL, "\n");
            if (p == NULL) break;
            srvbuf.players[i].p_mapchars[0] = p[0];
	    switch (p[0])
	    {
		/* pathetic hack */
		case 'I': srvbuf.players[i].p_team = 0; break;
		case 'F': srvbuf.players[i].p_team = 1; break;
		case 'R': srvbuf.players[i].p_team = 2; break;
		case 'K': srvbuf.players[i].p_team = 4; break;
		case 'O': srvbuf.players[i].p_team = 8; break;
		default : srvbuf.players[i].p_team = 7; break;
	    }

	    /* ship class */
            p = strtok(NULL, "\n");
            if (p == NULL) break;
            srvbuf.players[i].ship_type = atoi(p);
	    /* ??? note change from design, ship type number not string */

	    /* rank */
            p = strtok(NULL, "\n");
            if (p == NULL) break;
            srvbuf.players[i].p_rank = atoi(p);
	    /* ??? note change from design, rank number not string */

	    /* name */
            p = strtok(NULL, "\n");
            if (p == NULL) break;
            strncpy(srvbuf.players[i].p_name, p, 16);

	    /* login */
            p = strtok(NULL, "@");
            if (p == NULL) break;
            strncpy(srvbuf.players[i].p_login, p, 16);

	    /* monitor */
            p = strtok(NULL, "\n");
            if (p == NULL) break;
            strncpy(srvbuf.players[i].p_monitor, p, 16);

	    srvbuf.players[i].p_status = PALIVE;
	}
    }

    /* now either update an existing server entry or create a new one */
    for (i = 0, sp = servers; i < server_count; i++, sp++) {
        if (!strcmp(srvbuf.hostname, sp->hostname) &&
            !strcmp(srvbuf.ip_addr, sp->ip_addr) &&
            (srvbuf.type[0] == sp->type[0]) &&
            srvbuf.port == sp->port) {
		/* we know about the server already */

		/* check for flooding */
		if (srvbuf.player_count != 0) {
		    if ((srvbuf.last_update - sp->last_update) < 
			MIN_UREAD_TIME) {

			/* server flooding, delist it */
			sp->status = SS_NOCONN;
			sp->why_dead = WD_FLOODING;
			sp->last_update = srvbuf.last_update;
			return;
		    }
		}
		/* note above: player count zero is the only condition that
		server will violate our time limit, and we hardly expect any
		server operators to _want_ to use player count zero flooding */

		/* server is being nice, accept the update and return */
		bcopy(&srvbuf, sp, sizeof(SERVER));
		log_msg("usock: updated by %s (%s)\n", srvbuf.hostname, 
		    srvbuf.ip_addr );
		return;
        }
    }

    if (servers == NULL) {
        servers = (SERVER *) malloc(sizeof(SERVER));
    } else {
        servers = (SERVER *) realloc(servers,
                sizeof(SERVER) * (server_count+1));
    }
    if (servers == NULL) {
        fprintf(stderr, "ERROR: uread: malloc/realloc failure (servers)\n");
        exit(1);
    }

    bcopy(&srvbuf, &servers[server_count], sizeof(SERVER));
    server_count++;

    log_msg("usock: new server %s (%s)\n", srvbuf.hostname, srvbuf.ip_addr );
    return;

truncated:
    log_msg("uread: truncated packet of %d bytes from %s\n", bytes,
        inet_ntoa(srvbuf.addr));
    return;
}

/*
 * Basic strategy: do blocking I/O until we get everything we want.  Wake up
 * once every 60 seconds to check our status and look for more work to do.
 *
 * To keep the bookkeeping simple, we only communicate with one Netrek server
 * at a time.  It's certainly possible to do all at once, but why bother?
 */
void
main_loop()
{
    register int i, j;
    struct sockaddr_in addr;
    int on = 1;
    fd_set readfds, writefds;
    int cc, lsock, *listen_sock = NULL;
    int usock;
    struct itimerval udt;

#ifdef DEBUG2
    for (i = 0; i < server_count; i++) {
	printf("%d: %s\n", i, servers[i].hostname);
    }
#endif

    /* init the fd set */
    FD_ZERO(&readfd_set);
    FD_ZERO(&writefd_set);

    /* create UDP socket for servers to send us updates */
    if ((usock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("ERROR: unable to create UDP socket for server reports");
        log_msg("exit: unable to create UDP socket");
        exit(1);
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(USOCKPORT);
    if (bind(usock, &addr, sizeof(addr)) < 0) {
        perror("ERROR: unable to bind to UDP socket");
        log_msg("exit: unable to bind to UDP socket");
        exit(1);
    }
    FD_SET(usock, &readfd_set);

    /* Prepare sockets to accept calls */
    if ((listen_sock = (int *) malloc(port_count * sizeof(int))) == NULL) {
	fprintf(stderr, "ERROR: malloc failure (main/listen)\n");
	exit(1);
    }
    for (i = 0; i < port_count; i++) {
	if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	    perror("ERROR: unable to create socket to listen to");
	    log_msg("exit: unable to create listen socket");
	    exit(1);
	}
	setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(ports[i].port);
	if (bind(lsock, &addr, sizeof(addr)) < 0) {
	    perror("ERROR: unable to bind to socket");
	    log_msg("exit: unable to bind to listen socket");
	    exit(1);
	}
	if (listen(lsock, 5) < 0) {
	    perror("ERROR: unable to listen to socket\n");
	    log_msg("exit: unable to listen to listen socket");
	    exit(1);
	}
	FD_SET(lsock, &readfd_set);

	listen_sock[i] = lsock;
	if (verbose) printf("%s: Listening on port %d\n", prog,
	    ntohs(addr.sin_port));
    }

    if (verbose) printf("--- Ready to accept connections\n");

    coffee = TRUE;	/* start right away */
    udt.it_interval.tv_sec = TIME_RES;
    udt.it_interval.tv_usec = 0;
    udt.it_value.tv_sec = TIME_RES;
    udt.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &udt, 0);


    while (1) {
	if (coffee) {
	    /* got a wakeup signal, check status of server probe */
	    check_status();
	    coffee = FALSE;
	}
	bcopy(&readfd_set, &readfds, sizeof(fd_set));
	bcopy(&writefd_set, &writefds, sizeof(fd_set));
	if ((cc = select(maxfd, &readfds, &writefds, NULL, NULL)) <= 0) {
#ifdef DEBUG
	    if (cc < 0) {
		fprintf(stderr, "%s: ", prog);
		perror("select");
		/* usually an interrupted system call */
	    }
#endif
	    continue;
	}
	if (verbose) fseek(stdout, (off_t) 0, 2);

	/* activity on servers' UDP socket, read it */
	if (FD_ISSET(usock, &readfds)) {
	    uread(usock);
	    FD_CLR(usock, &readfds);
	}

	/* cc is #of interesting file descriptors */
	for (i = 0; i < maxfd && cc; i++) {
	    if (FD_ISSET(i, &readfds)) {
		for (j = 0; j < port_count; j++) {
		    /* is it a user request? */
		    if (i == listen_sock[j]) {
			new_user(j, i);	/* port index, fd */
			break;
		    }
		}
		if (j == port_count) {
		    /* must be news from the server we're chatting with */
		    handle_server(i);
		}
		cc--;
	    } else if (FD_ISSET(i, &writefds)) {
		if (i == busy_sock) {
		    /* finally got ahold of the darn server */
		    server_connected(i);
		    handle_server(i);
		} else {
		    /* looks like a user is writeable */
		    handle_user(i);
		}
		cc--;
	    }

	}

	if (verbose) fflush(stdout);
    }

    /*NOTREACHED*/
}
