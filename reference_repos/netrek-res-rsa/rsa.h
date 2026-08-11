/*
 * $Id: rsa.h,v 1.1 2000/02/17 05:57:22 ahn Exp $
 *
 * This is the server-side RSA support code.
 *
 * Originally written by Ray Jones.  Updated by Dave Ahn.
 * */

#ifndef __INCLUDED_rsa_h__
#define __INCLUDED_rsa_h__

/* encode the message using the key, global N to mod by, with key size digits
   and return the result in out */

void rsa_encode(unsigned char *out, unsigned char *message,
                unsigned char *key, unsigned char *global, const int digits);

#endif /* __INCLUDED_rsa_h__ */
