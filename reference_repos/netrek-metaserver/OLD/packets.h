/* 
 * Include file for socket I/O xtrek.
 *
 * Kevin P. Smith 1/29/89
 */

/* the following typedefs allow portability to machines without the
   ubiquitous 32-bit architecture (KSR1, Cray, DEC Alpha) */

typedef unsigned int CARD32;
typedef unsigned short CARD16;
typedef unsigned char CARD8;
typedef int LONG;

typedef int INT32;
typedef short INT16;
#if __STDC__ || defined(sgi) || defined(AIXV3)
typedef signed char INT8;
#else
/* stupid compilers */
typedef char INT8;
#endif

/* packets sent from xtrek server to remote client */
#define SP_MESSAGE 	1
#define SP_PLAYER_INFO 	2		/* general player info not elsewhere */
#define SP_KILLS	3 		/* # kills a player has */
#define SP_PLAYER	4		/* x,y for player */
#define SP_TORP_INFO	5		/* torp status */
#define SP_TORP		6		/* torp location */
#define SP_PHASER	7		/* phaser status and direction */
#define SP_PLASMA_INFO	8		/* player login information */
#define SP_PLASMA	9		/* like SP_TORP */
#define SP_WARNING	10		/* like SP_MESG */
#define SP_MOTD		11		/* line from .motd screen */
#define SP_YOU		12		/* info on you? */
#define SP_QUEUE	13		/* estimated loc in queue? */
#define SP_STATUS	14		/* galaxy status numbers */
#define SP_PLANET 	15		/* planet armies & facilities */
#define SP_PICKOK	16		/* your team & ship was accepted */
#define SP_LOGIN	17		/* login response */
#define SP_FLAGS	18		/* give flags for a player */
#define SP_MASK		19		/* tournament mode mask */
#define SP_PSTATUS	20		/* give status for a player */
#define SP_BADVERSION   21		/* invalid version number */
#define SP_HOSTILE	22		/* hostility settings for a player */
#define SP_STATS	23		/* a player's statistics */
#define SP_PL_LOGIN	24		/* new player logs in */
#define SP_RESERVED	25		/* for future use */
#define SP_PLANET_LOC	26		/* planet name, x, y */

/* paradise packets */
#define SP_SCAN		27		/*  scan packet */
#define SP_UDP_REPLY	28		/* notify client of UDP status */
#define SP_SEQUENCE	29		/* sequence # packet */
#define SP_SC_SEQUENCE	30		/* this trans is semi-critical info */
#define SP_RSA_KEY	31		/* RSA data packet */

#define SP_MOTD_PIC     32              /* motd bitmap pictures */
#define SP_STATS2	33		/*  new stats packet*/
#define SP_STATUS2	34		/*  new status packet*/
#define SP_PLANET2	35		/*  new planet packet*/
#define SP_NEW_MOTD     36              /* New MOTD info notification uses */
#define SP_THINGY	37	/* thingy location */
#define SP_THINGY_INFO	38	/* thingy status */
#define SP_SHIP_CAP	39		/* ship capabilities */

/* packets sent from remote client to xtrek server */
#define CP_MESSAGE      1		/* send a message */
#define CP_SPEED	2		/* set speed */
#define CP_DIRECTION	3		/* change direction */
#define CP_PHASER	4		/* phaser in a direction */
#define CP_PLASMA	5		/* plasma (in a direction) */
#define CP_TORP		6		/* fire torp in a direction */
#define CP_QUIT		7		/* self destruct */
#define CP_LOGIN	8		/* log in (name, password) */
#define CP_OUTFIT	9		/* outfit to new ship */
#define CP_WAR		10		/* change war status */
#define CP_PRACTR	11		/* create practice robot? */
#define CP_SHIELD	12		/* raise/lower sheilds */
#define CP_REPAIR	13		/* enter repair mode */
#define CP_ORBIT	14		/* orbit planet/starbase */
#define CP_PLANLOCK	15		/* lock on planet */
#define CP_PLAYLOCK	16		/* lock on player */
#define CP_BOMB		17		/* bomb a planet */
#define CP_BEAM		18		/* beam armies up/down */
#define CP_CLOAK	19		/* cloak on/off */
#define CP_DET_TORPS	20		/* detonate enemy torps */
#define CP_DET_MYTORP	21		/* detonate one of my torps */
#define CP_COPILOT	22		/* toggle copilot mode */
#define CP_REFIT	23		/* refit to different ship type */
#define CP_TRACTOR	24		/* tractor on/off */
#define CP_REPRESS	25		/* pressor on/off */
#define CP_COUP		26		/* coup home planet */
#define CP_SOCKET	27		/* new socket for reconnection */
#define CP_OPTIONS	28		/* send my options to be saved */
#define CP_BYE		29		/* I'm done! */
#define CP_DOCKPERM	30		/* set docking permissions */
#define CP_UPDATES	31		/* set number of usecs per update */
#define CP_RESETSTATS	32		/* reset my stats packet */
#define CP_RESERVED	33		/* for future use */

