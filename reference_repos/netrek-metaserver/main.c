/*
 * main.c - args, configuration, logging
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 * 
 * $Id: main.c,v 1.1 2006/02/14 06:43:11 unbelver Exp $
 * 
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <errno.h>
#include <unistd.h>
#include "meta.h"


/*
 * Globals
 */
int verbose = FALSE;		/* if TRUE, print extra info on terminal */
char *prog = NULL;		/* our program name */
SERVER *servers = NULL;		/* list of servers to check */
int server_count = 0;		/* #of servers in list */
PORT *ports = NULL;		/* list of ports to listen on */
int port_count = 0;		/* #of ports in list */
EXCLUDE *excludes = NULL;	/* list of clients to exclude */
int exclude_count = 0;
int try_alt_port = TRUE;	/* try port-1 first? */
int log_dns_names = TRUE;	/* do DNS lookup on hostnames for log file? */
int save_ckp = TRUE;		/* save checkpoint file? */
int wait_noconn = 30;		/* wait 30 mins if connection failed */
int wait_empty = 15;		/* wait 15 mins if server is empty */
int wait_open = 15;		/* wait 15 mins if server is open */
int wait_queue = 15;		/* wait 15 mins if server has a wait queue */
time_t uptime = 0;		/* when we started */
time_t checktime = 0;		/* when we started collecting data */

char info_file[MAXPATHLEN];	/* pathname of info file */
char faq_file[MAXPATHLEN];	/* pathmame of faq file */
char location[80];		/* city/state/country */
char admin[80];			/* e-mail addr of MSII maintainer */

FLAG flags[MAX_FLAG] = {
  { DF_HAD_TMODE,	'T',	"HAD-TMODE" },
  { DF_ALWAYS_DEAD,	'D',	"ALWAYS-DEAD" },
  { DF_RSA,		'R',	"RSA-RESERVED" },
};

PORT_DEF port_defs[] = {
  { DI_OLD, "old" },
  { DI_NEW, "new" },
  { DI_WEB, "web" },
  { DI_VERBOSE, "verbose" },
  { DI_INFO, "info" },
};
#define MAX_PORT_DEF	(sizeof(port_defs) / sizeof(PORT_DEF))

#define PIDFILE			"meta.pid"

/*
 * Private variables
 */
static FILE *logfp = NULL;	/* log file */
static int do_log = TRUE;	/* set from config file */
static int trun = 0;	/* truncate log file on startup? */


/*
 * Configuration stuff
 */
typedef enum {
  STMT_BOOL, STMT_INT, STMT_STRING, STMT_SERVER, STMT_PORT, STMT_EXCLUDE
} STMT_KIND;
struct keyword_t {
  STMT_KIND kind;
  char *keyword;
  int *var;
} keywords[] = {
  { STMT_INT,		"wait_noconn",	&wait_noconn },
  { STMT_INT,		"wait_empty",	&wait_empty },
  { STMT_INT,		"wait_open",	&wait_open },
  { STMT_INT,		"wait_queue",	&wait_queue },
  { STMT_BOOL,	"do_logging",	&do_log },
  { STMT_BOOL,	"log_dns_names", &log_dns_names },
  { STMT_BOOL,	"try_alt_port",	&try_alt_port },
  { STMT_BOOL,	"save_ckp",	&save_ckp },
  { STMT_STRING,	"info_file",	(int *) info_file },
  { STMT_STRING,	"faq_file",	(int *) faq_file },
  { STMT_STRING,	"location",	(int *) location },
  { STMT_STRING,	"admin",	(int *) admin },
  { STMT_SERVER,	"servers",	NULL },
  { STMT_PORT,	"port",		NULL },
  { STMT_EXCLUDE,	"exclude",	NULL },
};
#define MAX_KEYWORDS	(sizeof(keywords) / sizeof(struct keyword_t))


/*
 * print a message to the log
 *
 * (I'd use varargs, but vfprintf() doesn't exist everywhere, so why bother?)
 */
