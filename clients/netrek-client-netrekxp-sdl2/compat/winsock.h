/* winsock.h - POSIX socket compatibility shim
 *
 * Maps Winsock API calls to POSIX equivalents.
 */

#ifndef _COMPAT_WINSOCK_H
#define _COMPAT_WINSOCK_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/* Map Winsock functions to POSIX */
#define closesocket close
#define WSAGetLastError() errno
#define WSAECONNREFUSED ECONNREFUSED
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
typedef int SOCKET;

/* WSAStartup / WSACleanup are no-ops on POSIX */
typedef struct {
    unsigned short wVersion;
    unsigned short wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
} WSADATA;

static inline int WSAStartup(unsigned short ver, WSADATA *data) {
    (void)ver;
    if (data) {
        data->wVersion = ver;
        data->wHighVersion = ver;
    }
    return 0;
}

static inline int WSACleanup(void) { return 0; }

/* ioctlsocket → fcntl */
#define FIONBIO 1
static inline int ioctlsocket(int fd, long cmd, unsigned long *argp) {
    (void)cmd;
    int flags = fcntl(fd, F_GETFL, 0);
    if (*argp)
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    else
        return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

/* Windows Winsock type aliases used in parsemeta.c ICMP ping code */
typedef struct sockaddr_in *LPSOCKADDR_IN;
typedef struct sockaddr *LPSOCKADDR;
#define SOCKADDR_IN struct sockaddr_in
#define FAR

/* GetTickCount - millisecond timer (declared here, defined in sdl2window.c) */
unsigned long GetTickCount(void);

/* Define CYGWIN to skip Windows-specific fd_set manipulation in parsemeta.c */
#ifndef CYGWIN
#define CYGWIN
#endif

#endif /* _COMPAT_WINSOCK_H */
