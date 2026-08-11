/*
 * disp_web.c - output in fancy new format
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 * 
 * $Id: disp_web.c,v 1.2 2006/03/07 11:53:49 quozl Exp $
 * 
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "meta.h"

#define SEMI_VERBOSE		/* not quite fully verbose */

/* "when" should be the length of time in seconds */
static char *
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


int display_web(idx, port_idx)
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

  if(up->buf)
    {
      free(up->buf);
      up->buf = NULL;
    }

  up->data_size = up->buf_size = up->pos = 0;

  now = time(0);

  /* print server header */
  Uprintf(idx, "HTTP/1.0 200 OK\n");
  Uprintf(idx, "Server: netrekmetarc/1.0\n");
  Uprintf(idx, "MIME-version: 1.0\n");
  Uprintf(idx, "Content-type: text/html\n\n");

  /* print page */
  Uprintf(idx, "\
<html><head><title>Netrek Metaserver</title></head>\n\
<body><center><h1>Netrek Metaserver</h1></center>\n");

  Uprintf(idx, "<center><table border=2 cellpadding=6><tr>\
<td><h4>Server Host</h4></td>\
<td><h4>Port</h4></td>\
<td><h4>Minutes Ago</h4></td>\
<td><h4>Status</h4></td>\
<td><h4>Flags</h4></td>\
</tr>\n");

  /* print player info */
  sorted = sort_servers();
  for (srv = 0; srv < server_count; srv++) {
    /* (need to sort these?) */
    sp = &servers[sorted[srv]];
    if (sp->status == SS_WORKING) sp = &prev_info;
    Uprintf(idx, "\n<tr><td>%s</td><td>%d</td>\n", sp->hostname, sp->port);

    strcpy(flagbuf, "       ");
    for (i = 0; i < 6; i++)
      if (sp->display_flags & flags[i].bit)
	flagbuf[i] = flags[i].chr;
    flagbuf[6] = sp->type[0];		/* hack in the type field */

    ago = (now - sp->last_update) / 60;
    switch (sp->status) {
    case SS_INIT:
      Uprintf(idx, "<td colspan=3>(no data yet)</td>\n");
      break;
    case SS_WORKING:
      Uprintf(idx, "<td colspan=3>(scanning server now)</td>\n");
      break;
    case SS_NOCONN:
      switch (sp->why_dead) {
      case WD_CONN:
	Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "* No connection", 
		flagbuf);
	break;
      case WD_READ:
	Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "* Bad read", 
		flagbuf);
	break;
      case WD_GARBAGE:
	Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "* Garbaged read", 
		flagbuf);
	break;
      case WD_TIMEOUT:
	Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "* Timed out", 
		flagbuf);
	break;
      case WD_FLOODING:
	Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "* Flooding",
		flagbuf);
	break;
      default:
	Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "* Not responding", 
		flagbuf);
	break;
      }
      break;
    case SS_EMPTY:
      Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, "Nobody playing", flagbuf);
      break;
    case SS_OPEN:
      sprintf(buf, "OPEN: %d player%s", sp->player_count,
	      (sp->player_count != 1) ? "s" : "");
      Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, buf, flagbuf);
      break;
    case SS_QUEUE:
      sprintf(buf, "Wait queue: %d", sp->queue_size);
      Uprintf(idx, "<td>%d</td><td>%s</td><td>%s</td>\n", ago, buf, flagbuf);
      break;

    default:
      /* huh? */
      fprintf(stderr, "Internal ERROR: strange state in disp_new\n");
      break;
    }
    Uprintf(idx, "</tr>\n");
  }

  /* print footer */
  Uprintf(idx, "</table></center>\n");
  Uprintf(idx, "<p><i>(This is MetaServerII v%s on port %d, \n",
	  VERSION, ports[port_idx].port);
  cp = ctime(&now);
  cp[strlen(cp)-1] = '\0';	/* playing with fire? */
  Uprintf(idx, "date in %s is %s, data spans %s, \n", location, cp,
	  nice_time(now-checktime));
  Uprintf(idx, "send administrative requests to <a href=mailto:%s>%s</a>, ", admin, admin);

  Uprintf(idx, "retry periods in minutes are ");
  Uprintf(idx, "down:%d empty:%d open:%d queue:%d)</i><p>\n\n",
	  wait_noconn, wait_empty, wait_open, wait_queue);
  Uprintf(idx, "<p>See also <a href=\"http://metaserver.us.netrek.org:1080/\">US #1 metaserver</a><p>\n");
  Uprintf(idx, "<p>See also <a href=\"http://metaserver2.us.netrek.org:1080/\">US #2 metaserver</a></body></html>\n");

  return(0);
}
