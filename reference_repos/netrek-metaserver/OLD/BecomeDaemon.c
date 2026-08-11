/*
 * BecomeDaemon.c
 * shamelessly swiped from xdm source code.
 * Appropriate copyrights kept, and hereby follow
 * ERic mehlhaff, 3/11/92
 *
 * xdm - display manager daemon
 *
 * $XConsortium: daemon.c,v 1.5 89/01/20 10:43:49 jim Exp $
 *
 * Copyright 1988 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  M.I.T. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * Author:  Keith Packard, MIT X Consortium
 */


#include <sys/ioctl.h>
#ifdef hpux
#include <sys/ptyio.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <sys/file.h>


extern void exit ();

void BecomeDaemon ()
{
    register int i;
    int forkresult;

    /*
     * fork so that the process goes into the background automatically. Also
     * has a nice side effect of having the child process get inherited by
     * init (pid 1).
     */

    if ( (forkresult = fork ()) ){	/* if parent */
	  if(forkresult < 0 ){
		perror("Fork error!");
	  }
	  exit (0);			/* then no more work to do */
      }

    /*
     * Close standard file descriptors and get rid of controlling tty
     */

    close (0); 
    close (1);
    close (2);

    /*
     * Set up the standard file descriptors.
     */
    (void) open ("/", O_RDONLY);	/* root inode already in core */
    (void) open ("/tmp/metaout", O_WRONLY | O_APPEND | O_CREAT, 0644);
    (void) dup2 (1, 2);

    if ((i = open ("/dev/tty", O_RDWR)) >= 0) {	/* did open succeed? */
#if defined(SYSV) && defined(TIOCTTY)
	int zero = 0;
	(void) ioctl (i, TIOCTTY, &zero);
#else
	(void) ioctl (i, TIOCNOTTY, (char *) 0);    /* detach, BSD style */
#endif
	(void) close (i);
    }


#ifdef SYSV
#ifdef hpux
    setsid();
#else
    setpgrp (0, 0);
#endif
#else
    setpgrp (0, getpid());
#endif

}
