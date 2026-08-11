

/* meta.c
 * 
 * - Nick Trown         May 1993        Original Version. - Andy McFadden
 * May 1993 ?   Connect to Metaserver. - BDyess (Paradise)      ???
 * ug Fixes.  Add server type field. - Michael Kellen   Jan 1995        Don't
 * list Paradise Servers. List empty servers. - James Soutter   Jan 1995
 * Big Parsemeta changes.  Included some Paradise Code.  Added Known Servers
 * Option.  Added option for metaStatusLevel.  Bug Fixes.
 * - Jonathan Shekter Aug 1995 -- changed to read into buffer in all cases,
 * use findfile() interface for cachefiles.
 */

#undef DEBUG

#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/select.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>



/* Constants */

#define BUF     6144
#define LINE    80                       /* Width of a meta-server
                                                  * line */
/* Local Types */

#define LONG long

struct servers
  {
    char    address[LINE];
    int     port;
    int     age;
    int     players;
    int     status;
    char    typeflag;
  };

struct servers *serverlist = NULL;               /* The record for each
                                                  * server.      */
int     num_servers = 0;                     /* The number of servers.       */


char   *metaWindowName;                      /* The window's name.           */

char   metaserver[256] = "metaserver.netrek.org";
int    metaport = 3521;

/* The status strings:  The order of the strings up until statusNull is
 * important because the meta-client will display all the strings up to a
 * particular point.
 * 
 * The strings after statusNull are internal status types and are formatted
 * separatly from the other strings.
 * 
 * The string corresponding to "statusNull" is assigned to thoes servers which
 * have "statusNobody" or earlier strings in old, cached, meta-server data. */

char   *statusStrings[] =
{"OPEN:", "Wait queue:", "Nobody", "Timed out", "No connection",
 "Active", "CANNOT CONNECT", "DEFAULT SERVER"};

enum statusTypes
  {
    statusOpen = 0, statusWait, statusNobody, statusTout, statusNoConnect,
    statusNull, statusCantConnect, statusDefault
  };

const int defaultStatLevel = statusTout;


/* Functions */

extern void terminate(int error);

static int open_port(char *host, int port, int verbose)
/* The connection to the metaserver is by Andy McFadden. This calls the
 * metaserver and parses the output into something useful */
{
  struct sockaddr_in addr;
  struct hostent *hp;
  int     sock;


  /* Connect to the metaserver */
  /* get numeric form */
  if ((addr.sin_addr.s_addr = inet_addr(host)) == -1)
    {
      if ((hp = gethostbyname(host)) == NULL)
        {
          if (verbose)
            fprintf(stderr, "unknown host '%s'\n", host);
          return (-1);
        }
      else
        {
          addr.sin_addr.s_addr = *(LONG *) hp->h_addr;
        }
    }
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      if (verbose)
        perror("socket");
      return (-1);
    }
  if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0)
    {
      if (verbose)
        perror("connect");
      close(sock);
      return (-1);
    }
  return (sock);
}