#define SOCKVERSION 	4
struct packet_handler {
    int size;
    int (*handler)();
};

struct mesg_spacket {
    char type;		/* SP_MESSAGE */
    unsigned char m_flags;
    unsigned char m_recpt;
    unsigned char m_from;
    char mesg[80];
};

struct plyr_info_spacket {
    char type;		/* SP_PLAYER_INFO */
    char pnum;
    char shiptype;	
    char team;
};

struct plyr_login_spacket {
    char type;		/* SP_PL_LOGIN */
    char pnum;
    char rank;
    char pad1;
    char name[16];
    char monitor[16];
    char login[16];
};

struct hostile_spacket {
    char type;		/* SP_HOSTILE */
    char pnum;
    char war;
    char hostile;
};

struct stats_spacket {
    char type;		/* SP_STATS */
    char pnum;
    char pad1;
    char pad2;
    long tkills;	/* Tournament kills */
    long tlosses;	/* Tournament losses */
    long kills;		/* overall */
    long losses;	/* overall */
    long tticks;	/* ticks of tournament play time */
    long tplanets;	/* Tournament planets */
    long tarmies;	/* Tournament armies */
    long sbkills;	/* Starbase kills */
    long sblosses;	/* Starbase losses */
    long armies;	/* non-tourn armies */
    long planets;	/* non-tourn planets */
    long maxkills;	/* max kills as player * 100 */
    long sbmaxkills;	/* max kills as sb * 100 */
};

struct flags_spacket {
    char type;		/* SP_FLAGS */
    char pnum;		/* whose flags are they? */
    char pad1;
    char pad2;
    unsigned flags;
};

struct kills_spacket {
    char type;		/* SP_KILLS */
    char pnum;
    char pad1;
    char pad2;
    unsigned kills;	/* where 1234=12.34 kills and 0=0.00 kills */
};

struct player_spacket {
    char type;		/* SP_PLAYER */
    char pnum;		
    unsigned char dir;
    char speed;
    LONG x,y;
};

struct torp_info_spacket {
    char  type;		/* SP_TORP_INFO */
    char  war;		
    char  status;	/* TFREE, TDET, etc... */
    char  pad1;		/* pad needed for cross cpu compatibility */
    short tnum;		
    short pad2;
};

struct torp_spacket {
    char  type;		/* SP_TORP */
    unsigned char dir;
    short tnum;
    long  x,y;
};

struct phaser_spacket {
    char type;		/* SP_PHASER */
    char pnum;
    char status;	/* PH_HIT, etc... */
    unsigned char dir;
    long x,y;
    long target;
};

struct you_spacket {
    char type;		/* SP_YOU */
    char pnum;		/* Guy needs to know this... */
    char hostile;
    char swar;
    char armies;
    char pad1;
    char pad2;
    char pad3;
    unsigned flags;
    LONG damage;
    LONG shield;
    LONG fuel;
    short etemp;
    short wtemp;
    short whydead;
    short whodead;
};

struct status_spacket {
    char type;		/* SP_STATUS */
    char tourn;
    char pad1;
    char pad2;
    unsigned armsbomb;
    unsigned planets;
    unsigned kills;
    unsigned losses;
    unsigned time;
    unsigned long timeprod;
};

struct warning_spacket {
    char type;		/* SP_WARNING */
    char pad1;
    char pad2;
    char pad3;
    char mesg[80];
};

