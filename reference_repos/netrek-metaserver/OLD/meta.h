/*
 * meta.h - global vars and configuration stuff
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 */
/*#define DEBUG*/

/* files - adjust to taste */
#define LOGFILE		"metalog"
#define CONFIGFILE	"metarc"
#define CHECKFILE	"metackp"

/* some time constants, in seconds */
/* MAX_BUSY 20	BUSY_EXTEN 20 */
#define MAX_BUSY	40		/* max time to wait for a server */
#define BUSY_EXTEN	40		/* extension time for distant servers */
#define TIME_RES	40		/* how often do we wake up */
/*#define START_DIST	40		/* space initial checks by this much */
#define START_DIST	5
#define MAX_USER	10		/* after this long, bye bye user */
#define MAX_CKP_AGE	60*10		/* reject ckp files after 10 mins */

/* flood controls */
#define MAX_CALLS	3		/* you get this many calls... */
#define FLOOD_TIME	10		/* ...over this many seconds. */

/* server flood controls */
#define MIN_UREAD_TIME  60

/*
 * end of configurable stuff
 */

/* what version we are, and date for old format */
#define VERSION	"1.0.3"
#define VDATE	"6/1/93"

/* display flags */
typedef struct {
    int  bit;
    char chr;
    char *word;
} FLAG;
#define MAX_FLAG	8	/* increase to 32 as needed */
extern FLAG flags[MAX_FLAG];
#define DF_HAD_TMODE	0x0001
#define DF_ALWAYS_DEAD	0x0002
#define DF_RSA		0x0004
#define DF_TYPE		0x0040	/* not really a flag */

#define TRUE	1
#define FALSE	0

/* deal with funky SVR3 signal mechanism */
#ifdef _UTS
# define SYSV
#endif
#if defined (SYSV) && !defined(hpux)
# define SIGNAL(sig,action) sigset(sig, action)
#else
# define SIGNAL(sig,action) signal(sig, action)
#endif

/* assorted library routines */
extern char *malloc(),
	    *realloc();

/* player info */
typedef struct {
    int  p_status;
    int  p_rank;
    int  p_team;
    char p_mapchars[2];
    char p_name[16];
    char p_login[16];
    char p_monitor[16];
    int  ship_type;
} PLAYER;
#define MAX_PLAYER	32		/* must be >= server MAXPLAYER */
#define NUM_TYPES	8		/* #of different ship types */
#define PNUM_TYPES	16		/* #of different ship types */

/* global structure declarations */
typedef enum { SS_WORKING, SS_QUEUE, SS_OPEN, SS_EMPTY, SS_NOCONN, SS_INIT }
    SERVER_STATUS;	/* note: this affects sorting order */
typedef enum { WD_UNKNOWN, WD_CONN, WD_READ, WD_GARBAGE, WD_TIMEOUT, 
    WD_FLOODING } WHY_DEAD;
typedef enum { PS_PORTM, PS_PORT } PROBE_STATE;
typedef enum { CS_CLOSED, CS_CONNECTING, CS_CONNECTED } CONN_STATE;
typedef struct {
    SERVER_STATUS status;	/* status of the server */
    PROBE_STATE pstate;		/* what we are trying to do (port or port-1) */
    CONN_STATE cstate;		/* what our connection status is */
    WHY_DEAD why_dead;		/* if status==SS_NOCONN, why? */
    char hostname[64];	/* DNS */
    char ip_addr[32];	/* ASCII IP */
    long addr;		/* actual address */
    int  port;
    int  count;
    time_t down_time;
    time_t last_update;
    time_t next_update;
    long flags;			/* internal use */
    long display_flags;		/* for "flags" column */
    char comment[40];
    char mask;			/* tournament mode mask */
    char type[1];		/* may expand in the future */
    int  max_players;

    int  player_count;
    int  queue_size;
    PLAYER players[MAX_PLAYER];
} SERVER;		/* (if you add pointers here, update checkpointing) */

typedef struct {
    char *buf;
    int  pos;
    int  buf_size;
    int  data_size;
    time_t start_time;		/* if nonzero, then user is active */
} USER;

/* port stuff */
typedef enum { DI_OLD, DI_NEW, DI_WEB, DI_VERBOSE, DI_INFO } DISPLAY;
typedef struct {
    DISPLAY port_kind;
    char *port_name;
} PORT_DEF;
    
typedef struct {
    int  port;
    DISPLAY kind;
    char desc[64];
    char extra[64];
} PORT;

typedef struct {
    long addr;
} EXCLUDE;


/* some stuff from struct.h */
#define PFREE 0
#define POUTFIT 1
#define PALIVE 2
#define PEXPLODE 3
#define PDEAD 4
#define MAX_STATUS 5

#define PTQUEUE 5	/* Paradise servers only */

/* function prototypes */
extern void init_connections(/*void*/),
	    main_loop(/*void*/),
	    Uprintf(/*pseudo-var*/);
extern char *ctime(),
	    *n_to_a(/*long*/);
extern long lookup(/*char *host, char *iphost*/);
extern int wakeup(/*signal handler*/);
extern int *sort_servers(/*void*/);

/* global variables */
extern int verbose,
	   wait_noconn,
	   wait_empty,
	   wait_open,
	   wait_queue,
	   try_alt_port,
	   server_count,
	   port_count,
	   exclude_count,
	   log_dns_names,
	   new_conn,
	   mnew_conn,
	   busy,
	   busy_time;
extern char *prog,
	    location[],
	    admin[];
extern char *classes[], *pclasses[],
	    teamlet[],
	    shipnos[];
extern SERVER *servers,
	      prev_info;
extern USER *users;
extern PORT_DEF port_defs[];
extern PORT *ports;
extern EXCLUDE *excludes;
extern time_t uptime,
	      checktime;