void
log_msg(format, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7)
     char *format;
     int arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7;
{
  char buf[256];

  if (!do_log) return;
  if (logfp == NULL) {
    fprintf(stderr, "WARNING: attempt to write message to closed log\n");
    return;
  }

  /* date is in hex to keep it compact */
  sprintf(buf, "%.8lx: ", time(0));
  sprintf(buf+10, format, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
  if (*(buf + strlen(buf)-1) != '\n')
    strcat(buf, "\n");

  /* since there should only be one metaserver process, we probably don't */
  /* need to seek to the end every time (but it allows truncation...) */
  fseek(logfp, (off_t) 0, 2);
  fprintf(logfp, buf);
}


/* add an entry to the "server" list */
static void
add_server(type, host, iphost, port, players, comment)
     char *type, *host, *iphost, *port, *players, *comment;
{
  int portnum = atoi(port);
  int playernum = atoi(players);
  int rsa;

  if (servers == NULL) {
    servers = (SERVER *) malloc(sizeof(SERVER));
  } else {
    servers = (SERVER *) realloc(servers,
				 sizeof(SERVER) * (server_count+1));
  }
  if (servers == NULL) {
    fprintf(stderr, "ERROR: malloc/realloc failure (servers)\n");
    exit(1);
  }
  servers[server_count].status = SS_INIT;
  servers[server_count].cstate = CS_CLOSED;
  if (!strncmp(host, "RSA", 3)) {
    rsa = 1;
    strcpy(servers[server_count].hostname, host+3);
  } else {
    rsa = 0;
    strcpy(servers[server_count].hostname, host);
  }
  strcpy(servers[server_count].ip_addr, iphost);
  if ((servers[server_count].addr = lookup(host, iphost)) == -1)
    return;
  servers[server_count].type[0] = *type;
  servers[server_count].port = portnum;
  servers[server_count].max_players = playernum;
  servers[server_count].count = 0;
  servers[server_count].last_update = (time_t) 0;
  servers[server_count].down_time = 0;
  /* set the next_update time to space the checks 40 seconds apart */
  servers[server_count].next_update = (time_t) (time(0) +
						server_count*START_DIST);
  servers[server_count].flags = 0L;
  servers[server_count].display_flags = (DF_ALWAYS_DEAD) | (DF_RSA * rsa);
  if (comment == NULL)
    strcpy(servers[server_count].comment, "");
  else
    strcpy(servers[server_count].comment, comment);
  servers[server_count].player_count = 0;
  servers[server_count].solicit = FALSE;
  server_count++;
}

/* add an entry to the "port" list */
static void
add_port(port, type, description, extra)
     char *port, *type, *description, *extra;
{
  DISPLAY kind;
  int portnum = atoi(port);
  int i;

  /* see if portnum is valid and port kind is known */
  if (portnum < 1024) {
    fprintf(stderr, "WARNING: port %d (%s) reserved; stmt ignored\n",
	    portnum, port);
    return;
  }
  for (i = 0; i < MAX_PORT_DEF; i++) {
    if (!strcmp(type, port_defs[i].port_name)) {
      kind = port_defs[i].port_kind;
      break;
    }
  }
  if (i == MAX_PORT_DEF) {
    fprintf(stderr, "WARNING: unknown port kind '%s'\n", type);
    return;
  }

  /* create a new entry, and add it to the list */
  if (ports == NULL) {
    ports = (PORT *) malloc(sizeof(PORT));
  } else {
    ports = (PORT *) realloc(ports,
			     sizeof(PORT) * (port_count+1));
  }
  if (ports == NULL) {
    fprintf(stderr, "ERROR: malloc/realloc failure (ports)\n");
    exit(1);
  }
  ports[port_count].port = portnum;
  ports[port_count].kind = kind;
  strcpy(ports[port_count].desc, description);
  if (extra != NULL)
    strcpy(ports[port_count].extra, extra);
  else
    ports[port_count].extra[0] = '\0';
#ifdef DEBUG
  printf("port=%d, kind=%d, desc='%s', extra='%s'\n",
	 ports[port_count].port, ports[port_count].kind,
	 ports[port_count].desc, ports[port_count].extra);
#endif
  port_count++;
}

/* add an entry to the "exclude" list */
static void
add_exclude(host)
     char *host;
{
  if (excludes == NULL) {
    excludes = (EXCLUDE *) malloc(sizeof(EXCLUDE));
  } else {
    excludes = (EXCLUDE *) realloc(excludes,
				   sizeof(EXCLUDE) * (exclude_count+1));
  }
  if (excludes == NULL) {
    fprintf(stderr, "ERROR: malloc/realloc failure (excludes)\n");
    exit(1);
  }
  if ((excludes[exclude_count].addr = lookup(host, host)) == -1)
    return;
#ifdef DEBUG
  printf("exclude %s (0x%lx)\n", host, excludes[exclude_count].addr);
#endif

  exclude_count++;
}

/* for some stupid reason my inet_ntoa() is crashing (bad shared libs??) */
char *
my_ntoa(val)
     unsigned long val;
{
  static char buf[16];
  unsigned long foo;
  char *cp;
  int i;

  cp = buf;
  for (i = 3; i >= 0; i--) {
    /* sadly, sprintf() doesn't return #of chars on BSD systems */
    foo = val >> (8*i);
    sprintf(cp, "%d.", foo & 0xff);
    cp += strlen(cp);
  }
  *(cp-1) = '\0';	/* trim the last '.' off */
  return (buf);
}

/* print the list of excludes after the log file is open */
static void
print_excludes()
{
  int i;

  for (i = 0; i < exclude_count; i++)
    log_msg("exclude #%d is %s (0x%.8lx)", i, my_ntoa(excludes[i].addr),
	    excludes[i].addr);
}

/* find the next token */
static char *
next_token(str)
     char *str;
{
  /* skip leading whitespace */
  while (*str && (*str == ' ' || *str == '\t' || *str == '\n'))
    str++;
  if (*str)
    return (str);
  else
    return (NULL);
}


/*
 * read the configuration in (may be called from a signal handler)
 */
static void
load_config()
{
  FILE *fp;
  char buf[128], *cp, *cp2, *cp3, *cp4, *cp5, *cp6;
  int i, len, line;
  int serv_mode = FALSE;

  /* clear out any existing configuration stuff */
  if (server_count) {
    free(servers);
    server_count = 0;
  }
  *info_file = *faq_file = *admin = *location = '\0';

  if ((fp = fopen(CONFIGFILE, "r")) == NULL) {
    perror("ERROR: can't open config file");
    exit(1);
  }

  line = 0;
  while (1) {
    fgets(buf, 128, fp);
    line++;
    if (feof(fp)) break;
    if (ferror(fp)) {
      perror("ERROR: error reading config file");
      exit(1);
    }
    if (buf[strlen(buf)-1] == '\n')
      buf[strlen(buf)-1] = '\0';

    /* skip comments and blank lines */
    cp = next_token(buf);
    if (cp == NULL || *cp == '#')
      continue;

    /* once serv_mode is TRUE, we don't do anything but snag server names */
    if (serv_mode) {
      cp = strtok(cp, " \t");
      if ((cp2 = strtok(NULL, " \t")) == NULL) {	    /* Server name */
	fprintf(stderr,
		"WARNING: missing server name (line %d); ignored\n",
		line);
	break;
      }
      if ((cp3 = strtok(NULL, " \t")) == NULL) {	    /* IP address */
	fprintf(stderr,
		"WARNING: missing IP host (line %d); ignored\n", line);
	break;
      }
      if ((cp4 = strtok(NULL, " \t")) == NULL) {	    /* Port */
	fprintf(stderr,
		"WARNING: missing port (line %d); ignored\n", line);
	break;
      }
      if ((cp5 = strtok(NULL, " \t")) == NULL) {      /* Player # */
	fprintf(stderr,
		"WARNING: missing player number (line %d); ignored\n", line);
	break;
      }
      cp6 = next_token(cp4+strlen(cp5)+1);
      if (verbose) printf("Line is: %s %s %s %s %s %s\n",
			  cp, cp2, cp3, cp4, cp5, cp6);
      add_server(cp, cp2, cp3, cp4, cp5, cp6);

      continue;
    }

    /* find a token in "keywords" which matches */
    for (i = 0; i < MAX_KEYWORDS; i++) {
      len = strlen(keywords[i].keyword);	/* use table for speed? */
      if (!strncmp(cp, keywords[i].keyword, len)) {
	cp2 = next_token(cp+len);
	if (cp2 != NULL && *cp2 == ':') {
	  break;
	}
      }
    }
    if (i == MAX_KEYWORDS) {
      fprintf(stderr, "WARNING: unknown statement (line %d): %s\n",
	      line, buf);
      continue;
    }

    /* parse the right-hand side with strtok (unless it's a string) */
    if (keywords[i].kind == STMT_STRING || keywords[i].kind == STMT_SERVER){
      cp = next_token(cp2+1);
    } else if ((cp = strtok(cp2+1, " \t")) == NULL) {
      fprintf(stderr, "WARNING: missing rhs (line %d); ignored\n", line);
      continue;
    }

    switch (keywords[i].kind) {
    case STMT_BOOL:		/* boolean on/off */
      if (!strcasecmp(cp, "on"))
	*(keywords[i].var) = 1;
      else if (!strcasecmp(cp, "off"))
	*(keywords[i].var) = 0;
      else
	fprintf(stderr,
		"WARNING: boolean expr must be 'on' or 'off' (line %d)\n",
		line);
      break;
    case STMT_INT:		/* integer value */
      *((int *) keywords[i].var) = atoi(cp);
      break;
    case STMT_STRING:	/* string; dest should be allocated */
      if (keywords[i].var == NULL) {
	fprintf(stderr, "WARNING: no storage\n");
      } else {
	strcpy((char *) keywords[i].var, cp);
      }
      break;
    case STMT_PORT:		/* "port" line */
      if ((cp2 = strtok(NULL, " \t")) == NULL) {
	fprintf(stderr, "WARNING: port missing port kind; ignored\n");
	break;
      }
      if ((cp3 = strtok(NULL, "\"\t")) == NULL) {
	fprintf(stderr, "WARNING: port missing description; ignored\n");
	break;
      }
      cp4 = strtok(NULL, " \t");
      add_port(cp, cp2, cp3, cp4);
      break;
    case STMT_EXCLUDE:	/* "exclude" line */
      add_exclude(cp);
      break;
    case STMT_SERVER:	/* "server" line */
      serv_mode = TRUE;
      break;
    default:
      /* "shouldn't happen" */
      fprintf(stderr, "Internal error: bad keyword kind %d\n",
	      keywords[i].kind);
    }
  }

  /* sanity check: make sure we have something to do! */
  if (!server_count) {
    fprintf(stderr, "ERROR: no servers in list!\n");
    exit(1);
  }
  if (!port_count) {
    fprintf(stderr, "ERROR: no ports to listen on!\n");
    exit(1);
  }

#ifdef DEBUG2
  for (i = 0; i < server_count; i++) {
    printf("%d: %s\n", i, servers[i].hostname);
  }
#endif
}


/*
 * open the log file and print a friendly message
 */
static void
open_log()
{
  time_t now;
  char *ostr;

  if (!do_log) return;

  /* prepare the log file */
  if (!trun)
    ostr = "a";	/* just append */
  else
    ostr = "w";	/* truncate */

  if ((logfp = fopen(LOGFILE, ostr)) == NULL) {
    perror("ERROR: can't open log file for writing");
    exit(1);
  } else {
    setvbuf(logfp, NULL, _IONBF, 0);   /* no buffering */

    log_msg("\n");
    now = time(0);
    log_msg("** MetaServerII v%s started on %s", VERSION, ctime(&now));
  }
}


/*
 * Save status to checkpoint file
 *
 * Just does a dump of server structs.  Changing the struct makes existing
 * checkpoint files worthless.  Puts the current time and the start time at
 * the head of the file.
 */
void
save_checkpoint()
{
  FILE *fp;
  time_t now;

  if (!save_ckp) {
    log_msg("checkpoint NOT saved");
    return;
  }

  if ((fp = fopen(CHECKFILE, "w")) == NULL) {
    if (verbose) perror("fopen(checkfile)");
    log_msg("unable to open checkpoint file (err=%d)", errno);
    return;
  }

  now = time(0);
  if (fwrite((char *)&now, sizeof(time_t), 1, fp) <= 0) {
    if (verbose) perror("fwrite(checkfile) time-now");
    log_msg("unable to write to checkpoint file (t)(err=%d)", errno);
    fclose(fp);
    return;
  }
  if (fwrite((char *)&checktime, sizeof(time_t), 1, fp) <= 0) {
    if (verbose) perror("fwrite(checkfile) time-begin");
    log_msg("unable to write to checkpoint file (t2)(err=%d)", errno);
    fclose(fp);
    return;
  }

  if (fwrite((char *)servers, sizeof(SERVER), server_count, fp) <= 0) {
    if (verbose) perror("fwrite(checkfile)");
    log_msg("unable to write to checkpoint file (err=%d)", errno);
    fclose(fp);
    return;
  }

  fclose(fp);
  log_msg("checkpoint saved");
}


/*
 * Restore status from checkpoint file
 *
 * The server list may have changed, so we need to match each item from the
 * checkpoint file with an item in the server list.  This is not trapped
 * automatically.
 *
 * If the checkpoint file is corrupted, we can get pretty messed up.
 */
void
restore_checkpoint()
{
  SERVER *sp, srvbuf;
  FILE *fp;
  time_t filetime, now;
  int i, j;

  if ((fp = fopen(CHECKFILE, "r")) == NULL) {
    if (verbose) perror("fopen(checkfile)");
    log_msg("unable to open checkpoint file (r)(err=%d)", errno);
    return;
  }
  if (fread((char *)&filetime, sizeof(time_t), 1, fp) <= 0) {
    if (verbose) perror("fread(checkfile) time");
    log_msg("unable to read checkpoint file (t)(err=%d)", errno);
    fclose(fp);
    return;
  }
  if (uptime - filetime > MAX_CKP_AGE) {
    if (verbose) fprintf(stderr,
			 "Warning: checkpoint file too old (ignored)\n");
    log_msg("checkpoint file ignored (%d seconds old, max is %d)",
	    uptime - filetime, MAX_CKP_AGE);
    goto dodel;
  }
  if (verbose) printf("Checkpoint file is %d seconds old\n",
		      uptime - filetime);

  if (fread((char *)&checktime, sizeof(time_t), 1, fp) <= 0) {
    if (verbose) perror("fread(checkfile) time");
    log_msg("unable to read checkpoint file (t)(err=%d)", errno);
    fclose(fp);
    return;
  }

  /* for each entry in the checkpoint file, find the matching SERVER */
  /* (if config info has changed, ignore the checkpoint entry) */
  while (1) {
    if (fread((char *)(&srvbuf), sizeof(SERVER), 1, fp) <= 0)
      break;
#ifdef DEBUG
    printf("Got '%s'\n", srvbuf.hostname);
#endif

    /* shouldn't compare against already-matched servers... */
    for (i = 0, sp = servers; i < server_count; i++, sp++) {
      if (!strcmp(srvbuf.hostname, sp->hostname) &&
	  !strcmp(srvbuf.ip_addr, sp->ip_addr) &&
	  !strcmp(srvbuf.comment, sp->comment) &&
	  (srvbuf.type[0] == sp->type[0]) &&
	  srvbuf.port == sp->port) {
	memcpy(sp, &srvbuf, sizeof(SERVER));
      }
    }
  }

  /* do some minor cleaning in case we interrupted it last time */
  /* while we're here, bump up "next check" times for new servers */
  now = time(0);
  for (i = j = 0, sp = servers; i < server_count; i++, sp++) {
    sp->pstate = PS_PORTM;
    sp->cstate = CS_CLOSED;
    if (sp->status == SS_WORKING) sp->status = SS_INIT;

    if (sp->status == SS_INIT)
      sp->next_update = (time_t) (now + (j++) * START_DIST);
  }

  log_msg("checkpoint restored");

  /* delete the checkpoint file */
dodel:
  if (unlink(CHECKFILE) < 0) {
    if (verbose) perror("unlink(checkfile)");
    log_msg("unable to unlink checkfile (err=%d)", errno);
  }
}


/* fatal signal received */
static int
sig_death(sig, code, scp, addr)
     int sig, code;
     struct sigcontext *scp;
     char *addr;
{
  if (verbose) printf("%s: killed by signal %d\n", prog, sig);
  log_msg("exit: killed by signal %d", sig);

  save_checkpoint();

#ifdef ABORT
#ifndef PROF	/* always exit cleanly if we're doing profiling */
  log_msg("sig = %d, code = %d, scp = 0x%.8lx, addr = 0x%.8lx\n",
	  sig, code, scp, addr);
  log_msg("pc = 0x%.8lx\n", scp->sc_pc);
  log_msg("Dumping core...\n");
  abort();
#endif
#endif /*ABORT*/

  exit(2);
}


/* reconfigure signal receieved */
static int
sig_reconfig(sig)
     int sig;
{
  if (verbose) printf("%s: received reconfigure signal %d\n", prog, sig);
  /*log_msg("rereading configuration file (signal %d)", sig);*/
  log_msg("ignoring config signal %d", sig);

  /*load_config();*/
}


/*
 * initialize signal handling stuff
 */
static void
init_signals()
{
  SIGNAL(SIGINT, sig_death);
  SIGNAL(SIGQUIT, sig_death);
  SIGNAL(SIGTERM, sig_death);
  SIGNAL(SIGHUP, sig_reconfig);
  SIGNAL(SIGPIPE, SIG_IGN);
  /*SIGNAL(SIGSEGV, sig_crash);*/

  SIGNAL(SIGALRM, wakeup);

  SIGNAL(SIGCHLD, sig_child);
}


/*
 * Process args
 */
int
main(argc, argv)
     int argc;
     char *argv[];
{
  extern char *optarg;
  extern int optind;
  int c;
  FILE *fp;
  int pid = getpid();
  if ((fp = fopen(PIDFILE,"w"))==NULL) {
    perror("pid file");
    exit(0);
  }

  fprintf(fp,"%d\n", pid);
  fclose(fp);

  /* parse args */
  while ((c = getopt(argc, argv, "vtd")) != EOF)
    switch (c) {
    case 'v':
      verbose++;
      break;
    case 't':
      trun++;
      break;
    case 'd':
      BecomeDaemon();
      break;
    case '?':
    default:
      fprintf(stderr, "Netrek MetaServerII v%s\n", VERSION);
      fprintf(stderr, "usage: metaserverII [-d] [-v] [-t]\n\n");
      exit(2);
    }

  if ((prog = strrchr(argv[0], '/')) == NULL)
    prog = argv[0];
  else
    prog++;

  /* read the configuration file */
  load_config();

  /* initialize internal data structures */
  init_connections();

  /* open the log and print a friendly message */
  if (do_log) open_log();

  /* init the signal handling stuff */
  init_signals();

  uptime = checktime = time(0);

  /* if there's a checkpoint file, load the old data */
  restore_checkpoint();

  /* print the list of excluded hosts, for reference */
  print_excludes();

  /* nothing has failed yet, so go wait for connections */
  main_loop();

  exit(0);
  /*NOTREACHED*/
}