struct planet_spacket {
    char  type;		/* SP_PLANET */
    char  pnum;
    char  owner;
    char  info;		
    short flags;
    short pad2;
    long  armies;
};

struct torp_cpacket {
    char type;		/* CP_TORP */
    unsigned char dir;		/* direction to fire torp */
    char pad1;
    char pad2;
};

struct phaser_cpacket {
    char type;		/* CP_PHASER */
    unsigned char dir;
    char pad1;
    char pad2;
};

struct speed_cpacket {
    char type;		/* CP_SPEED */
    char speed;		
    char pad1;
    char pad2;
};

struct dir_cpacket {
    char type;		/* CP_DIRECTION */
    unsigned char dir;
    char pad1;
    char pad2;
};

struct shield_cpacket {
    char type;		/* CP_SHIELD */
    char state;		/* up/down */
    char pad1;
    char pad2;
};

struct repair_cpacket {
    char type;		/* CP_REPAIR */
    char state;		/* on/off */
    char pad1;
    char pad2;
};

struct orbit_cpacket {
    char type;		/* CP_ORBIT */
    char state;		/* on/off */
    char pad1;
    char pad2;
};

struct practr_cpacket {
    char type;		/* CP_PRACTR */
    char pad1;
    char pad2;
    char pad3;
};

struct bomb_cpacket {
    char type;		/* CP_BOMB */
    char state;
    char pad1;
    char pad2;
};

struct beam_cpacket {
    char type;		/* CP_BEAM */
    char state;
    char pad1; 
    char pad2;
};

struct cloak_cpacket {
    char type;		/* CP_CLOAK */
    char state;		
    char pad1;
    char pad2;
};

struct det_torps_cpacket {
    char type;		/* CP_DET_TORPS */
    char pad1;
    char pad2;
    char pad3;
};

struct copilot_cpacket {
    char type;		/* CP_COPLIOT */
    char state;
    char pad1;
    char pad2;
};

struct queue_spacket {
    char type;		/* SP_QUEUE */
    char pad1;
    short pos;
};

struct outfit_cpacket {
    char type;		/* CP_OUTFIT */
    char team;
    char ship;
    char pad1;
};

struct pickok_spacket {
    char type;		/* SP_PICKOK */
    char state;
    char pad2;
    char pad3;
};

struct login_cpacket {
    char type;		/* CP_LOGIN */
    char query;
    char pad2;
    char pad3;
    char name[16];
    char password[16];
    char login[16];
};

struct login_spacket {
    char type;		/* SP_LOGIN */
    char accept;	/* 1/0 */
    char pad2;
    char pad3;
    long flags;
    char keymap[96];
};

struct tractor_cpacket {
    char type;		/* CP_TRACTOR */
    char state;
    char pnum;
    char pad2;
};

struct repress_cpacket {
    char type;		/* CP_REPRESS */
    char state;
    char pnum;
    char pad2;
};

struct det_mytorp_cpacket {
    char type;		/* CP_DET_MYTORP */
    char pad1;
    short tnum;
};

struct war_cpacket {
    char type;		/* CP_WAR */
    char newmask;
    char pad1;
    char pad2;
};

struct refit_cpacket {
    char type;		/* CP_REFIT */
    char ship;
    char pad1;
    char pad2;
};

struct plasma_cpacket {
    char type;		/* CP_PLASMA */
    unsigned char dir;
    char pad1;
    char pad2;
};

struct plasma_info_spacket {
    char  type;		/* SP_PLASMA_INFO */
    char  war;		
    char  status;	/* TFREE, TDET, etc... */
    char  pad1;		/* pad needed for cross cpu compatibility */
    short pnum;		
    short pad2;
};

struct plasma_spacket {
    char  type;		/* SP_PLASMA */
    char  pad1;
    short pnum;
    long  x,y;
};

struct playlock_cpacket {
    char type;		/* CP_PLAYLOCK */
    char pnum;
    char pad1;
    char pad2;
};

struct planlock_cpacket {
    char type;		/* CP_PLANLOCK */
    char pnum;
    char pad1;
    char pad2;
};

struct coup_cpacket {
    char type;		/* CP_COUP */
    char pad1;
    char pad2;
    char pad3;
};