static void parseInput(char *in, FILE * out, int statusLevel)
/* Read the information we need from the meta-server. */
{
  char    line[LINE + 1], *numstr, *point, **statStr;
  struct servers *slist;
  int     rtn, max_servers;
  int     count;

#ifdef DEBUG
   printf("In parseInput\n");
#endif   
   
  /* Create some space to hold the entries that will be read.  More space can
   * be added later */

  serverlist = (struct servers *) malloc(sizeof(struct servers) * 10);

  max_servers = 10;
  num_servers = 0;


  /* Add the default server */

  while (1)
    {
      /* Read a line */
      point = line;
      count = LINE + 1;

      do
        {
          if (!(--count))
            {
              fputs("Warning: Line from meta server was too long!!!\n", stderr);
              ++point;                           /* Pretend we read a '\n' */
              break;
            }

          rtn = *in++;
          if (!rtn)                              /* use a zero to mark end of buffer */
            return;
            
          *(point++) = rtn;
        }
      while (rtn != EOF && rtn != '\n');

      *(--point) = '\0';

      if (out != NULL)                           /* Save a copy of the stuff
                                                  * we read */
        {
          fputs(line, out);
          putc('\n', out);
        }


      /* Find somewhere to put the information that is just about to be
       * parsed */

      if (num_servers >= max_servers)
        {
          max_servers += 5;
          serverlist = (struct servers *) realloc(serverlist,
                                      sizeof(struct servers) * max_servers);
        }

      slist = serverlist + num_servers;



      /* Is this a line we want? */

      if (sscanf(line, "-h %s -p %d %d %d",
                 slist->address, &(slist->port),
                 &(slist->age)) != 3)
        {
          continue;
        }

      /* Find the status of the server, eg "Not responding". */

      for (statStr = statusStrings + statusLevel
           ; statStr >= statusStrings
           ; --statStr)
        {
          if ((numstr = strstr(line, *statStr)) != NULL)
            {
              (slist->status) = statStr - statusStrings;
              (slist->players) = 0;
              sscanf(numstr, "%*[^0123456789] %d", &(slist->players));
              break;
            }
        }

      if (statStr < statusStrings)               /* No status was allocated */
        continue;


      /* Read the flags */

      slist->typeflag = *(point - 1);


      /* Don't list Paradise Servers  */

      if (slist->typeflag != 'P')
        {

#ifdef DEBUG 
          printf("HOST:%-30s PORT:%-6d %12s %-5d %d %c\n",
                  serverlist[num_servers].address,
                  serverlist[num_servers].port,
                  statusStrings[serverlist[num_servers].status],
                  serverlist[num_servers].players,
                  serverlist[num_servers].typeflag);
#endif

          ++num_servers;
        }
    }
}

#define MAXMETABYTES 2048
static int sock;		/* the socket to talk to the metaservers */
static int sent = 0;		/* number of solicitations sent		 */
static int seen = 0;		/* number of replies seen		 */

