/* gmp.h - redirect to system GMP if available, otherwise stub */
#ifndef _COMPAT_GMP_H
#define _COMPAT_GMP_H

#ifdef __has_include
#if __has_include(<gmp.h>)
/* Use angle brackets with _next to pick up system gmp.h */
#include_next <gmp.h>
#define HAVE_GMP 1
#else
/* GMP not available - stub out RSA */
#undef RSA
typedef unsigned long mpz_t[1];
static inline void mpz_init(mpz_t x) { x[0] = 0; }
static inline void mpz_clear(mpz_t x) { x[0] = 0; }
static inline void mpz_set_str(mpz_t x, const char *s, int b) { (void)x; (void)s; (void)b; }
#endif
#else
/* Fallback: try system gmp */
#include_next <gmp.h>
#endif

#endif /* _COMPAT_GMP_H */
