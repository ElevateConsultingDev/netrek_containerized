#include "config.h"
#include "copyright.h"

#include <stdio.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include INC_SYS_TIME
#include <sys/types.h>
#include <sys/socket.h>
#include "Wlib.h"
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "packets.h"
#include "playback.h"
#include "playerlist.h"
#include "local.h"
#include "map.h"
#include "socket.h"
#include "interface.h"
#include "redraw.h"
#include "death.h"
#include "dashboard.h"
#include "stats.h"

static void stline(int flag);
static void updateMaxStats(int redraw);

static time_t lastread;
static int needredraw = 0;
static unsigned long lastredraw = 0;

/* ------------------------------------------------------------------------
 * Dead reckoning between server updates.
 *
 * The server sends positions server_ups times a second (commonly 10 or 25),
 * so drawing raw p_x/p_y makes everything step rather than glide. Between
 * updates we advance each moving object along the course the server last
 * reported, then put the authoritative values back before returning, so
 * nothing outside the draw ever sees a predicted position.
 *
 * Applying the offset to the objects rather than to each drawing site means
 * the tactical, the phaser and tractor beams, and the lock triangle all stay
 * consistent with each other for free.
 *
 * Velocity is speed * WARP1 per update along p_dir; the trig tables are
 * indexed by the direction byte directly (Cos[0]=0, Sin[0]=-1, so 0 is up).
 *
 * OFF BY DEFAULT (`extrapolate: on` to try it). It does smooth ship motion
 * between updates, but it also re-derives every frame things that are really
 * snapshots taken at the moment of an update, and that shows:
 *
 *   - Phaser beams shake. A PHHIT2 beam runs from the shooter to a fixed
 *     absolute point (ph_x/ph_y), so reckoning the shooter forward while the
 *     endpoint stays put makes the beam pivot during its brief life.
 *   - Short packets carry ship direction quantised to 16 steps, so a reckoned
 *     position can drift and then snap when the real update lands.
 *
 * Doing this properly means interpolating toward known positions rather than
 * predicting past them, and holding weapon geometry fixed for its lifetime.
 *
 * Ships only. Torps are deliberately left alone: the short-packet torp
 * handler (handleVTorp) sends position deltas and never sets t_dir, so a
 * torp's stored direction is stale or belongs to whatever previously used
 * that slot. Reckoning along it sent torps off at wrong angles. Doing this
 * properly means deriving velocity from successive reported positions
 * rather than from a direction field that is not transmitted.
 * ------------------------------------------------------------------------ */
unsigned long last_update_ms = 0;       /* set by the position packet handlers */

static int extrap_on = 0;               /* offsets currently applied? */
static int save_px[MAXPLAYER], save_py[MAXPLAYER];

/* Fraction of an update elapsed, clamped to one interval so a late or
 * dropped packet coasts to a stop instead of flinging things off-screen. */
static double extrap_frac(void)
{
	unsigned long now;
	double f;

	if (!extrapolate || last_update_ms == 0 || server_ups <= 0) return 0.0;
	now = msetime();
	if (now <= last_update_ms) return 0.0;
	f = (double) (now - last_update_ms) * (double) server_ups / 1000.0;
	if (f > 1.0) f = 1.0;
	return f;
}

void extrap_apply(void)
{
	double f = extrap_frac();
	int i;

	if (extrap_on || f <= 0.0) return;
	extrap_on = 1;

	for (i = 0; i < MAXPLAYER; i++) {
		struct player *j = &players[i];
		save_px[i] = j->p_x;
		save_py[i] = j->p_y;
		if (j->p_status != PALIVE || j->p_speed <= 0) continue;
		double d = (double) j->p_speed * WARP1 * f;
		j->p_x += (int) (d * Cos[j->p_dir]);
		j->p_y += (int) (d * Sin[j->p_dir]);
	}
}

void extrap_restore(void)
{
	int i;

	if (!extrap_on) return;
	extrap_on = 0;
	for (i = 0; i < MAXPLAYER; i++) {
		players[i].p_x = save_px[i];
		players[i].p_y = save_py[i];
	}
}

