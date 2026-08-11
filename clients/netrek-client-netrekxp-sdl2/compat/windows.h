/* windows.h - POSIX compatibility shim for NetrekXP on macOS
 *
 * Provides stub definitions for Windows types used throughout
 * the NetrekXP source code, allowing it to compile on POSIX systems.
 */

#ifndef _COMPAT_WINDOWS_H
#define _COMPAT_WINDOWS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Basic Windows types */
typedef void *HWND;
typedef void *HANDLE;
typedef void *HINSTANCE;
typedef void *HBITMAP;
typedef void *HCURSOR;
typedef void *HPALETTE;
typedef void *HFONT;
typedef void *HBRUSH;
typedef void *HPEN;
typedef void *HDC;
typedef void *HICON;
typedef void *HMENU;
typedef void *HRGN;
typedef void *HMODULE;
typedef void *HGLOBAL;

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef int BOOL;
typedef unsigned int UINT;
/* Don't redefine LONG - config.h already does '#define LONG int' */
typedef unsigned long ULONG;
typedef long LRESULT;
typedef unsigned long WPARAM;
typedef long LPARAM;
typedef int HRESULT;

#define CALLBACK
#define WINAPI
#define PASCAL

typedef long (*WNDPROC)(void);
typedef long (*FARPROC)(void);

typedef char *LPSTR;
typedef const char *LPCSTR;
typedef const char *LPCTSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* RECT structure */
typedef struct {
    long left, top, right, bottom;
} RECT;

typedef struct {
    long x, y;
} POINT;

typedef struct {
    long cx, cy;
} SIZE;

