#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include "defs.h"
#include "struct.h"
#include "data.h"
#include "proto.h"
#include "planet.h"
#include "util.h"
#include "draft.h"

static void usage(void)
{
  fprintf(stderr, "\
Usage: setship SLOT get\n\
       setship SLOT COMMAND [COMMAND ...]\n\
\n\
position x y                move ship to x y coordinate\n\
dir n                       set course to n (0-255)\n\
speed n                     set speed to n (0-maxwarp)\n\
wobble                      set next torp standard wobble\n\
no-wobble                   set next torp without wobble\n\
fire-test-torpedo n         fire torp in direction n\n\
show-test-torpedo-position  show position of first torpedo owned\n\
destroy-test-torpedo        detonate torpedo\n\
sleep n                     sleep for n seconds\n\
set-inl-draft n             set INL draft mode state n (0=off)\n\
player                      change an observer to a player\n\
observer                    change a player to an observer\n\
captain                     mark a player as a draft captain\n\
damage n                    set damage to n\n\
shields n                   set shields remaining to n\n\
fuel n                      set fuel remaining to n\n\
kills n                     set kills to n (accepts fractions, e.g. 2.5)\n\
rank n                      set rank index to n (server may recompute it)\n\
show-player                 print slot, name, kills, rank, armies and stats\n\
show-upgrades               print sturgeon upgrades held by this ship\n\
upgrade TYPE N              grant sturgeon upgrade TYPE N times, free.\n\
                            Negative N takes them back. TYPE is the index\n\
                            show-upgrades prints: 8 engine cooling, 5 warp.\n\
stat NAME VALUE             set a career stat. NAME is one of\n\
                            kills deaths armsbomb planets ticks maxkills\n\
                            DI is derived from these, not stored.\n\
");
}

/* Calling sturgeon_apply_upgrade() links sturgeon.o, which drags in genspkt.o
 * and with it the ntserv message-bounce entry points. This tool never reaches
 * any of them, so satisfy the linker and leave them empty. */
#ifdef STURGEON
int bounceSBStats(int from) { (void)from; return 0; }
int bounceWhois(int from) { (void)from; return 0; }
int bounceSessionStats(int from) { (void)from; return 0; }
int bounceUDPStats(int from) { (void)from; return 0; }
int bouncePingStats(int from) { (void)from; return 0; }
#endif

struct torp *t_find(struct player *me, int status)
{
  struct torp *k = NULL;
  for (k = firstTorpOf(me); k <= lastTorpOf(me); k++)
    if (k->t_status == status)
      break;
  return k;
}

int t_attribute = TOWNERSAFE | TDETTEAMSAFE;
int t_torpspeed = -1;