struct pstatus_spacket {
    char type;		/* SP_PSTATUS */
    char pnum;
    char status;
    char pad1;
};

struct motd_spacket {
    char type;		/* SP_MOTD */
    char pad1;
    char pad2;
    char pad3;
    char line[80];
};

struct quit_cpacket {
    char type;		/* CP_QUIT */
    char pad1;
    char pad2;
    char pad3;
};

struct mesg_cpacket {
    char type;		/* CP_MESSAGE */
    char group;
    char indiv;
    char pad1;
    char mesg[80];
};

struct mask_spacket {
    char type;		/* SP_MASK */
    char mask;
    char pad1;
    char pad2;
};

struct socket_cpacket {
    char type;		/* CP_SOCKET */
    char version;
    char pad2;
    char pad3;
    unsigned socket;
};

struct options_cpacket {
    char type;		/* CP_OPTIONS */
    char pad1;
    char pad2;
    char pad3;
    unsigned flags;
    char keymap[96];
};

struct bye_cpacket {
    char type;		/* CP_BYE */
    char pad1;
    char pad2;
    char pad3;
};

struct badversion_spacket {
    char type;		/* SP_BADVERSION */
    char why;
    char pad2;
    char pad3;
};

struct dockperm_cpacket {
    char type;		/* CP_DOCKPERM */
    char state;
    char pad2;
    char pad3;
};

struct updates_cpacket {
    char type;		/* CP_UPDATES */
    char pad1;
    char pad2;
    char pad3;
    unsigned usecs;
};

struct resetstats_cpacket {
    char type;		/* CP_RESETSTATS */
    char verify;	/* 'Y' - just to make sure he meant it */
    char pad2;
    char pad3;
};

struct reserved_spacket {
    char type;		/* SP_RESERVED */
    char pad1;
    char pad2;
    char pad3;
    char data[16];
};

struct reserved_cpacket {
    char type;		/* CP_RESERVED */
    char pad1;
    char pad2;
    char pad3;
    char data[16];
    char resp[16];
};

struct planet_loc_spacket {
    char type;		/* SP_PLANET_LOC */
    char pnum;
    char pad2;
    char pad3;
    long x;
    long y;
    char name[16];
};

/* paradise packets */

struct scan_spacket {		/* ATM */
  INT8 type;			/* SP_SCAN */
  INT8 pnum;
  INT8 success;
  INT8 pad1;
  INT32 p_fuel;
  INT32 p_armies;
  INT32 p_shield;
  INT32 p_damage;
  INT32 p_etemp;
  INT32 p_wtemp;
};

struct udp_reply_spacket {	/* UDP */
  INT8 type;			/* SP_UDP_REPLY */
  INT8 reply;
  INT8 pad1;
  INT8 pad2;
  INT32 port;
};

struct sequence_spacket {	/* UDP */
  INT8 type;			/* SP_SEQUENCE */
  INT8 pad1;
  CARD16 sequence;
};
struct sc_sequence_spacket {	/* UDP */
  INT8 type;			/* SP_CP_SEQUENCE */
  INT8 pad1;
  CARD16 sequence;
};

/*
 * Game configuration.
 * KAO 1/23/93
 */

struct ship_cap_spacket {   /* Server configuration of client */
  INT8 type;			/* screw motd method */
  INT8 operation;		/* 0 = add/change a ship, 1 = remove a ship */
  INT16 s_type;			/* SP_SHIP_CAP */
  INT16 s_torpspeed;
  INT16 s_phaserrange;
  INT32 s_maxspeed;
  INT32 s_maxfuel;
  INT32 s_maxshield;
  INT32 s_maxdamage;
  INT32 s_maxwpntemp;
  INT32 s_maxegntemp;
  INT16 s_width;
  INT16 s_height;
  INT16 s_maxarmies;
  INT8 s_letter;
  INT8 pad2;
  INT8 s_name[16];
  INT8 s_desig1;
  INT8 s_desig2;
  INT16 s_bitmap;
};

struct motd_pic_spacket {
  INT8 type;			/* SP_MOTD_PIC */
  INT8 pad1;
  INT16 x, y, page;
  INT16 width, height;
  INT8 bits[1016];
};


	/*  This is used to send paradise style stats */