/* Draw an in-between frame if one is due.
 *
 * Drawing only when a packet arrives means every frame is drawn at the
 * instant of an update, when no time has yet elapsed, so dead reckoning
 * would never actually displace anything. The input loop calls this on a
 * clock so motion is carried between updates. Turning off `extrapolate`
 * restores the old packet-driven-only behaviour exactly. */
#define FRAME_INTERVAL_MS 16		/* ~60 fps */

void redraw_if_due(void)
{
	unsigned long now;

	if (!extrapolate || me == NULL) return;
	/* Only while actually flying: the entry and outfit screens are static,
	 * and drawing them at 60 fps is pure heat. */
	if (me->p_status != PALIVE) return;
	now = msetime();
	if (now < lastredraw + FRAME_INTERVAL_MS) return;
	lastredraw = now;
	redraw();
}

void intrupt(fd_set *readfds) {
	time_t  time(time_t *);
	unsigned long t;

	udcounter++;

#ifdef RECORDGAME
	if (playback)
		needredraw |= readFromFile();
	else
#endif
	needredraw |= readFromServer(readfds);

	t = msetime();
	if (needredraw && (t >= lastredraw + redrawDelay * 100)) {
		lastredraw = t;
		needredraw = 0;
		lastread = time(NULL);
		redraw();

		updateMaxStats(0);	/* update the max stats <isae> */

#if defined(WIN32) || defined(SDL2BACKEND)
		W_FlushScrollingWindow(messwa);
		W_FlushScrollingWindow(messwt);
		W_FlushScrollingWindow(messwi);
		W_FlushScrollingWindow(messwk);
		W_FlushScrollingWindow(reviewWin);
		W_FlushScrollingWindow(phaserwin);
#endif
		UpdatePlayerList();
	}

	if (reinitPlanets) {
		initPlanets();
		reinitPlanets = 0;
	}

	if (me->p_status == POUTFIT) {
		death();
	}
#if defined(SOUND) && defined(sgi)
	  else
		Engine_Sound(me->p_speed, me->p_ship.s_maxspeed);
#endif
	return;
}

void redraw(void) {
	/* erase warning line if necessary */
	if ((warntimer <= udcounter) && (warncount > 0)) {	/* XFIX */
		W_ClearArea(warnw, 5, 5, W_Textwidth * warncount, W_Textheight);
		warncount = 0;
	}

	run_clock(lastread);		/* for hosers who don't know what a Xclock is */

	/* Advance moving objects to where they should be right now; undone at
	 * the end of this function, before anything else can observe them. */
	extrap_apply();

	clearLocal();

#ifdef BEEPLITE
	if (tts_timer) {
		static int last_width;

		tts_timer--;
		if (!tts_timer) {	/* timed out */
			W_EraseTTSText(w, TWINSIDE, tts_pos, last_width);
			last_width = 0;
		} else if (tts_timer == tts_time - 1 && last_width) {
			/* first draw -- erase previous */
			W_EraseTTSText(w, TWINSIDE, tts_pos, last_width);
			/* draw new */
			W_WriteTTSText(w, TWINSIDE, tts_pos, tts_width, lastIn, tts_len);
			last_width = tts_width;
		} else {		/* regular draw */
			W_WriteTTSText(w, TWINSIDE, tts_pos, tts_width, lastIn, tts_len);
			last_width = tts_width;
		}
	}
#endif

	local();			/* redraw local window */
	W_FlushLineCaches(w);		/* XFIX */

	if (newDashboard)		/* 6/2/93 LAB */
		if (newDashboard == old_db)
			db_redraw(0);
		else
			redrawTstats();

	else if (newDashboard == old_db)
		stline(0);
	else
		redrawTstats();

	old_db = newDashboard;

	if (W_IsMapped(statwin))
		updateStats();

	W_FlushWindow(w);
	W_ProbeLatency(w);

	/* XFIX: last since its least accurate information */
	map();
	W_FlushWindow(mapw);

	/* Put the server's own numbers back before anyone else reads them. */
	extrap_restore();

	/* automatic step down in response to display lag */
        if (visual_l > 40000 && client_ups >= 25) {
	  fprintf(stderr, "redraw: visual latency %lu us exceeds %d ups, drop to 10 ups\n", visual_l, client_ups);
	  client_ups = 10;
	  sendUpdatePacket(1000000 / client_ups);
        }

        if (visual_l > 100000 && client_ups >= 10) {
	  fprintf(stderr, "redraw: visual latency %lu us exceeds %d ups, drop to 5 ups\n", visual_l, client_ups);
	  client_ups = 5;
	  sendUpdatePacket(1000000 / client_ups);
        }

        if (visual_l > 200000 && client_ups >= 5) {
	  fprintf(stderr, "redraw: visual latency %lu us exceeds %d ups, drop to 2 ups\n", visual_l, client_ups);
	  client_ups = 2;
	  sendUpdatePacket(1000000 / client_ups);
        }
}