static int ReadMetasSend()
{
  struct sockaddr_in address;	/* the address of the metaservers	 */
  int length;			/* length of the address		 */
  int bytes;			/* number of bytes received from meta'   */
  fd_set readfds;		/* the file descriptor set for select()	 */
  struct timeval timeout;	/* timeout for select() call		 */
  char packet[MAXMETABYTES];	/* buffer for packet returned by meta'	 */
  int max_servers;		/* number of servers we have defined	 */
  

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) { perror("ReadMetasSend: socket"); return 0; }

  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_family      = AF_INET;
  address.sin_port        = 0;
  if (bind(sock,(struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("ReadMetasSend: bind");
    return 0;
  }
    
  address.sin_family = AF_INET;
  address.sin_port = htons(metaport);
  /* attempt numeric translation first */
  if ((address.sin_addr.s_addr = inet_addr(metaserver)) == -1) {
    struct hostent *hp;
        
    /* then translation by name */
    if ((hp = gethostbyname(metaserver)) == NULL) {
      /* if it didn't work, return failure and warning */
      fprintf(stderr,
	"Cannot resolve metaserver address of %s, check for DNS problems?\n",
	metaserver);
      return 0;
    } else {
      int i;

      /* resolution worked, send a query to every metaserver listed */
      for(i=0;;i++) {

	/* check for end of list of addresses */
	if (hp->h_addr_list[i] == NULL) break;
	address.sin_addr.s_addr = *(long *) hp->h_addr_list[i];
	printf("Requesting player list from metaserver at %s\n",
		inet_ntoa(address.sin_addr));
	if (sendto(sock, "?", 1, 0, (struct sockaddr *)&address,
		sizeof(address)) < 0) {
	  perror("ReadMetasSend: sendto");
	  return 0;
	}
	sent++;
      }
    }
  } else {
    /* send only to the metaserver specified by numeric IP address */
    if (sendto(sock, "?", 1, 0, (struct sockaddr *)&address,
	sizeof(address)) < 0) {
      perror("ReadMetasSend: sendto");
      return 0;
    }
    sent++;
  }
  return 1;
}

static int ReadMetasRecv()
{
  struct sockaddr_in address;	/* the address of the metaservers	 */
  int length;			/* length of the address		 */
  int bytes;			/* number of bytes received from meta'   */
  fd_set readfds;		/* the file descriptor set for select()	 */
  struct timeval timeout;	/* timeout for select() call		 */
  char packet[MAXMETABYTES];	/* buffer for packet returned by meta'	 */
  int max_servers;		/* number of servers we have defined	 */
  
  /* now await and process replies */
  while(1) {
    char *p;
    int servers, i, j;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    timeout.tv_sec=0;
    timeout.tv_usec=1000;

    if (select(FD_SETSIZE, &readfds, NULL, NULL, &timeout) < 0) {
      perror("ReadMetasRecv: select");
      return 0;
    }

    /* if the wait timed out, then we give up */
    if (!FD_ISSET(sock, &readfds)) {
      /* here placeth logic that sees time for reply is up and asks again */
      return 0;
    }

    /* so we have data back from a metaserver */
    length = sizeof(address);
    bytes = recvfrom(sock, packet, MAXMETABYTES, 0, (struct sockaddr *)&address,
	&length );
    if (bytes < 0) {
      perror("ReadMetasRecv: recvfrom");
      fprintf(stderr, "sock=%d packet=%08.8x length=%d\n", sock, packet,
	MAXMETABYTES );
      return 0;
    }

    /* terminate the packet received */
    packet[bytes] = 0;
    printf("\n%s\n", packet);

    /* process the packet, updating our server list */

    /* version identifier */
    p = strtok(packet,",");
    if (p == NULL) continue;
    if (p[0] != 'r') continue;

    /* number of servers */
    p = strtok(NULL,"\n");
    if (p == NULL) continue;
    servers = atoi(p);

    /* sanity check on number of servers */
    if (servers > 2048) continue;
    if (servers < 0) continue;

    printf("\nMetaserver at %s responded with %d server%s\n",
	inet_ntoa(address.sin_addr),
	servers,
	servers == 1 ? "" : "s" );

    /* allocate or extend a server list */
    if (serverlist == NULL) {
      serverlist = (struct servers *) malloc(
	sizeof(struct servers) * servers);
    } else {
      serverlist = (struct servers *) realloc(serverlist, 
	sizeof(struct servers) * ( servers + num_servers ));
    }

    /* for each server listed by this metaserver packet */
    for(i=0;i<servers;i++) {
      struct servers *sp = serverlist;
      char *address, type;
      int port, status, age, players, queue;

      address = strtok(NULL,",");	/* hostname */
      if (address == NULL) continue;

      p = strtok(NULL,",");		/* port */
      if (p == NULL) continue;
      port = atoi(p);

      p = strtok(NULL,",");		/* status */
      if (p == NULL) continue;
      status = atoi(p);

      /* ignore servers here based on status */

      p = strtok(NULL,",");		/* age (of data in seconds) */
      if (p == NULL) continue;
      age = atoi(p);

      p = strtok(NULL,",");		/* players */
      if (p == NULL) continue;
      players = atoi(p);

      p = strtok(NULL,",");		/* queue size */
      if (p == NULL) continue;
      queue = atoi(p);

      p = strtok(NULL,"\n");		/* server type */
      if (p == NULL) continue;
      type = p[0];

      /* find in current server list? */
      for(j=0;j<num_servers;j++) {
        sp = serverlist + j;
        if ((!strcmp(sp->address,address)) && (sp->port == port)) {
          fprintf(stderr, "%s:%d already listed\n", address, port);
          break;
        }
      }

      /* if it was found, check age */
      if (j != num_servers) {
	if (age > sp->age) continue;
      } else {
        /* not found, store it at the end of the list */
        sp = serverlist + j;
	strncpy(sp->address,address,LINE);
	sp->port = port;
	sp->age = age;
	num_servers++;
      }

/* from meta.h of metaserver code */
#define SS_WORKING 0
#define SS_QUEUE 1
#define SS_OPEN 2
#define SS_EMPTY 3
#define SS_NOCONN 4
#define SS_INIT 5

      switch (status) {
        case SS_QUEUE:
	  sp->status = statusWait;
	  sp->players = queue;
	  break;
	case SS_OPEN:
	  sp->status = statusOpen;
	  sp->players = players;
	  break;
	case SS_EMPTY:
	  sp->status = statusNobody;
	  sp->players = 0;
	  break;
	case SS_NOCONN:			/* no connection */
	case SS_WORKING:		/* metaserver should not return this */
	case SS_INIT:			/* nor this */
	default:
	  sp->status = statusNoConnect;
	  sp->players = 0;
	  break;
      }
      sp->typeflag = type;
    }
    /* finished processing the packet */

    /* count the number of replies we have received */
    seen++;

    /* if we have seen the same number of replies to what we sent, end */
    if (sent == seen) return 1;
  }
}

static int ReadFromMeta(int statusLevel, int parse)
/* Read from the meta-server.  Return TRUE on success and FALSE on failure. */
{
  FILE   *in, *out;
  char   *cacheName;
  char   cacheFileName[PATH_MAX];
  char   tmpFileName[PATH_MAX];
  char   *sockbuf, *buf;
  int     bufleft=BUF-1;
  int     len;
  int     sock;

  if ((sock = open_port(metaserver, metaport, 1)) <= 0)
    {
      fprintf(stderr, "Cannot connect to MetaServer (%s , %d)\n",
              metaserver, metaport);
      return 0;
    }

  /* Allocate a buffer and read until full */
  buf = sockbuf = (char *)malloc(BUF);
  while (bufleft > 0 && (len = read(sock, buf, bufleft)) > 0)
    {
    bufleft-=len;
    buf += len;
    printf("Read %d bytes from Metaserver\n", len);
    }
  close (sock);
  *buf = 0;                   /* End of buffer marker */

  if (len<0)
    {
    perror ("read");
    free(sockbuf);
    return 0;
    }


  printf("Got this from a TCP request from metaserver (unparsed)\n%s", sockbuf);

  if(parse)
    parseInput(sockbuf, out, statusLevel);

  free(sockbuf);
  metaWindowName = "MetaServer List";

  return 1;
}

void    parsemeta(int metaType)
/* Read and Parse the metaserver information, either from the metaservers
 * by UDP (1), from a single metaserver by TCP (3), or from the cache (2).
 * 
 * NOTE: This function sets the variable "num_servers" which is later used to
 * decide the height of the metaserver window. */
{
  int     statusLevel;

  statusLevel = 0;

  if (statusLevel < 0)
    statusLevel = 0;
  else if (statusLevel >= statusNull)
    statusLevel = statusNull - 1;

  switch (metaType)
    {
      case 1:
        ReadMetasSend();
	ReadMetasRecv();
        return;
	break;
      case 2:
	if (ReadFromMeta(statusLevel,1)) return;
	break;
      case 3:
	if (ReadFromMeta(statusLevel,1)) return;
	break;
    }
}


static void metarefresh(int i)
/* Refresh line i in the list */
{
  char    buf[LINE + 1];
  struct servers *slist;

  slist = serverlist + i;

  sprintf(buf, "%-40s %14s ",
          slist->address,
          statusStrings[slist->status]);

  if (slist->status <= statusNull)
    {
      if (slist->status == statusOpen || slist->status == statusWait)
        {
          /* Don't print the number of players if nobody is playing */
          sprintf(buf + strlen(buf), "%-5d  ", serverlist[i].players);
        }
      else
        {
          strcat(buf, "       ");
        }

      switch (serverlist[i].typeflag)
        {
        case 'P':
          strcat(buf, "Paradise");
          break;
        case 'B':
          strcat(buf, "Bronco");
          break;
        case 'C':
          strcat(buf, "Chaos");
          break;
        case 'I':
          strcat(buf, "INL");
          break;
        case 'S':
          strcat(buf, "Sturgeon");
          break;
        case 'H':
          strcat(buf, "Hockey");
          break;
        case 'F':
          strcat(buf, "Dogfight");
          break;
        default:
          strcat(buf, "Unknown");
          break;
        }
    }
  printf("%s\n", buf);
}


static void metadone(void)
/* Unmap the metaWindow */
{
  /* Unmap window */
  free(serverlist);
}

int main(int argc, char *argv)
{  
  int i;
  
   printf("Doing a TCP request (only one server) to %s\n",metaserver);
   ReadFromMeta(0, 0);

   printf("\nNow doing UDP request (possibly more than one server) to %s\n",metaserver);
   printf("\nWhat you will see is the IP address(es) of the server(s),\n");
   printf("       followed by the raw packet(s) received, \n");
   printf("       followed by the parsed (and merged) output\n\n");
   ReadMetasSend();
   sleep(5);
   ReadMetasRecv();

   for(i=0;i<num_servers;i++)
     metarefresh(i);
}