struct stats_spacket2 {
  INT8 type;			/* SP_STATS2 */
  INT8 pnum;
  INT8 pad1;
  INT8 pad2;

  INT32 genocides;		/* number of genocides participated in */
  INT32 maxkills;		/* max kills ever * 100  */
  INT32 di;			/* destruction inflicted for all time * 100 */
  INT32 kills;			/* Kills in tournament play */
  INT32 losses;			/* Losses in tournament play */
  INT32 armsbomb;		/* Tournament armies bombed */
  INT32 resbomb;		/* resources bombed off */
  INT32 dooshes;		/* armies killed while being carried */
  INT32 planets;		/* Tournament planets conquered */
  INT32 tticks;			/* Tournament ticks */
				/* SB/WB/JS stats are entirely separate */
  INT32 sbkills;		/* Kills as starbase */
  INT32 sblosses;		/* Losses as starbase */
  INT32 sbticks;		/* Time as starbase */
  INT32 sbmaxkills;		/* Max kills as starbase * 100 */
  INT32 wbkills;		/* Kills as warbase */
  INT32 wblosses;		/* Losses as warbase */
  INT32 wbticks;		/* Time as warbase */
  INT32 wbmaxkills;		/* Max kills as warbase * 100 */
  INT32 jsplanets;		/* planets assisted with in JS */
  INT32 jsticks;		/* ticks played as a JS */
  INT32 rank;			/* Ranking of the player */
  INT32 royal;			/* royaly, specialty, rank */
};

	/*status info for paradise stats */
struct status_spacket2 {
  INT8 type;			/* SP_STATUS2 */
  INT8 tourn;
  INT8 pad1;
  INT8 pad2;
  CARD32 dooshes;		/* total number of armies dooshed */
  CARD32 armsbomb;		/* all t-mode armies bombed */
  CARD32 resbomb;		/* resources bombed */
  CARD32 planets;		/* all t-mode planets taken */
  CARD32 kills;			/* all t-mode kills made */
  CARD32 losses;		/* all t-mode losses */
  CARD32 sbkills;		/* total kills in SB's */
  CARD32 sblosses;		/* total losses in Sb's */
  CARD32 sbtime;		/* total time in SB's */
  CARD32 wbkills;		/* kills in warbases */
  CARD32 wblosses;		/* losses in warbases */
  CARD32 wbtime;		/* total time played in wb's */
  CARD32 jsplanets;		/* total planets taken by jump ships */
  CARD32 jstime;		/* total time in a jump ship */
  CARD32 time;			/* t mode time in this game */
  CARD32 timeprod;		/* t-mode ship ticks--sort of like */
};


	/*planet info for a paradise planet*/
struct planet_spacket2 {
  INT8 type;			/* SP_PLANET2 */
  INT8 pnum;			/* planet number */
  INT8 owner;			/* owner of the planet */
  INT8 info;			/* who has touched planet */
  INT32 flags;			/* planet's flags */
  INT32 timestamp;		/* timestamp for info on planet */
  INT32 armies;			/* armies on the planet */
};

struct obvious_packet {
  INT8 type;			/* SP_NEW_MOTD */
  INT8 pad1;			/* CP_ASK_MOTD */
};

struct thingy_info_spacket {
  INT8 type;			/* SP_TORP_INFO */
  INT8 war;
  INT16 shape;			/* a thingy_types */
  INT16 tnum;
  INT16 owner;
};

struct thingy_spacket {
  INT8 type;			/* SP_TORP */
  CARD8 dir;
  INT16 tnum;
  INT32 x, y;
};

struct rsa_key_spacket {
  INT8 type;			/* SP_RSA_KEY */
  INT8 pad1;
  INT8 pad2;
  INT8 pad3;
  CARD8 data[32];
};


struct ping_spacket {
  INT8 type;			/* SP_PING */
  CARD8 number;			/* id (ok to wrap) */
  CARD16 lag;			/* delay of last ping in ms */

  CARD8 tloss_sc;		/* total loss server-client 0-100% */
  CARD8 tloss_cs;		/* total loss client-server 0-100% */

  CARD8 iloss_sc;		/* inc. loss server-client 0-100% */
  CARD8 iloss_cs;		/* inc. loss client-server 0-100% */
};
