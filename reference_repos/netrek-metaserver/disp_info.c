/*
 * disp_info.c - send a file to the client
 *
 * MetaServerII
 * Copyright (c) 1993 by Andy McFadden
 *
 * $Id: disp_info.c,v 1.2 2006/02/14 07:21:31 quozl Exp $
 *
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include "meta.h"

static int send_info(int idx, char *file);

/*
 * display the info file
 */
int
display_info(idx, port_idx)
     int idx, port_idx;
{
  if (verbose) printf("%s: sending info '%s'\n", prog, ports[port_idx].extra);
  send_info(idx, ports[port_idx].extra);
  return (0);
}



static int
send_info(idx, file)
     int idx;
     char *file;
{
  USER *up;
  FILE *fp;
  char buf[128];

  up = &users[idx];
  up->data_size = up->buf_size = up->pos = 0;

  if ((fp = fopen(file, "r")) == NULL) {
    if (errno == EMFILE)
      /* I don't see how this could be happening... weird */
      Uprintf(idx,
	      "\nToo many files open here; wait a bit and try again\n\n");
    else
      Uprintf(idx,"\nUnable to open %s (err %d); tell the admin (%s)\n\n",
	      file, errno, admin);
    log_msg("Unable to open %s (errno=%d)", file, errno);
    return (0);
  }

  while (1) {
    fgets(buf, 128, fp);
    if (ferror(fp))
      log_msg("error reading %s: %d", file, errno);
    if (ferror(fp) || feof(fp))
      break;

    Uprintf(idx, "%s", buf);
  }

  fclose(fp);

  return (0);
}
