void redraw(void);
/* dead reckoning between server updates; exposed for tests/extrap_test.c */
void extrap_apply(void);
void extrap_restore(void);
void redraw_if_due(void);
void redrawTstats(void);
#include <sys/select.h>
void intrupt(fd_set *readfds);