static void stline(int flag) {
  static char buf1[80];
  static char buf2[80];
  static char whichbuf = 0;
  register char *buf, *oldbuf;
  register char *s;
  register int i, j;
  int     k;
  struct player *plr;

  if ((me->p_flags & (PFPLOCK | PFOBSERV)) == (PFPLOCK | PFOBSERV))
    plr = players + me->p_playerl;
  else
    plr = me;


  /* Instead of one sprintf, we do all this by hand for optimization */

  if (flag)
    whichbuf = 0;				 /* We must completely * *
						  * refresh */

  if (whichbuf != 2)
    {
      buf = buf1;
      oldbuf = buf2;
    }
  else
    {
      buf = buf2;
      oldbuf = buf1;
    }
  buf[0] = (me->p_flags & PFSHIELD ? 'S' : ' ');
  if (me->p_flags & PFGREEN)
    buf[1] = 'G';
  else if (me->p_flags & PFYELLOW)
    buf[1] = 'Y';
  else if (me->p_flags & PFRED)
    buf[1] = 'R';
  buf[2] = (me->p_flags & (PFPLLOCK | PFPLOCK) ? 'L' : ' ');
  buf[3] = (me->p_flags & PFREPAIR ? 'R' : ' ');
  buf[4] = (me->p_flags & PFBOMB ? 'B' : ' ');
  buf[5] = (me->p_flags & PFORBIT ? 'O' : ' ');
  if (me->p_ship.s_type == STARBASE)
    buf[6] = (me->p_flags & PFDOCKOK ? 'D' : ' ');
  else
    buf[6] = (me->p_flags & PFDOCK ? 'D' : ' ');
  buf[7] = (me->p_flags & PFCLOAK ? 'C' : ' ');
  buf[8] = (me->p_flags & PFWEP ? 'W' : ' ');
  buf[9] = (me->p_flags & PFENG ? 'E' : ' ');
  if (me->p_flags & PFPRESS)
    buf[10] = 'P';
  else if (me->p_flags & PFTRACT)
    buf[10] = 'T';
  else
    buf[10] = ' ';
  if (me->p_flags & PFBEAMUP)
    buf[11] = 'u';
  else if (me->p_flags & PFBEAMDOWN)
    buf[11] = 'd';
  else
    buf[11] = ' ';
  buf[12] = (status->tourn) ? 't' : ' ';
  buf[13] = ' ';
  buf[14] = '0' + ((me->p_speed % 100) / 10);
  if (buf[14] == '0')
    buf[14] = ' ';
  buf[15] = '0' + (me->p_speed % 10);		 /* speed */
  buf[16] = ' ';
  buf[17] = ' ';
  buf[18] = '0' + (me->p_damage / 100);
  if (buf[18] == '0')
    buf[18] = ' ';
  buf[19] = '0' + ((me->p_damage % 100) / 10);
  if ((buf[19] == '0') && (me->p_damage < 100))
    buf[19] = ' ';
  buf[20] = '0' + (me->p_damage % 10);
  buf[21] = ' ';
  buf[22] = '0' + (me->p_shield / 100);
  if (buf[22] == '0')
    buf[22] = ' ';
  buf[23] = '0' + ((me->p_shield % 100) / 10);
  if ((buf[23] == '0') && (me->p_shield < 100))
    buf[23] = ' ';
  buf[24] = '0' + (me->p_shield % 10);
  buf[25] = ' ';
  buf[26] = ' ';
  buf[27] = '0' + ((plr->p_ntorp % 100) / 10);
  if (buf[27] == '0')
    buf[27] = ' ';
  buf[28] = '0' + (plr->p_ntorp % 10);
  buf[29] = ' ';
  buf[30] = ' ';
  buf[31] = ' ';
  buf[32] = '0' + ((int) (plr->p_kills / 100));
  if (buf[32] == '0')
    buf[32] = ' ';
  buf[33] = '0' + ((int) (plr->p_kills / 10)) % 10;
  if (buf[32] == ' ' && buf[33] == '0')
    buf[33] = ' ';
  buf[34] = '0' + (((int) plr->p_kills) % 10);
  buf[35] = '.';
  buf[36] = '0' + (((int) (plr->p_kills * 10)) % 10);
  buf[37] = '0' + (((int) (plr->p_kills * 100)) % 10);
  buf[38] = ' ';
  buf[39] = ' ';
  buf[40] = ' ';
  buf[41] = ' ';
  buf[42] = '0' + ((me->p_armies % 100) / 10);
  if (buf[42] == '0')
    buf[42] = ' ';
  buf[43] = '0' + (me->p_armies % 10);
  buf[44] = ' ';
  buf[45] = ' ';

  buf[46] = '0' + (me->p_fuel / 100000);
  if (buf[46] == '0')
    buf[46] = ' ';
  buf[47] = '0' + ((me->p_fuel % 100000) / 10000);
  if ((buf[47] == '0') && (me->p_fuel < 100000))
    buf[47] = ' ';
  buf[48] = '0' + ((me->p_fuel % 10000) / 1000);
  if ((buf[48] == '0') && (me->p_fuel < 10000))
    buf[48] = ' ';
  buf[49] = '0' + ((me->p_fuel % 1000) / 100);
  if ((buf[49] == '0') && (me->p_fuel < 1000))
    buf[49] = ' ';
  buf[50] = '0' + ((me->p_fuel % 100) / 10);
  if ((buf[50] == '0') && (me->p_fuel < 100))
    buf[50] = ' ';
  buf[51] = '0' + (me->p_fuel % 10);
  buf[52] = ' ';
  buf[53] = ' ';
  buf[54] = ' ';

  buf[55] = '0' + ((me->p_wtemp / 10) / 100);
  if (buf[55] == '0')
    buf[55] = ' ';
  buf[56] = '0' + (((me->p_wtemp / 10) % 100) / 10);
  if ((buf[56] == '0') && (me->p_wtemp < 1000))
    buf[56] = ' ';
  buf[57] = '0' + ((me->p_wtemp / 10) % 10);

  buf[58] = ' ';
  buf[59] = ' ';
  buf[60] = ' ';

  buf[61] = '0' + ((me->p_etemp / 10) / 100);
  if (buf[61] == '0')
    buf[61] = ' ';
  buf[62] = '0' + (((me->p_etemp / 10) % 100) / 10);
  if ((buf[62] == '0') && (me->p_etemp < 1000))
    buf[62] = ' ';
  buf[63] = '0' + ((me->p_etemp / 10) % 10);

  if (whichbuf == 0)
    {
      /* Draw status line */
      W_WriteText(tstatw, 50, 16, textColor, buf, 64, W_RegularFont);
      whichbuf = 1;
    }
  else
    {						 /* Hacks to make it print *
						  * * only what is necessary */
      whichbuf = 3 - whichbuf;
      j = -1;
      for (i = 0; i < 64; i++)
	{
	  if (*(buf++) != *(oldbuf++))
	    {
	      /* Different string */
	      if (j == -1)
		{
		  k = i;
		  s = buf - 1;
		}
	      j = 0;
	    }
	  else
	    {
	      /* Same string */
	      if (j == -1)
		continue;
	      j++;
	      if (j == 20)
		{				 /* Random number */
		  W_WriteText(tstatw, 50 + W_Textwidth * k, 16, textColor,
			      s, i - k - 19, W_RegularFont);
		  j = -1;
		}
	    }
	}
      if (j != -1)
	{
	  W_WriteText(tstatw, 50 + W_Textwidth * k, 16, textColor, s, i - k - j,
		      W_RegularFont);
	}
    }
}

