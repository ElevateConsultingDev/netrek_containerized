/*
 * disp_old.c - output in format compatible with older metaserver
 * (also has Uprintf() and sort_servers() routines)
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 * 
 * $Id: disp_old.c,v 1.1 2006/02/14 06:43:11 unbelver Exp $
 * 
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "meta.h"

int
display_old(idx, port_idx)
     int idx;
{
  USER *up;
  SERVER *sp;
  time_t now;
  int srv, *sorted;
  char buf[80], buf2[80];

  up = &users[idx];
  up->data_size = up->buf_size = up->pos = 0;

  now = time(0);

  /* print header */
  Uprintf(idx, "\n\nMETASERVER-II %s, %s\n", VERSION, VDATE);
  Uprintf(idx, "Server Check period: %d minutes\n", wait_open);

  now = time(0);

  /* print player info */
  sorted = sort_servers();
  for (srv = 0; srv < server_count; srv++) {
    /* (need to sort these?) */
    sp = &servers[sorted[srv]];
    if (sp->status == SS_WORKING) sp = &prev_info;
    sprintf(buf, "%s(%d)", sp->hostname, sp->port);
    Uprintf(idx, "%-40.40s: ", buf);

    strncpy(buf, ctime(&sp->last_update)+11, 8);
    *(buf+8) = '\0';

    strncpy(buf2, ctime(&sp->down_time)+4, 15);
    *(buf2+15) = '\0';

    switch (sp->status) {
    case SS_INIT:
      Uprintf(idx, "Haven't yet checked this one...\n");
      break;
    case SS_WORKING:
      Uprintf(idx, "(scanning server now)\n");
      break;
    case SS_NOCONN:
      /*
	Uprintf(idx, "Couldn't connect as of %s\n", buf);
	*/
      Uprintf(idx, "Down as of %s\n", buf2);
      break;
    case SS_EMPTY:
    case SS_OPEN:
      Uprintf(idx, "%2d Players online at %s\n", sp->player_count, buf);
      break;
    case SS_QUEUE:
      Uprintf(idx, "Wait queue %2d at %s\n", sp->queue_size, buf);
      break;

    default:
      /* huh? */
      fprintf(stderr, "Internal ERROR: strange state in disp_new\n");
      break;
    }
  }

  /* print footer */
  Uprintf(idx, "\n\tThat's all!\n\n");
}


#define INITIAL_SIZE	64000
#define MAX_PRINT	256
#ifdef BAD_IDEA
# define EXPAND_SIZE	4096		/* must be > MAX_PRINT */
#endif
/*
 * Print something to an expandable buffer.  Works like sprintf(), mostly.
 * Assumes that nothing we're passed will exceed MAX_PRINT bytes.
 *
 * When everybody has vsprintf, I'll do this right.
 */
void
Uprintf(idx, format, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7)
     int idx;
     char *format;
     long arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7;
{
  USER *up;

  if (idx > 1024 || idx < 0) {
    fprintf(stderr, "Internal ERROR: bogus idx %d\n", idx);
    return;
  }

  up = &users[idx];
  if (!up->data_size) {
    if (up->buf != NULL) {
      /* debug */
      printf("HEY: Reassigning non-null buf for %d\n", idx);
      /* endif */
      log_msg("Reassigning non-null buf for %d\n", idx);
      /* ought to free() here, but that might make us crash */
    }
    if ((up->buf = (char *) malloc(INITIAL_SIZE)) == NULL) {
      fprintf(stderr, "malloc failure!\n");
      log_msg("exit: malloc failure in Uprintf");
      exit(1);
    }
    up->buf_size = INITIAL_SIZE;
  }
  if (up->buf_size - up->data_size < MAX_PRINT) {
#ifdef BAD_IDEA
    /* buffer too small, realloc */
    up->buf_size += EXPAND_SIZE;
    if ((up->buf = (char *) realloc(up->buf, up->buf_size)) == NULL) {
      fprintf(stderr, "realloc failure!\n");
      log_msg("exit: realloc failure in Uprintf");
      exit(1);
    }
#else
    /* ran out of room; bummer */
    log_msg("NOTE: ran out of space in Uprintf()");
#endif
  }
  sprintf(up->buf + up->data_size, format, arg0, arg1, arg2, arg3, arg4,
	  arg5, arg6, arg7);
  up->data_size += strlen(up->buf + up->data_size);
}


static int *alpha = NULL;		/* alphabetical host sorting order */

/*
 * Comparison function for qsort()
 */
static int
srvcmp(elmp1, elmp2)
     int *elmp1, *elmp2;
{
  register SERVER *s1, *s2;
  register int elm1 = *elmp1, elm2 = *elmp2;
  int st, pc, ho, po;

  s1 = &servers[elm1];
  if (s1->status == SS_WORKING) s1 = &prev_info;
  s2 = &servers[elm2];
  if (s2->status == SS_WORKING) s2 = &prev_info;

  st = s1->status - s2->status;
  if (s1->status == SS_QUEUE)
    pc = s1->queue_size - s2->queue_size;
  else if (s1->status == SS_NOCONN)
    pc = 0;
  else
    pc = s1->player_count - s2->player_count;
  ho = alpha[elm1] - alpha[elm2];
  po = s1->port - s2->port;

  /*if ((st > 0) || (!st && pc < 0) || (!st && !pc && ho > 0))*/
  if ((st < 0) || (!st && pc > 0) || (!st && !pc && ho > 0) ||
      (!st && !pc && !ho && po < 0))
    return (1);
  else
    return (-1);
  /* note there should not be a case in which two entries match exactly */
}

/*
 * Returns a pointer to an array of ints, indicating the sorted order of
 * the servers.
 *
 * Sorting order is:
 *   status
 *     player_count/queue_size
 *       alphabetical hostname
 *         port number
 *
 * IDEA: we can reduce the CPU usage by sorting the internal list by
 * host and port number once, during initialization.  However, this would
 * require a stable sort on the other categories, which qs doesn't
 * guarantee.
 */
int *
sort_servers()
{
  static int *sorted = NULL;
  register int i, j, *ip;
  int k;

  /* allocate some stuff first time through */
  if (sorted == NULL) {
    if (verbose) printf("Allocated sort_servers array\n");	/* DEBUG */
    if ((sorted = (int *) malloc(server_count * sizeof(int))) == NULL) {
      fprintf(stderr, "malloc failure in sort_servers\n");
      log_msg("exit: malloc failure in sort_servers");
      exit(1);
    }

    /* to ease the CPU load, compute the alphabetical order once */
    if ((alpha = (int *) malloc(server_count * sizeof(int))) == NULL) {
      fprintf(stderr, "malloc failure in sort_servers (alpha)\n");
      log_msg("exit: malloc failure in sort_servers (alpha)");
      exit(1);
    }
    for (i = 0, ip = sorted; i < server_count; i++, ip++)
      *ip = i;
    for (i = 0; i < server_count-1; i++)
      for (j = i+1; j < server_count; j++)
	if (strcmp(servers[sorted[i]].hostname,
		   servers[sorted[j]].hostname) > 0) {
	  k = sorted[i];
	  sorted[i] = sorted[j];
	  sorted[j] = k;
	}
    /* change sorted indices into parallel table of relative positions */
    for (i = 0, ip = sorted; i < server_count; i++, ip++)
      alpha[*ip] = i;
  }

  /* intialize the sorted array to itself */
  for (i = 0, ip = sorted; i < server_count; i++, ip++)
    *ip = i;

  /* sort the array with the system qsort */
  qsort((char *) sorted, server_count, sizeof(int), srvcmp);

  return (sorted);
}