/* MSG structure (stub) */
typedef struct {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG;

/* Windows constants */
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

#define MB_OK 0
#define MB_ICONEXCLAMATION 0x30
#define MB_ICONASTERISK 0x40
#define MB_ICONINFORMATION MB_ICONASTERISK

#define NO_ERROR 0
#define ERROR_SUCCESS 0

#define IMAGE_BITMAP 0
#define IMAGE_ICON   1
#define IMAGE_CURSOR 2
#define LR_DEFAULTCOLOR 0x0000
#define LR_MONOCHROME   0x0001
#define LR_LOADFROMFILE  0x0010

#define COLOR_BTNFACE     15
#define COLOR_BTNTEXT     18
#define COLOR_3DFACE      15
#define COLOR_3DDKSHADOW  21
#define COLOR_3DHILIGHT   20

#define MAKEINTRESOURCE(i) ((LPSTR)((ULONG)(WORD)(i)))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))

/* Stub functions */
static inline HINSTANCE GetModuleHandle(const char *name) { (void)name; return NULL; }
static inline int GetModuleFileName(HINSTANCE h, char *buf, int sz) {
    (void)h;
    if (sz > 0) buf[0] = '\0';
    return 0;
}
static inline void FreeConsole(void) {}
static inline int AllocConsole(void) { return 1; }
static inline int GetConsoleTitle(char *buf, int sz) { (void)buf; (void)sz; return 0; }
static inline int WriteConsole(HANDLE h, const char *buf, int len, DWORD *written, void *x) {
    (void)h; (void)x;
    if (written) *written = len;
    return fwrite(buf, 1, len, stdout);
}
static inline HANDLE GetStdHandle(DWORD id) { (void)id; return NULL; }
static inline void BringWindowToTop(HWND h) { (void)h; }
static inline int ShellExecute(HWND h, const char *a, const char *b, const char *c, const char *d, int e) {
    (void)h; (void)a; (void)b; (void)c; (void)d; (void)e; return 0;
}
static inline int MessageBox(HWND h, const char *text, const char *cap, UINT type) {
    (void)h; (void)type;
    fprintf(stderr, "%s: %s\n", cap ? cap : "Message", text ? text : "");
    return 0;
}
static inline void MessageBeep(UINT type) { (void)type; }
static inline int WNetGetUser(const char *a, char *name, int *sz) {
    (void)a; (void)name; (void)sz;
    return 1; /* fail - force login from rc file */
}

/* LoadImage stub - returns NULL (our SDL2 backend handles bitmaps differently) */
static inline HANDLE LoadImage(HINSTANCE h, const char *name, UINT type, int cx, int cy, UINT flags) {
    (void)h; (void)name; (void)type; (void)cx; (void)cy; (void)flags;
    return NULL;
}

/* Thread stubs - replaced by pthreads in our port */
#include <pthread.h>
#include <unistd.h>

typedef void *(*LPTHREAD_START_ROUTINE)(void *);

static inline HANDLE CreateThread(void *attr, size_t stack,
                                   LPTHREAD_START_ROUTINE func,
                                   void *arg, DWORD flags, DWORD *tid) {
    (void)attr; (void)stack; (void)flags;
    pthread_t *pt = (pthread_t *)malloc(sizeof(pthread_t));
    if (!pt) return NULL;
    if (pthread_create(pt, NULL, func, arg) != 0) {
        free(pt);
        return NULL;
    }
    if (tid) *tid = (DWORD)(uintptr_t)*pt;
    return (HANDLE)pt;
}

static inline int WaitForSingleObject(HANDLE h, DWORD ms) {
    (void)ms;
    if (!h) return WAIT_TIMEOUT;
    pthread_t *pt = (pthread_t *)h;
    pthread_join(*pt, NULL);
    return WAIT_OBJECT_0;
}

static inline void CloseHandle(HANDLE h) {
    free(h);
}

static inline void ExitThread(DWORD code) {
    (void)code;
    pthread_exit(NULL);
}

/* Event/Semaphore stubs */
static inline HANDLE CreateEvent(void *a, int manual, int init, const char *name) {
    (void)a; (void)manual; (void)init; (void)name;
    return NULL;
}
static inline int SetEvent(HANDLE h) { (void)h; return 1; }
static inline int ResetEvent(HANDLE h) { (void)h; return 1; }
static inline int PostMessage(HWND h, UINT msg, WPARAM w, LPARAM l) {
    (void)h; (void)msg; (void)w; (void)l;
    return 1;
}

/* SetSysColors stub */
static inline void SetSysColors(int n, const int *elems, DWORD *colors) {
    (void)n; (void)elems; (void)colors;
}

/* WriteFile stub */
static inline int WriteFile(HANDLE h, const void *buf, int len, int *written, void *x) {
    (void)h; (void)x;
    int w = (int)fwrite(buf, 1, len, stdout);
    if (written) *written = w;
    return w > 0;
}

/* String pointer types */
typedef char *LPTSTR;

/* Clipboard stubs */
#define CF_TEXT 1
static inline int OpenClipboard(HWND h) { (void)h; return 0; /* fail = no paste */ }
static inline HGLOBAL GetClipboardData(UINT fmt) { (void)fmt; return NULL; }
static inline void *GlobalLock(HGLOBAL h) { (void)h; return NULL; }
static inline int GlobalUnlock(HGLOBAL h) { (void)h; return 0; }
static inline int CloseClipboard(void) { return 1; }

/* Registry stubs */
typedef void *HKEY;
#define HKEY_CURRENT_USER ((HKEY)(void*)0x80000001)
#define KEY_READ 0x20019
#define REG_SZ 1
#define ERROR_MORE_DATA 234
static inline long RegOpenKeyEx(HKEY k, const char *sub, unsigned long opt, unsigned long sam, HKEY *res) {
    (void)k; (void)sub; (void)opt; (void)sam; (void)res;
    return 1; /* fail */
}
static inline long RegQueryValueEx(HKEY k, const char *name, void *res, unsigned long *type,
                                    unsigned char *data, unsigned long *sz) {
    (void)k; (void)name; (void)res; (void)type; (void)data; (void)sz;
    return 1; /* fail */
}
static inline long RegCloseKey(HKEY k) { (void)k; return 0; }

#endif /* _COMPAT_WINDOWS_H */