/* that's not used anymore      13/04/94 [007] newcourse(int x, int y) {
 * return ((unsigned char) nint(atan2((double) (x - me->p_x), (double)
 * (me->p_y - y)) / 3.14159 * 128.)); } */

void redrawTstats(void) {
	if (newDashboard)		/* 6/2/93 LAB */
		db_redraw(1);
	else {
		W_ClearWindow(tstatw);
		stline(1);		/* This is for refresh. We redraw player stats too */
		updateMaxStats(1);	/* <isae> Seperated it */
	}
	return;
}

/* update stat window record for max speed, army capacity */
static void updateMaxStats(int redraw) {
  char    buf[BUFSIZ];
  static int lastdamage = -1;
  static int lastkills = -1;
  static int lastship = -1;
  int     maxspeed;
  int     troop_capacity;
  float   kills;
  int     mykills;

  if (newDashboard)
    return;

  if ((me->p_flags & (PFPLOCK | PFOBSERV)) == (PFPLOCK | PFOBSERV))
    kills = players[me->p_playerl].p_kills;
  else
    kills = me->p_kills;

  mykills = (int) (10. * kills);

  /* don't really need a update if nothing's changed! */
  if (!redraw && lastkills == mykills && lastship == me->p_ship.s_type &&
      lastdamage == me->p_damage)
    return;

  lastkills = mykills;
  lastdamage = me->p_damage;
  lastship = me->p_ship.s_type;

  if (me->p_ship.s_type == ASSAULT)
    troop_capacity = (((kills * 3) > me->p_ship.s_maxarmies) ?
		      me->p_ship.s_maxarmies : (int) (kills * 3));
  else if (me->p_ship.s_type != STARBASE)
    troop_capacity = (((kills * 2) > me->p_ship.s_maxarmies) ?
		      me->p_ship.s_maxarmies : (int) (kills * 2));
  else
    troop_capacity = me->p_ship.s_maxarmies;

  maxspeed = (me->p_ship.s_maxspeed + 2) -
      (me->p_ship.s_maxspeed + 1) *
      ((float) me->p_damage / (float) (me->p_ship.s_maxdamage));
  if (maxspeed > me->p_ship.s_maxspeed)
    maxspeed = me->p_ship.s_maxspeed;
  if (maxspeed < 0)
    maxspeed = 0;


  sprintf(buf,
  "Flags        Warp Dam Shd Torps  Kills Armies   Fuel  Wtemp Etemp  Time");
  W_WriteText(tstatw, 50, 5, textColor, buf, strlen(buf), W_RegularFont);
  sprintf(buf,
	"Maximum:   %2d/%2d  %3d %3d              %2d/%2d  %6d   %3d   %3d",
	  maxspeed, me->p_ship.s_maxspeed,
	  me->p_ship.s_maxdamage, me->p_ship.s_maxshield,
	  troop_capacity, me->p_ship.s_maxarmies,
	  me->p_ship.s_maxfuel, me->p_ship.s_maxwpntemp / 10,
	  me->p_ship.s_maxegntemp / 10);
  W_WriteText(tstatw, 50, 27, textColor, buf, strlen(buf), W_RegularFont);
}