int setship(const char *cmds)
{
  const char delimiters[] = " ";
  char *copy, *token;
  struct player *me;

  copy = strdup(cmds);
  token = strtok(copy, delimiters);
  if (!token) { usage(); return 1; }
  openmem(0);

 state_0:
  me = player_by_number(token);
  if (me == NULL) {
    fprintf(stderr, "unknown slot\n");
    exit(1);
  }

 state_1:
  if (!(token = strtok (NULL, delimiters))) return 0;

#ifdef STURGEON
  /* Grant a sturgeon upgrade for free. Setting p_upgradelist alone would show
   * the upgrade but do nothing: the ship stats it buys live in p_ship, and
   * sturgeon_apply_upgrade() is what actually moves them. That is the same
   * call the server makes when reapplying upgrades after a refit, and it only
   * touches the player struct, so it is safe from here.
   *
   * This does not charge kills, and does not touch p_upgrades (the tally the
   * upgrade cap and STURGEON_LITE work from), so a granted upgrade is free
   * and does not count against a limit. */
  if (!strcmp(token, "upgrade")) {
    int type, n;
    if (!(token = strtok (NULL, delimiters))) return 0;
    type = atoi(token);
    if (!(token = strtok (NULL, delimiters))) return 0;
    n = atoi(token);
    if (type < 0 || type > UPG_DETDMG) {
      fprintf(stderr, "upgrade type must be 0..%d\n", UPG_DETDMG);
      return 0;
    }
    if (n > 0) {
      me->p_upgradelist[type] += n;
      sturgeon_apply_upgrade(type, me, n);
    } else if (n < 0) {                 /* take some back */
      int have = me->p_upgradelist[type];
      int take = (-n > have) ? have : -n;
      if (take) {
        me->p_upgradelist[type] -= take;
        sturgeon_unapply_upgrade(type, me, take);
      }
    }
    goto state_1;
  }

  /* Sturgeon upgrades. p_upgradelist is indexed by the UPG_* constants and
   * upgradename[] runs parallel to it, so index 8 is engine cooling in both.
   * The name table has one entry fewer than NUMUPGRADES, hence the bound. */
  if (!strcmp(token, "show-upgrades")) {
    /* upgradename[] is extern, so its size is not visible here. It has one
     * entry per UPG_* constant, UPG_TEMPSHIELD(0) through UPG_DETDMG(18),
     * which is one fewer than NUMUPGRADES. Keep the loop inside it. */
    const int names = UPG_DETDMG + 1;
    int i, any = 0;
    printf("slot %d name '%s' ship %d\n", me->p_no, me->p_name,
           me->p_ship.s_type);
    printf("  kills spent %.2f  rank credit %.2f  free %d  undo %d\n",
           me->p_upgrades, me->p_rankcredit,
           me->p_free_upgrade, me->p_undo_upgrade);
    /* the stats the upgrades actually move, so a grant can be seen to work */
    printf("  ship: maxwarp %d  maxfuel %d  maxshield %d  maxdamage %d"
           "  egncool %d  wpncool %d\n",
           me->p_ship.s_maxspeed, me->p_ship.s_maxfuel,
           me->p_ship.s_maxshield, me->p_ship.s_maxdamage,
           me->p_ship.s_egncoolrate, me->p_ship.s_wpncoolrate);
    for (i = 0; i < NUMUPGRADES && i < names; i++) {
      if (!me->p_upgradelist[i]) continue;
      printf("  [%2d] %-26s %d\n", i, upgradename[i], me->p_upgradelist[i]);
      any = 1;
    }
    if (!any) printf("  (no upgrades)\n");
    goto state_1;
  }
#endif

  if (!strcmp(token, "show-player")) {
    printf("slot %d name '%s' kills %.2f rank %d armies %d",
           me->p_no, me->p_name, me->p_kills, me->p_stats.st_rank, me->p_armies);
    printf(" damage %d shields %d fuel %d\n",
           me->p_damage, me->p_shield, me->p_fuel);
    printf("  stats   kills %d deaths %d armsbomb %d planets %d ticks %d\n",
           ltd_kills(me, LTD_TOTAL), ltd_deaths(me, LTD_TOTAL),
           ltd_armies_bombed(me, LTD_TOTAL), ltd_planets_taken(me, LTD_TOTAL),
           ltd_ticks(me, LTD_TOTAL));
    goto state_1;
  }

  if (!strcmp(token, "show-position")) {
    printf("frame %d", context->frame);
    printf(" speed %d", me->p_speed);
    printf(" dir %d", me->p_dir);
    printf(" position %d %d", me->p_x, me->p_y);
    printf("\n");
    goto state_1;
  }

  if (!strcmp(token, "position")) {
    int p_x, p_y;
    if (!(token = strtok (NULL, delimiters))) return 0;
    p_x = atoi(token);
    if (!(token = strtok (NULL, delimiters))) return 0;
    p_y = atoi(token);
    p_x_y_set(me, p_x, p_y);
    goto state_1;
  }

  if (!strcmp(token, "dir")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_dir = atoi(token);
    me->p_desdir = atoi(token);
    bay_release(me);
    me->p_flags &= ~(PFBOMB | PFORBIT | PFBEAMUP | PFBEAMDOWN);
    goto state_1;
  }

  if (!strcmp(token, "speed")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_desspeed = atoi(token);
    me->p_flags &= ~(PFREPAIR | PFBOMB | PFORBIT | PFBEAMUP | PFBEAMDOWN);
    me->p_flags &= ~(PFPLOCK | PFPLLOCK);
    goto state_1;
  }

  if (!strcmp(token, "wait-for-stop")) {
    while (me->p_speed) usleep(20000);
    goto state_1;
  }

  if (!strcmp(token, "lock-planet")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    struct planet *pl = planet_find(token);
    //* lock on, from lock_planet() in interface.c 
    me->p_flags |= PFPLLOCK;
    me->p_flags &= ~(PFPLOCK|PFORBIT|PFBEAMUP|PFBEAMDOWN|PFBOMB);
    me->p_planet = pl->pl_no;
    goto state_1;
  }

  if (!strcmp(token, "wait-for-orbit")) {
    while (!(me->p_flags & PFORBIT)) usleep(20000);
    goto state_1;
  }

  if (!strcmp(token, "wobble")) {
    t_attribute |= TWOBBLE;
    goto state_1;
  }

  if (!strcmp(token, "no-wobble")) {
    t_attribute &= ~TWOBBLE;
    goto state_1;
  }

  if (!strcmp(token, "torp-speed")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    t_torpspeed = atoi(token);
    goto state_1;
  }

  if (!strcmp(token, "fire-test-torpedo")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    struct ship *myship = &me->p_ship;
    struct torp *k = t_find(me, TFREE);
    me->p_ntorp++;
    k->t_status = TMOVE;
    k->t_type = TPLASMA;
    k->t_attribute = t_attribute;
    k->t_owner = me->p_no;
    t_x_y_set(k, me->p_x, me->p_y);
    k->t_turns  = myship->s_torpturns;
    k->t_damage = 0;
    k->t_gspeed = (t_torpspeed == -1 ? myship->s_torpspeed : t_torpspeed)
      * WARP1;
    k->t_fuse   = 500;
    k->t_dir    = atoi(token);
    k->t_war    = me->p_war;
    k->t_team   = me->p_team;
    k->t_whodet = NODET;
    goto state_1;
  }

  if (!strcmp(token, "show-test-torpedo-position")) {
    struct torp *k = t_find(me, TMOVE);
    if (k != NULL) {
      printf("torp %d x %d y %d\n", k->t_dir, k->t_x, k->t_y);
    }
    goto state_1;
  }

  if (!strcmp(token, "destroy-test-torpedo")) {
    struct torp *k = t_find(me, TMOVE);
    if (k != NULL) {
      k->t_status = TOFF;
    }
    goto state_1;
  }

  if (!strcmp(token, "monitor-coordinates")) {
    for (;;) {
      printf("p_x %X p_y %X p_x_internal %X p_y_internal %X\n", me->p_x, me->p_y, me->p_x_internal, me->p_y_internal);
      usleep(20000);
    }
    goto state_1;
  }

  if (!strcmp(token, "monitor-docking")) {
    for (;;) {
      printf("p_flags & PFDOCK %X p_dock_with %X p_dock_bay %X\n", me->p_flags & PFDOCK, me->p_dock_with, me->p_dock_bay);
      usleep(20000);
    }
    goto state_1;
  }

  if (!strcmp(token, "sleep")) {
    if (!(token = strtok(NULL, delimiters))) return 0;
    sleep(atoi(token));
    goto state_1;
  }

  if (!strcmp(token, "set-inl-draft")) {
    if (!(token = strtok(NULL, delimiters))) return 0;
    me->p_inl_draft = atoi(token);
    goto state_1;
  }

  if (!strcmp(token, "monitor-inl-draft")) {
    for (;;) {
      printf("p_inl_captain %d p_inl_draft %d (%s) p_inl_x %d p_inl_y %d p_inl_pick %d\n", me->p_inl_captain, me->p_inl_draft, inl_draft_name(me->p_inl_draft), me->p_inl_x, me->p_inl_y, me->p_inl_pick);
      usleep(20000);
    }
    goto state_1;
  }

  if (!strcmp(token, "show-inl-draft")) {
    printf("p_inl_captain %d p_inl_draft %d (%s) p_inl_x %d p_inl_y %d p_inl_pick %d\n", me->p_inl_captain, me->p_inl_draft, inl_draft_name(me->p_inl_draft), me->p_inl_x, me->p_inl_y, me->p_inl_pick);
    goto state_1;
  }

  if (!strcmp(token, "wait-for-inl-draft-to-end")) {
    for (;;) {
      if (me->p_inl_draft == INL_DRAFT_OFF) exit(0);
      usleep(20000);
    }
    goto state_1;
  }

  if (!strcmp(token, "player")) {
    me->p_flags &= ~PFOBSERV;
    me->p_status = PALIVE;
    goto state_1;
  }
  if (!strcmp(token, "observer")) {
    me->p_flags |= PFOBSERV;
    me->p_status = POBSERV;
    goto state_1;
  }

  if (!strcmp(token, "captain")) {
    me->p_inl_captain = 1;
    goto state_1;
  }

  if (!strcmp(token, "no-captain")) {
    me->p_inl_captain = 0;
    goto state_1;
  }

  if (!strcmp(token, "damage")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_damage = me->p_ship.s_maxdamage - atoi(token);
    goto state_1;
  }

  if (!strcmp(token, "shields")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_shield = atoi(token);
    goto state_1;
  }

  if (!strcmp(token, "fuel")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_fuel = atoi(token);
    goto state_1;
  }

  /* kills is a float: 2.5 kills is a real and common value */
  if (!strcmp(token, "kills")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_kills = atof(token);
    goto state_1;
  }

  /* Career stats. This server is built with LTD_STATS, so the counters the
   * client sees are summed out of p_stats.ltd[race][ship] rather than the
   * flat st_* fields. Write into the player's current race/ship slot and the
   * LTD_TOTAL rollup picks it up on the next read.
   *
   * DI is not stored anywhere: the client computes it as
   * ratings * (tticks / 36000), and the ratings come from these counters. So
   * DI is raised by setting kills/bomb/planets and giving the player enough
   * ticks to have plausibly earned them. */
  if (!strcmp(token, "stat")) {
    char *name;
    double v;
#ifdef LTD_STATS
    struct ltd_stats *ltd =
      &me->p_stats.ltd[ltd_race(me->p_team)][me->p_ship.s_type];
#endif
    if (!(name  = strtok (NULL, delimiters))) return 0;
    if (!(token = strtok (NULL, delimiters))) return 0;
    v = atof(token);
#ifdef LTD_STATS
         if (!strcmp(name, "kills"))    ltd->kills.total   = (unsigned int) v;
    else if (!strcmp(name, "maxkills")) ltd->kills.max     = v;
    else if (!strcmp(name, "deaths"))   ltd->deaths.total  = (unsigned int) v;
    else if (!strcmp(name, "armsbomb")) ltd->bomb.armies   = (unsigned int) v;
    else if (!strcmp(name, "planets"))  ltd->planets.taken = (unsigned int) v;
    else if (!strcmp(name, "ticks"))    ltd->ticks.total   = (unsigned int) v;
    else { fprintf(stderr, "unknown stat '%s'\n", name); return 0; }
#else
    struct stats *st = &me->p_stats;
         if (!strcmp(name, "kills"))    st->st_tkills    = (int) v;
    else if (!strcmp(name, "maxkills")) st->st_maxkills  = v;
    else if (!strcmp(name, "deaths"))   st->st_tlosses   = (int) v;
    else if (!strcmp(name, "armsbomb")) st->st_tarmsbomb = (int) v;
    else if (!strcmp(name, "planets"))  st->st_tplanets  = (int) v;
    else if (!strcmp(name, "ticks"))    st->st_tticks    = (int) v;
    else { fprintf(stderr, "unknown stat '%s'\n", name); return 0; }
#endif
    goto state_1;
  }

  /* Rank is an index into the server's rank table. The server recomputes it
   * from career stats at the usual points, so treat this as "for this life"
   * rather than a permanent promotion. */
  if (!strcmp(token, "rank")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_stats.st_rank = atoi(token);
    goto state_1;
  }

  if (!strcmp(token, "disconnect")) {
    if (!(token = strtok (NULL, delimiters))) return 0;
    me->p_disconnect = atoi(token);
    goto state_1;
  }

  goto state_0;
}

int main(int argc, char **argv)
{
  int i, j, k;
  int length = 0;
  char *command;

  for (j=1; j < argc; j++){
    for (k=0; k < strlen(argv[j]); k++) { length++; }
    if (j != (argc-1)) { length++; }
  }

  command = malloc(length);

  i=0;
  for (j=1; j < argc; j++){
    for (k=0; k < strlen(argv[j]); k++)
      command[i++] = argv[j][k];
    if (j != (argc-1)) { command[i++] = ' '; }
  }

  setship(command);
  exit(0);
}
